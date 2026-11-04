#include "chimera/vm/piano_roll_vm.hpp"
#include "chimera/vm/tr.hpp"

#include "chimera/engine.h"
#include "chimera/vm/json.hpp"

#include <algorithm>
#include <iterator>
#include <fstream>
#include <sstream>

namespace chimera::vm {

namespace {
/* how long a movie may be made by painting or filling it: 2^31 frames, a
 * script's paint(0, 2^40, ...) otherwise grew it until memory ran out */
bool withinFillLimit(int64_t last, std::string &error)
{
	if (last < (int64_t{1} << 31)) return true;
	error = tr("piano_roll.movie_too_long");
	return false;
}
} // namespace

PianoRollViewModel::PianoRollViewModel(Dispatcher &dispatcher, InputLog &inputs, SessionViewModel &session)
	: ViewModel("pianoRoll", dispatcher), inputs_(inputs), session_(session)
{
	expose("currentFrame", "the frame the machine is about to run", currentFrame);
	expose("currentMarker", "the frame of the marker the machine is at or past, -1 before the first", currentMarker);
	expose("frameCount", "how many frames have inputs", frameCount);
	expose("followPlayback", "keep the current frame in view while the machine runs", followPlayback, true);
	expose("moveWithMainWindow", "TAStudio's window moves with the main window, keeping its place beside it", moveWithMainWindow, true);
	expose("mainSplit", "the width of TAStudio's side (its boxes), 0 for Chimera's", mainSplit, true);
	expose("sideSplit", "the height of TAStudio's Markers box, 0 for Chimera's", sideSplit, true);
	expose("followScroll", "how following scrolls: near, top, bottom or center", followScroll, true);
	expose("followAlways", "following scrolls on every frame, not only when the frame is out of view", followAlways, true);
	expose("wheelRows", "the frames a notch of the mouse wheel scrolls", wheelRows, true);
	expose("editHiddenColumns", "edits of frames change the hidden columns too; off, only the columns shown",
		editHiddenColumns, true);
	expose("scriptEditing", "the edits made now are a script's: they leave recording mode on, where an edit by hand turns it off",
		scriptEditing, true);
	expose("denoteStatesColor", "a frame whose state the greenzone holds has a colour of its own", denoteStatesColor, true);
	expose("denoteStatesIcons", "a frame whose state the greenzone holds has an icon in the frame column", denoteStatesIcons, true);
	expose("denoteMarkersColor", "a marker's frame number has the marker colour", denoteMarkersColor, true);
	expose("denoteMarkersIcons", "a marker's frame has an icon in the frame column", denoteMarkersIcons, true);
	expose("rollColors", "the roll's own colours over the theme's: a JSON object, a theme role each", rollColors, true);
	expose("rollFont", "the roll's font: {\"family\", \"pointSize\", \"bold\", \"italic\"}; \"\" for the window's", rollFont, true);
	/* the machine says which frames in view hold states */
	auto watch = [this] { session_.watchStates(topFrame.get(), visibleFrames.get() + 1); };
	viewWatch_ = topFrame.changed.connect(watch);
	viewCountWatch_ = visibleFrames.changed.connect(watch);
	statesWatch_ = session_.statesShown.connect([this] {
		const int64_t top = topFrame.get(), last = std::min(top + visibleFrames.get(), inputs_.size() - 1);
		if (last >= top) frames_.changed.emit(top, last);
	});
	expose("patternMode", "when painting follows a column's pattern: never, autofire (the Auto-hold columns) or always",
		patternMode, true);
	expose("patternKind", "the pattern a column's Auto-hold plays: hold, autofire or custom", patternKind, true);
	command("setPattern", "a control's own pattern: runs of values (\"On x2, Off x1\" for a button, \"127 x2, 0 x1\" for "
		"an analog control), the value it goes back to after the last, and whether lag frames are passed over",
		{"name", "runs", "loop", "skipLag"}, [this](const Args &a, std::string &error) -> Value {
			std::string name, runs;
			int64_t loop = 0;
			bool skipLag = true;
			if (!arg(a, 0, name, error) || !arg(a, 1, runs, error)) return {};
			if (a.size() > 2 && !arg(a, 2, loop, error)) return {};
			if (a.size() > 3 && !arg(a, 3, skipLag, error)) return {};
			const std::vector<std::string> &names = session_.buttonNames();
			Pattern *p = nullptr;
			bool isButton = false;
			for (size_t i = 0; i < names.size() && i < buttonPatterns_.size(); i++)
				if (names[i] == name) p = &buttonPatterns_[i], isButton = true;
			for (size_t i = 0; i < session_.axes().size() && i < axisPatterns_.size(); i++)
				if (session_.axes()[i].name == name) p = &axisPatterns_[i];
			if (p == nullptr)
			{
				error = tr("piano_roll.no_control", {name});
				return {};
			}
			/* "On x2, Off x1": a value each run, and how many times */
			Pattern made;
			made.skipLag = skipLag;
			size_t at = 0;
			while (at < runs.size())
			{
				size_t end = runs.find(',', at);
				std::string run = runs.substr(at, end == std::string::npos ? std::string::npos : end - at);
				at = end == std::string::npos ? runs.size() : end + 1;
				const size_t first = run.find_first_not_of(" \t"), last = run.find_last_not_of(" \t");
				if (first == std::string::npos) continue;
				run = run.substr(first, last - first + 1);
				int64_t times = 1;
				const size_t x = run.find_last_of("xX");
				if (x != std::string::npos && x + 1 < run.size() && run.find_first_not_of("0123456789", x + 1) == std::string::npos &&
					x > 0 && run[x - 1] == ' ')
				{
					/* a number std::stoll cannot hold ended the program */
					if (run.size() - x - 1 > 9)
					{
						error = tr("piano_roll.run_too_long", {run});
						return {};
					}
					times = std::stoll(run.substr(x + 1));
					run = run.substr(0, run.find_last_not_of(" ", x - 1) + 1);
				}
				int32_t value = 0;
				if (isButton)
				{
					std::string w = run;
					for (char &c : w) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
					if (w == "on" || w == "1" || w == "true") value = 1;
					else if (w == "off" || w == "0" || w == "false") value = 0;
					else
					{
						error = tr("piano_roll.run_not_on_off", {run});
						return {};
					}
				}
				else
				{
					char *stop = nullptr;
					const long v = std::strtol(run.c_str(), &stop, 10);
					if (run.empty() || stop == nullptr || *stop != '\0')
					{
						error = tr("piano_roll.run_not_number", {run});
						return {};
					}
					value = static_cast<int32_t>(v);
				}
				if (times < 1 || times > 100000)
				{
					error = tr("piano_roll.run_length");
					return {};
				}
				made.values.insert(made.values.end(), static_cast<size_t>(times), value);
			}
			if (made.values.empty())
			{
				error = tr("piano_roll.pattern_empty");
				return {};
			}
			if (loop < 0 || loop >= static_cast<int64_t>(made.values.size()))
			{
				error = tr("piano_roll.loop_range", {std::to_string(made.values.size() - 1)});
				return {};
			}
			made.loop = loop;
			*p = std::move(made);
			storePatterns();
			pushColumnHolds();
			session_.markChanged();
			return {};
		});
	command("toggleAutoHold", "a column's Auto-hold on or off (TAStudio's column menu): while the game plays its button "
		"is held, fired or follows its own pattern, as patternKind says, and painting it follows its pattern when "
		"patternMode is autofire. An analog column holds the value given (its most when none is)",
		{"name", "value"}, [this](const Args &a, std::string &error) -> Value {
			std::string name;
			int64_t value = 0;
			if (!arg(a, 0, name, error)) return {};
			const bool valueGiven = a.size() > 1;
			if (valueGiven && !arg(a, 1, value, error)) return {};
			bool known = false;
			for (int32_t c = 0; c < frames_.columnCount(); c++) known = known || frames_.columnName(c) == name;
			if (!known)
			{
				error = tr("piano_roll.no_column", {name});
				return {};
			}
			const bool on = autoHeld_.count(name) == 0;
			if (on && valueGiven)
				for (const SessionViewModel::Axis &ax : session_.axes())
					if (ax.name == name)
					{
						if (value < ax.min || value > ax.max)
						{
							error = tr("piano_roll.axis_range", {name, std::to_string(ax.min), std::to_string(ax.max)});
							return {};
						}
						axisHoldValues_[name] = static_cast<int32_t>(value);
						storePatterns();
						session_.markChanged();
					}
			if (on) autoHeld_.insert(name);
			else autoHeld_.erase(name);
			autoHeldChanged();
			return on;
		});
	expose("autoHoldMin", "the least value autoHoldQuestion's analog control takes", autoHoldMin);
	expose("autoHoldMax", "the most value autoHoldQuestion's analog control takes", autoHoldMax);
	expose("autoHoldInitial", "the value autoHoldQuestion offers: the control's on the current frame", autoHoldInitial);
	command("autoHoldQuestion", "what to ask before turning a column's Auto-hold on, as Chimera asks (chimera issue 298): "
		"for an analog control, unless its own pattern is played, the value to hold it at (autoHoldMin, autoHoldMax and "
		"autoHoldInitial say the rest); \"\" when there is nothing to ask", {"name"},
		[this](const Args &a, std::string &error) -> Value {
			std::string name;
			if (!arg(a, 0, name, error)) return {};
			if (autoHeld_.count(name) != 0 || patternKind.get() == "custom") return std::string();
			const auto &axes = session_.axes();
			for (size_t x = 0; x < axes.size(); x++)
				if (axes[x].name == name)
				{
					const int64_t f = currentFrame.get();
					int64_t now = axes[x].neutral;
					if (f >= 0 && f < inputs_.size())
					{
						std::vector<int32_t> values(inputs_.axisCount());
						if (!values.empty()) inputs_.axesAt(f, values.data());
						if (x < values.size()) now = values[x];
					}
					autoHoldMin.set(axes[x].min);
					autoHoldMax.set(axes[x].max);
					autoHoldInitial.set(now);
					return tr("piano_roll.hold_at", {name, std::to_string(axes[x].min), std::to_string(axes[x].max)});
				}
			return std::string();
		});
	holdWatches_.push_back(session_.autoHeldColumnsLoaded.connect([this](const std::vector<std::string> &names) {
		autoHeld_ = std::set<std::string>(names.begin(), names.end());
		autoHeldChanged();
	}));
	holdWatches_.push_back(patternKind.changed.connect([this] { pushColumnHolds(); }));
	holdWatches_.push_back(session_.autofireOn.changed.connect([this] { pushColumnHolds(); }));
	holdWatches_.push_back(session_.autofireOff.changed.connect([this] { pushColumnHolds(); }));
	holdWatches_.push_back(session_.autofireSkipsLag.changed.connect([this] { pushColumnHolds(); }));
	expose("recentMacros", "macros placed lately, newest first, a line each", recentMacros, true);
	expose("lagFramesToHide", "of a run of lag frames, the first this many are not shown (the project's own; 0 shows all)",
		lagFramesToHide, true);
	expose("hideWasLagFrames", "frames that were lag before an edit took them out of the greenzone count as lag "
		"(the project's own)", hideWasLagFrames, true);
	lagHideWatch_ = lagFramesToHide.changed.connect([this] {
		if (lagFramesToHide.get() < 0) lagFramesToHide.set(0);
		storeLagHiding();
	});
	wasLagWatch_ = hideWasLagFrames.changed.connect([this] { storeLagHiding(); });
	expose("horizontal", "TAStudio's Rotate: the frames run across and the buttons down (the project's own)", horizontal, true);
	expose("scriptColumns", "a script's own columns after the machine's, JSON [{name, text, width}]", scriptColumns);
	expose("scriptQueries", "a script colours or writes the roll's cells (Lua's tastudio.onqueryitem...)", scriptQueries, true);
	expose("iconEpoch", "the icons a script names are read again when this moves", iconEpoch, true);
	/* a script's own columns, by name: one a script adds again (it was stopped and started, or another script
	 * names it too) is the same column, its header and width the newest, and it goes when the last script that
	 * added it stops (issue 475: each start added another, and a stopped script's stayed) */
	command("addScriptColumn", "a script's own column: its name (what the callbacks are told), its header and width, "
		"and the script adding it (the column goes when every script that added it has stopped)", {"name", "text", "width", "script"},
		[this](const Args &a, std::string &error) -> Value {
			std::string name, text;
			int64_t width = 30, script = 0;
			if (!arg(a, 0, name, error) || !arg(a, 1, text, error)) return {};
			if (a.size() > 2) arg(a, 2, width, error);
			if (a.size() > 3) arg(a, 3, script, error);
			json::Json list;
			std::string err;
			json::parse(scriptColumns.get(), list, err);
			list.kind = json::Json::Kind::Array;
			json::Json *one = nullptr;
			for (json::Json &c : list.items)
				if (const json::Json *n = c.find("name"); n != nullptr && n->text == name) one = &c;
			if (one == nullptr)
			{
				list.items.emplace_back();
				one = &list.items.back();
				one->kind = json::Json::Kind::Object;
			}
			json::Json scripts;
			scripts.kind = json::Json::Kind::Array;
			if (const json::Json *had = one->find("scripts")) scripts = *had;
			if (std::none_of(scripts.items.begin(), scripts.items.end(), [&](const json::Json &x) { return x.text == std::to_string(script); }))
				scripts.items.push_back(json::Json::integer(script));
			one->members.clear();
			one->members.emplace_back("name", json::Json::string(name));
			one->members.emplace_back("text", json::Json::string(text));
			one->members.emplace_back("width", json::Json::integer(std::clamp<int64_t>(width, 8, 1000)));
			one->members.emplace_back("scripts", scripts);
			scriptColumns.set(json::write(list));
			return {};
		});
	command("removeScriptColumns", "a script stopped: the columns it added go, but those another running script added too",
		{"script"}, [this](const Args &a, std::string &error) -> Value {
			int64_t script = 0;
			if (!arg(a, 0, script, error)) return {};
			json::Json list;
			std::string err;
			if (!json::parse(scriptColumns.get(), list, err) || list.kind != json::Json::Kind::Array) return {};
			std::vector<json::Json> kept;
			for (json::Json &c : list.items)
			{
				json::Json *scripts = nullptr;
				for (auto &[key, value] : c.members)
					if (key == "scripts") scripts = &value;
				if (scripts != nullptr)
					std::erase_if(scripts->items, [&](const json::Json &x) { return x.text == std::to_string(script); });
				if (scripts == nullptr || !scripts->items.empty()) kept.push_back(c);
			}
			list.items = std::move(kept);
			const std::string now = json::write(list);
			if (now != scriptColumns.get()) scriptColumns.set(now);
			return {};
		});
	command("clearScriptColumns", "a script's own columns and callbacks gone (the script stopped)", {}, [this](const Args &, std::string &) -> Value {
		scriptColumns.set("[]");
		scriptQueries.set(false);
		return {};
	});
	horizontalWatch_ = horizontal.changed.connect([this] {
		session_.setTastudioMember("HorizontalOrientation", horizontal.get() ? "true" : "false");
	});
	command("saveMacro", "writes the frames first to last as a macro, a .chimeraMacro file as Chimera's TAStudio writes one: "
		"the movie's columns, the controller, then a line a frame", {"first", "last", "path"},
		[this](const Args &a, std::string &error) -> Value {
			int64_t first = 0, last = 0;
			std::string path;
			if (!arg(a, 0, first, error) || !arg(a, 1, last, error) || !arg(a, 2, path, error)) return {};
			const std::string text = macroText(first, last, {}, false, true, error);
			if (text.empty()) return {};
			std::ofstream out(path, std::ios::binary);
			out << text;
			if (!out)
			{
				error = tr("session.could_not_write", {path});
				return {};
			}
			return {};
		}, &session_.isOpen);
	command("placeMacro", "a .chimeraMacro file's frames written from this frame on, as Chimera's TAStudio places one: "
		"only its own columns, over the frames there or put in before them, pressed over what is there when it says so",
		{"frame", "path"}, [this](const Args &a, std::string &error) -> Value {
			int64_t at = 0;
			std::string path;
			if (!arg(a, 0, at, error) || !arg(a, 1, path, error)) return {};
			std::ifstream in(path, std::ios::binary);
			if (!in)
			{
				error = tr("new_project.cannot_read", {path});
				return {};
			}
			std::vector<std::string> lines;
			for (std::string line; std::getline(in, line);)
			{
				if (!line.empty() && line.back() == '\r') line.pop_back();
				lines.push_back(line);
			}
			if (!placeMacroLines(at, std::move(lines), path, error)) return {};
			addRecentMacro(path);
			return {};
		}, &session_.isOpen);
	patternsWatch_ = session_.tastudioLoaded.connect([this](const std::map<std::string, std::string> &members) {
		pendingMembers_ = members;
	});
	/* a game alone, or nothing open: Chimera's default patterns */
	projectGoneWatch_ = session_.hasProject.changed.connect([this] {
		if (session_.hasProject.get()) return;
		autoHeld_.clear();
		axisHoldValues_.clear();
		lagFramesToHide.set(0);
		hideWasLagFrames.set(false);
		horizontal.set(false);
		defaultPatterns();
		autoHeldChanged();
	});
	editHiddenWatch_ = editHiddenColumns.changed.connect([this] {
		pushRecordMask();
		pushColumnHolds();
	});
	expose("greenEnd", "frames before this one were run with the inputs they have now", greenEnd);
	expose("topFrame", "the first frame in view", topFrame, true);
	expose("visibleFrames", "how many frames the view shows at once (the view sets it)", visibleFrames, true);
	rows("frames", "one row a frame, one column a button; a cell is true where the button is held", frames_);
	rows("markers", "the movie's markers: frame, text, whether the frame's state is kept, whether it is one of the run's "
		"own, and its frame's zone: \"green\" (run as it stands), \"lag\" (run, and lag) or \"\" (not run yet)", markerRows_);
	rows("columns", "the piano roll's columns, a control each: name, label, shown", columnRows_);
	rows("history", "the undo history, in order: each step's name, and whether it is done (the rest are undone)", historyRows_);
	rows("patterns", "each control's own pattern (kept in the project): name, runs (\"On x1, Off x1\" or \"127 x2, 0 x1\"), "
		"loop (the value it goes back to), skipLag (lag frames passed over), button, and an analog control's min, max and neutral",
		patternRows_);
	expose("maxUndoSteps", "the most undo steps kept", maxUndoSteps, true);
	expose("copyFrameNumbers", "Copy puts each frame's number before its line", copyFrameNumbers, true);
	expose("clipboardText", "the frames last copied or cut, a line each, for the system's clipboard", clipboardText);
	expose("markersKeepStates", "a marker put on a frame keeps that frame's state", markersKeepStates, true);
	command("pasteText", "the lines given (as Copy writes them; the system's clipboard) written over the frames from "
		"this one, or put in at it when insert is true; nothing is pasted if a line is not a frame's input",
		{"frame", "text", "insert"}, [this](const Args &a, std::string &error) -> Value {
			int64_t frame = 0;
			std::string text;
			bool insert = false;
			if (!arg(a, 0, frame, error) || !arg(a, 1, text, error)) return {};
			if (a.size() > 2 && !arg(a, 2, insert, error)) return {};
			if (frame < 0 || frame > inputs_.size())
			{
				error = tr("piano_roll.no_frame", {std::to_string(frame)});
				return {};
			}
			/* a line each; Windows line ends too, and a last empty line is not a frame */
			std::vector<std::string> lines;
			size_t at = 0;
			while (at < text.size())
			{
				const size_t end = text.find('\n', at);
				std::string line = text.substr(at, end == std::string::npos ? std::string::npos : end - at);
				if (!line.empty() && line.back() == '\r') line.pop_back();
				lines.push_back(line);
				if (end == std::string::npos) break;
				at = end + 1;
			}
			if (lines.empty())
			{
				error = tr("piano_roll.nothing_to_paste");
				return {};
			}
			std::vector<Row> rows;
			size_t bad = lines.size();
			const size_t axisCount = inputs_.axisCount();
			session_.onMachineWait([&](ce_session *s) {
				if (s == nullptr) return;
				for (size_t i = 0; i < lines.size(); i++)
				{
					Row r;
					r.axes.resize(axisCount);
					/* every line a movie writes starts with its '|'; the engine reads
					 * a line by place, so the frame number Copy can put before it
					 * goes first, or it would come in as buttons held */
					lines[i] = withoutFrameNumber(lines[i]);
					if (lines[i].empty() || lines[i][0] != '|' ||
						!decodeEntry(s, lines[i], r.buttons, r.axes.empty() ? nullptr : r.axes.data()))
					{
						bad = i;
						return;
					}
					rows.push_back(std::move(r));
				}
			});
			if (rows.size() != lines.size())
			{
				error = bad < lines.size() ? tr("piano_roll.not_input_line", {std::to_string(bad + 1), lines[bad]}) : tr("session.no_machine");
				return {};
			}
			clipboard_ = std::move(rows);
			copiedFrom_ = frame;
			clipboardFrames.set(static_cast<int64_t>(clipboard_.size()));
			if (insert)
			{
				naming_ = tr("piano_roll.step_paste_insert", {std::to_string(frame)});
				putIn(frame, clipboard_);
			}
			else
			{
				naming_ = tr("piano_roll.step_copy_over", {std::to_string(frame)});
				overwrite(frame, clipboard_);
			}
			return {};
		});
	command("undoTo", "takes back steps until this one (by its place in the history) is the last done", {"step"},
		[this](const Args &a, std::string &error) -> Value {
			int64_t i = 0;
			if (!arg(a, 0, i, error)) return {};
			if (i < -1 || i >= historyRows_.rowCount())
			{
				error = tr("piano_roll.no_step", {std::to_string(i)});
				return {};
			}
			while (static_cast<int64_t>(undo_.size()) - 1 > i) undoOrRedo(true);
			return {};
		});
	command("redoTo", "does steps again until this one (by its place in the history) is the last done", {"step"},
		[this](const Args &a, std::string &error) -> Value {
			int64_t i = 0;
			if (!arg(a, 0, i, error)) return {};
			if (i < 0 || i >= historyRows_.rowCount())
			{
				error = tr("piano_roll.no_step", {std::to_string(i)});
				return {};
			}
			while (static_cast<int64_t>(undo_.size()) - 1 < i && !redo_.empty()) undoOrRedo(false);
			return {};
		});
	command("clearHistory", "forgets every step, done or undone", {}, [this](const Args &, std::string &) -> Value {
		undo_.clear();
		redo_.clear();
		canUndo.set(false);
		canRedo.set(false);
		historyChanged();
		return {};
	});
	command("clearHistoryTo", "forgets the steps done up to this one (by its place in the history)", {"step"},
		[this](const Args &a, std::string &error) -> Value {
			int64_t i = 0;
			if (!arg(a, 0, i, error)) return {};
			if (i < 0 || i >= static_cast<int64_t>(undo_.size()))
			{
				error = tr("piano_roll.forget_done_only");
				return {};
			}
			undo_.erase(undo_.begin(), undo_.begin() + i + 1);
			canUndo.set(!undo_.empty());
			historyChanged();
			return {};
		});
	maxStepsWatch_ = maxUndoSteps.changed.connect([this] { trimHistory(); });
	expose("hiddenColumns", "the columns not shown, by control name, a line each", hiddenColumns);
	/* a column shown or hidden is a change of the project's, as Chimera flags it */
	command("hideColumn", "a control's column no longer shown", {"name"}, [this](const Args &a, std::string &error) -> Value {
		std::string name;
		if (!arg(a, 0, name, error)) return {};
		hidden_.insert(name);
		hiddenChanged();
		session_.markChanged();
		return {};
	});
	command("showColumn", "a control's column shown again", {"name"}, [this](const Args &a, std::string &error) -> Value {
		std::string name;
		if (!arg(a, 0, name, error)) return {};
		hidden_.erase(name);
		hiddenChanged();
		session_.markChanged();
		return {};
	});
	command("showAllColumns", "every column shown", {}, [this](const Args &, std::string &) -> Value {
		hidden_.clear();
		hiddenChanged();
		session_.markChanged();
		return {};
	});
	rows("columnGroups", "the Columns menu's groups, in order: name (\"Player 1\", \"Keys (A - M)\"), what its own toggle "
		"says (\"Show Player 1\", \"Show Keys\"; the same for each keys group), and whether all its columns are shown", groupRows_);
	command("showGroup", "every column of a Columns menu group shown or hidden: a player's (\"Player 1\"), or all the "
		"keyboard keys (\"Keys\")", {"group", "shown"}, [this](const Args &a, std::string &error) -> Value {
			std::string group;
			bool shown = true;
			if (!arg(a, 0, group, error) || !arg(a, 1, shown, error)) return {};
			const std::vector<std::string> groups = columnGroups();
			bool any = false;
			for (int32_t c = 0; c < frames_.columnCount(); c++)
			{
				const std::string &g = groups[static_cast<size_t>(c)];
				if (g != group && !(group == "Keys" && g.rfind("Keys (", 0) == 0)) continue;
				any = true;
				if (shown) hidden_.erase(frames_.columnName(c));
				else hidden_.insert(frames_.columnName(c));
			}
			if (!any)
			{
				error = tr("piano_roll.no_group", {group});
				return {};
			}
			hiddenChanged();
			session_.markChanged();
			return {};
		});
	hiddenWatch_ = session_.hiddenColumnsLoaded.connect([this](const std::vector<std::string> &names) {
		hidden_ = std::set<std::string>(names.begin(), names.end());
		/* a project read has one input roll, as Chimera's TAStudio starts */
		rolls_.assign(1, hidden_);
		activeRoll.set(0);
		rollCount.set(1);
		hiddenChanged();
	});
	expose("rollCount", "TAStudio's input rolls: how many", rollCount);
	expose("activeRoll", "the input roll last used, from 0: its columns are the menus' and what edits keep to", activeRoll);
	expose("scrollSync", "the input rolls scroll together (Sync scroll across all input rolls)", scrollSync, true);
	command("setActiveRoll", "the input roll used now (one clicked)", {"roll"}, [this](const Args &a, std::string &error) -> Value {
		int64_t r = 0;
		if (!arg(a, 0, r, error)) return {};
		if (r < 0 || r >= rollCount.get())
		{
			error = tr("piano_roll.no_roll", {std::to_string(r)});
			return {};
		}
		if (r == activeRoll.get()) return {};
		rolls_[static_cast<size_t>(activeRoll.get())] = hidden_;
		activeRoll.set(r);
		hidden_ = rolls_[static_cast<size_t>(r)];
		hiddenChanged();
		return {};
	});
	command("newRoll", "another input roll, beside the one used, as Chimera's New input roll: the columns hidden in "
		"any roll, when there are some, else the used roll's columns", {}, [this](const Args &, std::string &) -> Value {
			rolls_[static_cast<size_t>(activeRoll.get())] = hidden_;
			std::set<std::string> hiddenAnywhere;
			for (const std::set<std::string> &r : rolls_) hiddenAnywhere.insert(r.begin(), r.end());
			std::set<std::string> made = hidden_;
			if (!hiddenAnywhere.empty())
			{
				made.clear();
				for (int32_t c = 0; c < frames_.columnCount(); c++)
					if (hiddenAnywhere.count(frames_.columnName(c)) == 0) made.insert(frames_.columnName(c));
			}
			/* put where the used roll was, which moves along one, as Chimera's */
			const size_t at = static_cast<size_t>(activeRoll.get());
			rolls_.insert(rolls_.begin() + static_cast<long>(at), made);
			rollCount.set(static_cast<int64_t>(rolls_.size()));
			activeRoll.set(static_cast<int64_t>(at) + 1);
			frames_.reset.emit();
			return static_cast<int64_t>(at);
		});
	command("deleteRoll", "the input roll used taken away (there is always one); the first is used then", {},
		[this](const Args &, std::string &) -> Value {
			if (rolls_.size() <= 1) return {};
			rolls_.erase(rolls_.begin() + static_cast<long>(activeRoll.get()));
			rollCount.set(static_cast<int64_t>(rolls_.size()));
			activeRoll.set(0);
			hidden_ = rolls_.front();
			hiddenChanged();
			return {};
		});
	expose("markerCount", "how many markers people placed (the run's own three are not counted)", markerCount);
	expose("markerAddedRow", "the row, in the markers' list, of the marker last put on a frame (-1 in between)", markerAddedRow);
	expose("lastInputFrame", "the run's own Last input marker: the last frame anything is pressed on (0 when none is)", lastInputFrame);
	expose("runEndFrame", "the run's own Run end marker: the movie's last frame", runEndFrame);
	expose("canUndo", "an edit can be taken back", canUndo);
	expose("canRedo", "an edit taken back can be done again", canRedo);

	command("addMarker", "puts a marker on a frame (or renames the one there)", {"frame", "text"},
		[this](const Args &a, std::string &error) -> Value {
			int64_t f = 0;
			std::string text;
			if (!arg(a, 0, f, error)) return {};
			if (a.size() > 1 && !arg(a, 1, text, error)) return {};
			if (f < 0)
			{
				error = tr("session.no_frame_before_0");
				return {};
			}
			/* a marker named again keeps what it kept */
			const auto there = markers_.find(f);
			const bool keep = there != markers_.end() ? there->second.keepState : markersKeepStates.get();
			auto before = markers_;
			markers_[f] = Marker{f, text, keep};
			markersEdited(f);
			keepMarkers(std::move(before), tr("piano_roll.step_set_marker", {std::to_string(f)}));
			showMarker(f);
			return {};
		});
	command("moveMarker", "Chimera's Edit Marker Frame: a marker moved to another frame, its text and state kept; "
		"not the run's own, and not onto a frame that has one", {"from", "to"},
		[this](const Args &a, std::string &error) -> Value {
			int64_t from = 0, to = 0;
			if (!arg(a, 0, from, error) || !arg(a, 1, to, error)) return {};
			const auto it = markers_.find(from);
			if (from == 0 || it == markers_.end())
			{
				error = permanentAt(from) ? tr("piano_roll.marker_fixed_move", {std::to_string(from)})
										  : tr("piano_roll.no_marker", {std::to_string(from)});
				return {};
			}
			if (to < 0 || markers_.count(to) != 0 || permanentAt(to))
			{
				error = to < 0 ? tr("session.no_frame_before_0") : tr("piano_roll.marker_there", {std::to_string(to)});
				return {};
			}
			Marker m = it->second;
			m.frame = to;
			auto before = markers_;
			markers_.erase(it);
			markers_[to] = m;
			markersEdited(std::min(from, to));
			keepMarkers(std::move(before), tr("piano_roll.step_set_marker", {std::to_string(to)}));
			if (std::max(from, to) < inputs_.size()) frames_.changed.emit(std::max(from, to), std::max(from, to));
			return {};
		});
	command("zoneOf", "a frame's place in the greenzone, as Chimera's lists colour it: \"green\" (run as it stands), "
		"\"lag\" (run, and lag) or \"\" (not run yet)", {"frame"},
		[this](const Args &a, std::string &error) -> Value {
			int64_t f = 0;
			if (!arg(a, 0, f, error)) return {};
			return f >= 0 && f < greenEnd.get() && f < inputs_.size() ? std::string(inputs_.lagAt(f) ? "lag" : "green") : std::string();
		});
	command("removeMarker", "takes the marker off a frame", {"frame"},
		[this](const Args &a, std::string &error) -> Value {
			int64_t f = 0;
			if (!arg(a, 0, f, error)) return {};
			/* the run's own are the movie speaking, and so is a marker on frame 0, as in Chimera */
			if (f == 0 || markers_.count(f) == 0)
			{
				error = permanentAt(f) ? tr("piano_roll.marker_fixed_remove", {std::to_string(f)})
									   : tr("piano_roll.no_marker", {std::to_string(f)});
				return {};
			}
			auto before = markers_;
			markers_.erase(f);
			markersEdited(f);
			keepMarkers(std::move(before), tr("piano_roll.step_remove_marker", {std::to_string(f)}));
			return {};
		});
	command("addMarkers", "TAStudio's Set Markers: an unnamed marker on every frame from first to last (one there "
		"already is named again, empty); how many", {"first", "last"},
		[this](const Args &a, std::string &error) -> Value {
			int64_t first = 0, last = 0;
			if (!arg(a, 0, first, error) || !arg(a, 1, last, error)) return {};
			if (first < 0 || last < first)
			{
				error = tr("piano_roll.not_a_range", {std::to_string(first), std::to_string(last)});
				return {};
			}
			/* markers are the movie's, and the row after it: a range past that would never end */
			if (last > inputs_.size())
			{
				error = tr("piano_roll.past_end", {std::to_string(last)});
				return {};
			}
			auto before = markers_;
			for (int64_t f = first; f <= last; f++)
			{
				const auto there = markers_.find(f);
				const bool keep = there != markers_.end() ? there->second.keepState : markersKeepStates.get();
				markers_[f] = Marker{f, "", keep};
			}
			markersEdited(first);
			keepMarkers(std::move(before), tr("piano_roll.step_set_marker", {std::to_string(first)}));
			showMarker(first);
			if (last != first && last < inputs_.size()) frames_.changed.emit(first, last);
			return last - first + 1;
		});
	command("removeMarkers", "TAStudio's Remove Markers: every marker from first to last taken off; how many",
		{"first", "last"}, [this](const Args &a, std::string &error) -> Value {
			int64_t first = 0, last = 0;
			if (!arg(a, 0, first, error) || !arg(a, 1, last, error)) return {};
			int64_t removed = 0;
			auto before = markers_;
			for (auto it = markers_.lower_bound(std::max<int64_t>(first, 1)); it != markers_.end() && it->first <= last; removed++)
				it = markers_.erase(it);
			if (removed > 0)
			{
				markersEdited(first);
				keepMarkers(std::move(before), tr("piano_roll.step_remove_marker", {std::to_string(first)}));
				if (last != first) frames_.changed.emit(first, std::min<int64_t>(last, std::max<int64_t>(first, inputs_.size() - 1)));
			}
			return removed;
		});
	command("removeMarkerRows", "the markers at these places in the markers list (\"1,4\") taken off, as one step; the run's own "
		"stay; how many", {"rows"}, [this](const Args &a, std::string &error) -> Value {
			std::string list;
			if (!arg(a, 0, list, error)) return {};
			std::vector<int64_t> frames;
			int64_t fixed = -1;
			std::stringstream in(list);
			for (std::string one; std::getline(in, one, ',');)
			{
				if (one.empty()) continue;
				char *end = nullptr;
				const long row = std::strtol(one.c_str(), &end, 10);
				if (end == nullptr || *end != '\0' || row < 0 || static_cast<size_t>(row) >= markerList_.size())
				{
					error = tr("piano_roll.no_marker_row", {one});
					return {};
				}
				const ListedMarker &m = markerList_[static_cast<size_t>(row)];
				/* the run's own are the movie speaking, and so is a marker on frame 0, as in Chimera */
				if (m.permanent || m.frame == 0 || markers_.count(m.frame) == 0)
				{
					if (fixed < 0) fixed = m.frame;
					continue;
				}
				frames.push_back(m.frame);
			}
			if (frames.empty())
			{
				if (fixed >= 0) error = tr("piano_roll.marker_fixed_remove", {std::to_string(fixed)});
				return {};
			}
			auto before = markers_;
			for (const int64_t f : frames) markers_.erase(f);
			const int64_t first = *std::min_element(frames.begin(), frames.end());
			markersEdited(first);
			keepMarkers(std::move(before), tr("piano_roll.step_remove_marker", {std::to_string(first)}));
			return static_cast<int64_t>(frames.size());
		});
	command("setMarkerAt", "TAStudio's double click on a frame number, as Chimera's: the person's own marker on the frame is to be "
		"edited (its text back); with none, an unnamed marker is put there (nothing back)", {"frame"},
		[this](const Args &a, std::string &error) -> Value {
			int64_t f = 0;
			if (!arg(a, 0, f, error)) return {};
			if (const auto it = markers_.find(f); it != markers_.end()) return it->second.text;
			run("addMarker", {f, std::string()}, error);
			return {};
		});
	command("markerAt", "the text of the marker on a frame (the person's, else the run's own name); nothing for none", {"frame"},
		[this](const Args &a, std::string &error) -> Value {
			int64_t f = 0;
			if (!arg(a, 0, f, error)) return {};
			if (const auto it = markers_.find(f); it != markers_.end()) return it->second.text;
			for (const ListedMarker &m : markerList_)
				if (m.frame == f) return m.text;
			return {};
		});
	command("markerFrames", "the frames with markers, a line each, a marker each (the run's own too)", {},
		[this](const Args &, std::string &) -> Value {
			std::string out;
			for (const ListedMarker &m : markerList_) out += std::to_string(m.frame) + "\n";
			return out;
		});
	command("nextMarker", "the first marker after a frame, or -1", {"frame"},
		[this](const Args &a, std::string &error) -> Value {
			int64_t f = 0;
			if (!arg(a, 0, f, error)) return {};
			/* the run's own count: the next is at the latest the run's end */
			for (const ListedMarker &m : markerList_)
				if (m.frame > f) return m.frame;
			return int64_t{-1};
		});
	command("previousMarker", "the last marker before a frame, or -1", {"frame"},
		[this](const Args &a, std::string &error) -> Value {
			int64_t f = 0;
			if (!arg(a, 0, f, error)) return {};
			for (auto it = markerList_.rbegin(); it != markerList_.rend(); ++it)
				if (it->frame < f) return it->frame;
			return int64_t{-1};
		});

	command("toggle", "flips one button on one frame", {"frame", "button"},
		[this](const Args &a, std::string &error) -> Value {
			int64_t f = 0, b = 0;
			if (!arg(a, 0, f, error) || !arg(a, 1, b, error)) return {};
			if (!paint(f, f, b, std::nullopt, false, error)) return {};
			return inputs_.at(f).test(static_cast<size_t>(b));
		});
	command("press", "a press on a cell: a button's flips, and a drag from it gives the cells it passes the same state; "
		"an analog control's is picked to edit, and a drag from it gives the cells it passes its value. With alt (or as "
		"patternMode says) a button's cells follow its pattern instead",
		{"frame", "column", "alt"}, [this](const Args &a, std::string &error) -> Value {
			int64_t f = 0, b = 0;
			bool alt = false;
			if (!arg(a, 0, f, error) || !arg(a, 1, b, error)) return {};
			if (a.size() > 2 && !arg(a, 2, alt, error)) return {};
			patternStroke_ = false;
			const std::string &mode = patternMode.get();
			const bool byPattern = mode == "always" || (mode == "autofire" && autoHeld_.count(frames_.columnName(static_cast<int32_t>(b))) != 0);
			if ((alt && b < buttonCount()) || byPattern)
			{
				if (f < 0 || b < 0 || b >= frames_.columnCount() || (b >= buttonCount() && f >= inputs_.size()))
				{
					error = tr("piano_roll.no_frame_or_control");
					return {};
				}
				/* as Chimera: a button's pattern presses when the cell was not held, else it lets go */
				patternColumn_ = b;
				patternFrom_ = f;
				patternOn_ = b < buttonCount() ? !inputs_.at(f).test(static_cast<size_t>(b)) : true;
				if (b >= buttonCount()) pickAxis(f, b - buttonCount());
				if (!paintPattern(f, f, false, error)) return {};
				patternStroke_ = true;
				stroke_ = b < buttonCount();
				axisJoin_ = b >= buttonCount();
				return patternOn_;
			}
			if (b >= buttonCount())
			{
				const int64_t axis = b - buttonCount();
				if (axis >= static_cast<int64_t>(session_.axes().size()) || f < 0 || f >= inputs_.size())
				{
					error = tr("piano_roll.no_frame_or_control");
					return {};
				}
				pickAxis(f, axis);
				dragAxisValue_ = axisValue.get();
				axisJoin_ = false;
				return axisValue.get();
			}
			if (!paint(f, f, b, std::nullopt, false, error)) return {};
			dragHeld_ = inputs_.at(f).test(static_cast<size_t>(b));
			/* the drag that follows is the same edit: one undo takes it all back */
			stroke_ = true;
			return dragHeld_;
		});
	command("drag", "a drag from a pressed cell reached this one: it takes the pressed cell's state",
		{"frame", "button"}, [this](const Args &a, std::string &error) -> Value {
			int64_t f = 0, b = 0;
			if (!arg(a, 0, f, error) || !arg(a, 1, b, error)) return {};
			/* a pattern's stroke: every frame from where it began, the pattern counted from the first */
			if (patternStroke_ && b == patternColumn_)
			{
				inputs_.countRerecords(false);
				paintPattern(std::min(patternFrom_, f), std::max(patternFrom_, f), true, error);
				inputs_.countRerecords(true);
				return {};
			}
			/* the stroke's first edit counts a rerecord; the cells after it do not */
			if (b >= buttonCount())
			{
				inputs_.countRerecords(!axisJoin_);
				if (paintAxis(f, f, b - buttonCount(), dragAxisValue_, axisJoin_, error)) axisJoin_ = true;
			}
			else
			{
				inputs_.countRerecords(!stroke_);
				paint(f, f, b, dragHeld_, stroke_, error);
			}
			inputs_.countRerecords(true);
			return {};
		});
	command("agentLines", "the MCP server's set_input, as the engine checked it: lines of input written from a frame on, "
		"past the movie's end too, one undo step with the engine's name for it", {"frame", "lines", "label"},
		[this](const Args &a, std::string &error) -> Value {
			int64_t frame = 0;
			std::string text, label;
			if (!arg(a, 0, frame, error) || !arg(a, 1, text, error) || !arg(a, 2, label, error)) return {};
			std::vector<std::string> lines;
			for (size_t at = 0; at <= text.size();)
			{
				const size_t end = text.find('\n', at);
				lines.push_back(text.substr(at, end == std::string::npos ? std::string::npos : end - at));
				if (end == std::string::npos) break;
				at = end + 1;
			}
			if (frame < 0 || lines.empty())
			{
				error = tr("piano_roll.unknown_step");
				return {};
			}
			std::vector<Row> rows;
			const size_t axisCount = inputs_.axisCount();
			session_.onMachineWait([&](ce_session *s) {
				if (s == nullptr) return;
				for (const std::string &line : lines)
				{
					Row r;
					r.axes.resize(axisCount);
					if (!decodeEntry(s, line, r.buttons, r.axes.empty() ? nullptr : r.axes.data())) return;
					rows.push_back(std::move(r));
				}
			});
			if (rows.size() != lines.size())
			{
				error = tr("piano_roll.unknown_step");
				return {};
			}
			/* past the end, the movie grows to take them, in the same step */
			const int64_t before = inputs_.size(), n = static_cast<int64_t>(rows.size());
			Step step;
			step.sizeBefore = before;
			if (frame + n > before) inputs_.resize(frame + n);
			for (int64_t i = 0; i < n; i++)
			{
				const Row was = readRow(frame + i);
				writeRow(frame + i, rows[static_cast<size_t>(i)]);
				step.frames[frame + i] = {was.buttons, rows[static_cast<size_t>(i)].buttons};
				if (!was.axes.empty()) step.axes[frame + i] = {was.axes, rows[static_cast<size_t>(i)].axes};
			}
			step.sizeAfter = inputs_.size();
			step.name = label;
			keep(std::move(step), false);
			frameCount.set(inputs_.size());
			frames_.forget();
			edited(frame, frame + n - 1, before);
			return inputs_.size();
		});
	command("agentInsert", "the MCP server's insert_frames: empty frames put in, one undo step with the engine's name",
		{"frame", "count", "label"}, [this](const Args &a, std::string &error) -> Value {
			int64_t frame = 0, count = 0;
			std::string label;
			if (!arg(a, 0, frame, error) || !arg(a, 1, count, error) || !arg(a, 2, label, error)) return {};
			if (frame < 0 || frame > inputs_.size() || count < 1)
			{
				error = tr("piano_roll.unknown_step");
				return {};
			}
			Row empty;
			empty.axes.resize(inputs_.axisCount());
			if (!empty.axes.empty()) inputs_.neutralAxes(empty.axes.data());
			agentEditing_ = true;
			naming_ = label;
			putIn(frame, std::vector<Row>(static_cast<size_t>(count), empty));
			naming_.clear();
			agentEditing_ = false;
			return inputs_.size();
		});
	command("agentDelete", "the MCP server's delete_frames: frames taken out, one undo step with the engine's name",
		{"frame", "count", "label"}, [this](const Args &a, std::string &error) -> Value {
			int64_t frame = 0, count = 0;
			std::string label;
			if (!arg(a, 0, frame, error) || !arg(a, 1, count, error) || !arg(a, 2, label, error)) return {};
			if (frame < 0 || count < 1 || frame + count > inputs_.size())
			{
				error = tr("piano_roll.unknown_step");
				return {};
			}
			agentEditing_ = true;
			naming_ = label;
			takeOut(frame, frame + count - 1);
			naming_.clear();
			agentEditing_ = false;
			return inputs_.size();
		});
	command("rightDragStart", "Chimera's right button pressed on a frame number with Shift, Ctrl or Alt held: the frames "
		"picked (or the frame pressed) are taken up for the drag; true when a drag edits", {"frame", "ctrl", "shift", "alt"},
		[this](const Args &a, std::string &error) -> Value {
			int64_t f = 0;
			bool ctrl = false, shift = false, alt = false;
			if (!arg(a, 0, f, error) || !arg(a, 1, ctrl, error) || !arg(a, 2, shift, error) || !arg(a, 3, alt, error)) return {};
			rightInput_.clear();
			rightStepMade_ = false;
			if (f < 0 || f >= inputs_.size() || !(ctrl || shift || alt)) return false;
			rightCtrl_ = ctrl;
			rightShift_ = shift;
			rightAlt_ = alt;
			const int64_t first = selectFirst.get(), last = std::min(selectLast.get(), inputs_.size() - 1);
			if (first >= 0 && f >= first && f <= last)
			{
				/* the frames picked: their own inputs */
				for (int64_t r = first; r <= last; r++) rightInput_.push_back(readRow(r));
				rightFrame_ = first + (ctrl && shift ? last - first + 1 : 0);
			}
			else
			{
				rightInput_.push_back(readRow(f));
				rightFrame_ = f;
			}
			rightStart_ = rightLow_ = rightHigh_ = f;
			return true;
		});
	command("rightDragTo", "the right-button drag reached a frame: Shift extends the input taken up over the frames passed, "
		"Ctrl with Shift puts copies of it in, Ctrl puts a copy of it there, Alt moves it there; one undo step for the drag",
		{"frame"}, [this](const Args &a, std::string &error) -> Value {
			int64_t f = 0;
			if (!arg(a, 0, f, error)) return {};
			if (rightInput_.empty()) return {};
			rightDragOnTo(f);
			return {};
		});
	command("rightDragEnd", "the right button let go: the drag's edit is done", {}, [this](const Args &, std::string &) -> Value {
		const bool edited = rightStepMade_;
		rightInput_.clear();
		rightStepMade_ = false;
		return edited;
	});
	command("paint", "holds (true) or releases (false) one button on frames first..last", {"first", "last", "button", "held"},
		[this](const Args &a, std::string &error) -> Value {
			int64_t first = 0, last = 0, b = 0;
			bool held = false;
			if (!arg(a, 0, first, error) || !arg(a, 1, last, error) || !arg(a, 2, b, error) || !arg(a, 3, held, error)) return {};
			if (!withinFillLimit(last, error)) return {};
			paint(first, last, b, held, false, error);
			return {};
		});
	command("fill", "makes the inputs at least this many frames long, the new frames empty", {"frames"},
		[this](const Args &a, std::string &error) -> Value {
			int64_t n = 0;
			if (!arg(a, 0, n, error)) return {};
			if (n < 0 || !withinFillLimit(n - 1, error)) return {};
			const int64_t before = inputs_.size();
			if (n > before)
			{
				Step step{{}, before, n};
				inputs_.resize(n);
				keep(std::move(step), false);
				edited(before, n - 1, before);
			}
			return inputs_.size();
		});
	expose("selectFirst", "the first frame picked, -1 for none", selectFirst, true);
	expose("selectLast", "the last frame picked, -1 for none", selectLast, true);
	expose("clipboardFrames", "frames copied, ready to paste", clipboardFrames);
	expose("bindMarkers", "markers move with the frames when frames are put in or taken out", bindMarkers, true);
	expose("pickedFrame", "the frame of the analog cell picked to edit, -1 for none", pickedFrame);
	expose("pickedAxis", "the analog control picked to edit, by its place among the analog controls", pickedAxis);
	expose("axisName", "the picked analog control's name", axisName);
	expose("axisMin", "the picked analog control's least value", axisMin);
	expose("axisMax", "the picked analog control's greatest value", axisMax);
	expose("axisValue", "the picked analog cell's value", axisValue);
	command("unpick", "no analog cell is picked for editing", {}, [this](const Args &, std::string &) -> Value {
		pickedFrame.set(-1);
		pickedAxis.set(-1);
		return {};
	});
	command("nudgeAxis", "the picked analog cell's value moved as Chimera's analog keys move it: \"up\", \"down\" (by "
		"one), \"up10\", \"down10\" (by ten), \"max\", \"min\"; nothing when no cell is picked",
		{"how"}, [this](const Args &a, std::string &error) -> Value {
			std::string how;
			if (!arg(a, 0, how, error)) return {};
			if (pickedFrame.get() < 0 || pickedAxis.get() < 0) return false;
			const int64_t lo = axisMin.get(), hi = axisMax.get(), now = axisValue.get();
			int64_t to = now;
			if (how == "up") to = now + 1;
			else if (how == "down") to = now - 1;
			else if (how == "up10") to = now + 10;
			else if (how == "down10") to = now - 10;
			else if (how == "max") to = hi;
			else if (how == "min") to = lo;
			else
			{
				error = tr("piano_roll.no_move", {how});
				return {};
			}
			to = std::clamp(to, lo, hi);
			if (to != now) paintAxis(pickedFrame.get(), pickedFrame.get(), pickedAxis.get(), to, false, error);
			return true;
		});
	command("setAxis", "an analog control's value on a frame", {"frame", "axis", "value"},
		[this](const Args &a, std::string &error) -> Value {
			int64_t f = 0, axis = 0, value = 0;
			if (!arg(a, 0, f, error) || !arg(a, 1, axis, error) || !arg(a, 2, value, error)) return {};
			paintAxis(f, f, axis, value, false, error);
			return {};
		});
	command("paintAxis", "an analog control's value on frames first..last", {"first", "last", "axis", "value"},
		[this](const Args &a, std::string &error) -> Value {
			int64_t first = 0, last = 0, axis = 0, value = 0;
			if (!arg(a, 0, first, error) || !arg(a, 1, last, error) || !arg(a, 2, axis, error) || !arg(a, 3, value, error)) return {};
			if (!withinFillLimit(last, error)) return {};
			paintAxis(first, last, axis, value, false, error);
			return {};
		});
	command("select", "picks the frames first to last (-1, -1 for none)", {"first", "last"},
		[this](const Args &a, std::string &error) -> Value {
			int64_t first = 0, last = 0;
			if (!arg(a, 0, first, error) || !arg(a, 1, last, error)) return {};
			if (first > last) std::swap(first, last);
			selectFirst.set(std::max<int64_t>(-1, first));
			selectLast.set(std::max<int64_t>(-1, last));
			return {};
		});
	command("insertFrames", "puts empty frames in at a frame; the frames from it move later", {"frame", "count"},
		[this](const Args &a, std::string &error) -> Value {
			int64_t frame = 0, count = 1;
			if (!arg(a, 0, frame, error)) return {};
			if (a.size() > 1 && !arg(a, 1, count, error)) return {};
			if (frame < 0 || frame > inputs_.size() || count < 1 || count > (int64_t{1} << 24))
			{
				error = tr("piano_roll.insert_range", {std::to_string(inputs_.size())});
				return {};
			}
			std::vector<Row> rows(static_cast<size_t>(count));
			std::vector<int32_t> neutral(static_cast<size_t>(inputs_.axisCount()));
			if (!neutral.empty()) inputs_.neutralAxes(neutral.data());
			for (Row &r : rows) r.axes = neutral;
			naming_ = tr("piano_roll.step_insert", count, {std::to_string(frame)});
			putIn(frame, rows);
			return {};
		});
	command("deleteFrames", "takes the frames first to last out; the frames after move earlier", {"first", "last"},
		[this](const Args &a, std::string &error) -> Value {
			int64_t first = 0, last = 0;
			if (!range(a, first, last, error)) return {};
			naming_ = tr("piano_roll.step_delete", last - first + 1, {std::to_string(first) + "-" + std::to_string(last)});
			takeOut(first, last);
			return {};
		});
	command("cloneFrames", "puts a copy of the frames first to last in after them, as many times as asked (once if not)",
		{"first", "last", "times"}, [this](const Args &a, std::string &error) -> Value {
			int64_t first = 0, last = 0, times = 1;
			if (!range(a, first, last, error)) return {};
			if (a.size() > 2 && !arg(a, 2, times, error)) return {};
			if (times < 1 || times > 100000)
			{
				error = tr("piano_roll.times_range");
				return {};
			}
			std::vector<Row> rows;
			for (int64_t t = 0; t < times; t++)
				for (int64_t f = first; f <= last; f++) rows.push_back(readRow(f));
			naming_ = times > 1 ? tr("piano_roll.step_clone_times", {std::to_string(first) + "-" + std::to_string(last), std::to_string(times)}) : tr("piano_roll.step_clone", {std::to_string(first) + "-" + std::to_string(last)});
			putIn(last + 1, rows);
			return {};
		});
	command("clearFrames", "empties the frames first to last (nothing held, analog controls at rest)", {"first", "last"},
		[this](const Args &a, std::string &error) -> Value {
			int64_t first = 0, last = 0;
			if (!range(a, first, last, error)) return {};
			std::vector<int32_t> neutral(static_cast<size_t>(inputs_.axisCount()));
			if (!neutral.empty()) inputs_.neutralAxes(neutral.data());
			naming_ = tr("piano_roll.step_clear", {std::to_string(first) + "-" + std::to_string(last)});
			overwrite(first, std::vector<Row>(static_cast<size_t>(last - first + 1), Row{Buttons(), neutral}));
			return {};
		});
	command("copyFrames", "keeps the frames first to last, to paste", {"first", "last"},
		[this](const Args &a, std::string &error) -> Value {
			int64_t first = 0, last = 0;
			if (!range(a, first, last, error)) return {};
			clipboard_.clear();
			for (int64_t f = first; f <= last; f++) clipboard_.push_back(readRow(f));
			copiedFrom_ = first;
			clipboardFrames.set(static_cast<int64_t>(clipboard_.size()));
			/* the lines, with each frame's number before it when asked, as
			 * many digits as the movie's longest (five at least) */
			const std::vector<std::string> lines = inputs_.lines(first, last);
			int digits = 1;
			for (int64_t n = std::max<int64_t>(inputs_.size(), 1); n >= 10; n /= 10) digits++;
			digits = std::max(digits, 5);
			std::string text;
			for (size_t i = 0; i < lines.size(); i++)
			{
				if (copyFrameNumbers.get())
				{
					char number[32];
					std::snprintf(number, sizeof number, "%0*lld ", digits, static_cast<long long>(first + static_cast<int64_t>(i)));
					text += number;
				}
				text += lines[i] + "\n";
			}
			clipboardText.set(text);
			return clipboardFrames.get();
		});
	command("reselectClipboard", "picks again the frames last copied or cut, where they were", {},
		[this](const Args &, std::string &error) -> Value {
			if (clipboard_.empty() || inputs_.size() == 0)
			{
				error = tr("piano_roll.nothing_copied");
				return {};
			}
			const int64_t last = std::min<int64_t>(copiedFrom_ + static_cast<int64_t>(clipboard_.size()) - 1, inputs_.size() - 1);
			std::string e;
			run("select", {std::min(copiedFrom_, last), last}, e);
			return {};
		});
	command("cutFrames", "keeps the frames first to last to paste, and takes them out", {"first", "last"},
		[this](const Args &a, std::string &error) -> Value {
			int64_t first = 0, last = 0;
			if (!range(a, first, last, error)) return {};
			clipboard_.clear();
			for (int64_t f = first; f <= last; f++) clipboard_.push_back(readRow(f));
			copiedFrom_ = first;
			clipboardFrames.set(static_cast<int64_t>(clipboard_.size()));
			/* Chimera's Cut writes the lines alone */
			std::string text;
			for (const std::string &line : inputs_.lines(first, last)) text += line + "\n";
			clipboardText.set(text);
			naming_ = tr("piano_roll.step_cut", {std::to_string(first) + "-" + std::to_string(last)});
			takeOut(first, last);
			return clipboardFrames.get();
		});
	command("pasteFrames", "writes the frames copied over the frames from this one", {"frame"},
		[this](const Args &a, std::string &error) -> Value {
			int64_t frame = 0;
			if (!arg(a, 0, frame, error)) return {};
			if (clipboard_.empty() || frame < 0 || frame > inputs_.size())
			{
				error = clipboard_.empty() ? tr("piano_roll.nothing_copied") : tr("piano_roll.no_frame", {std::to_string(frame)});
				return {};
			}
			naming_ = tr("piano_roll.step_copy_over", {std::to_string(frame)});
			overwrite(frame, clipboard_);
			return {};
		});
	command("pasteInsertFrames", "puts the frames copied in at this frame; the frames from it move later", {"frame"},
		[this](const Args &a, std::string &error) -> Value {
			int64_t frame = 0;
			if (!arg(a, 0, frame, error)) return {};
			if (clipboard_.empty() || frame < 0 || frame > inputs_.size())
			{
				error = clipboard_.empty() ? tr("piano_roll.nothing_copied") : tr("piano_roll.no_frame", {std::to_string(frame)});
				return {};
			}
			naming_ = tr("piano_roll.step_paste_insert", {std::to_string(frame)});
			putIn(frame, clipboard_);
			return {};
		});
	command("truncate", "takes out every frame from this one to the end", {"frame"},
		[this](const Args &a, std::string &error) -> Value {
			int64_t frame = 0;
			if (!arg(a, 0, frame, error)) return {};
			if (frame < 0 || frame >= inputs_.size())
			{
				error = tr("piano_roll.no_frame", {std::to_string(frame)});
				return {};
			}
			naming_ = tr("piano_roll.step_truncate", {std::to_string(frame)});
			if (limited()) truncateShown(frame);
			else takeOut(frame, inputs_.size() - 1);
			return {};
		});
	command("undo", "takes the last edit back", {}, [this](const Args &, std::string &) -> Value {
		undoOrRedo(true);
		return {};
	}, &canUndo);
	command("redo", "does an edit taken back again", {}, [this](const Args &, std::string &) -> Value {
		undoOrRedo(false);
		return {};
	}, &canRedo);

	forgetWatches_.push_back(frames_.changed.connect([this](int64_t, int64_t) { frames_.forget(); }));
	forgetWatches_.push_back(frames_.appended.connect([this](int64_t, int64_t) { frames_.forget(); }));
	forgetWatches_.push_back(frames_.reset.connect([this] { frames_.forget(); }));
	markersWatch_ = session_.markersLoaded.connect([this](const std::vector<Marker> &loaded) {
		markers_.clear();
		for (const Marker &m : loaded) markers_[m.frame] = m;
		markerCount.set(static_cast<int64_t>(markers_.size()));
		refreshRun(0);
		listMarkers();
		frames_.reset.emit();
	});
	/* another machine's movie: worked out again from nothing, and the last
	 * project's steps and selection are not this one's - undone here they
	 * would write the last movie's frames into this one (Chimera's TAStudio
	 * is a new window for each project) */
	runOpensWatch_ = session_.opens.changed.connect([this] {
		lastInputFrame.set(-1);
		undo_.clear();
		redo_.clear();
		canUndo.set(false);
		canRedo.set(false);
		historyChanged();
		selectFirst.set(-1);
		selectLast.set(-1);
		refreshRun(0);
	});
	frameWatch_ = session_.frame.changed.connect([this] {
		currentFrame.set(session_.frame.get());
		pickCurrentMarker();
		follow();
	});
	topWatch_ = topFrame.changed.connect([this] { clampTop(); });
	/* turned on, it goes to the current frame at once, running or not (issue 486) */
	followWatch_ = followPlayback.changed.connect([this] { follow(); });
	greenWatch_ = session_.greenEnd.changed.connect([this] {
		const int64_t was = greenEnd.get(), now = session_.greenEnd.get();
		greenEnd.set(now);
		const int64_t first = std::min(was, now), last = std::min(std::max(was, now), inputs_.size()) - 1;
		if (last >= first) frames_.changed.emit(first, last);
		/* the markers whose frames came into the greenzone or left it */
		int64_t rowFirst = -1, rowLast = -1;
		for (size_t i = 0; i < markerList_.size(); i++)
			if (markerList_[i].frame >= std::min(was, now) && markerList_[i].frame < std::max(was, now))
			{
				if (rowFirst < 0) rowFirst = static_cast<int64_t>(i);
				rowLast = static_cast<int64_t>(i);
			}
		if (rowFirst >= 0) markerRows_.changed.emit(rowFirst, rowLast);
	});
	buttonsWatch_ = session_.buttonsChanged.connect([this] {
		/* a project's own patterns; the ones there are when its machine
		 * started again; else Chimera's defaults */
		if (pendingMembers_)
		{
			loadPatterns(*pendingMembers_);
			pendingMembers_.reset();
		}
		else if (controlNames() != patternNames_)
			defaultPatterns();
		pushColumnHolds();
		pushRecordMask();
		groupRows_.reset.emit();
		frames_.reset.emit();
	});
	resetWatch_ = session_.inputsReset.connect([this] {
		frameCount.set(inputs_.size());
		clampTop();
		refreshRun(0);
		frames_.reset.emit();
	});
	/* what the machine recorded, or another movie in its place: the run's own markers follow */
	changedWatch_ = session_.inputsChanged.connect([this](int64_t a, int64_t b) {
		frames_.changed.emit(a, b);
		refreshRun(a);
	});
	recordedWatch_ = session_.framesRecorded.connect([this](const std::vector<SessionViewModel::RecordedFrame> &frames) {
		framesRecorded(frames);
	});
	appendedWatch_ = session_.inputsAppended.connect([this](int64_t a, int64_t b) {
		frameCount.set(inputs_.size());
		frames_.appended.emit(a, b);
		refreshRun(a);
	});
	listMarkers();
}

void PianoRollViewModel::follow()
{
	const int64_t now = currentFrame.get(), top = topFrame.get(), shown = visibleFrames.get();
	if (!followPlayback.get() || shown <= 0) return;
	/* as TAStudio scrolls to a row, counting the frames shown */
	const bool inView = now >= top && now <= shownFrom(top, shown - 1);
	const std::string &how = followScroll.get();
	int64_t to = top;
	if (how != "top" && how != "bottom" && how != "center")
	{
		if (!inView) to = now < top ? now : shownFrom(now, -(shown - 1));
	}
	else if (!inView || followAlways.get())
		to = how == "top" ? now : how == "bottom" ? shownFrom(now, -(shown - 1)) : shownFrom(now, -(shown / 2));
	to = std::max<int64_t>(0, to);
	if (to != top) topFrame.set(to);
}

void PianoRollViewModel::clampTop()
{
	const int64_t most = std::max<int64_t>(0, inputs_.size() - 1);
	if (topFrame.get() > most) topFrame.set(most);
	else if (topFrame.get() < 0) topFrame.set(0);
}

int32_t PianoRollViewModel::buttonCount() const
{
	return static_cast<int32_t>(session_.buttonNames().size());
}

bool PianoRollViewModel::paint(int64_t first, int64_t last, int64_t b, std::optional<bool> held, bool join, std::string &error)
{
	if (first > last) std::swap(first, last);
	if (first < 0 || b < 0 || b >= buttonCount())
	{
		error = tr("piano_roll.no_frame_or_button");
		return false;
	}
	const int64_t before = inputs_.size();
	Step step{{}, before, 0};
	const std::string button = frames_.columnName(static_cast<int32_t>(b));
	const std::string span = first == last ? std::to_string(first) : std::to_string(first) + "-" + std::to_string(last);
	step.name = join ? tr("piano_roll.step_paint", {button, ""})
		: held ? tr(*held ? "piano_roll.step_set_on" : "piano_roll.step_set_off", {button, span})
			   : tr("piano_roll.step_toggle", {button, span});
	for (int64_t f = first; f <= last; f++)
	{
		const Buttons was = inputs_.at(f);
		Buttons now = was;
		now.set(static_cast<size_t>(b), held ? *held : !was.test(static_cast<size_t>(b)));
		inputs_.set(f, now);
		step.frames[f] = {was, now};
	}
	step.sizeAfter = inputs_.size();
	keep(std::move(step), join);
	edited(first, last, before);
	return true;
}

void PianoRollViewModel::keep(Step step, bool join)
{
	session_.markChanged();
	/* an edit of frames by hand ends recording, as Chimera's TAStudio does on
	 * the next edit (StopRecordingOnNextEdit); a script's and the machine's
	 * own recording leave it on */
	const bool framesEdited = !step.frames.empty() || !step.axes.empty() || step.shiftCount != 0 || step.sizeBefore != step.sizeAfter;
	if (!recordingStep_ && framesEdited)
	{
		lastWasRecord_ = false;
		if (!scriptEditing.get()) session_.recordingMode.set(false);
	}
	if (!join && step.name.empty()) step.name = naming_.empty() ? describe(step) : naming_;
	naming_.clear();
	if (join && !undo_.empty())
	{
		/* a stroke is named for where it began */
		if (!step.name.empty()) undo_.back().name = step.name + std::to_string(std::min(undo_.back().frames.empty() ? 0 : undo_.back().frames.begin()->first,
																	 step.frames.empty() ? INT64_MAX : step.frames.begin()->first));
		/* the stroke goes on: a frame it passes again keeps its first "before" */
		Step &last = undo_.back();
		for (const auto &[f, change] : step.frames)
		{
			auto it = last.frames.find(f);
			if (it == last.frames.end()) last.frames.emplace(f, change);
			else it->second.second = change.second;
		}
		for (const auto &[f, change] : step.axes)
		{
			auto it = last.axes.find(f);
			if (it == last.axes.end()) last.axes.emplace(f, change);
			else it->second.second = change.second;
		}
		last.sizeAfter = step.sizeAfter;
	}
	else
	{
		stroke_ = false;
		undo_.push_back(std::move(step));
		trimHistory();
	}
	redo_.clear();
	canUndo.set(!undo_.empty());
	canRedo.set(false);
	historyChanged();
}

void PianoRollViewModel::trimHistory()
{
	const size_t most = static_cast<size_t>(std::max<int64_t>(1, maxUndoSteps.get()));
	if (undo_.size() > most) undo_.erase(undo_.begin(), undo_.begin() + static_cast<std::ptrdiff_t>(undo_.size() - most));
	historyChanged();
}

void PianoRollViewModel::historyChanged()
{
	/* a frame recorded while the machine runs joins the step before: as
	 * many steps, the last one longer */
	historyRows_.refreshed(historyTold_);
	historyTold_ = historyRows_.rowCount();
}

std::string PianoRollViewModel::describe(const Step &step)
{
	auto range = [](int64_t a, int64_t b) { return a == b ? std::to_string(a) : std::to_string(a) + "-" + std::to_string(b); };
	if (step.shiftCount > 0) return tr("piano_roll.step_insert", step.shiftCount, {std::to_string(step.shiftAt)});
	if (step.shiftCount < 0) return tr("piano_roll.step_delete", -step.shiftCount, {range(step.shiftAt, step.shiftAt - step.shiftCount - 1)});
	if (step.frames.empty() && step.sizeAfter != step.sizeBefore) return tr("piano_roll.step_fill", step.sizeAfter);
	if (!step.frames.empty()) return tr("piano_roll.step_change_frames", {range(step.frames.begin()->first, step.frames.rbegin()->first)});
	return tr("piano_roll.step_change");
}

std::string PianoRollViewModel::HistoryRows::columnName(int32_t column) const
{
	static const char *const names[] = {"name", "done"};
	return column >= 0 && column < 2 ? names[column] : "";
}

Value PianoRollViewModel::HistoryRows::cell(int64_t row, int32_t column) const
{
	if (row < 0 || row >= rowCount()) return {};
	const size_t done = vm_.undo_.size();
	const Step &s = static_cast<size_t>(row) < done ? vm_.undo_[static_cast<size_t>(row)]
													: vm_.redo_[vm_.redo_.size() - 1 - (static_cast<size_t>(row) - done)];
	switch (column)
	{
	case 0: return s.name;
	case 1: return static_cast<size_t>(row) < done;
	default: return {};
	}
}

void PianoRollViewModel::undoOrRedo(bool back)
{
	std::vector<Step> &from = back ? undo_ : redo_, &to = back ? redo_ : undo_;
	if (from.empty()) return;
	Step step = std::move(from.back());
	from.pop_back();
	stroke_ = false;
	lastWasRecord_ = false;
	if (!scriptEditing.get()) session_.recordingMode.set(false);
	session_.markChanged();
	const int64_t before = inputs_.size();
	const bool shifted = step.shiftCount != 0;
	/* the rows the step set, as they were or became (where frames moved,
	 * in the places after the move) */
	auto setRows = [&](bool asBefore) {
		std::map<int64_t, bool> touched;
		for (const auto &[f, change] : step.frames) touched[f] = true;
		for (const auto &[f, change] : step.axes) touched[f] = true;
		for (const auto &[f, unused] : touched)
		{
			const auto b = step.frames.find(f);
			const auto x = step.axes.find(f);
			const Buttons buttons = b != step.frames.end() ? (asBefore ? b->second.first : b->second.second) : inputs_.at(f);
			const std::vector<int32_t> *axes = x != step.axes.end() ? (asBefore ? &x->second.first : &x->second.second) : nullptr;
			inputs_.setRow(f, buttons, axes != nullptr && !axes->empty() ? axes->data() : nullptr);
		}
	};
	if (back)
	{
		setRows(true);
		if (step.shiftCount > 0) inputs_.remove(step.shiftAt, step.shiftCount);
		else if (step.shiftCount < 0)
		{
			inputs_.insert(step.shiftAt, -step.shiftCount);
			for (size_t i = 0; i < step.removedButtons.size(); i++)
				inputs_.setRow(step.shiftAt + static_cast<int64_t>(i), step.removedButtons[i],
					step.removedAxes[i].empty() ? nullptr : step.removedAxes[i].data());
		}
		if (!shifted) inputs_.resize(step.sizeBefore);
		if (step.markersBefore) markers_ = *step.markersBefore;
	}
	else
	{
		if (step.shiftCount > 0) inputs_.insert(step.shiftAt, step.shiftCount);
		else if (step.shiftCount < 0) inputs_.remove(step.shiftAt, -step.shiftCount);
		if (!shifted) inputs_.resize(std::max(step.sizeAfter, before));
		setRows(false);
		if (!shifted) inputs_.resize(step.sizeAfter);
		if (step.markersAfter) markers_ = *step.markersAfter;
	}
	int64_t first = step.frames.empty() ? std::min(step.sizeBefore, step.sizeAfter) : step.frames.begin()->first;
	int64_t last = step.frames.empty() ? std::max(step.sizeBefore, step.sizeAfter) - 1 : step.frames.rbegin()->first;
	if (!step.axes.empty())
	{
		first = std::min(first, step.axes.begin()->first);
		last = std::max(last, step.axes.rbegin()->first);
	}
	const bool markersMoved = step.markersBefore.has_value();
	/* a marker's step changes no frame: nothing to run again */
	const bool onlyMarkers = step.frames.empty() && step.axes.empty() && step.sizeBefore == step.sizeAfter && step.shiftCount == 0;
	const int64_t shiftAt = step.shiftAt;
	to.push_back(std::move(step));
	canUndo.set(!undo_.empty());
	canRedo.set(!redo_.empty());
	historyChanged();
	if (markersMoved) markersEdited(0);
	if (shifted || inputs_.size() < before)
	{
		/* frames moved or went: the list is told so as a whole */
		const int64_t from = shifted ? shiftAt : first;
		frameCount.set(inputs_.size());
		clampTop();
		frames_.forget();
		frames_.reset.emit();
		if (from < std::max(before, inputs_.size())) session_.edited(from);
		refreshRun(from);
		return;
	}
	if (onlyMarkers) return;
	edited(first, last, before);
}

void PianoRollViewModel::rightDragOnTo(int64_t frame)
{
	/* the drag's last effect taken back, so the whole drag is worked out from
	 * where it began and kept as one step */
	if (rightStepMade_)
	{
		undoOrRedo(true);
		if (!redo_.empty()) redo_.pop_back();
		canRedo.set(!redo_.empty());
		historyChanged();
		rightStepMade_ = false;
	}
	const int64_t n = static_cast<int64_t>(rightInput_.size()), size = inputs_.size();
	auto taken = [&](int64_t at) {
		const int64_t i = ((at - rightFrame_) % n + n) % n;
		return rightInput_[static_cast<size_t>(i)];
	};
	/* as Chimera does: not past where the input taken up still fits */
	frame = std::max<int64_t>(0, std::min(frame, size - n));
	rightLow_ = std::min(rightLow_, frame);
	rightHigh_ = std::max(rightHigh_, frame);
	std::string name;
	if (rightShift_ && rightCtrl_)
	{
		/* copies put in after where it came from, as many as frames dragged past it */
		name = tr("piano_roll.step_right_extend_insert");
		const int64_t at = rightStart_ < rightFrame_ ? rightFrame_ : rightFrame_ + 1;
		const int64_t count = frame - at + 1;
		if (count <= 0) return;
		std::vector<Row> rows;
		for (int64_t i = 0; i < count; i++) rows.push_back(taken(at + i));
		naming_ = name;
		putIn(at, rows);
	}
	else if (rightShift_)
	{
		/* every frame the drag passed over takes the input, repeated */
		name = tr("piano_roll.step_right_extend");
		std::vector<Row> rows;
		for (int64_t f = rightLow_; f <= std::min(rightHigh_, size - 1); f++) rows.push_back(taken(f));
		naming_ = name;
		overwrite(rightLow_, rows);
	}
	else if (rightCtrl_)
	{
		/* a copy where the pointer is, the original kept */
		name = tr("piano_roll.step_right_copy");
		if (frame == rightFrame_) return;
		naming_ = name;
		overwrite(frame, rightInput_);
	}
	else
	{
		/* moved where the pointer is, the frames between taking its place */
		name = tr("piano_roll.step_right_move");
		if (frame == rightFrame_) return;
		const int64_t low = std::min(frame, rightFrame_);
		std::vector<Row> between;
		if (frame < rightFrame_)
			for (int64_t f = frame; f < rightFrame_; f++) between.push_back(readRow(f));
		else
			for (int64_t f = rightFrame_ + n; f < frame + n; f++) between.push_back(readRow(f));
		std::vector<Row> rows;
		if (frame < rightFrame_)
		{
			rows = rightInput_;
			rows.insert(rows.end(), between.begin(), between.end());
		}
		else
		{
			rows = between;
			rows.insert(rows.end(), rightInput_.begin(), rightInput_.end());
		}
		naming_ = name;
		overwrite(low, rows);
	}
	naming_.clear();
	rightStepMade_ = true;
}

PianoRollViewModel::Row PianoRollViewModel::readRow(int64_t frame) const
{
	Row r;
	r.buttons = inputs_.at(frame);
	r.axes.resize(inputs_.axisCount());
	if (!r.axes.empty()) inputs_.axesAt(frame, r.axes.data());
	return r;
}

void PianoRollViewModel::writeRow(int64_t frame, const Row &row)
{
	inputs_.setRow(frame, row.buttons, row.axes.empty() ? nullptr : row.axes.data());
}

void PianoRollViewModel::moveMarkers(Step &step, int64_t at, int64_t by)
{
	if (!bindMarkers.get() || markers_.empty()) return;
	step.markersBefore = markers_;
	std::map<int64_t, Marker> moved;
	for (const auto &[f, m] : markers_)
	{
		/* a marker on a frame taken out goes with it */
		if (by < 0 && f >= at && f < at - by) continue;
		Marker n = m;
		if (f >= at) n.frame = f + by;
		moved[n.frame] = n;
	}
	markers_ = moved;
	step.markersAfter = markers_;
}

void PianoRollViewModel::putIn(int64_t at, const std::vector<Row> &rows)
{
	if (rows.empty()) return;
	if (limited())
	{
		putInShown(at, rows);
		return;
	}
	const int64_t before = inputs_.size(), n = static_cast<int64_t>(rows.size());
	Step step;
	step.sizeBefore = before;
	step.shiftAt = at;
	step.shiftCount = n;
	std::vector<int32_t> neutral(static_cast<size_t>(inputs_.axisCount()));
	if (!neutral.empty()) inputs_.neutralAxes(neutral.data());
	moveMarkers(step, at, n);
	inputs_.insert(at, n);
	for (int64_t i = 0; i < n; i++)
	{
		writeRow(at + i, rows[static_cast<size_t>(i)]);
		step.frames[at + i] = {Buttons(), rows[static_cast<size_t>(i)].buttons};
		if (!neutral.empty()) step.axes[at + i] = {neutral, rows[static_cast<size_t>(i)].axes};
	}
	step.sizeAfter = inputs_.size();
	keep(std::move(step), false);
	if (bindMarkers.get()) markersEdited(0);
	frameCount.set(inputs_.size());
	frames_.forget();
	frames_.reset.emit();
	if (at < before) session_.edited(at);
	refreshRun(at);
}

void PianoRollViewModel::takeOut(int64_t first, int64_t last)
{
	if (limited())
	{
		takeOutShown(first, last);
		return;
	}
	const int64_t before = inputs_.size(), n = last - first + 1;
	Step step;
	step.sizeBefore = before;
	step.shiftAt = first;
	step.shiftCount = -n;
	for (int64_t f = first; f <= last; f++)
	{
		Row r = readRow(f);
		step.removedButtons.push_back(r.buttons);
		step.removedAxes.push_back(std::move(r.axes));
	}
	moveMarkers(step, first, -n);
	inputs_.remove(first, n);
	step.sizeAfter = inputs_.size();
	keep(std::move(step), false);
	if (bindMarkers.get()) markersEdited(0);
	frameCount.set(inputs_.size());
	clampTop();
	frames_.forget();
	frames_.reset.emit();
	session_.edited(first);
	refreshRun(first);
}

void PianoRollViewModel::overwrite(int64_t at, const std::vector<Row> &rows)
{
	if (rows.empty()) return;
	const int64_t before = inputs_.size();
	const bool shownOnly = limited();
	Step step;
	step.sizeBefore = before;
	for (size_t i = 0; i < rows.size(); i++)
	{
		const int64_t f = at + static_cast<int64_t>(i);
		const Row was = readRow(f);
		const Row now = shownOnly ? mixShown(was, rows[i]) : rows[i];
		writeRow(f, now);
		step.frames[f] = {was.buttons, now.buttons};
		if (!was.axes.empty()) step.axes[f] = {was.axes, now.axes};
	}
	step.sizeAfter = inputs_.size();
	keep(std::move(step), false);
	frames_.forget();
	edited(at, at + static_cast<int64_t>(rows.size()) - 1, before);
}

void PianoRollViewModel::autoHeldChanged()
{
	session_.setAutoHeldColumns(std::vector<std::string>(autoHeld_.begin(), autoHeld_.end()));
	pushColumnHolds();
	columnRows_.reset.emit();
	frames_.reset.emit();
}

void PianoRollViewModel::pushColumnHolds()
{
	/* as Chimera's TAStudio.UpdateAutoFire: held every frame, fired as
	 * autofire's counts say, or the column's own pattern */
	const std::string &kind = patternKind.get();
	const int64_t on = std::max<int64_t>(1, session_.autofireOn.get()), off = std::max<int64_t>(1, session_.autofireOff.get());
	std::vector<SessionViewModel::ColumnPattern> all;
	const std::vector<std::string> &buttons = session_.buttonNames();
	const auto &axes = session_.axes();
	for (const std::string &name : autoHeld_)
	{
		/* a hidden column records nothing when hidden ones are not edited */
		if (limited() && hidden_.count(name) != 0) continue;
		SessionViewModel::ColumnPattern cp;
		const Pattern *own = nullptr;
		int32_t pressed = 1, released = 0;
		bool found = false;
		for (size_t b = 0; b < buttons.size() && !found; b++)
			if (buttons[b] == name)
			{
				found = true;
				cp.index = static_cast<int32_t>(b);
				if (b < buttonPatterns_.size()) own = &buttonPatterns_[b];
			}
		for (size_t x = 0; x < axes.size() && !found; x++)
			if (axes[x].name == name)
			{
				found = true;
				cp.isAxis = true;
				cp.index = static_cast<int32_t>(x);
				if (x < axisPatterns_.size()) own = &axisPatterns_[x];
				const auto held = axisHoldValues_.find(name);
				pressed = held != axisHoldValues_.end() && held->second >= axes[x].min && held->second <= axes[x].max
							  ? held->second : static_cast<int32_t>(axes[x].max);
				released = static_cast<int32_t>(axes[x].neutral);
			}
		if (!found) continue;
		if (kind == "custom" && own != nullptr)
		{
			cp.values = own->values;
			cp.skipLag = own->skipLag;
			cp.loop = own->loop;
		}
		else if (kind == "autofire")
		{
			cp.values.assign(static_cast<size_t>(on), pressed);
			cp.values.insert(cp.values.end(), static_cast<size_t>(off), released);
			cp.skipLag = session_.autofireSkipsLag.get();
		}
		else
			cp.values = {pressed};
		all.push_back(std::move(cp));
	}
	session_.setColumnPatterns(std::move(all));
}

void PianoRollViewModel::storeLagHiding()
{
	session_.setTastudioMember("LagFramesToHide", std::to_string(lagFramesToHide.get()));
	session_.setTastudioMember("HideWasLagFrames", hideWasLagFrames.get() ? "true" : "false");
	frames_.reset.emit();
}

bool PianoRollViewModel::laggy(int64_t frame) const
{
	/* a lag frame of the greenzone's; one before an edit, when asked */
	return inputs_.lagAt(frame) && (frame < greenEnd.get() || hideWasLagFrames.get());
}

bool PianoRollViewModel::frameHidden(int64_t frame) const
{
	/* as Chimera does: in a run of lag frames the first N are not
	 * shown, the rest of a longer run are */
	const int64_t n = lagFramesToHide.get();
	if (n <= 0 || !laggy(frame)) return false;
	int64_t before = 0;
	for (int64_t f = frame - 1; f >= 0 && before < n && laggy(f); f--) before++;
	return before < n;
}

int64_t PianoRollViewModel::shownFrom(int64_t frame, int64_t steps) const
{
	int64_t f = frame;
	const int64_t size = inputs_.size();
	for (int64_t i = 0; i < steps; i++)
	{
		f++;
		while (f < size && frameHidden(f)) f++;
	}
	for (int64_t i = 0; i > steps; i--)
	{
		f--;
		while (f > 0 && frameHidden(f)) f--;
	}
	return std::max<int64_t>(f, 0);
}

int64_t PianoRollViewModel::Frames::nextShown(int64_t row) const
{
	return vm_.shownFrom(row, 1);
}

std::vector<std::string> PianoRollViewModel::columnGroups() const
{
	/* as Chimera's TAStudio.SetUpToolStripColumns: "P<n> ..." to Player n,
	 * "Key ..." to groups of keys, the rest at the top */
	const int32_t count = frames_.columnCount();
	std::vector<std::string> groups(static_cast<size_t>(count));
	std::vector<int32_t> keys;
	for (int32_t c = 0; c < count; c++)
	{
		const std::string name = frames_.columnName(c);
		if (name.rfind("Key ", 0) == 0) keys.push_back(c);
		else if (name.size() >= 2 && name[0] == 'P' && std::isdigit(static_cast<unsigned char>(name[1])))
			groups[static_cast<size_t>(c)] = "Player " + std::string(1, name[1]);
	}
	constexpr size_t kKeysAGroup = 30;
	for (size_t i = 0; i < keys.size(); i += kKeysAGroup)
	{
		const size_t last = std::min(keys.size(), i + kKeysAGroup) - 1;
		const std::string label = "Keys (" + frames_.columnName(keys[i]).substr(4) + " - " + frames_.columnName(keys[last]).substr(4) + ")";
		for (size_t k = i; k <= last; k++) groups[static_cast<size_t>(keys[k])] = label;
	}
	return groups;
}

std::vector<std::string> PianoRollViewModel::groupNames() const
{
	std::vector<std::string> players, keys;
	for (const std::string &g : columnGroups())
	{
		if (g.empty()) continue;
		std::vector<std::string> &into = g.rfind("Keys (", 0) == 0 ? keys : players;
		if (std::find(into.begin(), into.end(), g) == into.end()) into.push_back(g);
	}
	std::sort(players.begin(), players.end());
	/* Chimera lists the keys' groups first */
	keys.insert(keys.end(), players.begin(), players.end());
	return keys;
}

std::string PianoRollViewModel::GroupRows::columnName(int32_t column) const
{
	static const char *const names[] = {"name", "toggle", "shown", "firstOfToggle"};
	return column >= 0 && column < 4 ? names[column] : "";
}

Value PianoRollViewModel::GroupRows::cell(int64_t row, int32_t column) const
{
	const std::vector<std::string> names = vm_.groupNames();
	if (row < 0 || row >= static_cast<int64_t>(names.size())) return {};
	const std::string &name = names[static_cast<size_t>(row)];
	const bool isKeys = name.rfind("Keys (", 0) == 0;
	switch (column)
	{
	case 0: return name;
	case 1: return isKeys ? tr("piano_roll.group_keys") : name.rfind("Player ", 0) == 0 ? tr("piano_roll.group_player", {name.substr(7)}) : name;
	/* the keys' groups share one toggle, shown once */
	case 3: return !isKeys || row == 0 || names[static_cast<size_t>(row) - 1].rfind("Keys (", 0) != 0;
	case 2:
	{
		const std::vector<std::string> groups = vm_.columnGroups();
		for (int32_t c = 0; c < vm_.frames_.columnCount(); c++)
		{
			const std::string &g = groups[static_cast<size_t>(c)];
			const bool member = isKeys ? g.rfind("Keys (", 0) == 0 : g == name;
			if (member && vm_.hidden_.count(vm_.frames_.columnName(c)) != 0) return false;
		}
		return true;
	}
	default: return {};
	}
}

int PianoRollViewModel::playerOf(const std::string &name)
{
	if (name.size() < 3 || name[0] != 'P' || !std::isdigit(static_cast<unsigned char>(name[1]))) return 0;
	size_t i = 1;
	long n = 0;
	while (i < name.size() && std::isdigit(static_cast<unsigned char>(name[i])) && n <= 255) n = n * 10 + (name[i++] - '0');
	return i < name.size() && name[i] == ' ' && n <= 255 ? static_cast<int>(n) : 0;
}

std::vector<bool> PianoRollViewModel::playerEdges(const std::vector<int32_t> &players, const std::vector<bool> &shown)
{
	std::vector<bool> edges(players.size() + 1, false);
	int32_t previous = -1;
	for (size_t c = 0; c < players.size(); c++)
	{
		if (c < shown.size() && !shown[c]) continue;
		if (players[c] != previous) edges[c] = true;
		previous = players[c];
	}
	if (previous != -1) edges[players.size()] = true;
	return edges;
}

int32_t PianoRollViewModel::Frames::columnPlayer(int32_t column) const
{
	const auto &players = vm_.session_.buttonPlayers();
	const auto &names = vm_.session_.buttonNames();
	if (column >= 0 && column < static_cast<int32_t>(names.size()))
		return static_cast<size_t>(column) < players.size() ? players[static_cast<size_t>(column)] : 0;
	const auto &axes = vm_.session_.axes();
	const size_t axis = static_cast<size_t>(column - static_cast<int32_t>(names.size()));
	return column >= 0 && axis < axes.size() ? axes[axis].player : 0;
}

std::string PianoRollViewModel::macroText(int64_t first, int64_t last, const std::vector<std::string> &columns, bool overlay, bool replace,
	std::string &error) const
{
	/* the frame after the last can be picked, but has no input */
	if (last == inputs_.size()) last--;
	if (first > last) std::swap(first, last);
	if (first < 0 || last >= inputs_.size())
	{
		error = tr("piano_roll.frames_range", {std::to_string(inputs_.size() - 1)});
		return {};
	}
	const std::string key = movieKey();
	if (key.empty())
	{
		error = tr("piano_roll.no_columns");
		return {};
	}
	std::vector<std::string> keys;
	for (size_t at = 0; at <= key.size();)
	{
		const size_t bar = key.find('|', at);
		keys.push_back(key.substr(at, bar == std::string::npos ? std::string::npos : bar - at));
		if (bar == std::string::npos) break;
		at = bar + 1;
	}
	/* the columns asked for, in the movie's order; none named is all of them */
	std::vector<std::string> mine;
	for (const std::string &k : keys)
		if (columns.empty() || std::find(columns.begin(), columns.end(), k) != columns.end()) mine.push_back(k);
	if (mine.empty())
	{
		error = tr("piano_roll.columns_not_movie");
		return {};
	}
	int64_t groups = 0;
	for (char c : movieKeyRaw()) groups += c == '#' ? 1 : 0;
#ifdef _WIN32
	const char *eol = "\r\n";
#else
	const char *eol = "\n";
#endif
	std::string zoneKey;
	for (const std::string &k : mine) zoneKey += (zoneKey.empty() ? "" : "|") + k;
	std::string text = zoneKey + eol + session_.controllerName() + eol + std::to_string(groups) + eol + (overlay ? "True" : "False") + "," +
		(replace ? "True" : "False") + eol;
	if (mine.size() == keys.size())
	{
		/* the movie's own lines, as Chimera's zone keeps them when its key is the movie's */
		for (const std::string &line : inputs_.lines(first, last)) text += line + eol;
		return text;
	}
	/* a line a frame of those columns only, as Chimera does
	 * writes a zone's: grouped by player, each group's axes (five wide and a
	 * comma) before its buttons (a mnemonic, '.' let go) */
	const std::vector<std::string> &buttons = session_.buttonNames();
	const auto &axes = session_.axes();
	const std::string &mnemonics = session_.mnemonics();
	int most = 0;
	for (const std::string &k : mine) most = std::max(most, playerOf(k));
	for (int64_t f = first; f <= last; f++)
	{
		const Row r = readRow(f);
		std::string line = "|";
		for (int g = 0; g <= most; g++)
		{
			for (const std::string &k : mine)
			{
				if (playerOf(k) != g) continue;
				const auto ax = std::find_if(axes.begin(), axes.end(), [&k](const SessionViewModel::Axis &x) { return x.name == k; });
				if (ax == axes.end()) continue;
				const size_t x = static_cast<size_t>(ax - axes.begin());
				std::string v = std::to_string(x < r.axes.size() ? r.axes[x] : ax->neutral);
				while (v.size() < 5) v.insert(0, " ");
				line += v + ",";
			}
			for (const std::string &k : mine)
			{
				if (playerOf(k) != g) continue;
				const auto b = std::find(buttons.begin(), buttons.end(), k);
				if (b == buttons.end()) continue;
				const size_t i = static_cast<size_t>(b - buttons.begin());
				const bool pressed = r.buttons.test(i);
				line += pressed ? (i < mnemonics.size() ? mnemonics[i] : k[0]) : '.';
			}
			line += "|";
		}
		text += line + eol;
	}
	return text;
}

bool PianoRollViewModel::placeMacroLines(int64_t at, std::vector<std::string> lines, const std::string &name, std::string &error)
{
	if (at < 0 || at > inputs_.size())
	{
		error = tr("piano_roll.no_frame", {std::to_string(at)});
		return false;
	}
	while (!lines.empty() && lines.back().empty()) lines.pop_back();
	if (lines.size() < 4)
	{
		error = tr("piano_roll.not_macro", {name});
		return false;
	}
	/* its columns, each one the movie has */
	std::vector<std::string> keys;
	for (size_t at2 = 0; at2 <= lines[0].size();)
	{
		const size_t bar = lines[0].find('|', at2);
		keys.push_back(lines[0].substr(at2, bar == std::string::npos ? std::string::npos : bar - at2));
		if (bar == std::string::npos) break;
		at2 = bar + 1;
	}
	const std::string mine = movieKey();
	std::vector<std::string> movieKeys;
	for (size_t at2 = 0; at2 <= mine.size();)
	{
		const size_t bar = mine.find('|', at2);
		movieKeys.push_back(mine.substr(at2, bar == std::string::npos ? std::string::npos : bar - at2));
		if (bar == std::string::npos) break;
		at2 = bar + 1;
	}
	for (const std::string &k : keys)
		if (std::find(movieKeys.begin(), movieKeys.end(), k) == movieKeys.end())
		{
			error = tr("piano_roll.macro_incompatible", {lines[1], lines[2]});
			return false;
		}
	auto lower = [](std::string t) {
		for (char &c : t) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
		return t;
	};
	const size_t comma = lines[3].find(',');
	const bool overlay = lower(lines[3].substr(0, comma)) == "true";
	const bool replace = comma == std::string::npos || lower(lines[3].substr(comma + 1)) != "false";
	/* each line read by its columns: a button a character, '.' let go; an analog value up to its comma */
	const std::vector<std::string> &buttons = session_.buttonNames();
	const auto &axes = session_.axes();
	std::vector<Row> rows;
	const Row blank = emptyRow();
	for (size_t i = 4; i < lines.size(); i++)
	{
		const int64_t f = at + static_cast<int64_t>(i - 4);
		Row r = replace && f < inputs_.size() ? readRow(f) : blank;
		const std::string &entry = lines[i];
		size_t p = 0;
		for (const std::string &k : keys)
		{
			while (p < entry.size() && entry[p] == '|') p++;
			if (p >= entry.size())
			{
				error = tr("piano_roll.macro_short", {std::to_string(i - 4)});
				return false;
			}
			const auto axis = std::find_if(axes.begin(), axes.end(), [&k](const SessionViewModel::Axis &x) { return x.name == k; });
			if (axis != axes.end())
			{
				const size_t end = entry.find(',', p);
				if (end == std::string::npos)
				{
					error = tr("piano_roll.macro_no_value", {std::to_string(i - 4), k});
					return false;
				}
				const int32_t v = static_cast<int32_t>(std::strtol(entry.substr(p, end - p).c_str(), nullptr, 10));
				const size_t x = static_cast<size_t>(axis - axes.begin());
				if (x < r.axes.size() && (!overlay || v != axis->neutral)) r.axes[x] = v;
				p = end + 1;
				continue;
			}
			const auto b = std::find(buttons.begin(), buttons.end(), k);
			const bool pressed = entry[p] != '.';
			p++;
			if (b == buttons.end()) continue;
			if (pressed || !overlay) r.buttons.set(static_cast<size_t>(b - buttons.begin()), pressed);
		}
		rows.push_back(std::move(r));
	}
	if (rows.empty())
	{
		error = tr("piano_roll.macro_empty", {name});
		return false;
	}
	naming_ = tr("piano_roll.step_place_macro", {std::to_string(at)});
	if (replace) overwrite(at, rows);
	else putIn(at, rows);
	return true;
}

std::string PianoRollViewModel::movieKeyRaw() const
{
	/* the movie's LogKey, as its text has it ("#P1 Up|P1 Down|...|") */
	const std::string all = inputs_.text();
	ce_movie_log *log = ce_movie_log_new();
	std::string key;
	if (ce_movie_log_parse(log, all.data(), all.size()) == 0)
		if (const char *k = ce_movie_log_key(log)) key = k;
	ce_movie_log_free(log);
	return key;
}

std::string PianoRollViewModel::movieKey() const
{
	/* as Chimera cleans it: no '#', no last '|' */
	std::string key = movieKeyRaw();
	key.erase(std::remove(key.begin(), key.end(), '#'), key.end());
	if (!key.empty() && key.back() == '|') key.pop_back();
	return key;
}

std::vector<std::string> PianoRollViewModel::controlNames() const
{
	std::vector<std::string> names = session_.buttonNames();
	for (const SessionViewModel::Axis &ax : session_.axes()) names.push_back(ax.name);
	return names;
}

void PianoRollViewModel::defaultPatterns()
{
	/* Chimera's: a button on a frame and off the next; an axis at its most */
	buttonPatterns_.assign(session_.buttonNames().size(), Pattern{{1, 0}, true, 0});
	axisPatterns_.clear();
	for (const SessionViewModel::Axis &ax : session_.axes()) axisPatterns_.push_back(Pattern{{static_cast<int32_t>(ax.max)}, true, 0});
	patternRows_.reset.emit();
	storePatterns();
}

void PianoRollViewModel::storePatterns()
{
	/* as Chimera writes them in the project's TAStudio part */
	auto write = [](const std::vector<Pattern> &all, bool isButton) {
		json::Json list;
		list.kind = json::Json::Kind::Array;
		for (const Pattern &p : all)
		{
			json::Json one, values;
			one.kind = json::Json::Kind::Object;
			values.kind = json::Json::Kind::Array;
			for (int32_t v : p.values) values.items.push_back(isButton ? json::Json::boolean_(v != 0) : json::Json::integer(v));
			one.members.emplace_back("_index", json::Json::integer(0));
			one.members.emplace_back("SkipsLag", json::Json::boolean_(p.skipLag));
			one.members.emplace_back("Pattern", values);
			one.members.emplace_back("Loop", json::Json::integer(p.loop));
			list.items.push_back(one);
		}
		return json::write(list);
	};
	session_.setTastudioMember("BoolPatterns", write(buttonPatterns_, true));
	session_.setTastudioMember("AxisPatterns", write(axisPatterns_, false));
	json::Json holds;
	holds.kind = json::Json::Kind::Object;
	for (const auto &[name, value] : axisHoldValues_) holds.members.emplace_back(name, json::Json::integer(value));
	session_.setTastudioMember("AxisHoldValues", json::write(holds));
	patternNames_ = controlNames();
	patternRows_.reset.emit();
}

void PianoRollViewModel::loadPatterns(const std::map<std::string, std::string> &members)
{
	/* the project's own, when it has one a control (as Chimera checks);
	 * else the defaults */
	auto read = [](const std::map<std::string, std::string> &m, const char *name, size_t count, std::vector<Pattern> &out) {
		const auto it = m.find(name);
		json::Json list;
		std::string err;
		if (it == m.end() || !json::parse(it->second, list, err) || list.kind != json::Json::Kind::Array || list.items.size() != count)
			return false;
		std::vector<Pattern> read;
		for (const json::Json &one : list.items)
		{
			Pattern p;
			if (const json::Json *s = one.find("SkipsLag"); s != nullptr && s->kind == json::Json::Kind::Bool) p.skipLag = s->boolean;
			if (const json::Json *l = one.find("Loop")) p.loop = static_cast<int64_t>(l->number());
			if (const json::Json *v = one.find("Pattern"))
				for (const json::Json &x : v->items)
					p.values.push_back(x.kind == json::Json::Kind::Bool ? (x.boolean ? 1 : 0) : static_cast<int32_t>(x.number()));
			if (p.values.empty()) return false;
			if (p.loop < 0 || p.loop >= static_cast<int64_t>(p.values.size())) p.loop = 0;
			read.push_back(std::move(p));
		}
		out = std::move(read);
		return true;
	};
	/* the lag frames hidden: the project's, else none */
	{
		int64_t hide = 0;
		bool wasLag = false;
		json::Json v;
		std::string err;
		if (const auto it = members.find("LagFramesToHide"); it != members.end() && json::parse(it->second, v, err))
			hide = std::max<int64_t>(0, static_cast<int64_t>(v.number()));
		if (const auto it = members.find("HideWasLagFrames"); it != members.end() && json::parse(it->second, v, err) &&
			v.kind == json::Json::Kind::Bool)
			wasLag = v.boolean;
		lagFramesToHide.set(hide);
		hideWasLagFrames.set(wasLag);
		/* turned on its side, when the project was */
		bool across = false;
		if (const auto it = members.find("HorizontalOrientation"); it != members.end() && json::parse(it->second, v, err) &&
			v.kind == json::Json::Kind::Bool)
			across = v.boolean;
		horizontal.set(across);
	}
	axisHoldValues_.clear();
	if (const auto it = members.find("AxisHoldValues"); it != members.end())
	{
		json::Json holds;
		std::string err;
		if (json::parse(it->second, holds, err) && holds.kind == json::Json::Kind::Object)
			for (const auto &[name, value] : holds.members)
				if (value.kind == json::Json::Kind::Number) axisHoldValues_[name] = static_cast<int32_t>(value.number());
	}
	std::vector<Pattern> buttons = buttonPatterns_, axes = axisPatterns_;
	const bool haveButtons = read(members, "BoolPatterns", session_.buttonNames().size(), buttons);
	const bool haveAxes = read(members, "AxisPatterns", session_.axes().size(), axes);
	if (!haveButtons || !haveAxes)
	{
		defaultPatterns();
		return;
	}
	buttonPatterns_ = std::move(buttons);
	axisPatterns_ = std::move(axes);
	storePatterns();
}

const PianoRollViewModel::Pattern *PianoRollViewModel::patternOf(int64_t column) const
{
	const int64_t buttons = buttonCount();
	if (column >= 0 && column < buttons && static_cast<size_t>(column) < buttonPatterns_.size()) return &buttonPatterns_[static_cast<size_t>(column)];
	const int64_t axis = column - buttons;
	if (axis >= 0 && static_cast<size_t>(axis) < axisPatterns_.size()) return &axisPatterns_[static_cast<size_t>(axis)];
	return nullptr;
}

std::vector<int32_t> PianoRollViewModel::patternValues(const Pattern &p, int64_t first, int64_t last) const
{
	std::vector<int32_t> out;
	size_t at = 0;
	for (int64_t f = first; f <= last; f++)
	{
		/* a lag frame takes no input: it keeps the value before, and the pattern waits */
		if (p.skipLag && f > first && inputs_.lagAt(f))
		{
			out.push_back(out.back());
			continue;
		}
		out.push_back(p.values[at]);
		if (++at >= p.values.size()) at = static_cast<size_t>(p.loop);
	}
	return out;
}

bool PianoRollViewModel::paintPattern(int64_t first, int64_t last, bool join, std::string &error)
{
	const Pattern *p = patternOf(patternColumn_);
	if (p == nullptr || p->values.empty())
	{
		error = tr("piano_roll.no_such_control");
		return false;
	}
	const std::vector<int32_t> values = patternValues(*p, first, last);
	const int64_t b = patternColumn_;
	bool ok = true;
	for (int64_t f = first; f <= last && ok; f++)
	{
		const int32_t v = values[static_cast<size_t>(f - first)];
		const bool joined = join || f > first;
		if (b < buttonCount()) ok = paint(f, f, b, patternOn_ && v != 0, joined, error);
		else ok = paintAxis(f, f, b - buttonCount(), v, joined, error);
	}
	if (!join && b < buttonCount() && !undo_.empty())
		undo_.back().name = tr("piano_roll.step_paint", {frames_.columnName(static_cast<int32_t>(b)), std::to_string(first)});
	return ok;
}

std::string PianoRollViewModel::runsText(const Pattern &p, bool isButton)
{
	std::string out;
	for (size_t i = 0; i < p.values.size();)
	{
		size_t n = 1;
		while (i + n < p.values.size() && p.values[i + n] == p.values[i]) n++;
		if (!out.empty()) out += ", ";
		out += (isButton ? tr(p.values[i] != 0 ? "common.on" : "common.off") : std::to_string(p.values[i])) + " x" + std::to_string(n);
		i += n;
	}
	return out;
}

std::string PianoRollViewModel::PatternRows::columnName(int32_t column) const
{
	static const char *const names[] = {"name", "runs", "loop", "skipLag", "button", "min", "max", "neutral"};
	return column >= 0 && column < 8 ? names[column] : "";
}

Value PianoRollViewModel::PatternRows::cell(int64_t row, int32_t column) const
{
	if (row < 0 || row >= rowCount()) return {};
	const size_t buttons = vm_.buttonPatterns_.size();
	const bool isButton = static_cast<size_t>(row) < buttons;
	const Pattern &p = isButton ? vm_.buttonPatterns_[static_cast<size_t>(row)] : vm_.axisPatterns_[static_cast<size_t>(row) - buttons];
	switch (column)
	{
	case 0:
		return isButton ? (static_cast<size_t>(row) < vm_.session_.buttonNames().size() ? vm_.session_.buttonNames()[static_cast<size_t>(row)] : "")
						: (static_cast<size_t>(row) - buttons < vm_.session_.axes().size() ? vm_.session_.axes()[static_cast<size_t>(row) - buttons].name : "");
	case 1: return runsText(p, isButton);
	case 2: return p.loop;
	case 3: return p.skipLag;
	case 4: return isButton;
	/* an analog control's range and its rest, for a pattern's values (a button's: off and on) */
	case 5:
	case 6:
	case 7:
	{
		const size_t a = static_cast<size_t>(row) - buttons;
		if (isButton || a >= vm_.session_.axes().size()) return column == 6 ? int64_t{1} : int64_t{0};
		const SessionViewModel::Axis &ax = vm_.session_.axes()[a];
		return column == 5 ? ax.min : column == 6 ? ax.max : ax.neutral;
	}
	default: return {};
	}
}

bool PianoRollViewModel::Frames::columnHiddenIn(int64_t view, int32_t column) const
{
	if (view == vm_.activeRoll.get() || view < 0 || view >= static_cast<int64_t>(vm_.rolls_.size())) return columnHidden(column);
	return vm_.rolls_[static_cast<size_t>(view)].count(columnName(column)) != 0;
}

bool PianoRollViewModel::limited() const
{
	return !agentEditing_ && !editHiddenColumns.get() && !hidden_.empty();
}

Buttons PianoRollViewModel::shownButtons() const
{
	Buttons shown;
	const std::vector<std::string> &names = session_.buttonNames();
	for (size_t b = 0; b < names.size(); b++)
		if (hidden_.count(names[b]) == 0) shown.set(b);
	return shown;
}

std::vector<bool> PianoRollViewModel::shownAxes() const
{
	std::vector<bool> shown;
	for (const SessionViewModel::Axis &ax : session_.axes()) shown.push_back(hidden_.count(ax.name) == 0);
	return shown;
}

PianoRollViewModel::Row PianoRollViewModel::mixShown(const Row &to, const Row &from) const
{
	const Buttons shown = shownButtons();
	Row r = to;
	r.buttons = to.buttons.without(shown) | (from.buttons & shown);
	const std::vector<bool> axes = shownAxes();
	for (size_t i = 0; i < r.axes.size() && i < from.axes.size() && i < axes.size(); i++)
		if (axes[i]) r.axes[i] = from.axes[i];
	return r;
}

PianoRollViewModel::Row PianoRollViewModel::emptyRow() const
{
	Row r;
	r.axes.resize(inputs_.axisCount());
	if (!r.axes.empty()) inputs_.neutralAxes(r.axes.data());
	return r;
}

void PianoRollViewModel::putInShown(int64_t at, const std::vector<Row> &rows)
{
	/* as Chimera inserts input in the shown columns only: empty frames added at
	 * the end, the shown columns moved later by as many, the new frames'
	 * shown columns written; the hidden ones stay on their frames */
	const int64_t before = inputs_.size(), n = static_cast<int64_t>(rows.size());
	if (naming_.empty()) naming_ = tr("piano_roll.step_insert", n, {std::to_string(at)});
	Step step;
	step.sizeBefore = before;
	moveMarkers(step, at, n);
	inputs_.resize(before + n);
	auto set = [&](int64_t f, const Row &now) {
		const Row was = f < before ? readRow(f) : emptyRow();
		writeRow(f, now);
		step.frames[f] = {was.buttons, now.buttons};
		if (!was.axes.empty()) step.axes[f] = {was.axes, now.axes};
	};
	for (int64_t f = before + n - 1; f >= at + n; f--) set(f, mixShown(readRow(f), readRow(f - n)));
	for (int64_t i = 0; i < n; i++) set(at + i, mixShown(readRow(at + i), rows[static_cast<size_t>(i)]));
	step.sizeAfter = inputs_.size();
	keep(std::move(step), false);
	if (bindMarkers.get()) markersEdited(0);
	frameCount.set(inputs_.size());
	frames_.forget();
	frames_.reset.emit();
	if (at < before) session_.edited(at);
	refreshRun(at);
}

void PianoRollViewModel::takeOutShown(int64_t first, int64_t last)
{
	/* as Chimera removes frames in the shown columns only: the shown columns
	 * after the frames move earlier, the last ones left empty; the movie
	 * keeps its length and the hidden columns their frames */
	const int64_t size = inputs_.size(), n = last - first + 1;
	if (naming_.empty())
		naming_ = tr("piano_roll.step_delete", n, {std::to_string(first) + "-" + std::to_string(last)});
	Step step;
	step.sizeBefore = size;
	moveMarkers(step, first, -n);
	const Row blank = emptyRow();
	for (int64_t f = first; f < size; f++)
	{
		const Row was = readRow(f);
		const Row now = mixShown(was, f + n < size ? readRow(f + n) : blank);
		writeRow(f, now);
		step.frames[f] = {was.buttons, now.buttons};
		if (!was.axes.empty()) step.axes[f] = {was.axes, now.axes};
	}
	step.sizeAfter = size;
	keep(std::move(step), false);
	if (bindMarkers.get()) markersEdited(0);
	frames_.forget();
	frames_.reset.emit();
	session_.edited(first);
	refreshRun(first);
}

void PianoRollViewModel::truncateShown(int64_t frame)
{
	/* as Chimera's Truncate with columns limited: the shown columns cleared
	 * from the frame on, then the empty frames at the end taken off */
	const int64_t size = inputs_.size();
	Step step;
	step.sizeBefore = size;
	const Row blank = emptyRow();
	for (int64_t f = frame; f < size; f++)
	{
		const Row was = readRow(f);
		const Row now = mixShown(was, blank);
		writeRow(f, now);
		step.frames[f] = {was.buttons, now.buttons};
		if (!was.axes.empty()) step.axes[f] = {was.axes, now.axes};
	}
	int64_t end = size;
	while (end > frame)
	{
		const Row r = readRow(end - 1);
		if (r.buttons.any() || r.axes != blank.axes) break;
		end--;
	}
	inputs_.resize(end);
	step.sizeAfter = end;
	keep(std::move(step), false);
	frameCount.set(inputs_.size());
	clampTop();
	frames_.forget();
	frames_.reset.emit();
	session_.edited(frame);
	refreshRun(frame);
}

void PianoRollViewModel::pickAxis(int64_t frame, int64_t axis)
{
	const SessionViewModel::Axis &ax = session_.axes()[static_cast<size_t>(axis)];
	std::vector<int32_t> values(inputs_.axisCount());
	if (!values.empty()) inputs_.axesAt(frame, values.data());
	axisName.set(ax.name);
	axisMin.set(ax.min);
	axisMax.set(ax.max);
	axisValue.set(static_cast<size_t>(axis) < values.size() ? values[static_cast<size_t>(axis)] : ax.neutral);
	pickedFrame.set(frame);
	pickedAxis.set(axis);
}

bool PianoRollViewModel::paintAxis(int64_t first, int64_t last, int64_t axis, int64_t value, bool join, std::string &error)
{
	if (first > last) std::swap(first, last);
	const auto &axes = session_.axes();
	if (axis < 0 || axis >= static_cast<int64_t>(axes.size()) || first < 0)
	{
		error = tr("piano_roll.no_frame_or_analog");
		return false;
	}
	const SessionViewModel::Axis &ax = axes[static_cast<size_t>(axis)];
	if (value < ax.min || value > ax.max)
	{
		error = tr("piano_roll.axis_range", {ax.name, std::to_string(ax.min), std::to_string(ax.max)});
		return false;
	}
	const int64_t before = inputs_.size();
	Step step;
	step.sizeBefore = before;
	step.name = join ? tr("piano_roll.step_paint_axis", {ax.name, ""})
		: tr("piano_roll.step_set_axis", {ax.name, std::to_string(value), first == last ? std::to_string(first) : std::to_string(first) + "-" + std::to_string(last)});
	for (int64_t f = first; f <= last; f++)
	{
		const Row was = readRow(f);
		Row now = was;
		if (static_cast<size_t>(axis) < now.axes.size()) now.axes[static_cast<size_t>(axis)] = static_cast<int32_t>(value);
		writeRow(f, now);
		step.frames[f] = {was.buttons, was.buttons};
		step.axes[f] = {was.axes, now.axes};
	}
	step.sizeAfter = inputs_.size();
	keep(std::move(step), join);
	frames_.forget();
	edited(first, last, before);
	if (pickedAxis.get() == axis && pickedFrame.get() >= first && pickedFrame.get() <= last) axisValue.set(value);
	return true;
}

bool PianoRollViewModel::range(const Args &a, int64_t &first, int64_t &last, std::string &error) const
{
	if (!arg(a, 0, first, error) || !arg(a, 1, last, error)) return false;
	if (first > last) std::swap(first, last);
	if (first < 0 || last >= inputs_.size())
	{
		error = tr("piano_roll.frames_range", {std::to_string(inputs_.size() - 1)});
		return false;
	}
	return true;
}

void PianoRollViewModel::edited(int64_t first, int64_t last, int64_t oldCount)
{
	const int64_t now = inputs_.size();
	frameCount.set(now);
	if (first < oldCount)
	{
		frames_.changed.emit(first, std::min(last, oldCount - 1));
		/* frames the machine may have run: its states after the edit no longer hold */
		session_.edited(first);
	}
	if (now > oldCount) frames_.appended.emit(oldCount, now - 1);
	refreshRun(std::min(first, oldCount));
}

void PianoRollViewModel::pushRecordMask()
{
	if (limited()) session_.setRecordMask(shownButtons(), shownAxes());
	else session_.setRecordMask(std::nullopt);
}

void PianoRollViewModel::framesRecorded(const std::vector<SessionViewModel::RecordedFrame> &frames)
{
	recordingStep_ = true;
	for (const SessionViewModel::RecordedFrame &r : frames)
	{
		Step step;
		step.frames[r.frame] = {r.before, r.after};
		if (r.axesBefore != r.axesAfter) step.axes[r.frame] = {r.axesBefore, r.axesAfter};
		step.sizeAfter = r.length;
		step.sizeBefore = r.appended ? r.frame : r.length;
		/* frames recorded while the machine runs are one step, named for the first */
		const bool join = r.running && lastWasRecord_ && !undo_.empty();
		step.name = tr("piano_roll.step_record", {join ? std::string() : std::to_string(r.frame)});
		keep(std::move(step), join);
		/* a frame recorded while paused stands alone; the next run starts a step of its own */
		lastWasRecord_ = r.running;
	}
	recordingStep_ = false;
}

void PianoRollViewModel::setHiddenColumns(const std::set<std::string> &names)
{
	if (names == hidden_) return;
	hidden_ = names;
	hiddenChanged();
	session_.markChanged();
}

void PianoRollViewModel::hiddenChanged()
{
	std::string text;
	for (const std::string &n : hidden_) text += (text.empty() ? "" : "\n") + n;
	hiddenColumns.set(text);
	/* the project keeps the first roll's columns */
	rolls_[static_cast<size_t>(activeRoll.get())] = hidden_;
	const std::set<std::string> &first = rolls_.front();
	session_.setHiddenColumns(std::vector<std::string>(first.begin(), first.end()));
	pushRecordMask();
	pushColumnHolds();
	columnRows_.reset.emit();
	groupRows_.reset.emit();
	frames_.reset.emit();
}

std::string PianoRollViewModel::ColumnRows::columnName(int32_t column) const
{
	static const char *const names[] = {"name", "label", "shown", "autoHold", "group"};
	return column >= 0 && column < 5 ? names[column] : "";
}

Value PianoRollViewModel::ColumnRows::cell(int64_t row, int32_t column) const
{
	if (row < 0 || row >= rowCount()) return {};
	const int32_t c = static_cast<int32_t>(row);
	switch (column)
	{
	case 0: return vm_.frames_.columnName(c);
	case 1: return vm_.frames_.columnShortName(c);
	case 2: return !vm_.frames_.columnHidden(c);
	case 3: return vm_.autoHeld_.count(vm_.frames_.columnName(c)) != 0;
	case 4:
	{
		const std::vector<std::string> groups = vm_.columnGroups();
		return static_cast<size_t>(c) < groups.size() ? groups[static_cast<size_t>(c)] : std::string();
	}
	default: return {};
	}
}

std::string PianoRollViewModel::Frames::columnName(int32_t column) const
{
	const auto &names = vm_.session_.buttonNames();
	if (column >= 0 && column < static_cast<int32_t>(names.size())) return names[static_cast<size_t>(column)];
	const auto &axes = vm_.session_.axes();
	const size_t axis = static_cast<size_t>(column - static_cast<int32_t>(names.size()));
	return column >= 0 && axis < axes.size() ? axes[axis].name : std::string();
}

std::string PianoRollViewModel::Frames::columnShortName(int32_t column) const
{
	if (column >= vm_.buttonCount())
	{
		const size_t axis = static_cast<size_t>(column - vm_.buttonCount());
		const auto &axes = vm_.session_.axes();
		return axis < axes.size() ? axes[axis].header : std::string();
	}
	/* as Chimera heads it: the package's header, else the button's letter */
	const std::vector<std::string> &headers = vm_.session_.buttonHeaders();
	if (column >= 0 && column < static_cast<int32_t>(headers.size())) return headers[static_cast<size_t>(column)];
	return columnName(column);
}

uint32_t PianoRollViewModel::Frames::rowFlags(int64_t row) const
{
	const bool permanent = vm_.permanentAt(row);
	return (vm_.inputs_.lagAt(row) ? kLag : 0) | (row < vm_.greenEnd.get() ? kGreen : 0) |
		(vm_.markers_.count(row) || permanent ? kMarker : 0) | (permanent ? kPermanent : 0) |
		(vm_.session_.hasState(row) ? kStated : 0) | (vm_.inputs_.ranAt(row) ? kRan : 0);
}

std::string PianoRollViewModel::Frames::rowNote(int64_t row) const
{
	const auto it = vm_.markers_.find(row);
	return it == vm_.markers_.end() ? std::string() : it->second.text;
}

void PianoRollViewModel::replaceAll(const std::vector<Marker> &markers)
{
	markers_.clear();
	for (const Marker &m : markers) markers_[m.frame] = m;
	undo_.clear();
	redo_.clear();
	historyChanged();
	canUndo.set(false);
	canRedo.set(false);
	frameCount.set(inputs_.size());
	clampTop();
	markersEdited(0);
	refreshRun(0);
	frames_.reset.emit();
}

/* a marker set, moved or taken off is a step of the undo history */
void PianoRollViewModel::keepMarkers(std::map<int64_t, Marker> before, std::string name)
{
	Step step;
	step.sizeBefore = step.sizeAfter = inputs_.size();
	step.markersBefore = std::move(before);
	step.markersAfter = markers_;
	step.name = std::move(name);
	keep(std::move(step), false);
}

void PianoRollViewModel::markersEdited(int64_t frame)
{
	markerCount.set(static_cast<int64_t>(markers_.size()));
	std::vector<Marker> all;
	for (const auto &[f, m] : markers_) all.push_back(m);
	session_.setMarkers(std::move(all));
	session_.markChanged();
	listMarkers();
	if (frame < inputs_.size()) frames_.changed.emit(frame, frame);
}

bool PianoRollViewModel::frameEmpty(int64_t frame) const
{
	if (inputs_.at(frame).any()) return false;
	const size_t n = inputs_.axisCount();
	if (n == 0) return true;
	std::vector<int32_t> axes(n), neutral(n);
	inputs_.axesAt(frame, axes.data());
	inputs_.neutralAxes(neutral.data());
	return axes == neutral;
}

void PianoRollViewModel::refreshRun(int64_t frame)
{
	const int64_t size = inputs_.size();
	const int64_t wasLast = lastInputFrame.get(), wasEnd = runEndFrame.get();
	int64_t last = wasLast;
	if (size == 0)
		last = 0;
	else
	{
		/* an edit after the answer cannot have erased it: the walk back stops there */
		const int64_t floor = wasLast < frame ? frame : 0;
		bool found = false;
		for (int64_t i = size - 1; i >= floor; i--)
			if (!frameEmpty(i))
			{
				last = i;
				found = true;
				break;
			}
		if (!found && floor == 0) last = 0;
	}
	const int64_t end = std::max<int64_t>(0, size - 1);
	if (last == wasLast && end == wasEnd) return;
	lastInputFrame.set(last);
	runEndFrame.set(end);
	session_.setRunMarkers({0, last, end});
	listMarkers();
	for (int64_t f : {wasLast, wasEnd, last, end})
		if (f < size) frames_.changed.emit(f, f);
}

void PianoRollViewModel::listMarkers()
{
	/* the run's end moves on every frame recorded: as many markers, one moved */
	const auto before = static_cast<int64_t>(markerList_.size());
	markerList_.clear();
	markerList_.push_back({0, tr("piano_roll.marker_run_start"), true, true});
	markerList_.push_back({lastInputFrame.get(), tr("piano_roll.marker_last_input"), true, true});
	markerList_.push_back({runEndFrame.get(), tr("piano_roll.marker_run_end"), true, true});
	for (const auto &[f, m] : markers_) markerList_.push_back({f, m.text, m.keepState, false});
	/* by frame; on a shared frame the run's own first, in run order, then the person's */
	std::stable_sort(markerList_.begin(), markerList_.end(), [](const ListedMarker &a, const ListedMarker &b) {
		if (a.frame != b.frame) return a.frame < b.frame;
		return a.permanent && !b.permanent;
	});
	pickCurrentMarker();
	markerRows_.refreshed(before);
}

void PianoRollViewModel::showMarker(int64_t frame)
{
	for (size_t i = 0; i < markerList_.size(); i++)
		if (!markerList_[i].permanent && markerList_[i].frame == frame)
		{
			markerAddedRow.set(-1);
			markerAddedRow.set(static_cast<int64_t>(i));
			return;
		}
}

void PianoRollViewModel::pickCurrentMarker()
{
	/* the last marker at or before the frame */
	const int64_t frame = currentFrame.get();
	const auto after = std::upper_bound(markerList_.begin(), markerList_.end(), frame,
		[](int64_t f, const ListedMarker &m) { return f < m.frame; });
	currentMarker.set(after == markerList_.begin() ? -1 : std::prev(after)->frame);
}

std::string PianoRollViewModel::MarkerRows::columnName(int32_t column) const
{
	static const char *const names[] = {"frame", "text", "keepState", "permanent", "zone"};
	return column >= 0 && column < 5 ? names[column] : "";
}

Value PianoRollViewModel::MarkerRows::cell(int64_t row, int32_t column) const
{
	if (row < 0 || row >= rowCount()) return {};
	const ListedMarker &m = vm_.markerList_[static_cast<size_t>(row)];
	switch (column)
	{
	case 0: return m.frame;
	case 1: return m.text;
	case 2: return m.keepState;
	case 3: return m.permanent;
	/* as Chimera's lag log: known for a frame the greenzone holds */
	case 4: return m.frame < vm_.greenEnd.get() ? std::string(vm_.inputs_.lagAt(m.frame) ? "lag" : "green") : std::string();
	default: return {};
	}
}

Value PianoRollViewModel::Frames::cell(int64_t row, int32_t column) const
{
	const int32_t buttons = vm_.buttonCount();
	if (column < 0 || column >= columnCount()) return false;
	/* the row past the movie holds nothing */
	if (row >= vm_.inputs_.size()) return column >= buttons ? Value() : Value(false);
	if (row != cachedRow_)
	{
		cachedBits_ = vm_.inputs_.at(row);
		cachedAxes_.assign(vm_.inputs_.axisCount(), 0);
		if (!cachedAxes_.empty()) vm_.inputs_.axesAt(row, cachedAxes_.data());
		cachedRow_ = row;
	}
	if (column >= buttons)
	{
		const size_t axis = static_cast<size_t>(column - buttons);
		return axis < cachedAxes_.size() ? Value(static_cast<int64_t>(cachedAxes_[axis])) : Value();
	}
	return cachedBits_.test(static_cast<size_t>(column));
}

void PianoRollViewModel::addRecentMacro(const std::string &path)
{
	/* newest first, eight at most: Chimera's recent macros, TAStudio's and Macro Input's */
	std::vector<std::string> recent{path};
	std::istringstream was(recentMacros.get());
	for (std::string line; std::getline(was, line);)
		if (!line.empty() && line != path && recent.size() < 8) recent.push_back(line);
	std::string text;
	for (const std::string &r : recent) text += (text.empty() ? "" : "\n") + r;
	recentMacros.set(text);
}

std::string PianoRollViewModel::withoutFrameNumber(const std::string &line) { return movieLineFromBar(line); }

} // namespace chimera::vm
