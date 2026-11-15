/* A long seek's dialogs (LongSeek.qml, issue 595): the question opens when
 * the session asks it, and Yes, No and Escape answer it; the progress dialog
 * opens while a long seek runs, shows its bar and its three lines, and Cancel
 * and Escape cancel it; neither opens where the window does not show them.
 * Real keys and clicks, through Qt's test module, over a stand-in session
 * that keeps the commands it is sent. */
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QTest>

#include <cstdio>

namespace {

int failures = 0, checks = 0;
#define CHECK(c) \
	do { \
		checks++; \
		if (!(c)) { std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); failures++; } \
	} while (0)

const char *const kWindow = R"QML(
import QtQuick
import QtQuick.Controls
import "qrc:/chimera"
ApplicationWindow {
    width: 640; height: 480; visible: true
    property alias stand: stand
    property alias dialogs: dialogs
    QtObject {
        id: stand
        property string longSeekQuestion: ""
        property double longSeekTo: -1
        property double longSeekProgress: 0
        property string longSeekRemainingText: ""
        property string longSeekSpeedText: ""
        property string longSeekTimeText: ""
        property string calls: ""
        function run(name, args) { calls += name + "(" + (args === undefined ? "" : args.join(",")) + ");" }
    }
    LongSeek { id: dialogs; anchors.fill: parent; session: stand }
}
)QML";

/* the button of a dialog whose text is `text` */
QQuickItem *buttonIn(QObject *dialog, const QString &text)
{
	for (QQuickItem *item : dialog->findChildren<QQuickItem *>())
		if (QString(item->metaObject()->className()).contains(QLatin1String("Button")) && item->property("text").toString().remove('&') == text &&
			item->isVisible())
			return item;
	return nullptr;
}

void click(QQuickWindow *window, QQuickItem *item)
{
	const QPointF centre = item->mapToScene(QPointF(item->width() / 2, item->height() / 2));
	QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, centre.toPoint());
}

bool opened(QObject *dialog) { return dialog != nullptr && dialog->property("opened").toBool(); }

} // namespace

