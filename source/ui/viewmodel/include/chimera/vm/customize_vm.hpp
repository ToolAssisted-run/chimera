#pragma once
/* Chimera's Customize window (Config > Customize), for what the window
 * has: how the machine behaves around the window. Each is remembered.
 *
 * - startPaused (ticked at first): a project opened or made waits paused
 *   (owner, 2026-11-02). Unticked, a project opened runs, or waits when the
 *   person paused the last one, as Chimera's pause is the person's and not
 *   the machine's. Either way the same project started again (Restart, a
 *   change that restarts it) runs or waits as it was left.
 * - startFullscreen: the window opens full screen (read at the start).
 * Nothing else pauses the machine: neither a menu open nor the window going
 *   behind another (owner, 2026-10-24, Chimera's Run in background and
 *   Pause when menu activated removed).
 * - neverAskSave: changes not saved are let go without asking when a
 *   project is closed, another opened or the program quit.
 * - luaDuringTurbo: scripts are run while turbo is held.
 * - showContextMenu: a right click on the picture opens the window's menu
 *   (Chimera's Enable Context Menu).
 * - alternateLayouts: a key is named by what the keyboard's layout makes it
 *   rather than by its place, on Windows (Chimera's Handle alternate
 *   keyboard layouts); keys are named by place on the others, as Chimera's.
 * - singleInstance: a second start of the program hands the files it is
 *   given to the window already running and ends, on Windows (Chimera's
 *   Single instance mode; read when the program starts).
 * - gameCubeAdapter: Wii U and Switch GameCube adapters are read, through
 *   SDL's own USB reading (Chimera's Enable Wii U/Switch GameCube Adapter
 *   Support; read when the program starts).
 * - mergeModifiers: both Shift keys are "Shift", rather than "LeftShift"
 *   and "RightShift", likewise Ctrl and Alt (Chimera's Merge L+R modifier
 *   keys). Changing it can rewrite the keys bound (modifierKeys).
 * - language: the language the window's words are in (Config > Language;
 *   "en" English), chosen from `languages`, the window's translations as
 *   "code<TAB>name" lines, English first. At the first open the window sets
 *   it to the first of the system's languages it has (owner, 2026-10-28,
 *   issue 376).
 * The frame advance skipping lag frames and background input are the
 * session's (skipLagFrames, acceptBackgroundInput). */

#include <cstdint>
#include <map>
#include <memory>
#include <string>

#include "chimera/vm/session_vm.hpp"
#include "chimera/vm/view_model.hpp"

namespace chimera::vm {

class CustomizeViewModel : public ViewModel
{
public:
	CustomizeViewModel(Dispatcher &dispatcher, SessionViewModel &session);

	Property<bool> startPaused{true}, startFullscreen{false}, neverAskSave{false}, luaDuringTurbo{true}, showContextMenu{true};
	Property<bool> alternateLayouts{false}, mergeModifiers{true}, singleInstance{false}, gameCubeAdapter{false};
	Property<std::string> language, languages{"en\tEnglish"};
	/* a run nobody watches (--headless): Start Paused does not hold its
	 * projects, which its script drives */
	void setUnattended(bool on) { unattended_ = on; }

	/* Chimera's rewrite of the keys bound when mergeModifiers changes: merging, each side's name becomes the kind's
	 * ("LeftShift" and "RightShift" "Shift"); separating, the kind's becomes
	 * the left's. Super's are left out: no key is named so. */
	static std::map<std::string, std::string> modifierKeys(bool merging);

	/* whether a script is to be run for the frame now: not while turbo is
	 * held, unless asked */
	bool luaRunsNow() const { return luaDuringTurbo.get() || !session_.turboHeld(); }

private:
	void say(const char *command);
	SessionViewModel &session_;
	/* a machine opened runs: a project by startPaused, a restart as the last was left */
	bool runOnOpen_ = true;
	/* a run nobody watches (--headless): nothing waits for a person, Start Paused included */
	bool unattended_ = false;
	uint64_t opensSeen_ = 0;
	std::shared_ptr<int> alive_ = std::make_shared<int>(0);
	Connection runningWatch_, opensWatch_;
};

} // namespace chimera::vm
