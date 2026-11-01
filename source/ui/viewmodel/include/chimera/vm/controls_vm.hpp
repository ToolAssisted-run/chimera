#pragma once
/* Controls, as Chimera's Controller Config: which keys and gamepad inputs
 * hold each of the machine's buttons (Normal Controls), which fire a button
 * while held (Autofire Controls), and what moves each analog control (Analog
 * Controls: a gamepad axis or the pointer, a sensitivity, a dead zone, and a
 * key for either end). The package says what they are at first. The window
 * works on a copy (edit): Save gives the copy to the machine and remembers
 * what differs from the package, for that core; Cancel lets it go. Key names
 * are Chimera's ("Z", "Up", "J1 POV0U", "X1 A", "X1 LeftThumbX Axis").
 * Which controls TAStudio shows as columns is chosen here too, in the same
 * copy (issue 391: TAStudio's Columns menu was too long to use with many
 * buttons). */

#include <array>
#include <set>
#include <string>
#include <vector>

#include "chimera/vm/bindings.hpp"
#include "chimera/vm/tr.hpp"
#include "chimera/vm/json.hpp"
#include "chimera/vm/session_vm.hpp"
#include "chimera/vm/view_model.hpp"

namespace chimera::vm {

class PianoRollViewModel;

class ControlsViewModel : public ViewModel
{
public:
	/* `roll`: TAStudio's piano roll, whose columns the window chooses (none: no column boxes) */
	ControlsViewModel(Dispatcher &dispatcher, SessionViewModel &session, PianoRollViewModel *roll = nullptr);

	/* what differs from the package, every core's: the buttons' keys and the
	 * autofire keys as {"core": {"button": "keys"}}, the analog controls as
	 * {"core": {"control": {"Value", "Mult", "Deadzone", "ButtonBindPositive",
	 * "ButtonBindNegative"}}} (the window's own settings file keeps them) */
	Property<std::string> saved{"{}"};
	Property<std::string> savedAutofire{"{}"};
	Property<std::string> savedAnalog{"{}"};
	/* Chimera's Auto Tab: a key pressed in a box replaces its keys and the
	 * next box takes the next key; off, a key pressed is one more for the box */
	Property<bool> autoTab{true};
	/* Chimera's relative mouse sensitivity: how hard the pointer's movement
	 * (RMouse X and Y) pushes an analog control, in percent, 1 to 1000; at
	 * 100, 120 pixels in a frame push it all the way, as Chimera's do */
	Property<int64_t> mouseSensitivity{100};
	/* the window's copy of it, kept by Save */
	Property<int64_t> mouseSensitivityCopy{100};
	/* the core whose controls are listed, and the window's title */
	Property<std::string> core;
	Property<std::string> title{tr("controls.default_title")};
	/* the tabs inside each kind, in the order their first control comes:
	 * "Player 1", "Player 2"..., "Console" (JSON arrays; one tab is none) */
	Property<std::string> buttonGroups{"[]"};
	Property<std::string> axisGroups{"[]"};
	/* the window's copy is open: changes wait for Save */
	Property<bool> editing{false};
	/* the TAStudio input roll the column boxes are for, from 1, when
	 * TAStudio has several (0: it has one), as of the copy's opening */
	Property<int64_t> columnsRoll{0};
	/* the picture of the controller, counted up each time another is shown (0: none) */
	Property<int64_t> pictureShown{0};
	Property<std::string> status;

	/* the picture's bytes (a PNG), for the view to draw */
	const std::string &picture() const { return session_.controllerPicture(); }

	/* Chimera's tab for a control: "Player N" for player N's, as the core
	 * declares it (ce_session_player_of), else "Console" */
	std::string groupOf(const std::string &name) const;

private:
	enum Table
	{
		Normal,
		Autofire,
		Positive,
		Negative,
	};
	class KeyRows : public RowModel
	{
	public:
		KeyRows(ControlsViewModel &vm, Table table) : vm_(vm), table_(table) {}
		int64_t rowCount() const override { return static_cast<int64_t>(vm_.session_.buttonNames().size()); }
		int32_t columnCount() const override { return 7; }
		std::string columnName(int32_t column) const override;
		Value cell(int64_t row, int32_t column) const override;

	private:
		ControlsViewModel &vm_;
		Table table_;
	};
	class AnalogRows : public RowModel
	{
	public:
		explicit AnalogRows(ControlsViewModel &vm) : vm_(vm) {}
		int64_t rowCount() const override { return static_cast<int64_t>(vm_.session_.axes().size()); }
		int32_t columnCount() const override { return 10; }
		std::string columnName(int32_t column) const override;
		Value cell(int64_t row, int32_t column) const override;

	private:
		ControlsViewModel &vm_;
	};

	bool table(const Args &a, Table &out, std::string &error) const;
	bool index(const Args &a, size_t at, Table table, size_t &out, std::string &error) const;
	/* a box's keys, of the copy */
	std::string &keysOf(Table table, size_t i);
	/* the copy, from what the machine has now, unless it is open */
	void begin();
	/* the copy given to the machine, and what differs from the package remembered (save) */
	void keep();
	/* the machine's controls: the package's, with this core's changes over them */
	void apply();
	void changed();
	/* the tabs of these controls, in the order their first control comes (a JSON array) */
	std::string groupsOf(const std::vector<std::string> &names) const;
	/* a control is a TAStudio column, of the copy while it is open */
	bool columnShown(const std::string &name) const;

	SessionViewModel &session_;
	PianoRollViewModel *roll_;
	/* the copy's hidden TAStudio columns, by control name */
	std::set<std::string> hiddenDraft_;
	std::array<std::vector<std::string>, 2> draft_;
	std::vector<AnalogBind> analogDraft_;
	KeyRows normalRows_{*this, Normal}, autofireRows_{*this, Autofire};
	AnalogRows analogRows_{*this};
	/* the rows each list had when it was last made: as many again change in place */
	int64_t keyRowsMade_ = -1, analogRowsMade_ = -1;
	Connection opensWatch_, columnsWatch_, savedWatch_, savedAutofireWatch_, savedAnalogWatch_, sensitivityWatch_;
};

} // namespace chimera::vm
