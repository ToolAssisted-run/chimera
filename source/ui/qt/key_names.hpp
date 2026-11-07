#pragma once

#include <QGuiApplication>
#include <QKeyEvent>
#include <QObject>
#include <QString>

namespace chimera::qt {

/* Chimera's two choices about key names (Customize): with alternate
 * keyboard layouts, a key on Windows is named by what the layout makes it
 * rather than by its place; with merged modifiers both Shift keys are
 * "Shift" rather than "LeftShift" and "RightShift", likewise Ctrl and Alt */
void setKeyNaming(bool alternateLayouts, bool mergeModifiers);

/* Chimera's name for a key, or "" for a key Chimera has no name for. As
 * Chimera's, a key is named by its place on the keyboard, as the key there
 * on a US keyboard: its scan code (on macOS its virtual key code). A key
 * the place does not name, and a key event no keyboard sent, are named by
 * Qt's key. */
QString keyName(int key, Qt::KeyboardModifiers modifiers, quint32 scanCode, quint32 virtualKey);
inline QString keyName(const QKeyEvent &e) { return keyName(e.key(), e.modifiers(), e.nativeScanCode(), e.nativeVirtualKey()); }

/* the name with the modifier keys held before it, as Chimera writes a
 * hotkey ("Ctrl+Shift+V", in Chimera's order); a modifier with the others
 * held ("Ctrl+Shift" for Shift pressed while Ctrl is held) */
QString keyCombination(const QString &name, Qt::KeyboardModifiers modifiers);

/* keys as they are shown: on a Mac the modifier keys by the names on its
 * keyboard (issue 538) - Ctrl is Control, Alt is Option, and the keys
 * Chimera names LeftWin and RightWin are Command - and everywhere else the
 * names as they are. Only what is shown: settings and movies keep
 * Chimera's names, the same on every system. */
QString shownKeys(const QString &keys, bool mac);

/* A key's name for QML, as the window's key filter names it: bare for a
 * machine's button, with the modifiers held for a hotkey. Installed on the
 * application, it sees every key event first: it follows which modifier
 * keys are held, and keeps the place of the key event being delivered,
 * which QML's key events do not carry. */
class KeyNamer : public QObject
{
	Q_OBJECT
public:
	using QObject::QObject;
	Q_INVOKABLE QString name(int key, int modifiers, bool combination) const;
	/* keys as this system shows them (shownKeys) */
	Q_INVOKABLE QString shown(const QString &keys) const;
	/* Shift held now, whichever window has the keyboard */
	Q_INVOKABLE bool shiftHeld() const { return QGuiApplication::queryKeyboardModifiers().testFlag(Qt::ShiftModifier); }

protected:
	bool eventFilter(QObject *watched, QEvent *e) override;

private:
	int lastKey_ = 0;
	quint32 lastScanCode_ = 0, lastVirtualKey_ = 0;
};

/* Hands a hotkey whose action is the window's (a menu's, a dialog's) to the
 * window: the key is the hotkeys' to bind, what it does is the menu's. */
class HotkeyRelay : public QObject
{
	Q_OBJECT
public:
	using QObject::QObject;
	void press(const std::string &action) { Q_EMIT pressed(QString::fromStdString(action)); }

Q_SIGNALS:
	void pressed(const QString &action);
};

} // namespace chimera::qt
