/* Keys are named as Chimera names them: by their place on the keyboard, as
 * the key at that place on a US keyboard, whatever the layout makes it (a
 * French keyboard's A key, where a US keyboard has Q, is "Q"), or with
 * alternate keyboard layouts, on Windows, by what the layout makes it; the
 * modifier keys merged ("Shift") or each side its own ("LeftShift"); and a
 * combination written in Chimera's order with the sides held. Key events
 * are made here with the codes each system's keyboard gives. */
#include "../key_names.hpp"

#include <QCoreApplication>
#include <QGuiApplication>
#include <QKeyEvent>

#include <cstdio>

#ifdef _WIN32
#include <windows.h>
#endif

namespace {
int checks = 0, failed = 0;

void check(bool ok, const char *what, int line)
{
	checks++;
	if (ok) return;
	failed++;
	std::printf("FAIL %s:%d: %s\n", __FILE__, line, what);
}
#define CHECK(x) check((x), #x, __LINE__)

using chimera::qt::keyCombination;
using chimera::qt::keyName;
using chimera::qt::setKeyNaming;

/* a key going down or up on a keyboard, as the system's event gives it */
void send(QObject &to, QEvent::Type type, int key, Qt::KeyboardModifiers mods, quint32 scanCode, quint32 virtualKey)
{
	QKeyEvent e(type, key, mods, scanCode, virtualKey, 0);
	QCoreApplication::sendEvent(&to, &e);
}
} // namespace

