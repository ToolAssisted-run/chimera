#include "chimera/vm/hotkeys_vm.hpp"

#include "chimera/vm/bindings.hpp"

#include "chimera/vm/json.hpp"
#include "chimera/vm/tr.hpp"

#include <algorithm>
#include <cctype>
#include <cstring>

namespace chimera::vm {

using json::Json;

namespace {
std::vector<std::string> keyList(const std::string &keys)
{
	std::vector<std::string> out;
	size_t at = 0;
	while (at <= keys.size())
	{
		const size_t comma = keys.find(',', at);
		std::string one = keys.substr(at, comma == std::string::npos ? std::string::npos : comma - at);
		while (!one.empty() && one.front() == ' ') one.erase(0, 1);
		while (!one.empty() && one.back() == ' ') one.pop_back();
		if (!one.empty()) out.push_back(one);
		if (comma == std::string::npos) break;
		at = comma + 1;
	}
	return out;
}

std::string trimmed(std::string t)
{
	while (!t.empty() && t.front() == ' ') t.erase(0, 1);
	while (!t.empty() && t.back() == ' ') t.pop_back();
	return t;
}

/* a box's keys as Chimera keeps them: split at the commas, blank ones left out */
std::vector<std::string> pieces(const std::string &keys)
{
	std::vector<std::string> out;
	size_t at = 0;
	while (at <= keys.size())
	{
		const size_t comma = keys.find(',', at);
		std::string one = keys.substr(at, comma == std::string::npos ? std::string::npos : comma - at);
		if (!trimmed(one).empty()) out.push_back(one);
		if (comma == std::string::npos) break;
		at = comma + 1;
	}
	return out;
}

std::string joined(const std::vector<std::string> &keys)
{
	std::string out;
	for (const std::string &k : keys) out += (out.empty() ? "" : ",") + k;
	return out;
}

bool special(const std::string &key)
{
	return key == "Escape" || key == "WMouse L" || key == "WMouse M" || key == "WMouse R" || key == "WMouse 1" || key == "WMouse 2";
}

std::string lower(std::string t)
{
	for (char &c : t) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
	return t;
}

/* a name as the end of a key: its words in lower case, joined by _ */
std::string slug(const std::string &name)
{
	std::string out;
	for (char c : name)
	{
		if (std::isalnum(static_cast<unsigned char>(c))) out += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
		else if (!out.empty() && out.back() != '_') out += '_';
	}
	while (!out.empty() && out.back() == '_') out.pop_back();
	return out;
}

/* What a person reads for an action, in the language tables
 * (docs/translations.md): its key, and the number a branch slot's carries.
 * The name itself is the action's identifier, kept in the settings. */
std::pair<std::string, std::string> labelOf(const std::string &name)
{
	for (const char *slot : {"Save Branch ", "Load Branch "})
	{
		const size_t n = std::strlen(slot);
		if (name.compare(0, n, slot) == 0) return {name[0] == 'S' ? "hotkey.save_branch" : "hotkey.load_branch", name.substr(n)};
	}
	return {"hotkey." + slug(name), ""};
}

std::string label(const std::string &name)
{
	const auto [key, number] = labelOf(name);
	return number.empty() ? tr(key.c_str()) : tr(key.c_str(), {number});
}

/* Chimera's hotkeys in its order (HotkeyInfo): its tabs, and the order inside each */
const std::vector<std::string> &chimeraOrder()
{
	static const std::vector<std::string> order = [] {
		std::vector<std::string> o = {"Frame Advance", "Rewind", "Pause", "Fast Forward", "Turbo", "Toggle Throttle", "Autofire",
			"Autohold", "Clear Autohold", "Screenshot", "Full Screen", "Open Project", "Close Project", "Load Last Project", "Display FPS",
			"Frame Counter", "Lag Counter", "Game Time", "Input Display", "Toggle BG Input", "Toggle Menu", "Volume Up",
			"Volume Down", "Encode Video", "Larger Window", "Smaller Window", "Increase Speed", "Decrease Speed", "Reset Speed",
			"Reboot Core", "Toggle Sound", "Exit Program", "Screen Raw to Clipboard", "Screen Client to Clipboard",
			"Toggle Skip Lag Frame", "Toggle Key Priority", "Frame Inch", "Toggle Messages", "Toggle Display Nothing",
			"Accept Background Input", "Capture Mouse", "Toggle Stay on Top", "Toggle read-only", "Play Movie", "Record Movie",
			"Stop Movie", "Play from beginning", "Save Movie", "RAM Watch", "RAM Search", "Hex Editor", "Trace Logger",
			"Lua Console", "TAStudio", "New Search", "Do Search", "Previous Compare To", "Next Compare To", "Previous Operator",
			"Next Operator", "Add Branch", "Delete Branch"};
		for (int slot = 1; slot <= 10; slot++)
		{
			o.push_back("Save Branch " + std::to_string(slot));
			o.push_back("Load Branch " + std::to_string(slot));
		}
		for (const char *n : {"Show Cursor", "Select Current Frame", "Seek To Selected Frame", "Seek To...", "Toggle Follow Cursor",
				 "Toggle Auto-Restore", "Seek To Green Arrow", "Toggle Turbo Seek", "Cycle Greenzone", "Undo", "Redo",
				 "Seek To Prev Marker", "Seek To Next Marker", "Cancel Seek", "Set Marker", "Delete Marker", "Sel. bet. Markers",
				 "Select All", "Reselect Clip.", "Clear Frames", "Delete Frames", "Insert Frame", "Insert # Frames", "Clone Frames",
				 "Clone # Times", "Paste Insert", "Analog Increment", "Analog Decrement", "Analog Incr. by 10",
				 "Analog Decr. by 10", "Analog Maximum", "Analog Minimum", "Toggle All Freezes", "Toggle Last Lua Script"})
			o.push_back(n);
		return o;
	}();
	return order;
}

} // namespace

HotkeysViewModel::HotkeysViewModel(Dispatcher &dispatcher) : ViewModel("hotkeys", dispatcher)
{
	expose("saved", "the keys changed, as {\"action\": \"keys\"}", saved, true);
	expose("status", "what Hotkeys last had to say", status);
	expose("bound", "every action's keys as they are now, as {\"action\": \"keys\"}", bound);
	expose("keyPriority", "for a key that is a hotkey and a button: 0 both act, 1 the button wins, 2 the hotkey wins",
		keyPriority, true);
	expose("noMixedPriority", "the Toggle Key Priority hotkey leaves \"both\" out of its cycle", noMixedPriority, true);
	command("cycleKeyPriority", "the next key priority, as Chimera's Toggle Key Priority: both, buttons first, hotkeys "
		"first (\"both\" left out when noMixedPriority)", {}, [this](const Args &, std::string &) -> Value {
			int64_t p = keyPriority.get() + 1;
			if (p > 2) p = 0;
			if (noMixedPriority.get() && p == 0) p = 1;
			keyPriority.set(p);
			status.set(tr(p == 0 ? "hotkeys.priority_both" : p == 1 ? "hotkeys.priority_input" : "hotkeys.priority_hotkey"));
			return p;
		});
	expose("autoTab", "Auto Tab: a key pressed replaces a box's keys and the next box takes the next; off, it is one more", autoTab, true);
	expose("groups", "the tabs, in Chimera's order (a JSON array)", groups);
	expose("groupKeys", "the language keys of the tabs' names, in the same order (a JSON array)", groupKeys);
	expose("editing", "the window's copy is open: changes wait for save", editing);
	rows("hotkeys", "one row an action, in Chimera's order: group, name, its keys (the copy's while editing), Chimera's keys "
		"for it, changed, and the language key of what a person reads for it with the number that fills it", rows_);
	command("edit", "opens the window's copy of the keys, from the keys as they are", {}, [this](const Args &, std::string &) -> Value {
		begin();
		return {};
	});
	command("press", "a key or gamepad input pressed in an action's box, as Chimera's: Escape empties it; with Auto Tab the "
		"key replaces its keys, without it is one more. True when the next box takes the next key", {"action", "key"},
		[this](const Args &a, std::string &error) -> Value {
			Action *act = nullptr;
			std::string key;
			if (!action(a, act, error) || !arg(a, 1, key, error)) return {};
			begin();
			if (key == "Escape")
			{
				act->draft.clear();
				keysChanged();
				return autoTab.get();
			}
			if (key.empty() || key == "Alt+F4" || special(key)) return false;
			std::vector<std::string> now = pieces(act->draft);
			if (std::none_of(now.begin(), now.end(), [&](const std::string &k) { return trimmed(k) == key; }))
			{
				if (autoTab.get()) now.clear();
				now.push_back(key);
			}
			act->draft = joined(now);
			keysChanged();
			return autoTab.get();
		});
	command("special", "one of the bindings a key cannot give, added to an action's box: Escape, WMouse L, WMouse M, WMouse R, "
		"WMouse 1, WMouse 2. True when the next box takes the next key", {"action", "name"},
		[this](const Args &a, std::string &error) -> Value {
			Action *act = nullptr;
			std::string name;
			if (!action(a, act, error) || !arg(a, 1, name, error)) return {};
			if (!special(name))
			{
				error = tr("hotkeys.not_special", {name});
				return {};
			}
			begin();
			std::vector<std::string> now = pieces(act->draft);
			if (std::none_of(now.begin(), now.end(), [&](const std::string &k) { return trimmed(k) == name; })) now.push_back(name);
			act->draft = joined(now);
			keysChanged();
			return autoTab.get();
		});
	command("bind", "an action's keys on the copy, replaced (\"F,X1 RightShoulder\"; \"\" for none)", {"action", "keys"},
		[this](const Args &a, std::string &error) -> Value {
			Action *act = nullptr;
			std::string keys;
			if (!action(a, act, error) || !arg(a, 1, keys, error)) return {};
			begin();
			act->draft = keys;
			keysChanged();
			return {};
		});
	command("defaults", "the copy: Chimera's keys, every tab's (no group) or one tab's (Restore Defaults, Restore Current Tab "
		"Defaults)", {"group"}, [this](const Args &a, std::string &error) -> Value {
			std::string group;
			if (!a.empty() && !arg(a, 0, group, error)) return {};
			begin();
			for (Action &act : actions_)
				if (group.empty() || act.group == group) act.draft = act.defaults;
			keysChanged();
			return {};
		});
	command("clear", "the copy: no keys, every tab's (no group) or one tab's (Clear All, Clear Current Tab)", {"group"},
		[this](const Args &a, std::string &error) -> Value {
			std::string group;
			if (!a.empty() && !arg(a, 0, group, error)) return {};
			begin();
			for (Action &act : actions_)
				if (group.empty() || act.group == group) act.draft.clear();
			keysChanged();
			return {};
		});
	command("save", "the copy kept: what differs from Chimera's keys remembered", {}, [this](const Args &, std::string &) -> Value {
		if (!editing.get()) return {};
		Json all;
		all.kind = Json::Kind::Object;
		for (const Action &act : actions_)
			if (act.draft != act.defaults) all.members.emplace_back(act.name, Json::string(act.draft));
		editing.set(false);
		saved.set(json::write(all));
		apply();
		status.set(tr("hotkeys.saved"));
		return {};
	});
	command("replaceKeys", "Chimera's rewrite of the keys bound when Merge L+R modifier keys changes: the key names the "
		"object names replaced ({\"LeftShift\": \"Shift\"}) in every action's keys, kept, and in the copy if it is open",
		{"names"}, [this](const Args &a, std::string &error) -> Value {
			std::string text;
			std::map<std::string, std::string> names;
			if (!arg(a, 0, text, error) || !keyNamesFrom(text, names, error)) return {};
			Json all;
			all.kind = Json::Kind::Object;
			for (Action &act : actions_)
			{
				const std::string keys = replaceKeyNames(act.keys, names);
				if (keys != act.defaults) all.members.emplace_back(act.name, Json::string(keys));
				if (editing.get()) act.draft = replaceKeyNames(act.draft, names);
			}
			saved.set(json::write(all));
			apply();
			return {};
		});
	command("cancel", "the copy let go", {}, [this](const Args &, std::string &) -> Value {
		editing.set(false);
		keysChanged();
		return {};
	});
	command("find", "the action a Find names (what a person reads for it, or its name; any case), as {\"action\", \"group\"}; "
		"\"\" for none", {"text"}, [this](const Args &a, std::string &error) -> Value {
			std::string text;
			if (!arg(a, 0, text, error)) return {};
			const std::string wanted = lower(trimmed(text));
			for (size_t i : order_)
				if (lower(label(actions_[i].name)) == wanted || lower(actions_[i].name) == wanted)
				{
					Json found;
					found.kind = Json::Kind::Object;
					found.members.emplace_back("action", Json::string(actions_[i].name));
					found.members.emplace_back("group", Json::string(actions_[i].group));
					return json::write(found);
				}
			return std::string();
		});
	command("suggest", "the actions whose names, as a person reads them, start with what a Find has so far (any case), in "
		"Chimera's order, as a JSON array of {\"action\", \"label\"}",
		{"text"}, [this](const Args &a, std::string &error) -> Value {
			std::string text;
			if (!arg(a, 0, text, error)) return {};
			Json list;
			list.kind = Json::Kind::Array;
			const std::string start = lower(text);
			if (!start.empty())
				for (size_t i : order_)
					if (const std::string shown = label(actions_[i].name); lower(shown).rfind(start, 0) == 0)
					{
						Json one;
						one.kind = Json::Kind::Object;
						one.members.emplace_back("action", Json::string(actions_[i].name));
						one.members.emplace_back("label", Json::string(shown));
						list.items.push_back(std::move(one));
					}
			return json::write(list);
		});
	command("run", "runs an action as its key would, pressed and let go", {"action"}, [this](const Args &a, std::string &error) -> Value {
		std::string name;
		if (!arg(a, 0, name, error)) return {};
		Action *act = find(name);
		if (act == nullptr)
		{
			error = tr("hotkeys.no_action", {name});
			return {};
		}
		act->handler(true);
		act->handler(false);
		return {};
	});
	savedWatch_ = saved.changed.connect([this] { apply(); });
}

void HotkeysViewModel::add(std::string group, std::string name, std::string defaultKeys, Handler handler)
{
	actions_.push_back({std::move(group), std::move(name), defaultKeys, defaultKeys, defaultKeys, std::move(handler)});
	/* Chimera's order; an action Chimera has not, after its own, in the order added */
	const std::vector<std::string> &chimera = chimeraOrder();
	auto rank = [&](size_t i) {
		const auto at = std::find(chimera.begin(), chimera.end(), actions_[i].name);
		return std::make_pair(at == chimera.end() ? chimera.size() : static_cast<size_t>(at - chimera.begin()), i);
	};
	order_.clear();
	for (size_t i = 0; i < actions_.size(); i++) order_.push_back(i);
	std::sort(order_.begin(), order_.end(), [&](size_t x, size_t y) { return rank(x) < rank(y); });
	Json tabs, tabKeys;
	tabs.kind = tabKeys.kind = Json::Kind::Array;
	std::vector<std::string> seen;
	for (size_t i : order_)
		if (std::find(seen.begin(), seen.end(), actions_[i].group) == seen.end())
		{
			seen.push_back(actions_[i].group);
			tabs.items.push_back(Json::string(actions_[i].group));
			tabKeys.items.push_back(Json::string("hotkey_group." + slug(actions_[i].group)));
		}
	groups.set(json::write(tabs));
	groupKeys.set(json::write(tabKeys));
	apply();
	/* one action more: the rows are made again */
	rows_.reset.emit();
}

bool HotkeysViewModel::action(const Args &a, Action *&out, std::string &error)
{
	std::string name;
	if (!arg(a, 0, name, error)) return false;
	out = find(name);
	if (out == nullptr) error = tr("hotkeys.no_action", {name});
	return out != nullptr;
}

void HotkeysViewModel::begin()
{
	if (editing.get()) return;
	for (Action &a : actions_) a.draft = a.keys;
	editing.set(true);
	keysChanged();
}

void HotkeysViewModel::keysChanged()
{
	if (const int64_t n = rows_.rowCount(); n > 0) rows_.changed.emit(0, n - 1);
}

HotkeysViewModel::Action *HotkeysViewModel::find(const std::string &name)
{
	for (Action &a : actions_)
		if (a.name == name) return &a;
	return nullptr;
}

void HotkeysViewModel::apply()
{
	Json all;
	std::string error;
	if (!json::parse(saved.get(), all, error) || all.kind != Json::Kind::Object) all.kind = Json::Kind::Object;
	Json now;
	now.kind = Json::Kind::Object;
	for (Action &a : actions_)
	{
		const Json *mine = all.find(a.name);
		/* a hotkey renamed: a binding saved under the old name is the new
		 * one's (owner, 2026-10-26: the ROM hotkeys became the project's) */
		static const std::pair<const char *, const char *> renamed[] = {
			{"Open Project", "Open ROM"}, {"Close Project", "Close ROM"}, {"Load Last Project", "Load Last ROM"}};
		for (const auto &[now, old] : renamed)
			if (mine == nullptr && a.name == now) mine = all.find(old);
		a.keys = mine != nullptr && mine->kind == Json::Kind::String ? mine->text : a.defaults;
		now.members.emplace_back(a.name, Json::string(a.keys));
	}
	bound.set(json::write(now));
	keysChanged();
}

bool HotkeysViewModel::key(const std::string &name, bool down)
{
	bool ran = false;
	for (Action &a : actions_)
	{
		const auto held = std::make_pair(a.name, name);
		if (!down)
		{
			/* let go: whatever it put down comes up, rebound since or not */
			if (down_.erase(held) != 0)
			{
				a.handler(false);
				ran = true;
			}
			continue;
		}
		bool bound = false;
		for (const std::string &k : keyList(a.keys)) bound = bound || k == name;
		if (!bound || down_.count(held) != 0) continue; /* not its key, or the key repeating */
		down_.insert(held);
		a.handler(true);
		ran = true;
	}
	return ran;
}

bool HotkeysViewModel::boundTo(const std::string &name) const
{
	for (const Action &a : actions_)
		for (const std::string &k : keyList(a.keys))
			if (k == name) return true;
	return false;
}

HotkeysViewModel::Route HotkeysViewModel::route(int64_t priority, bool hotkeyBound, bool controlBound)
{
	Route r;
	r.hotkey = priority != 1;
	r.control = priority != 2;
	/* the button side wins only for a key that is a button */
	if (r.control && !controlBound) r.hotkey = true;
	/* and the hotkey side only for a key that is a hotkey */
	if (!(r.hotkey && hotkeyBound)) r.control = true;
	return r;
}

bool HotkeysViewModel::takesCombination(int64_t priority, bool hotkeyBound, bool controlBound, bool commandHeld)
{
	const bool control = controlBound && !commandHeld;
	const Route r = route(priority, hotkeyBound, control);
	return (r.hotkey && hotkeyBound) || (r.control && control);
}

std::string HotkeysViewModel::Rows::columnName(int32_t column) const
{
	static const char *const names[] = {"group", "action", "keys", "chimeraKeys", "changed", "labelKey", "labelNumber"};
	return column >= 0 && column < 7 ? names[column] : "";
}

Value HotkeysViewModel::Rows::cell(int64_t row, int32_t column) const
{
	if (row < 0 || row >= rowCount()) return {};
	const Action &a = vm_.actions_[vm_.order_[static_cast<size_t>(row)]];
	const std::string &keys = vm_.editing.get() ? a.draft : a.keys;
	switch (column)
	{
	case 0: return a.group;
	case 1: return a.name;
	case 2: return keys;
	case 3: return a.defaults;
	case 4: return keys != a.defaults;
	case 5: return labelOf(a.name).first;
	case 6: return labelOf(a.name).second;
	default: return {};
	}
}

} // namespace chimera::vm
