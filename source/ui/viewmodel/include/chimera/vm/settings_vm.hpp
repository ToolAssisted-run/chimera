#pragma once
/* A machine's settings, as its package declares them (and the game, for an
 * arcade game's dip switches): which apply is the engine's to say, from the
 * game files and the other settings; edits wait until applied, and are then
 * given to the running machine, or the machine starts again with them. Chimera
 * knows no setting by name: a row is whatever the package declared. */

#include "chimera/vm/json.hpp"
#include "chimera/vm/tr.hpp"
#include "chimera/vm/settings_form.hpp"
#include "chimera/vm/session_vm.hpp"
#include "chimera/vm/view_model.hpp"

namespace chimera::vm {

class SettingsViewModel : public ViewModel
{
public:
	SettingsViewModel(Dispatcher &dispatcher, SessionViewModel &session);
	/* the machine's settings not at their default, for a bug report */
	std::vector<SettingsForm::Changed> changedSettings() const { return form_.changed(); }

	/* edits not applied yet */
	Property<bool> pending{false};
	Property<int64_t> shown{0};
	Property<std::string> status{tr("session.no_machine")};
	/* File > Project Settings (issue 370): the start of the project's clock, as
	 * the New Project window writes it (UTC); whether anything the machine is
	 * made from differs from what it was made from (each setting at its value
	 * or default, so the default typed back is no change; the clock too); and
	 * whether the window can restart with them (changed, and the clock reads) */
	Property<std::string> clockStart;
	Property<bool> changed{false};
	Property<bool> canRestart{false};
	/* what is in the way of restarting, "" when nothing is */
	Property<std::string> restartNote;
	Property<bool> clockWrong{false};

	/* which of a declaration's settings apply (SettingsForm's rule) */
	static std::vector<size_t> applicable(const std::vector<json::Json> &decl, const std::string &slots,
		const json::Json &values, const std::string &machineSetting)
	{
		return SettingsForm::applicable(decl, slots, values, machineSetting);
	}

	/* columns of the "settings" rows */
	enum Column { kName, kLabel, kType, kValue, kOptions, kOptionLabels, kMin, kMax, kDescription, kEdited, kColumns };

	/* what the machine is made from, as text that is the same for the same
	 * machine: every declared setting at its value or default, and every value
	 * set beyond them, in name order */
	static std::string machineText(const std::vector<json::Json> &decl, const json::Json &values);

private:
	void evaluate();
	void compare();

	SessionViewModel &session_;
	SettingsForm form_;
	SettingsForm::Rows rows_{form_};
	/* the rows the views were last told of: a change that keeps their number
	 * updates them in place, and a list keeps its place (issue 417) */
	int64_t rowsTold_ = 0;
	SettingsForm::PresetRows presetRows_{form_};
	Connection opensWatch_, clockWatch_;
	json::Json applied_;
	std::string madeFrom_;
	int64_t clockBefore_ = 0;
};

} // namespace chimera::vm
