#include "key_names.hpp"

#include "chimera/vm/gamepads.hpp"

#include <algorithm>
#include <cstdint>
#include <iterator>
#include <set>

#ifdef _WIN32
#include <windows.h>
#endif

namespace chimera::qt {

namespace {
struct KeyCode
{
	uint32_t code;
	const char *name;
};
#include "key_codes.inc"

template <size_t N>
const char *lookup(const KeyCode (&table)[N], uint32_t code)
{
	for (const KeyCode &k : table)
		if (k.code == code) return k.name;
	return nullptr;
}

bool alternateLayouts = false, mergeModifiers = true;
/* the modifier keys held, by their own names (LeftShift, RightCtrl...) */
std::set<QString> held;

/* the modifiers Chimera writes before a key, in its order (Super keys are
 * left out, as no key is ever named so: the Windows keys are LeftWin and
 * RightWin, which Chimera does not count as modifiers) */
const char *const mergedModifiers[] = {"Ctrl", "Alt", "Shift"};
const char *const unmergedModifiers[] = {"LeftCtrl", "RightCtrl", "LeftAlt", "RightAlt", "LeftShift", "RightShift"};

/* "Ctrl", "Alt" or "Shift" for a modifier key, either side; "" for any other */
QString kindOf(const QString &name)
{
	for (const char *kind : mergedModifiers)
		if (name == QLatin1String(kind) || name == QLatin1String("Left") + QLatin1String(kind) ||
			name == QLatin1String("Right") + QLatin1String(kind))
			return QLatin1String(kind);
	return {};
}

/* Qt's flag for a kind of modifier held */
Qt::KeyboardModifier flagOf(const QString &kind)
{
	if (kind == QLatin1String("Shift")) return Qt::ShiftModifier;
	if (kind == QLatin1String("Alt")) return Qt::AltModifier;
#ifdef __APPLE__
	/* the Control key: Qt's Control is the Command key there, which Chimera names a Windows key */
	return Qt::MetaModifier;
#else
	return Qt::ControlModifier;
#endif
}

/* the name, both sides one when they are merged */
QString merged(const QString &name)
{
	if (!mergeModifiers) return name;
	const QString kind = kindOf(name);
	return kind.isEmpty() ? name : kind;
}

/* a key's name by its place, as Chimera's host input reads it on each
 * system; null when the place names no key, or the event has no place */
const char *byPlace(quint32 scanCode, quint32 virtualKey)
{
#if defined(_WIN32)
	if (scanCode == 0) return nullptr;
	/* NumLock and Pause have one scan code in a window's key messages, told
	 * apart by the extended bit the other way from Chimera's raw input:
	 * their virtual keys say which is which */
	if (virtualKey == VK_PAUSE) return "Pause";
	if (virtualKey == VK_NUMLOCK) return "NumLock";
	const uint32_t raw = (scanCode & 0xFF) | ((scanCode & 0xFF00) == 0xE000 ? 0x80 : 0);
	const char *name = lookup(windowsKeys, raw);
	/* as Chimera does: the key the layout makes it,
	 * but for the keys it keeps by their place, and the modifier keys (it
	 * names both Alt keys Windows keys there) */
	if (alternateLayouts && name != nullptr && kindOf(QLatin1String(name)).isEmpty() &&
		std::find(std::begin(windowsKeepScanCodes), std::end(windowsKeepScanCodes), raw) == std::end(windowsKeepScanCodes))
		return lookup(windowsLayoutKeys, virtualKey);
	return name;
#elif defined(__APPLE__)
	(void)scanCode;
	/* a virtual key code of 0 is the A key: only the system's own events have one */
	if (QGuiApplication::platformName() != QLatin1String("cocoa")) return nullptr;
	return lookup(macKeys, virtualKey);
#else
	(void)virtualKey;
	if (scanCode == 0) return nullptr;
	return lookup(linuxKeys, scanCode);
#endif
}

/* a key's name by Qt's key, for a key with no place: the name the key the
 * layout makes it has on a US keyboard; a modifier is the left one */
QString byQtKey(int key, Qt::KeyboardModifiers modifiers)
{
	const bool keypad = modifiers.testFlag(Qt::KeypadModifier);
	if (key >= Qt::Key_A && key <= Qt::Key_Z) return QString(QChar('A' + (key - Qt::Key_A)));
	if (key >= Qt::Key_0 && key <= Qt::Key_9)
		return (keypad ? QStringLiteral("Keypad") : QStringLiteral("Number")) + QString::number(key - Qt::Key_0);
	if (key >= Qt::Key_F1 && key <= Qt::Key_F24) return QStringLiteral("F") + QString::number(key - Qt::Key_F1 + 1);
	switch (key)
	{
	case Qt::Key_Up: return QStringLiteral("Up");
	case Qt::Key_Down: return QStringLiteral("Down");
	case Qt::Key_Left: return QStringLiteral("Left");
	case Qt::Key_Right: return QStringLiteral("Right");
	case Qt::Key_Return: return QStringLiteral("Enter");
	case Qt::Key_Enter: return keypad ? QStringLiteral("KeypadEnter") : QStringLiteral("Enter");
	case Qt::Key_Space: return QStringLiteral("Space");
	case Qt::Key_Tab: return QStringLiteral("Tab");
	case Qt::Key_Backspace: return QStringLiteral("Backspace");
	case Qt::Key_Escape: return QStringLiteral("Escape");
	case Qt::Key_Insert: return QStringLiteral("Insert");
	case Qt::Key_Delete: return QStringLiteral("Delete");
	case Qt::Key_Home: return QStringLiteral("Home");
	case Qt::Key_End: return QStringLiteral("End");
	case Qt::Key_PageUp: return QStringLiteral("PageUp");
	case Qt::Key_PageDown: return QStringLiteral("PageDown");
	case Qt::Key_Pause: return QStringLiteral("Pause");
	case Qt::Key_CapsLock: return QStringLiteral("CapsLock");
	case Qt::Key_NumLock: return QStringLiteral("NumLock");
	case Qt::Key_ScrollLock: return QStringLiteral("ScrollLock");
	case Qt::Key_Shift: return QStringLiteral("LeftShift");
	case Qt::Key_Control: return QStringLiteral("LeftCtrl");
	case Qt::Key_Alt: return QStringLiteral("LeftAlt");
	case Qt::Key_Semicolon: return QStringLiteral("Semicolon");
	case Qt::Key_Equal: return QStringLiteral("Equals");
	case Qt::Key_Comma: return QStringLiteral("Comma");
	case Qt::Key_Minus: return keypad ? QStringLiteral("KeypadSubtract") : QStringLiteral("Minus");
	case Qt::Key_Period: return keypad ? QStringLiteral("KeypadDecimal") : QStringLiteral("Period");
	case Qt::Key_Slash: return keypad ? QStringLiteral("KeypadDivide") : QStringLiteral("Slash");
	case Qt::Key_Asterisk: return QStringLiteral("KeypadMultiply");
	case Qt::Key_Plus: return QStringLiteral("KeypadAdd");
	case Qt::Key_QuoteLeft: return QStringLiteral("Backtick");
	case Qt::Key_BracketLeft: return QStringLiteral("LeftBracket");
	case Qt::Key_BracketRight: return QStringLiteral("RightBracket");
	case Qt::Key_Backslash: return QStringLiteral("Backslash");
	case Qt::Key_Apostrophe: return QStringLiteral("Apostrophe");
	default: return {};
	}
}
} // namespace

void setKeyNaming(bool alternate, bool merge)
{
	alternateLayouts = alternate;
	mergeModifiers = merge;
}

QString keyName(int key, Qt::KeyboardModifiers modifiers, quint32 scanCode, quint32 virtualKey)
{
	if (const char *name = byPlace(scanCode, virtualKey)) return merged(QLatin1String(name));
	return merged(byQtKey(key, modifiers));
}

QString keyCombination(const QString &name, Qt::KeyboardModifiers modifiers)
{
	if (name.isEmpty()) return name;
	const QString own = kindOf(name);
	QString before;
	if (mergeModifiers)
	{
		for (const char *m : mergedModifiers)
			if (own != QLatin1String(m) && modifiers.testFlag(flagOf(QLatin1String(m)))) before += QLatin1String(m) + QLatin1Char('+');
		return before + name;
	}
	/* the sides held, as followed; a kind Qt says is held with no side
	 * seen going down (pressed before the window had the keyboard) is the left */
	for (const char *kind : mergedModifiers)
	{
		const QString k = QLatin1String(kind);
		if (!modifiers.testFlag(flagOf(k))) continue;
		bool any = false;
		for (const char *m : unmergedModifiers)
			if (kindOf(QLatin1String(m)) == k && held.count(QLatin1String(m)) != 0)
			{
				any = true;
				if (name != QLatin1String(m)) before += QLatin1String(m) + QLatin1Char('+');
			}
		if (!any && own != k) before += QLatin1String("Left") + k + QLatin1Char('+');
	}
	return before + name;
}

QString shownKeys(const QString &keys, bool mac)
{
	if (!mac) return keys;
	static const struct { const char *name, *shown; } macNames[] = {
		{"LeftCtrl", "Left Control"}, {"RightCtrl", "Right Control"}, {"Ctrl", "Control"},
		{"LeftAlt", "Left Option"}, {"RightAlt", "Right Option"}, {"Alt", "Option"},
		{"LeftWin", "Left Command"}, {"RightWin", "Right Command"},
	};
	/* a whole name only, between the separators of a binding ("Ctrl+Shift+S, F1") */
	QString out;
	qsizetype at = 0;
	while (at < keys.size())
	{
		qsizetype end = at;
		while (end < keys.size() && keys[end] != QLatin1Char('+') && keys[end] != QLatin1Char(',')) end++;
		const QString part = keys.mid(at, end - at);
		const QString name = part.trimmed();
		QString shown = part;
		for (const auto &m : macNames)
			if (name == QLatin1String(m.name))
			{
				shown = part.left(part.indexOf(name)) + QLatin1String(m.shown) + part.mid(part.indexOf(name) + name.size());
				break;
			}
		out += shown;
		if (end < keys.size()) out += keys[end];
		at = end + 1;
	}
	return out;
}

QString KeyNamer::shown(const QString &keys, const QString &padFaces) const
{
	const QString pads = QString::fromStdString(vm::Gamepads::shown(keys.toStdString(), padFaces.toStdString()));
#ifdef __APPLE__
	return shownKeys(pads, true);
#else
	return shownKeys(pads, false);
#endif
}

QString KeyNamer::name(int key, int modifiers, bool combination) const
{
	const auto mods = Qt::KeyboardModifiers(modifiers);
	/* the place of the event being delivered, which is this one */
	const QString n = key == lastKey_ ? keyName(key, mods, lastScanCode_, lastVirtualKey_) : merged(byQtKey(key, mods));
	return combination ? keyCombination(n, mods) : n;
}

bool KeyNamer::eventFilter(QObject *watched, QEvent *e)
{
	if (e->type() == QEvent::KeyPress || e->type() == QEvent::KeyRelease)
	{
		const auto *k = static_cast<QKeyEvent *>(e);
		lastKey_ = k->key();
		lastScanCode_ = k->nativeScanCode();
		lastVirtualKey_ = k->nativeVirtualKey();
		if (!k->isAutoRepeat())
			if (const char *place = byPlace(lastScanCode_, lastVirtualKey_); place != nullptr && !kindOf(QLatin1String(place)).isEmpty())
			{
				if (e->type() == QEvent::KeyPress) held.insert(QLatin1String(place));
				else held.erase(QLatin1String(place));
			}
	}
	/* a key let go while another program has the keyboard is never seen */
	else if (e->type() == QEvent::ApplicationStateChange && QGuiApplication::applicationState() != Qt::ApplicationActive)
		held.clear();
	return QObject::eventFilter(watched, e);
}

} // namespace chimera::qt
