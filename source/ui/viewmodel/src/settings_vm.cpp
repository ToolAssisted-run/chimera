#include "chimera/vm/settings_vm.hpp"
#include "chimera/vm/tr.hpp"

#include "chimera/engine.h"
#include "chimera/vm/new_project_vm.hpp"

#include <map>

namespace chimera::vm {

using json::Json;

SettingsViewModel::SettingsViewModel(Dispatcher &dispatcher, SessionViewModel &session)
	: ViewModel("settings", dispatcher), session_(session)
{
	applied_.kind = Json::Kind::Object;
	form_.setApplied(&applied_);
	expose("pending", "edits not applied yet", pending);
	expose("shown", "settings that apply to this machine and game", shown);
	expose("status", "what the settings last had to say", status);
	expose("clockStart", "when the project's machine clock starts, UTC, written as 2017-05-27 12:44:28 (from 1970 to 2200)", clockStart, true);
	expose("changed", "whether anything the machine is made from differs from what it was made from", changed);
	expose("canRestart", "whether Project Settings can restart the machine with these", canRestart);
	expose("restartNote", "what is in the way of restarting, \"\" when nothing is", restartNote);
	expose("clockWrong", "the clock's start is not a time Chimera reads", clockWrong);
	clockWatch_ = clockStart.changed.connect([this] { compare(); });
	rows("settings", "one row a setting: name, label, type, value, options, option labels, min, max, description, edited", rows_);
	rows("presets", "the presets this machine is offered: id, label, description", presetRows_);
	command("applyPreset", "writes a preset's values into the settings (applied by apply)", {"id"},
		[this](const Args &a, std::string &error) -> Value {
			std::string id, label;
			if (!arg(a, 0, id, error)) return {};
			std::vector<std::string> skipped;
			if (!form_.applyPreset(id, label, skipped, error)) return {};
			std::string note = tr("settings.preset_written", {label});
			if (!skipped.empty())
			{
				note += tr("settings.not_for_machine");
				for (const std::string &s : skipped) note += " " + s;
			}
			status.set(note);
			pending.set(json::write(form_.values()) != json::write(applied_));
			evaluate();
			return {};
		}, &session_.isOpen);

	command("set", "changes a setting (applied by apply)", {"name", "value"},
		[this](const Args &a, std::string &error) -> Value {
			std::string name;
			if (!arg(a, 0, name, error)) return {};
			if (a.size() < 2)
			{
				error = tr("new_project.argument_missing", {"2"});
				return {};
			}
			if (!form_.set(name, a[1], error)) return {};
			pending.set(json::write(form_.values()) != json::write(applied_));
			evaluate();
			return {};
		}, &session_.isOpen);
	command("restartWith", "File > Project Settings: the project takes these settings and clock, and its machine starts again with them at frame 0, the inputs kept", {},
		[this](const Args &, std::string &error) -> Value {
			const std::optional<int64_t> clock = NewProjectViewModel::parseClockText(clockStart.get());
			if (!session_.hasProject.get() || !clock || !changed.get())
			{
				error = !session_.hasProject.get() ? tr("session.no_project") : !clock ? restartNote.get() : tr("settings.nothing_changed");
				return {};
			}
			status.set(tr("settings.restarting"));
			session_.changeProjectSettings(json::write(form_.recorded()), *clock, [this](bool ok) {
				status.set(ok ? tr("settings.restarted")
							  : tr("settings.not_restarted"));
			});
			return {};
		}, &canRestart);
	command("reset", "the window opened again: the values and the clock as the machine was made with them", {},
		[this](const Args &, std::string &) -> Value {
			form_.setValues(applied_);
			pending.set(false);
			clockStart.set(NewProjectViewModel::clockText(clockBefore_));
			evaluate();
			return {};
		});
	command("discard", "forgets the edits not applied", {}, [this](const Args &, std::string &) -> Value {
		form_.setValues(applied_);
		pending.set(false);
		evaluate();
		return {};
	}, &pending);
	command("apply", "gives the edits to the machine: live when the core can take them, else it starts again with them",
		{}, [this](const Args &, std::string &) -> Value {
			const Json values = form_.values();
			status.set(tr("settings.applying"));
			session_.changeSettings(json::write(values), [this, values](bool ok, std::string what) {
				if (ok)
				{
					applied_ = values;
					pending.set(json::write(form_.values()) != json::write(applied_));
				}
				status.set(what);
				evaluate();
			});
			return {};
		}, &pending);

	/* each machine opened: its declaration, and the values it was opened with */
	opensWatch_ = session_.opens.changed.connect([this] {
		session_.onMachine([this](ce_session *s) {
			if (s == nullptr) return;
			std::string config;
			const char *perr = nullptr;
			if (ce_package *p = ce_package_open(session_.packageOnMachine().c_str(), &perr))
			{
				uint64_t len = 0;
				if (const uint8_t *d = ce_package_entry_texts(p, "waterbox.config", &len))
					config.assign(reinterpret_cast<const char *>(d), static_cast<size_t>(len));
				ce_package_free(p);
			}
			const char *game = ce_session_game_settings(s);
			const std::string gameText = game != nullptr ? game : "";
			const std::string values = session_.settingsOnMachine(), slots = session_.slotsOnMachine();
			const int64_t clock = session_.clockOnMachine();
			later([this, config, gameText, values, slots, clock] {
				std::vector<Json> decl;
				std::string error, machineSetting;
				Json cfg, extra, v;
				if (json::parse(config, cfg, error))
				{
					if (const Json *list = cfg.find("settings")) decl = list->items;
					if (const Json *m = cfg.find("machineSetting"); m != nullptr && m->kind == Json::Kind::String)
						machineSetting = m->text;
				}
				if (!gameText.empty() && json::parse(gameText, extra, error))
					for (const Json &d : extra.items) decl.push_back(d);
				const size_t declared = decl.size();
				form_.declare(std::move(decl), machineSetting);
				/* a project's machine goes with its files, and is not changed here */
				form_.listMachineSetting(!session_.hasProject.get());
				std::vector<Json> machineDecls;
				if (const Json *m = cfg.find("machines")) machineDecls = m->items;
				form_.declareMachines(std::move(machineDecls));
				std::vector<Json> presets;
				if (const Json *p = cfg.find("presets")) presets = p->items;
				form_.declarePresets(std::move(presets));
				form_.setSlots(slots);
				if (!json::parse(values, v, error) || v.kind != Json::Kind::Object) v = Json(), v.kind = Json::Kind::Object;
				applied_ = v;
				form_.setValues(v);
				pending.set(false);
				status.set(tr("settings.declared", declared));
				madeFrom_ = machineText(form_.declarations(), v);
				clockBefore_ = clock;
				clockStart.set(NewProjectViewModel::clockText(clock));
				evaluate();
			});
		});
	});
}

std::string SettingsViewModel::machineText(const std::vector<Json> &decl, const Json &values)
{
	std::map<std::string, std::string> all;
	for (const Json &d : decl)
		if (const Json *name = d.find("name"))
		{
			const Json *v = values.find(name->text);
			if (v == nullptr) v = d.find("default");
			if (v != nullptr) all[name->text] = json::write(*v);
		}
	if (values.kind == Json::Kind::Object)
		for (const auto &[name, v] : values.members) all[name] = json::write(v);
	std::string out;
	for (const auto &[name, v] : all) out += name + "=" + v + "\n";
	return out;
}

void SettingsViewModel::compare()
{
	const std::optional<int64_t> clock = NewProjectViewModel::parseClockText(clockStart.get());
	changed.set(machineText(form_.declarations(), form_.values()) != madeFrom_ || (clock && *clock != clockBefore_));
	clockWrong.set(!clock);
	restartNote.set(!clock ? tr("settings.clock_format") : changed.get() ? "" : tr("settings.nothing_changed_yet"));
	canRestart.set(session_.hasProject.get() && clock.has_value() && changed.get());
}

void SettingsViewModel::evaluate()
{
	compare();
	form_.evaluate();
	shown.set(static_cast<int64_t>(form_.exposed().size()));
	rows_.refreshed(rowsTold_);
	rowsTold_ = rows_.rowCount();
	presetRows_.reset.emit();
}

} // namespace chimera::vm