int main(int argc, char **argv)
{
	QGuiApplication app(argc, argv);
	chimera::qt::KeyNamer namer;
	app.installEventFilter(&namer);
	QObject target;
	const auto none = Qt::NoModifier;

	/* a key event no keyboard sent: by Qt's key */
	setKeyNaming(false, true);
	CHECK(keyName(Qt::Key_A, none, 0, 0) == "A");
	CHECK(keyName(Qt::Key_1, none, 0, 0) == "Number1");
	CHECK(keyName(Qt::Key_1, Qt::KeypadModifier, 0, 0) == "Keypad1");
	CHECK(keyName(Qt::Key_Shift, none, 0, 0) == "Shift");
	setKeyNaming(false, false);
	CHECK(keyName(Qt::Key_Shift, none, 0, 0) == "LeftShift");
	setKeyNaming(false, true);

#if defined(_WIN32)
	/* Windows: the scan code, 0xE000 added for an extended one, and the layout's virtual key */
	CHECK(keyName(Qt::Key_Q, none, 0x10, 'Q') == "Q");
	/* French: the key at Q's place is A */
	CHECK(keyName(Qt::Key_A, none, 0x10, 'A') == "Q");
	CHECK(keyName(Qt::Key_Exclam, Qt::ShiftModifier, 0x02, '1') == "Number1");
	CHECK(keyName(Qt::Key_Control, none, 0xE01D, VK_CONTROL) == "Ctrl");
	CHECK(keyName(Qt::Key_Enter, Qt::KeypadModifier, 0xE01C, VK_RETURN) == "KeypadEnter");
	CHECK(keyName(Qt::Key_Return, none, 0x1C, VK_RETURN) == "Enter");
	CHECK(keyName(Qt::Key_End, Qt::KeypadModifier, 0x4F, VK_END) == "Keypad1");
	/* NumLock and Pause, one scan code in a window's messages */
	CHECK(keyName(Qt::Key_Pause, none, 0x45, VK_PAUSE) == "Pause");
	CHECK(keyName(Qt::Key_NumLock, none, 0xE045, VK_NUMLOCK) == "NumLock");
	setKeyNaming(false, false);
	CHECK(keyName(Qt::Key_Control, none, 0xE01D, VK_CONTROL) == "RightCtrl");
	CHECK(keyName(Qt::Key_Shift, none, 0x36, VK_SHIFT) == "RightShift");
	CHECK(keyName(Qt::Key_Alt, none, 0x38, VK_MENU) == "LeftAlt");
	/* alternate layouts: the layout's key; the keypad's and the modifiers by place */
	setKeyNaming(true, false);
	CHECK(keyName(Qt::Key_A, none, 0x10, 'A') == "A");
	CHECK(keyName(Qt::Key_End, Qt::KeypadModifier, 0x4F, VK_END) == "Keypad1");
	CHECK(keyName(Qt::Key_Alt, none, 0x38, VK_MENU) == "LeftAlt");
	CHECK(keyName(Qt::Key_AltGr, none, 0xE038, VK_MENU) == "RightAlt");
	setKeyNaming(false, true);
	const quint32 rightCtrl = 0xE01D, leftShift = 0x2A, rightShift = 0x36, keyS = 0x1F, keyQ = 0x10, keyA = 'A';
	const quint32 ctrlKey = VK_CONTROL, shiftKey = VK_SHIFT, sKey = 'S';
#elif defined(__APPLE__)
	std::printf("SKIP: the places of a Mac's keys are its own events'\n");
	/* (named so that what follows compiles; the test ends here) */
	const quint32 rightCtrl = 0, leftShift = 0, rightShift = 0, keyS = 0, keyQ = 0, keyA = 0;
	const quint32 ctrlKey = 0, shiftKey = 0, sKey = 0;
	(void)rightCtrl; (void)leftShift; (void)rightShift; (void)keyS; (void)keyQ; (void)keyA;
	(void)ctrlKey; (void)shiftKey; (void)sKey;
	return 0;
#else
	/* Linux: the XKB key code, as X11 and Wayland give it */
	CHECK(keyName(Qt::Key_Q, none, 24, 0) == "Q");
	/* French: the key at Q's place is A */
	CHECK(keyName(Qt::Key_A, none, 24, 0) == "Q");
	CHECK(keyName(Qt::Key_Exclam, Qt::ShiftModifier, 10, 0) == "Number1");
	CHECK(keyName(Qt::Key_Control, none, 105, 0) == "Ctrl");
	CHECK(keyName(Qt::Key_Enter, Qt::KeypadModifier, 104, 0) == "KeypadEnter");
	CHECK(keyName(Qt::Key_Escape, none, 9, 0) == "Escape");
	CHECK(keyName(Qt::Key_End, Qt::KeypadModifier, 87, 0) == "Keypad1");
	CHECK(keyName(Qt::Key_1, Qt::KeypadModifier, 87, 0) == "Keypad1");
	CHECK(keyName(Qt::Key_Super_L, none, 133, 0) == "LeftWin");
	setKeyNaming(false, false);
	CHECK(keyName(Qt::Key_Control, none, 105, 0) == "RightCtrl");
	CHECK(keyName(Qt::Key_Shift, none, 62, 0) == "RightShift");
	CHECK(keyName(Qt::Key_Alt, none, 64, 0) == "LeftAlt");
	/* alternate layouts are Windows' only, as Chimera's */
	setKeyNaming(true, false);
	CHECK(keyName(Qt::Key_A, none, 24, 0) == "Q");
	setKeyNaming(false, true);
	const quint32 rightCtrl = 105, leftShift = 50, rightShift = 62, keyS = 39, keyQ = 24, keyA = 0;
	const quint32 ctrlKey = 0, shiftKey = 0, sKey = 0;
#endif

	/* combinations, in Chimera's order */
	CHECK(keyCombination("X", Qt::ShiftModifier | Qt::ControlModifier | Qt::AltModifier) == "Ctrl+Alt+Shift+X");
	CHECK(keyCombination("Shift", Qt::ShiftModifier | Qt::ControlModifier) == "Ctrl+Shift");
	CHECK(keyCombination("Shift", Qt::ShiftModifier) == "Shift");
	CHECK(keyCombination("LeftWin", Qt::NoModifier) == "LeftWin");
	/* the sides held, followed from the keys going down and up */
	send(target, QEvent::KeyPress, Qt::Key_Control, Qt::ControlModifier, rightCtrl, ctrlKey);
	send(target, QEvent::KeyPress, Qt::Key_S, Qt::ControlModifier, keyS, sKey);
	CHECK(namer.name(Qt::Key_S, Qt::ControlModifier, true) == "Ctrl+S");
	setKeyNaming(false, false);
	CHECK(namer.name(Qt::Key_S, Qt::ControlModifier, true) == "RightCtrl+S");
	send(target, QEvent::KeyPress, Qt::Key_Shift, Qt::ControlModifier | Qt::ShiftModifier, leftShift, shiftKey);
	CHECK(namer.name(Qt::Key_Shift, Qt::ControlModifier | Qt::ShiftModifier, true) == "RightCtrl+LeftShift");
	send(target, QEvent::KeyPress, Qt::Key_Shift, Qt::ControlModifier | Qt::ShiftModifier, rightShift, shiftKey);
	CHECK(keyCombination("S", Qt::ControlModifier | Qt::ShiftModifier) == "RightCtrl+LeftShift+RightShift+S");
	send(target, QEvent::KeyRelease, Qt::Key_Shift, Qt::ControlModifier | Qt::ShiftModifier, leftShift, shiftKey);
	send(target, QEvent::KeyRelease, Qt::Key_Shift, Qt::ControlModifier, rightShift, shiftKey);
	send(target, QEvent::KeyRelease, Qt::Key_Control, Qt::NoModifier, rightCtrl, ctrlKey);
	CHECK(keyCombination("S", Qt::NoModifier) == "S");
	/* held before the window had the keyboard: Qt says so, the side unseen */
	CHECK(keyCombination("S", Qt::AltModifier) == "LeftAlt+S");
	/* QML's key events carry no place: the event being delivered gives it */
	setKeyNaming(false, true);
	send(target, QEvent::KeyPress, Qt::Key_A, none, keyQ, keyA);
	CHECK(namer.name(Qt::Key_A, none, false) == "Q");
	CHECK(namer.name(Qt::Key_B, none, false) == "B");

	/* keys as a Mac shows them (issue 538): its own names for the modifier
	 * keys, whole names only; elsewhere, and in what is kept, Chimera's */
	CHECK(chimera::qt::shownKeys("Ctrl+Shift+S", true) == "Control+Shift+S");
	CHECK(chimera::qt::shownKeys("LeftWin+Alt+F1, RightCtrl+Z", true) == "Left Command+Option+F1, Right Control+Z");
	CHECK(chimera::qt::shownKeys("RightWin", true) == "Right Command");
	CHECK(chimera::qt::shownKeys("LeftAlt+RightAlt+AltGr", true) == "Left Option+Right Option+AltGr");
	CHECK(chimera::qt::shownKeys("LeftWin+Ctrl+S", false) == "LeftWin+Ctrl+S");
	CHECK(chimera::qt::shownKeys("", true).isEmpty());
	std::printf("%s: %d checks, %d failed\n", failed == 0 ? "PASS" : "FAIL", checks, failed);
	return failed == 0 ? 0 : 1;
}