int main(int argc, char **argv)
{
	QGuiApplication app(argc, argv);
	QQmlApplicationEngine engine;
	engine.loadData(kWindow);
	auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().value(0));
	CHECK(window != nullptr);
	if (window == nullptr) return 1;
	window->requestActivate();
	CHECK(QTest::qWaitForWindowActive(window));
	QObject *stand = window->property("stand").value<QObject *>();
	QObject *dialogs = window->property("dialogs").value<QObject *>();
	QObject *question = window->findChild<QObject *>(QStringLiteral("longSeekQuestion"));
	QObject *progress = window->findChild<QObject *>(QStringLiteral("longSeekProgress"));
	CHECK(stand != nullptr && dialogs != nullptr && question != nullptr && progress != nullptr);
	if (stand == nullptr || dialogs == nullptr || question == nullptr || progress == nullptr) return 1;
	auto calls = [&] { return stand->property("calls").toString(); };
	auto ask = [&](const char *text) {
		stand->setProperty("longSeekQuestion", QString());
		stand->setProperty("longSeekQuestion", QString::fromUtf8(text));
	};

	/* asked: the question shown in its words; Escape is no */
	ask("Going to frame 9,000 replays 9,000 frames.");
	CHECK(QTest::qWaitFor([&] { return opened(question); }, 2000));
	auto *words = window->findChild<QQuickItem *>(QStringLiteral("longSeekQuestionText"));
	CHECK(words != nullptr && words->property("text").toString() == QLatin1String("Going to frame 9,000 replays 9,000 frames."));
	QTest::keyClick(window, Qt::Key_Escape);
	CHECK(QTest::qWaitFor([&] { return calls() == QLatin1String("answerLongSeek(false);"); }, 2000));
	CHECK(QTest::qWaitFor([&] { return !opened(question); }, 2000));
	stand->setProperty("longSeekQuestion", QString());

	/* asked again: No, then Yes, each a click */
	ask("again");
	CHECK(QTest::qWaitFor([&] { return opened(question); }, 2000));
	QQuickItem *no = buttonIn(question, QStringLiteral("No"));
	CHECK(no != nullptr);
	if (no != nullptr) click(window, no);
	CHECK(QTest::qWaitFor([&] { return calls().endsWith(QLatin1String("answerLongSeek(false);")) && calls().count(';') == 2; }, 2000));
	stand->setProperty("longSeekQuestion", QString());
	ask("once more");
	CHECK(QTest::qWaitFor([&] { return opened(question); }, 2000));
	QQuickItem *yes = buttonIn(question, QStringLiteral("Yes"));
	CHECK(yes != nullptr);
	if (yes != nullptr) click(window, yes);
	CHECK(QTest::qWaitFor([&] { return calls().endsWith(QLatin1String("answerLongSeek(true);")); }, 2000));
	/* the session takes the question back as it answers */
	stand->setProperty("longSeekQuestion", QString());
	CHECK(QTest::qWaitFor([&] { return !opened(question); }, 2000));

	/* the long seek runs: its bar and its three lines */
	stand->setProperty("calls", QString());
	stand->setProperty("longSeekProgress", 0.25);
	stand->setProperty("longSeekRemainingText", QStringLiteral("6,750"));
	stand->setProperty("longSeekSpeedText", QStringLiteral("900 fps"));
	stand->setProperty("longSeekTimeText", QStringLiteral("7s"));
	stand->setProperty("longSeekTo", 9000.0);
	CHECK(QTest::qWaitFor([&] { return opened(progress); }, 2000));
	auto *bar = window->findChild<QQuickItem *>(QStringLiteral("longSeekBar"));
	CHECK(bar != nullptr && qAbs(bar->property("value").toDouble() - 0.25) < 1e-9);
	auto textOf = [&](const char *name) {
		auto *label = window->findChild<QQuickItem *>(QString::fromLatin1(name));
		return label != nullptr ? label->property("text").toString() : QStringLiteral("(none)");
	};
	CHECK(textOf("longSeekRemaining") == QLatin1String("6,750") && textOf("longSeekSpeed") == QLatin1String("900 fps") &&
		  textOf("longSeekTime") == QLatin1String("7s"));
	stand->setProperty("longSeekProgress", 0.5);
	CHECK(QTest::qWaitFor([&] { return qAbs(bar->property("value").toDouble() - 0.5) < 1e-9; }, 2000));
	/* Cancel: the session told to put everything back */
	auto *cancel = window->findChild<QQuickItem *>(QStringLiteral("longSeekCancel"));
	CHECK(cancel != nullptr);
	if (cancel != nullptr) click(window, cancel);
	CHECK(QTest::qWaitFor([&] { return calls() == QLatin1String("cancelLongSeek();"); }, 2000));
	stand->setProperty("longSeekTo", -1.0);
	CHECK(QTest::qWaitFor([&] { return !opened(progress); }, 2000));
	/* Escape cancels as well */
	stand->setProperty("longSeekTo", 9000.0);
	CHECK(QTest::qWaitFor([&] { return opened(progress); }, 2000));
	QTest::keyClick(window, Qt::Key_Escape);
	CHECK(QTest::qWaitFor([&] { return calls() == QLatin1String("cancelLongSeek();cancelLongSeek();"); }, 2000));
	stand->setProperty("longSeekTo", -1.0);
	/* landed: the dialog goes with nothing sent */
	stand->setProperty("longSeekTo", 9000.0);
	CHECK(QTest::qWaitFor([&] { return opened(progress); }, 2000));
	stand->setProperty("longSeekTo", -1.0);
	CHECK(QTest::qWaitFor([&] { return !opened(progress); }, 2000));
	QTest::qWait(100);
	CHECK(calls() == QLatin1String("cancelLongSeek();cancelLongSeek();"));

	/* not shown here (the other window shows them): neither opens */
	dialogs->setProperty("shown", false);
	ask("not here");
	stand->setProperty("longSeekTo", 9000.0);
	QTest::qWait(200);
	CHECK(!opened(question) && !opened(progress));
	/* shown again while asked: it opens */
	dialogs->setProperty("shown", true);
	CHECK(QTest::qWaitFor([&] { return opened(question) || opened(progress); }, 2000));

	std::printf("%d checks, %d failed\n", checks, failures);
	return failures == 0 ? 0 : 1;
}
