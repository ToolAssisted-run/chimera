/* TAStudio's number prompt (NumberPrompt.qml), for Insert # Frames, Clone #
 * Times and Auto-hold's value (issue 621): opened while another item of the
 * window has the keyboard (the piano roll, as when a hotkey opens it), its
 * box takes the keyboard at once, what is typed replaces the number, and
 * Enter is OK. Real keys, through Qt's test module. */
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
    width: 500; height: 300; visible: true
    property int acceptedNumber: -1
    property alias prompt: prompt
    // the piano roll, which has the keyboard
    Item { id: roll; objectName: "roll"; focus: true; anchors.fill: parent }
    NumberPrompt {
        id: prompt
        title: "Insert frames"
        question: "How many?"
        anchors.centerIn: parent
        from: 1; to: 1000; value: 5
        onAccepted: acceptedNumber = prompt.value
    }
}
)QML";

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
	auto *roll = window->findChild<QQuickItem *>(QStringLiteral("roll"));
	CHECK(roll != nullptr && roll->hasActiveFocus());
	QObject *prompt = window->property("prompt").value<QObject *>();
	CHECK(prompt != nullptr);
	if (prompt == nullptr) return 1;
	QMetaObject::invokeMethod(prompt, "openWith", Q_ARG(QVariant, 7));
	CHECK(QTest::qWaitFor([&] { return prompt->property("opened").toBool(); }));
	/* the box, not the roll, has the keyboard, with no click */
	auto *box = window->findChild<QQuickItem *>(QStringLiteral("numberBox"));
	CHECK(box != nullptr);
	if (box == nullptr) return 1;
	CHECK(QTest::qWaitFor([&] { return box->hasActiveFocus() || box->property("activeFocus").toBool(); }, 2000));
	CHECK(!roll->hasActiveFocus());
	CHECK(prompt->property("value").toInt() == 7);

	/* typed over the number (it is all selected), then Enter: accepted with it, closed */
	for (const char c : {'4', '2'}) QTest::keyClick(window, c);
	QTest::keyClick(window, Qt::Key_Return);
	CHECK(QTest::qWaitFor([&] { return window->property("acceptedNumber").toInt() == 42; }, 2000));
	CHECK(QTest::qWaitFor([&] { return !prompt->property("visible").toBool(); }, 2000));
	/* the keyboard is given back to what had it */
	CHECK(QTest::qWaitFor([&] { return roll->hasActiveFocus(); }, 2000));

	std::printf("%d checks, %d failed\n", checks, failures);
	return failures == 0 ? 0 : 1;
}
