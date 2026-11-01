/* TAStudio's text prompt (TextPrompt.qml), for a branch's or a marker's
 * text: its field fits inside the dialog, and Enter is OK, as in Chimera's
 * TAStudio (issues 437, 455). Real keys, through Qt's test module. */
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
    property string acceptedText: "(none)"
    property alias prompt: prompt
    TextPrompt {
        id: prompt
        title: "Branch text"
        anchors.centerIn: parent
        placeholderText: "Branch 1"
        onAccepted: acceptedText = prompt.text
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
	QObject *prompt = window->property("prompt").value<QObject *>();
	CHECK(prompt != nullptr);
	if (prompt == nullptr) return 1;
	QMetaObject::invokeMethod(prompt, "open");
	auto *field = window->findChild<QQuickItem *>(QStringLiteral("promptField"));
	CHECK(field != nullptr);
	if (field == nullptr) return 1;
	CHECK(QTest::qWaitFor([&] { return prompt->property("opened").toBool() && field->hasActiveFocus(); }));

	/* the field inside the dialog: its right edge within the dialog's own */
	auto *box = qobject_cast<QQuickItem *>(prompt->property("contentItem").value<QObject *>());
	CHECK(box != nullptr && field->width() >= 300);
	if (box != nullptr)
	{
		const QPointF right = field->mapToItem(box, QPointF(field->width(), 0));
		CHECK(right.x() <= box->width() + 0.5);
	}

	/* typed, then Enter: the dialog is accepted with the text, and closes */
	for (const char c : {'a', 'b', 'c'}) QTest::keyClick(window, c);
	QTest::keyClick(window, Qt::Key_Return);
	CHECK(QTest::qWaitFor([&] { return window->property("acceptedText").toString() == QLatin1String("abc"); }, 2000));
	CHECK(QTest::qWaitFor([&] { return !prompt->property("visible").toBool(); }, 2000));

	std::printf("%d checks, %d failed\n", checks, failures);
	return failures == 0 ? 0 : 1;
}
