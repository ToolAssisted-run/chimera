#pragma once
/* Hotkeys: keys that run the window's actions (frame advance, rewind, the
 * branch slots...), as Chimera's do. Each action is named and grouped as
 * Chimera names it and has Chimera's key at first; they are listed in
 * Chimera's order. Configure Hotkeys changes them on a copy (edit) that
 * Save keeps, and what differs from Chimera's is remembered (`saved`). A key
 * held runs its action's "down", and its "up" when let go. Like Chimera by
 * default, a key that is both a hotkey and a machine's button does both;
 * Chimera's Key Priority says otherwise when asked (keyPriority). */

#include <functional>
#include <set>
#include <string>
#include <vector>

#include "chimera/vm/view_model.hpp"

namespace chimera::vm {

class HotkeysViewModel : public ViewModel
{
public:
	explicit HotkeysViewModel(Dispatcher &dispatcher);

	using Handler = std::function<void(bool down)>;
	/* an action keys can run: its group, its name, its keys at first ("" for
	 * none), and what it does */
	void add(std::string group, std::string name, std::string defaultKeys, Handler handler);
	/* a key went down or up (Chimera's key names): true when it ran any action */
	bool key(const std::string &name, bool down);
	/* whether any action has this key */
	bool boundTo(const std::string &name) const;
	/* every key any action has */
	std::vector<std::string> keys() const;

	/* Chimera's Key Priority, for a key that is a hotkey and a button: 0 both
	 * act, 1 the button wins (controls over hotkeys), 2 the hotkey wins; and
	 * Customize's choice to leave "both" out of the hotkey's cycle */
	Property<int64_t> keyPriority{0};
	Property<bool> noMixedPriority{false};
	struct Route
	{
		bool hotkey = true, control = true;
	};
	/* where a key goes, by Chimera's rule: a hotkey
	 * gives way to a button only when the key is a button, and a button to
	 * a hotkey only when the key is a hotkey */
	static Route route(int64_t priority, bool hotkeyBound, bool controlBound);
	/* whether a combination pressed is a hotkey's or a control's rather than
	 * a window's shortcut or menu letter (issue 405): a hotkey bound to the
	 * combination takes it, and a control takes its key - but not with Ctrl
	 * or the system key held, which make it a shortcut such as TAStudio's
	 * Ctrl+C (issue 453) */
	static bool takesCombination(int64_t priority, bool hotkeyBound, bool controlBound, bool commandHeld);

	/* the keys changed: {"action": "keys"} */
	Property<std::string> saved{"{}"};
	/* Chimera's Auto Tab: a key pressed in a box replaces its keys and the
	 * next box takes the next key; off, a key pressed is one more for the box */
	Property<bool> autoTab{true};
	/* the tabs, in Chimera's order (a JSON array) */
	Property<std::string> groups{"[]"};
	Property<std::string> groupKeys{"[]"};
	/* the window's copy is open: changes wait for save */
	Property<bool> editing{false};
	/* every action's keys as they are now, {"action": "keys"}, for a menu
	 * to show beside the action */
	Property<std::string> bound{"{}"};
	Property<std::string> status;

private:
	struct Action
	{
		std::string group, name, defaults, keys, draft;
		Handler handler;
	};
	class Rows : public RowModel
	{
	public:
		explicit Rows(HotkeysViewModel &vm) : vm_(vm) {}
		int64_t rowCount() const override { return static_cast<int64_t>(vm_.actions_.size()); }
		int32_t columnCount() const override { return 7; }
		std::string columnName(int32_t column) const override;
		Value cell(int64_t row, int32_t column) const override;

	private:
		HotkeysViewModel &vm_;
	};

	Action *find(const std::string &name);
	bool action(const Args &a, Action *&out, std::string &error);
	/* the copy, from the keys as they are, unless it is open */
	void begin();
	void apply();
	/* every row's keys may differ; the actions themselves never change, so
	 * the rows are changed in place, not rebuilt (a rebuild cost the
	 * window over a second, issue 388) */
	void keysChanged();

	std::vector<Action> actions_;
	/* the actions in Chimera's order, by place in actions_ */
	std::vector<size_t> order_;
	Rows rows_{*this};
	/* the actions down now, by the key that put them down */
	std::set<std::pair<std::string, std::string>> down_;
	Connection savedWatch_;
};

} // namespace chimera::vm
