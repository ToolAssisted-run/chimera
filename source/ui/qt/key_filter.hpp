#pragma once

#include <map>

#include <QEvent>
#include <QObject>
#include <QString>

#include "chimera/vm/host_input_vm.hpp"
#include "chimera/vm/hotkeys_vm.hpp"
#include "chimera/vm/session_vm.hpp"

class QKeyEvent;

namespace chimera::qt {

/* Keys pressed in the window go to the machine, by Chimera's key names, and
 * to the hotkeys, named with the modifiers held as Chimera writes them
 * ("Ctrl+Shift+V"); both, as Chimera does by default. A key typed into a
 * text field is the field's. A window that loses the keyboard lets go of
 * every key it held.
 *
 * A key a hotkey or a control takes is theirs and not a menu's: Alt+V bound
 * to a hotkey runs it and does not open the View menu, and Alt let go after
 * another key, or Alt that is itself bound, does not take the keyboard to
 * the menus, as the old window did (issue 405). Installed on the windows
 * whose keys play: the main window and TAStudio. */
class KeyFilter : public QObject
{
public:
	KeyFilter(vm::SessionViewModel &session, vm::HotkeysViewModel &hotkeys, vm::HostInputViewModel &input)
		: session_(session), hotkeys_(hotkeys), input_(input) {}

	/* a pointer button over the picture, routed as a key is (no modifiers) */
	void button(const QString &name, bool down);

protected:
	bool eventFilter(QObject *watched, QEvent *e) override;

private:
	vm::SessionViewModel &session_;
	vm::HotkeysViewModel &hotkeys_;
	vm::HostInputViewModel &input_;
	struct Held
	{
		QString combo;
		bool hotkey = true, control = true;
	};
	std::map<QString, Held> down_;
	/* what a press goes to; taken when it goes to something bound */
	vm::HotkeysViewModel::Route routeOf(const QString &name, const QString &combo, bool &taken) const;
	/* the menu bar's waiting for Alt to be let go, forgotten: Qt's menu bar
	 * (on Windows) forgets it at an event such as this, sent through the
	 * application to an object of no consequence */
	QObject forgetter_;
	void forgetAltForMenus();
	/* the key went to a text field being typed in */
	static bool typing(const QKeyEvent &k);
};

/* A key held on a button clicks it once, when it is let go, as a Windows
 * button does. A held key comes again and again as a release and a press
 * marked as repeats (on Windows and X11 alike), and Qt Quick's buttons click
 * at every release, repeat or not: Space held on TAStudio's Add Branch made
 * 20 to 30 branches (issue 494). The repeats are kept from every button and
 * check box of the application. */
class HeldKeyClicksOnce : public QObject
{
protected:
	bool eventFilter(QObject *watched, QEvent *e) override;
};

} // namespace chimera::qt
