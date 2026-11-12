/* A key a hotkey takes is the hotkey's and not a menu's (issue 405), in a
 * window with a menu bar as the main window's, in the window's own style:
 * Alt+V opens the View menu while nothing takes it, and runs the hotkey
 * bound to it without opening the menu. The menus' letters are drawn from
 * Alt pressed until a click, and a menu's text without its letter is as Qt
 * hides one. A key held on a button clicks it once, its repeats kept from it
 * (issue 494). Unbound arrows move TAStudio's roll (issue 542). */
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
#include "../key_names.hpp"
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

// a stand-in for TAStudio's piano roll, which has the keyboard (issue 542)
const char *const kRoll = R"(
import QtQuick
Window {
    width: 200; height: 100; visible: true
    property int moves: 0
    property bool extended: false
    Item {
        anchors.fill: parent
        focus: true
        function navigate(key: int, extend: bool): bool { moves++; extended = extend; return true }
    }
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

#ifndef __APPLE__
	/* nothing takes Alt+V: its letter opens the View menu (a Mac's menus have
	 * no Alt letters, and Qt turns the mnemonics off there) */
	QTest::keyClick(window, Qt::Key_V, Qt::AltModifier);
	CHECK(QTest::qWaitFor(opened, 2000));
	QMetaObject::invokeMethod(view, "close");
	CHECK(QTest::qWaitFor([&] { return !opened(); }, 2000));
#endif

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

	/* the roll's keys (issue 542): Down that nothing is bound to moves the
	 * roll's picked frame, Shift+Down extends it; a hotkey bound to Down
	 * takes it as before, and the roll is not moved */
	{
		QQmlApplicationEngine rollEngine;
		rollEngine.loadData(kRoll);
		auto *rollWindow = qobject_cast<QQuickWindow *>(rollEngine.rootObjects().value(0));
		CHECK(rollWindow != nullptr);
		if (rollWindow == nullptr) return 1;
		chimera::qt::KeyFilter rollKeys(session, hotkeys, input);
		rollWindow->installEventFilter(&rollKeys);
		rollWindow->requestActivate();
		CHECK(QTest::qWaitForWindowActive(rollWindow));
		QTest::keyClick(rollWindow, Qt::Key_Down);
		CHECK(rollWindow->property("moves").toInt() == 1 && !rollWindow->property("extended").toBool());
		QTest::keyClick(rollWindow, Qt::Key_Down, Qt::ShiftModifier);
		CHECK(rollWindow->property("moves").toInt() == 2 && rollWindow->property("extended").toBool());
		int downs = 0;
		hotkeys.add("General", "Roll Key Test", "Down", [&](bool down) { downs += down ? 1 : 0; });
		QTest::keyClick(rollWindow, Qt::Key_Down);
		QTest::qWait(100);
		CHECK(rollWindow->property("moves").toInt() == 2 && downs == 1);
	}

	/* Alt held while it is bound (issue 573): each repeat (a release and a
	 * press, as Windows sends them) is forgotten for the menus, as the first
	 * press is - Qt's menu bar on Windows waits for Alt to be let go again with
	 * each, and took the keyboard when it was, so plain letters opened menus */
	{
		struct Forgets : QObject
		{
			int count = 0;
			bool eventFilter(QObject *o, QEvent *e) override
			{
				if (e->type() == QEvent::ActivationChange && !o->isWindowType()) count++;
				return false;
			}
		} forgets;
		const QKeyEvent probe(QEvent::KeyPress, Qt::Key_Alt, Qt::AltModifier);
		const QString alt = chimera::qt::keyCombination(chimera::qt::keyName(probe), Qt::AltModifier);
		int alts = 0;
		hotkeys.add("General", "Alt Held Test", alt.toStdString(), [&](bool down) { alts += down ? 1 : 0; });
		app.installEventFilter(&forgets);
		window->requestActivate();
		CHECK(QTest::qWaitForWindowActive(window));
		auto altEvent = [&](QEvent::Type type, Qt::KeyboardModifiers mods, bool repeat) {
			QKeyEvent k(type, Qt::Key_Alt, mods, QString(), repeat);
			QCoreApplication::sendEvent(window, &k);
		};
		altEvent(QEvent::KeyPress, Qt::AltModifier, false);
		const int first = forgets.count;
		for (int i = 0; i < 4; i++)
		{
			altEvent(QEvent::KeyRelease, Qt::NoModifier, true);
			altEvent(QEvent::KeyPress, Qt::AltModifier, true);
		}
		altEvent(QEvent::KeyRelease, Qt::NoModifier, false);
		std::printf("bound %s held with 4 repeats: forgotten %d times for the menus, the hotkey ran %d times\n",
			alt.toUtf8().constData(), forgets.count, alts);
		CHECK(first == 1 && forgets.count == 5 && alts == 1);
		app.removeEventFilter(&forgets);
	}

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
