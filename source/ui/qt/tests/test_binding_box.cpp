/* The box a key is bound in (BindingBox.qml), for Configure Hotkeys and
 * Controller Config: every key pressed while it has the focus is the box's,
 * a window's shortcut on that key notwithstanding. A shortcut on Escape took
 * it, so Escape no longer emptied a box (issue 431); one on Ctrl+S would take
 * Ctrl+S from a box binding it. Real keys, through Qt's test module, named by
 * the window's own key namer. */
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickItem>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QTest>

#include <cstdio>

#include "../key_names.hpp"

namespace {

int failures = 0, checks = 0;
#define CHECK(c) \
	do { \
		checks++; \
		if (!(c)) { std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); failures++; } \
	} while (0)

const char *const kWindow = R"(
import QtQuick
import QtQuick.Controls
import "qrc:/chimera"
ApplicationWindow {
    width: 300; height: 120; visible: true
    property string taken: ""
    property int shortcuts: 0
    // what the box asks of the session: game pads caught while it waits
    QtObject {
        id: session
        property string caughtPad: ""
        function run(name) { return undefined }
    }
    // a shortcut of the window's own on the keys the box is to take
    Shortcut { sequences: ["Escape", "Ctrl+S"]; onActivated: shortcuts++ }
    BindingBox {
        x: 10; y: 10; width: 200
        keys: "F5"
        session: session
        keyNamer: namer
        combination: true
        press: (name) => { taken += (taken === "" ? "" : ",") + name; return false }
    }
}
)";

} // namespace

int main(int argc, char **argv)
{
	QGuiApplication app(argc, argv);
	/* the window's own style: the system's default one (Windows') is not in the bundle */
	QQuickStyle::setStyle(QStringLiteral("ChimeraStyle"));
	chimera::qt::KeyNamer namer;
	app.installEventFilter(&namer);
	QQmlApplicationEngine engine;
	engine.rootContext()->setContextProperty(QStringLiteral("namer"), &namer);
	engine.loadData(kWindow);
	auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().value(0));
	CHECK(window != nullptr);
	if (window == nullptr) return 1;
	window->requestActivate();
	CHECK(QTest::qWaitForWindowActive(window));
	auto *box = window->findChild<QQuickItem *>(QStringLiteral("bindingBox"));
	CHECK(box != nullptr);
	if (box == nullptr) return 1;

	/* clicked, the box waits for a key */
	QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, box->mapToScene(QPointF(20, 10)).toPoint());
	CHECK(QTest::qWaitFor([&] { return box->hasActiveFocus(); }, 2000));
	/* a key is bound when it is let go; Escape at once; Ctrl+S as itself */
	QTest::keyClick(window, Qt::Key_A);
	QTest::keyClick(window, Qt::Key_Escape);
#ifdef __APPLE__
	/* the Control key is Qt's Meta there (Qt's Control is Command: key_names.cpp) */
	QTest::keyClick(window, Qt::Key_S, Qt::MetaModifier);
#else
	QTest::keyClick(window, Qt::Key_S, Qt::ControlModifier);
#endif
	QTest::qWait(100);
	const QString taken = window->property("taken").toString();
	std::printf("taken: %s, shortcuts: %d\n", qPrintable(taken), window->property("shortcuts").toInt());
	CHECK(taken == QStringLiteral("A,Escape,Ctrl+S"));
	CHECK(window->property("shortcuts").toInt() == 0);

	std::printf("%d checks, %d failed\n", checks, failures);
	return failures == 0 ? 0 : 1;
}
