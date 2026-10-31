#include "chimera/vm/customize_vm.hpp"

#include "chimera/vm/json.hpp"

namespace chimera::vm {

using json::Json;

CustomizeViewModel::CustomizeViewModel(Dispatcher &dispatcher, SessionViewModel &session)
	: ViewModel("customize", dispatcher), session_(session)
{
	expose("startPaused", "the first machine opened waits paused; after that a machine opened runs when the last was "
		"running", startPaused, true);
	expose("startFullscreen", "the window opens full screen (read at the start)", startFullscreen, true);
	expose("neverAskSave", "changes not saved are let go without asking when a project is closed, another opened or "
		"the program quit", neverAskSave, true);
	expose("luaDuringTurbo", "scripts are run while turbo is held", luaDuringTurbo, true);
	expose("showContextMenu", "a right click on the picture opens the window's menu (Enable Context Menu)", showContextMenu, true);
	expose("alternateLayouts", "a key is named by what the keyboard's layout makes it rather than by its place, on Windows "
		"(Handle alternate keyboard layouts)", alternateLayouts, true);
	expose("mergeModifiers", "both Shift keys are \"Shift\" rather than \"LeftShift\" and \"RightShift\", likewise Ctrl and Alt "
		"(Merge L+R modifier keys)", mergeModifiers, true);
	expose("singleInstance", "a second start of the program hands its files to the window already running and ends, on "
		"Windows (Single instance mode; read when the program starts)", singleInstance, true);
	expose("gameCubeAdapter", "Wii U and Switch GameCube adapters are read, through SDL's USB reading (Enable Wii U/Switch "
		"GameCube Adapter Support; read when the program starts)", gameCubeAdapter, true);
	expose("language", "the language the window's words are in (\"en\" English, \"fr\", \"pt_BR\"), one of languages; "
		"at the first open the first of the system's languages the window has", language, true);
	expose("languages", "the languages the window has, a line each: its code, a tab, its name in itself and in English "
		"(\"Deutsch - German\"; English alone)", languages);
	command("modifierKeys", "the names Chimera's rewrite replaces when the modifier keys are merged (true) or separated "
		"(false), as {\"LeftShift\": \"Shift\"}", {"merging"}, [](const Args &a, std::string &error) -> Value {
			bool merging = true;
			if (!arg(a, 0, merging, error)) return {};
			Json out;
			out.kind = Json::Kind::Object;
			for (const auto &[from, to] : modifierKeys(merging)) out.members.emplace_back(from, Json::string(to));
			return json::write(out);
		});
	/* the person's pause, not the machine's: a stop while a machine is open
	 * counts, unless it is the machine being replaced by the next one (the
	 * open says so in the same turn) */
	runningWatch_ = session_.running.changed.connect([this] {
		if (!session_.isOpen.get()) return;
		if (session_.running.get())
		{
			runOnOpen_ = true;
			return;
		}
		const uint64_t seen = opensSeen_;
		std::weak_ptr<int> alive = alive_;
		dispatcher_.post([this, seen, alive] {
			if (!alive.expired() && opensSeen_ == seen) runOnOpen_ = false;
		});
	});
	opensWatch_ = session_.opens.changed.connect([this] {
		opensSeen_++;
		const bool run = firstOpen_ ? !startPaused.get() : runOnOpen_;
		firstOpen_ = false;
		runOnOpen_ = run;
		if (run) say("play");
	});
}

std::map<std::string, std::string> CustomizeViewModel::modifierKeys(bool merging)
{
	if (merging)
		return {{"LeftCtrl", "Ctrl"}, {"RightCtrl", "Ctrl"}, {"LeftAlt", "Alt"}, {"RightAlt", "Alt"}, {"LeftShift", "Shift"},
			{"RightShift", "Shift"}};
	return {{"Ctrl", "LeftCtrl"}, {"Alt", "LeftAlt"}, {"Shift", "LeftShift"}};
}

void CustomizeViewModel::say(const char *command)
{
	std::string error;
	session_.run(command, {}, error);
}

} // namespace chimera::vm
