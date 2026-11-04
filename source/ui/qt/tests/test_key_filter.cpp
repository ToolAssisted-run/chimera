/* A key a hotkey takes is the hotkey's and not a menu's (issue 405), in a
 * window with a menu bar as the main window's, in the window's own style:
 * Alt+V opens the View menu while nothing takes it, and runs the hotkey
 * bound to it without opening the menu. The menus' letters are drawn from
 * Alt pressed until a click, and a menu's text without its letter is as Qt
 * hides one. A key held on a button clicks it once, its repeats kept from it
 * (issue 494). */
#include <cstdio>
#include <functional>

#include <QGuiApplication>
#include <QKeyEvent>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QTest>

#include "../key_filter.hpp"
#include "../menu_letters.hpp"
#include "chimera/vm/input_log.hpp"

namespace {

int failures = 0, checks = 0;
#define CHECK(c) \
	do { \
		checks++; \
		if (!(c)) { std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); failures++; } \
	} while (0)

/* what the view models post runs on the window's thread, as in the window */
class Posted : public chimera::vm::Dispatcher
{
public:
	void post(std::function<void()> fn) override { QMetaObject::invokeMethod(qApp, std::move(fn), Qt::QueuedConnection); }
};

const char *const kWindow = R"(
import QtQuick
import QtQuick.Controls
ApplicationWindow {
    width: 320; height: 200; visible: true
    menuBar: MenuBar {
        Menu { title: "&File"; MenuItem { text: "&Open" } }
        Menu { objectName: "viewMenu"; title: "&View"; MenuItem { text: "&Size" } }
        delegate: MenuBarItem { objectName: "bar:" + text }
    }
    Item { anchors.fill: parent; focus: true }
}
)";

const char *const kButton = R"(
import QtQuick
import QtQuick.Controls
Window {
    id: win
    width: 200; height: 100; visible: true
    property int clicks: 0
    Button { text: "Add"; focus: true; onClicked: win.clicks++ }
}
)";

} // namespace

int main(int argc, char **argv)
{
	using chimera::qt::MenuLetters;
	QGuiApplication app(argc, argv);
	QQuickStyle::setStyle(QStringLiteral("ChimeraStyle"));

	CHECK(MenuLetters::plain(QStringLiteral("&File")) == QStringLiteral("File"));
	CHECK(MenuLetters::plain(QStringLiteral("Save && &Quit")) == QStringLiteral("Save & Quit"));
	CHECK(MenuLetters::plain(QStringLiteral("Open (&O)")) == QStringLiteral("Open"));
	CHECK(MenuLetters::plain(QString::fromUtf8("\xE3\x83\x95\xE3\x82\xA1\xE3\x82\xA4\xE3\x83\xAB(&F)")) ==
		QString::fromUtf8("\xE3\x83\x95\xE3\x82\xA1\xE3\x82\xA4\xE3\x83\xAB"));
	CHECK(MenuLetters::plain(QStringLiteral("No letter")) == QStringLiteral("No letter"));

	Posted d;
	chimera::vm::InputLog inputs;
	chimera::vm::SessionViewModel session(d, inputs);
	chimera::vm::HotkeysViewModel hotkeys(d);
	chimera::vm::HostInputViewModel input(d, session);
	MenuLetters letters;
	app.installEventFilter(&letters);

	QQmlApplicationEngine engine;
	engine.rootContext()->setContextProperty(QStringLiteral("menuLetters"), &letters);
	engine.loadData(kWindow);
	auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().value(0));
	CHECK(window != nullptr);
	if (window == nullptr) return 1;
	chimera::qt::KeyFilter keys(session, hotkeys, input);
	window->installEventFilter(&keys);
	window->requestActivate();
	CHECK(QTest::qWaitForWindowActive(window));
	QObject *view = window->findChild<QObject *>(QStringLiteral("viewMenu"));
	CHECK(view != nullptr);
	if (view == nullptr) return 1;
	auto opened = [&] { return view->property("opened").toBool(); };

	/* nothing takes Alt+V: its letter opens the View menu */
	QTest::keyClick(window, Qt::Key_V, Qt::AltModifier);
	CHECK(QTest::qWaitFor(opened, 2000));
	QMetaObject::invokeMethod(view, "close");
	CHECK(QTest::qWaitFor([&] { return !opened(); }, 2000));

	/* a hotkey bound to it runs, and the menu stays shut */
	int ran = 0;
	hotkeys.add("General", "Key Filter Test", "Alt+V", [&](bool down) { ran += down ? 1 : 0; });
	CHECK(hotkeys.boundTo("Alt+V"));
	QTest::keyClick(window, Qt::Key_V, Qt::AltModifier);
	QTest::qWait(300);
	CHECK(ran == 1 && !opened());

	/* the letters: drawn from Alt pressed, not after a click; Alt let go
	 * with no menu taken, not either */
	QObject *viewItem = window->findChild<QObject *>(QStringLiteral("bar:&View"));
	CHECK(viewItem != nullptr);
	/* the text kept whole, its letter underlined by Qt's label or not (issue 471) */
	auto underlined = [&] {
		QObject *content = viewItem != nullptr ? viewItem->property("contentItem").value<QObject *>() : nullptr;
		QObject *label = content != nullptr ? content->findChild<QObject *>(QStringLiteral("label")) : nullptr;
		return label != nullptr && content->property("text").toString() == QStringLiteral("&View") && label->property("mnemonicVisible").toBool();
	};
	CHECK(!letters.shown() && !underlined());
	QTest::keyPress(window, Qt::Key_Alt, Qt::AltModifier);
	CHECK(letters.shown() && underlined());
	QTest::keyRelease(window, Qt::Key_Alt, Qt::NoModifier);
	CHECK(!letters.shown());
	QTest::keyPress(window, Qt::Key_Alt, Qt::AltModifier);
	CHECK(letters.shown());
	QTest::mouseClick(window, Qt::LeftButton, Qt::AltModifier, QPoint(160, 150));
	CHECK(!letters.shown());
	QTest::keyRelease(window, Qt::Key_Alt, Qt::NoModifier);

	/* Space held on a button: pressed, five repeats (each a release and a
	 * press, as Windows and X11 send them), let go - one click */
	chimera::qt::HeldKeyClicksOnce held;
	app.installEventFilter(&held);
	QQmlApplicationEngine buttonEngine;
	buttonEngine.loadData(kButton);
	auto *buttonWindow = qobject_cast<QQuickWindow *>(buttonEngine.rootObjects().value(0));
	CHECK(buttonWindow != nullptr);
	if (buttonWindow == nullptr) return 1;
	buttonWindow->requestActivate();
	CHECK(QTest::qWaitForWindowActive(buttonWindow));
	auto send = [&](QEvent::Type type, bool repeat) {
		QKeyEvent k(type, Qt::Key_Space, Qt::NoModifier, QStringLiteral(" "), repeat);
		QCoreApplication::sendEvent(buttonWindow, &k);
	};
	send(QEvent::KeyPress, false);
	for (int i = 0; i < 5; i++)
	{
		send(QEvent::KeyRelease, true);
		send(QEvent::KeyPress, true);
	}
	send(QEvent::KeyRelease, false);
	QTest::qWait(50);
	const int clicks = buttonWindow->property("clicks").toInt();
	if (clicks != 1) std::fprintf(stderr, "Space held on a button clicked it %d times\n", clicks);
	CHECK(clicks == 1);

	std::printf("%d checks, %d failed\n", checks, failures);
	return failures == 0 ? 0 : 1;
}
