/* The contract between Chimera and the core packages it is given, checked
 * against real packages.
 *
 * Chimera ships no cores: each core publishes its own package, so a change to
 * Chimera could break every core at once, and a core could publish a package
 * Chimera misreads, with nothing in either build to say so. CI fills a folder
 * with packages (each core's own, or every core's newest published one) and
 * this reads them: no game, no firmware, no emulation. Whether the emulation
 * is right is each core's own tests'.
 *
 *   test-packages [<folder>]    the folder, else CHIMERA_CORES_DIR, else build/Cores
 *   test-packages --self        the rules alone, on packages made up here
 *
 * Prints PASS or FAIL for each check and what failed; exits 1 when one
 * failed, and 77 (a skip, to a test runner) when the folder holds no package:
 * a check that passes with nothing to check is worse than none. */
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <functional>
#include <map>
#include <regex>
#include <set>
#include <string>
#include <vector>

#include "chimera/engine.h"
#include "chimera/vm/cores_vm.hpp"
#include "chimera/vm/json.hpp"
#include "chimera/vm/settings_form.hpp"

using chimera::vm::CoresViewModel;
using chimera::vm::SettingsForm;
using chimera::vm::json::Json;
namespace fs = std::filesystem;

namespace {

/* the key names the window sends, by every system's table (key_names.cpp) */
struct KeyCode
{
	uint32_t code;
	const char *name;
};
#include "../../qt/key_codes.inc"

int failedChecks = 0;

void report(const char *check, const std::vector<std::string> &wrong)
{
	if (wrong.empty())
	{
		std::printf("PASS  %s\n", check);
		return;
	}
	failedChecks++;
	std::printf("FAIL  %s (%zu)\n", check, wrong.size());
	for (size_t i = 0; i < wrong.size() && i < 40; i++) std::printf("        %s\n", wrong[i].c_str());
}

std::string lower(std::string s)
{
	for (char &c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
	return s;
}

std::vector<std::string> split(const std::string &text, const char *seps)
{
	std::vector<std::string> parts;
	std::string part;
	auto flush = [&] {
		const size_t a = part.find_first_not_of(" \t"), b = part.find_last_not_of(" \t");
		parts.push_back(a == std::string::npos ? std::string() : part.substr(a, b - a + 1));
		part.clear();
	};
	for (char c : text)
	{
		if (std::strchr(seps, c) != nullptr) flush();
		else part += c;
	}
	flush();
	return parts;
}

/* a package, as read from its file */
struct Package
{
	std::string path, name; /* name: the file's, without .chimeraCore */
	std::string error;      /* why it could not be read, "" when it could */
	bool waterbox = false;
	Json cfg, keybinds;
	bool hasKeybinds = false;
	std::map<std::string, std::string> entries; /* what was asked of it, by name */
	std::string languageProblems;               /* ce_package_language_problems: "" when none */
};

std::string entryText(ce_package *p, const char *name)
{
	uint64_t len = 0;
	const uint8_t *d = ce_package_entry(p, name, &len);
	return d == nullptr ? std::string() : std::string(reinterpret_cast<const char *>(d), static_cast<size_t>(len));
}

Package readPackage(const std::string &path)
{
	Package k;
	k.path = path;
	k.name = fs::path(path).stem().string();
	const char *perr = nullptr;
	ce_package *p = ce_package_open(path.c_str(), &perr);
	if (p == nullptr)
	{
		k.error = std::string("not a core package: ") + (perr != nullptr ? perr : "it does not open");
		return k;
	}
	k.waterbox = ce_package_is_waterbox(p) != 0;
	k.languageProblems = ce_package_language_problems(p);
	if (!k.waterbox) k.error = "no core.wbx and waterbox.config at its top level";
	std::string jerr;
	if (k.waterbox && !chimera::vm::json::parse(entryText(p, "waterbox.config"), k.cfg, jerr))
		k.error = "waterbox.config does not read: " + jerr;
	const std::string binds = entryText(p, "default_keybinds.json");
	if (!binds.empty())
	{
		if (chimera::vm::json::parse(binds, k.keybinds, jerr)) k.hasKeybinds = true;
		else if (k.error.empty()) k.error = "default_keybinds.json does not read: " + jerr;
	}
	/* the controller's pictures and the licence list, for the picture check */
	auto keep = [&](const Json *input) {
		if (input == nullptr) return;
		if (const Json *pic = input->find("picture"); pic != nullptr && !pic->text.empty())
			if (ce_package_has_entry(p, pic->text.c_str())) k.entries[pic->text] = entryText(p, pic->text.c_str());
	};
	keep(k.cfg.find("input"));
	if (const Json *machines = k.cfg.find("machines"))
		for (const Json &m : machines->items) keep(m.find("input"));
	k.entries["licenses/licenses.json"] = entryText(p, "licenses/licenses.json");
	ce_package_free(p);
	return k;
}

/* the machines a package names: its systemId, or its machines' */
std::vector<std::string> systemIds(const Json &cfg)
{
	std::vector<std::string> ids;
	if (const Json *machines = cfg.find("machines"); machines != nullptr && !machines->items.empty())
	{
		for (const Json &m : machines->items)
			if (const Json *id = m.find("systemId"); id != nullptr && !id->text.empty()) ids.push_back(id->text);
			else if (const Json *mid = m.find("id"); mid != nullptr && !mid->text.empty()) ids.push_back(mid->text);
	}
	if (ids.empty())
		if (const Json *id = cfg.find("systemId"); id != nullptr && !id->text.empty()) ids.push_back(id->text);
	return ids;
}

/* the controllers a package declares: its own input and each machine's */
std::vector<const Json *> inputsOf(const Json &cfg)
{
	std::vector<const Json *> out;
	if (const Json *i = cfg.find("input")) out.push_back(i);
	if (const Json *machines = cfg.find("machines"))
		for (const Json &m : machines->items)
			if (const Json *i = m.find("input")) out.push_back(i);
	return out;
}

std::vector<std::string> buttonsOf(const Json &cfg)
{
	std::vector<std::string> out;
	for (const Json *i : inputsOf(cfg))
		if (const Json *b = i->find("buttons"))
			for (const Json &n : b->items) out.push_back(n.text);
	return out;
}

std::vector<std::string> axesOf(const Json &cfg)
{
	std::vector<std::string> out;
	for (const Json *i : inputsOf(cfg))
		if (const Json *a = i->find("axes"))
			for (const Json &n : a->items)
				if (const Json *name = n.find("name")) out.push_back(name->text);
	return out;
}

/* ---- the settings rules, as the window reads a package ---- */

/* a package with no machines is one machine, and narrows nothing; a machine
 * takes the declarations with no "when" and those whose "when" names one of
 * its own */
std::vector<const Json *> settingsFor(const Json &cfg, const Json *machine)
{
	std::vector<const Json *> out;
	const Json *settings = cfg.find("settings");
	if (settings == nullptr) return out;
	std::vector<std::string> scopes;
	if (machine != nullptr)
		if (const Json *when = machine->find("when"))
			for (const Json &w : when->items) scopes.push_back(w.text);
	for (const Json &d : settings->items)
	{
		const Json *when = d.find("when");
		if (scopes.empty() || when == nullptr || when->items.empty())
		{
			out.push_back(&d);
			continue;
		}
		for (const Json &w : when->items)
			if (std::find(scopes.begin(), scopes.end(), w.text) != scopes.end())
			{
				out.push_back(&d);
				break;
			}
	}
	return out;
}

std::vector<const Json *> machinesOf(const Json &cfg)
{
	std::vector<const Json *> out;
	if (const Json *machines = cfg.find("machines"); machines != nullptr && !machines->items.empty())
		for (const Json &m : machines->items) out.push_back(&m);
	else
		out.push_back(nullptr);
	return out;
}

std::string nameOf(const Json &decl)
{
	const Json *n = decl.find("name");
	return n != nullptr ? n->text : std::string();
}

std::vector<std::string> settingsDeclaredTwice(const Json &cfg)
{
	std::vector<std::string> out;
	for (const Json *machine : machinesOf(cfg))
	{
		std::map<std::string, int> count;
		std::vector<std::string> order;
		for (const Json *d : settingsFor(cfg, machine))
			if (count[nameOf(*d)]++ == 0) order.push_back(nameOf(*d));
		std::string where = "its one machine";
		if (machine != nullptr)
			if (const Json *id = machine->find("id")) where = id->text;
		for (const std::string &n : order)
			if (count[n] > 1) out.push_back("'" + n + "' has " + std::to_string(count[n]) + " declarations on " + where);
	}
	return out;
}

std::vector<std::string> soundRatesOutOfRange(const Json &cfg)
{
	auto real = [](double r) { return r >= 8000 && r <= 192000; };
	std::vector<std::string> out;
	if (const Json *audio = cfg.find("audio"))
		if (const Json *rate = audio->find("rate"); rate != nullptr && !real(rate->number()))
			out.push_back("audio.rate is " + rate->text);
	if (const Json *machines = cfg.find("machines"))
		for (const Json &m : machines->items)
			if (const Json *rate = m.find("audioRate"); rate != nullptr && !real(rate->number()))
				out.push_back("machine " + (m.find("id") != nullptr ? m.find("id")->text : std::string("?")) + ": audioRate is " + rate->text);
	return out;
}

std::vector<std::string> labelsOfNoValue(const Json &cfg)
{
	std::vector<std::string> out;
	if (const Json *settings = cfg.find("settings"))
		for (const Json &d : settings->items)
			if (const Json *labels = d.find("optionLabels"))
				for (const auto &[value, label] : labels->members)
				{
					bool found = false;
					if (const Json *options = d.find("options"))
						for (const Json &o : options->items) found = found || o.text == value;
					if (!found) out.push_back("'" + nameOf(d) + "' labels '" + value + "', which is not one of its values");
				}
	return out;
}

/* A core's tool finds each file it reads or writes under its name, beside two
 * that Chimera gives every tool run: the request ("tool") and the settings
 * ("settings"). A file of the tool's with one of those names could not be
 * given to it (user-decided, 2026-11-02; docs/project.md). */
std::vector<std::string> toolFilesNamedAsChimeras(const Json &cfg)
{
	static const std::set<std::string> taken = {"tool", "settings"};
	std::vector<std::string> out;
	if (const Json *tools = cfg.find("tools"))
		for (const Json &t : tools->items)
		{
			const Json *id = t.find("id");
			for (const char *list : {"inputs", "outputs"})
				if (const Json *files = t.find(list))
					for (const Json &f : files->items)
						if (const Json *name = f.find("name"); name != nullptr && taken.count(name->text))
							out.push_back("tool '" + (id != nullptr ? id->text : std::string("?")) + "' names a file '" + name->text
								+ "', the name of a file Chimera gives every tool");
		}
	return out;
}

std::vector<std::string> unknownSettingTypes(const Json &cfg)
{
	static const std::set<std::string> known = {"bool", "int", "uint", "long", "ulong", "float", "string", "enum", "datetime"};
	std::vector<std::string> out;
	if (const Json *settings = cfg.find("settings"))
		for (const Json &d : settings->items)
			if (!known.count(lower(SettingsForm::typeOf(d))))
			{
				const Json *t = d.find("type");
				const std::string line = "'" + nameOf(d) + "' is of type '" + (t != nullptr ? t->text : SettingsForm::typeOf(d)) + "', which Chimera does not know";
				if (std::find(out.begin(), out.end(), line) == out.end()) out.push_back(line);
			}
	return out;
}

/* where a package runs: "runsOn" lists at least one of the six, and
 * nothing else (2026-11-05). Which of them its own CPU is, publish-core.sh
 * checks against its core.wbx. */
std::vector<std::string> runsOnProblems(const Json &cfg)
{
	static const std::set<std::string> known = {"windows-x86_64", "windows-aarch64", "linux-x86_64", "linux-aarch64",
		"macos-x86_64", "macos-aarch64"};
	const Json *runsOn = cfg.find("runsOn");
	if (runsOn == nullptr || runsOn->kind != Json::Kind::Array || runsOn->items.empty())
		return {"it does not say where it runs ('runsOn')"};
	std::vector<std::string> out;
	for (const Json &e : runsOn->items)
		if (e.kind != Json::Kind::String || !known.count(e.text))
			out.push_back("'runsOn' lists '" + e.text + "', which is not one of the six");
	return out;
}

/* ---- the input names the window sends ---- */

const std::set<std::string> controllerButtons = {"A", "B", "X", "Y", "Back", "Guide", "Start", "LeftThumb", "RightThumb",
	"LeftShoulder", "RightShoulder", "DpadUp", "DpadDown", "DpadLeft", "DpadRight", "Misc", "Paddle1", "Paddle2", "Paddle3",
	"Paddle4", "Touchpad", "LStickUp", "LStickDown", "LStickLeft", "LStickRight", "RStickUp", "RStickDown", "RStickLeft",
	"RStickRight", "LeftTrigger", "RightTrigger"};
const std::set<std::string> controllerAxes = {"LeftThumbX", "LeftThumbY", "RightThumbX", "RightThumbY", "LeftTrigger",
	"RightTrigger", "TouchpadX", "TouchpadY"};
const std::regex joystickButton(R"(^([XYZWVSQPN][+-]|Axis\d+[+-]|B\d+|POV\d+[UDLR])$)");
const std::regex joystickAxis(R"(^([XYZWVSQPN]|Axis\d+) Axis$)");
const std::regex gamepadPart(R"(^([XJ])\d+ (.+)$)");
const std::set<std::string> mouseAxes = {"WMouse X", "WMouse Y", "RMouse X", "RMouse Y"};

std::set<std::string> keyAndMouseButtons()
{
	std::set<std::string> names = {"Shift", "Ctrl", "Alt", "WMouse L", "WMouse M", "WMouse R", "WMouse 1", "WMouse 2"};
	for (const KeyCode &k : windowsKeys) names.insert(k.name);
	for (const KeyCode &k : windowsLayoutKeys) names.insert(k.name);
	for (const KeyCode &k : linuxKeys) names.insert(k.name);
	for (const KeyCode &k : macKeys) names.insert(k.name);
	return names;
}

/* ---- the packages ---- */

std::vector<Package> packagesIn(const std::string &folder)
{
	std::vector<Package> out;
	std::error_code ec;
	std::vector<std::string> paths;
	for (const auto &e : fs::directory_iterator(folder, ec))
		if (e.path().extension() == ".chimeraCore") paths.push_back(e.path().string());
	std::sort(paths.begin(), paths.end());
	for (const std::string &p : paths) out.push_back(readPackage(p));
	return out;
}

/* each core's newest: an older version kept for old projects is what it was */
std::vector<const Package *> newest(const std::vector<Package> &all)
{
	std::map<std::string, const Package *> best;
	const std::regex suffix("-[0-9a-f]{7,40}$", std::regex::icase);
	for (const Package &k : all)
	{
		if (!k.error.empty()) continue;
		const std::string core = lower(std::regex_replace(k.name, suffix, ""));
		const Json *d = k.cfg.find("versionDate");
		const std::string date = d != nullptr ? d->text : std::string();
		auto it = best.find(core);
		std::error_code ec;
		if (it == best.end()) best[core] = &k;
		else
		{
			const Json *od = it->second->cfg.find("versionDate");
			const std::string odate = od != nullptr ? od->text : std::string();
			if (date > odate || (date == odate && fs::last_write_time(k.path, ec) > fs::last_write_time(it->second->path, ec))) it->second = &k;
		}
	}
	std::vector<const Package *> out;
	for (const auto &[core, k] : best) out.push_back(k);
	return out;
}

void checkPackages(const std::vector<Package> &all)
{
	const std::vector<const Package *> fresh = newest(all);
	std::vector<const Package *> readable;
	for (const Package &k : all)
		if (k.error.empty()) readable.push_back(&k);

	{
		std::vector<std::string> wrong;
		for (const Package &k : all)
			if (!k.error.empty()) wrong.push_back(k.name + ": " + k.error);
		report("every package is readable", wrong);
	}
	{
		std::vector<std::string> wrong;
		for (const Package *k : readable)
			if (const std::string why = CoresViewModel::abiRefusal(k->cfg); !why.empty()) wrong.push_back(k->name + ": " + why);
		report("every package is built for a guest ABI this Chimera runs", wrong);
	}
	{
		std::vector<std::string> wrong;
		for (const Package *k : readable)
			for (const std::string &w : runsOnProblems(k->cfg)) wrong.push_back(k->name + ": " + w);
		report("every package says where it runs", wrong);
	}
	{
		/* the package's texts are keys of its own language tables, one for
		 * every language Chimera has, every key in each (docs/translations.md) */
		std::vector<std::string> wrong;
		for (const Package *k : readable)
		{
			std::string line;
			for (const char c : k->languageProblems)
			{
				if (c != '\n') { line += c; continue; }
				wrong.push_back(k->name + ": " + line);
				line.clear();
			}
		}
		report("every package's texts are in every language Chimera has", wrong);
	}
	{
		/* what the window takes from a package: its core's name, its machines,
		 * and its settings, read as New Project and Project Settings read them */
		std::vector<std::string> wrong;
		for (const Package *k : readable)
		{
			const Json *core = k->cfg.find("coreName");
			if (core == nullptr || core->text.empty()) wrong.push_back(k->name + ": names no core");
			if (systemIds(k->cfg).empty()) wrong.push_back(k->name + ": claims no machine");
			std::vector<Json> decl;
			if (const Json *s = k->cfg.find("settings")) decl = s->items;
			std::vector<Json> machines;
			if (const Json *m = k->cfg.find("machines")) machines = m->items;
			const Json *chooser = k->cfg.find("machineSetting");
			SettingsForm form;
			form.declare(decl, chooser != nullptr ? chooser->text : std::string());
			form.declareMachines(machines);
			form.setSlots("{}");
		form.setValues(Json());
			form.evaluate();
			for (const Json &d : decl)
				if (nameOf(d).empty()) wrong.push_back(k->name + ": a setting with no name");
		}
		report("every package is one the window can read", wrong);
	}
	{
		std::vector<std::string> wrong;
		for (const Package *k : readable)
		{
			const Json *v = k->cfg.find("version");
			if (v == nullptr || v->text.empty()) wrong.push_back(k->name);
		}
		report("every package stamps a version", wrong);
	}
	{
		std::vector<std::string> wrong;
		for (const Package *k : readable)
			for (const std::string &w : settingsDeclaredTwice(k->cfg)) wrong.push_back(k->name + ": " + w);
		report("a setting has one declaration on every machine", wrong);
	}
	{
		std::vector<std::string> wrong;
		for (const Package *k : readable)
			for (const std::string &w : soundRatesOutOfRange(k->cfg)) wrong.push_back(k->name + ": " + w);
		report("every sound rate a package states is a real one", wrong);
	}
	{
		std::vector<std::string> wrong;
		for (const Package *k : readable)
			for (const std::string &w : labelsOfNoValue(k->cfg)) wrong.push_back(k->name + ": " + w);
		report("every option label names a value of its setting", wrong);
	}
	{
		std::vector<std::string> wrong;
		for (const Package *k : readable)
			for (const std::string &w : toolFilesNamedAsChimeras(k->cfg)) wrong.push_back(k->name + ": " + w);
		report("every file a core's tool reads or writes has a name of its own", wrong);
	}
	{
		std::vector<std::string> wrong;
		for (const Package *k : readable)
			for (const std::string &w : unknownSettingTypes(k->cfg)) wrong.push_back(k->name + ": " + w);
		report("every setting type is one Chimera knows", wrong);
	}

	/* the shipped keys, judged on each core's newest package */
	{
		/* by the exact name, as the window binds them: "P1 A" is bound only by "P1 A" (chimera#284) */
		std::vector<std::string> wrong;
		for (const Package *k : fresh)
		{
			if (!k->hasKeybinds) continue;
			const std::vector<std::string> buttons = buttonsOf(k->cfg), axes = axesOf(k->cfg);
			if (buttons.empty() && axes.empty()) continue;
			if (const Json *all = k->keybinds.find("AllTrollers"))
				for (const auto &[controller, binds] : all->members)
					for (const auto &[button, keys] : binds.members)
						if (std::find(buttons.begin(), buttons.end(), button) == buttons.end())
							wrong.push_back(k->name + "/" + controller + ": " + button);
			if (const Json *all = k->keybinds.find("AllTrollersAnalog"))
				for (const auto &[controller, binds] : all->members)
					for (const auto &[axis, bind] : binds.members)
						if (std::find(axes.begin(), axes.end(), axis) == axes.end())
							wrong.push_back(k->name + "/" + controller + ": " + axis + " (an axis)");
		}
		report("keybinds only name controls the controller has", wrong);
	}
	{
		/* chimera#314: ares bound its sticks to "X1 LStickX", which nothing sends */
		std::vector<std::string> wrong;
		for (const Package *k : fresh)
		{
			if (!k->hasKeybinds) continue;
			if (const Json *all = k->keybinds.find("AllTrollers"))
				for (const auto &[controller, binds] : all->members)
					for (const auto &[button, keys] : binds.members)
						for (const std::string &part : split(keys.text, ",+"))
						{
							std::smatch m;
							if (!std::regex_match(part, m, gamepadPart)) continue;
							const bool known = m[1] == "X" ? controllerButtons.count(m[2].str()) > 0 : std::regex_match(m[2].str(), joystickButton);
							if (!known) wrong.push_back(k->name + "/" + controller + "/" + button + ": " + part);
						}
			if (const Json *all = k->keybinds.find("AllTrollersAnalog"))
				for (const auto &[controller, binds] : all->members)
					for (const auto &[axis, bind] : binds.members)
					{
						const Json *value = bind.find("Value");
						for (const std::string &part : split(value != nullptr ? value->text : std::string(), ","))
						{
							std::smatch m;
							if (!std::regex_match(part, m, gamepadPart)) continue;
							const std::string what = m[2].str();
							bool known;
							if (m[1] == "X")
								known = what.size() > 5 && what.compare(what.size() - 5, 5, " Axis") == 0 && controllerAxes.count(what.substr(0, what.size() - 5)) > 0;
							else
								known = std::regex_match(what, joystickAxis);
							if (!known) wrong.push_back(k->name + "/" + controller + "/" + axis + ": " + part + " (an axis)");
						}
					}
		}
		report("keybinds name gamepad inputs Chimera sends", wrong);
	}
	{
		/* issue 338: a picture of the controller is a PNG in the package, named by its licence list */
		std::vector<std::string> wrong;
		for (const Package *k : fresh)
			for (const Json *input : inputsOf(k->cfg))
			{
				const Json *pic = input->find("picture");
				if (pic == nullptr || pic->text.empty()) continue;
				auto it = k->entries.find(pic->text);
				if (it == k->entries.end())
				{
					wrong.push_back(k->name + ": " + pic->text + " is not in the package");
					continue;
				}
				static const unsigned char png[8] = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
				if (it->second.size() < 8 || std::memcmp(it->second.data(), png, 8) != 0) wrong.push_back(k->name + ": " + pic->text + " is not a PNG");
				const std::string file = fs::path(pic->text).filename().string();
				if (k->entries.at("licenses/licenses.json").find(file) == std::string::npos)
					wrong.push_back(k->name + ": licenses/licenses.json does not name " + file);
			}
		report("a picture of the controller is a PNG the package licenses", wrong);
	}
	{
		/* found with chimera#336: 33 cores bound their directions to POV1, a second hat */
		std::vector<std::string> wrong;
		const std::regex hat(R"(^J\d+ POV(\d+)[UDLR]$)");
		for (const Package *k : fresh)
		{
			if (!k->hasKeybinds) continue;
			std::set<int> hats;
			if (const Json *all = k->keybinds.find("AllTrollers"))
				for (const auto &[controller, binds] : all->members)
					for (const auto &[button, keys] : binds.members)
						for (const std::string &part : split(keys.text, ",+"))
						{
							std::smatch m;
							if (std::regex_match(part, m, hat)) hats.insert(std::stoi(m[1].str()));
						}
			if (!hats.empty() && !hats.count(0))
			{
				std::string list;
				for (int h : hats) list += (list.empty() ? "POV" : "/POV") + std::to_string(h);
				wrong.push_back(k->name + " binds " + list + " and not POV0");
			}
		}
		report("keybinds name a joystick's first hat", wrong);
	}
	{
		/* chimera#317: sixteen cores shipped keys by names nothing sends */
		const std::set<std::string> keys = keyAndMouseButtons();
		static const std::map<std::string, std::string> merged = {{"LeftShift", "Shift"}, {"RightShift", "Shift"}, {"LeftCtrl", "Ctrl"},
			{"RightCtrl", "Ctrl"}, {"LeftAlt", "Alt"}, {"RightAlt", "Alt"}};
		const std::regex pad(R"(^[XJ]\d+ )");
		std::vector<std::string> wrong;
		for (const Package *k : fresh)
		{
			if (!k->hasKeybinds) continue;
			if (const Json *all = k->keybinds.find("AllTrollers"))
				for (const auto &[controller, binds] : all->members)
				{
					std::set<std::string> bound;
					for (const auto &[button, value] : binds.members)
						for (const std::string &part : split(value.text, ",+"))
						{
							if (part.empty()) continue;
							bound.insert(part);
							if (std::regex_search(part, pad)) continue;
							if (!keys.count(part)) wrong.push_back(k->name + "/" + controller + "/" + button + ": " + part);
						}
					for (const auto &[side, both] : merged)
						if (bound.count(side) && !bound.count(both))
							wrong.push_back(k->name + "/" + controller + ": " + side + " is bound but not " + both + ", the only one a default setup sends");
				}
			if (const Json *all = k->keybinds.find("AllTrollersAnalog"))
				for (const auto &[controller, binds] : all->members)
					for (const auto &[axis, bind] : binds.members)
					{
						const Json *value = bind.find("Value");
						for (const std::string &part : split(value != nullptr ? value->text : std::string(), ","))
						{
							if (part.empty() || std::regex_search(part, pad)) continue;
							if (!mouseAxes.count(part)) wrong.push_back(k->name + "/" + controller + "/" + axis + ": " + part + " (an axis bound to what is not one)");
						}
					}
		}
		report("keybinds name keys and mouse inputs Chimera sends", wrong);
	}
}

/* ---- the input roll's letters and headers ---- */

struct Controller
{
	std::string core, where;
	std::vector<std::string> buttons;
	std::vector<std::pair<std::string, std::string>> axes; /* name, header ("" none) */
	std::vector<bool> axisHeaded;
	bool declares = false;
	std::map<std::string, std::string> declared, headers;
};

/* "P2 Start" -> "Start"; "Reset" stays */
std::string bare(const std::string &button)
{
	const size_t space = button.find(' ');
	if (space > 1 && button[0] == 'P' && space + 1 < button.size()
		&& std::all_of(button.begin() + 1, button.begin() + static_cast<long>(space), [](char c) { return c >= '0' && c <= '9'; }))
		return button.substr(space + 1);
	return button;
}

std::string playerOf(const std::string &button)
{
	return bare(button) == button ? std::string("console") : button.substr(0, button.find(' '));
}

const std::string *letterOf(const Controller &c, const std::string &button)
{
	if (!c.declares) return nullptr;
	if (auto it = c.declared.find(button); it != c.declared.end()) return &it->second;
	if (auto it = c.declared.find(bare(button)); it != c.declared.end()) return &it->second;
	return nullptr;
}

std::string headerOf(const Controller &c, const std::string &button)
{
	if (auto it = c.headers.find(button); it != c.headers.end()) return it->second;
	if (auto it = c.headers.find(bare(button)); it != c.headers.end()) return it->second;
	const std::string *l = letterOf(c, button);
	return l != nullptr ? *l : std::string();
}

std::vector<Controller> controllers(const std::vector<Package> &all)
{
	std::vector<Controller> out;
	auto read = [&](const std::string &core, const std::string &where, const Json *input) {
		if (input == nullptr) return;
		Controller c;
		c.core = core;
		c.where = where;
		if (const Json *b = input->find("buttons"))
			for (const Json &n : b->items) c.buttons.push_back(n.text);
		if (const Json *a = input->find("axes"))
			for (const Json &n : a->items)
			{
				const Json *name = n.find("name"), *header = n.find("header");
				c.axes.emplace_back(name != nullptr ? name->text : std::string(), header != nullptr ? header->text : std::string());
				c.axisHeaded.push_back(header != nullptr);
			}
		if (const Json *m = input->find("mnemonics"); m != nullptr && m->kind == Json::Kind::Object)
		{
			c.declares = true;
			for (const auto &[k, v] : m->members) c.declared[k] = v.text;
		}
		if (const Json *h = input->find("headers"); h != nullptr && h->kind == Json::Kind::Object)
			for (const auto &[k, v] : h->members) c.headers[k] = v.text;
		out.push_back(std::move(c));
	};
	for (const Package &k : all)
	{
		if (!k.error.empty()) continue;
		read(k.name, "the package", k.cfg.find("input"));
		if (const Json *machines = k.cfg.find("machines"))
			for (const Json &m : machines->items)
			{
				const Json *label = m.find("label"), *id = m.find("id");
				read(k.name, label != nullptr ? label->text : id != nullptr ? id->text : "a machine", m.find("input"));
			}
	}
	return out;
}

void checkLetters(const std::vector<Package> &all)
{
	const std::vector<Controller> every = controllers(all);
	std::vector<const Controller *> declaring;
	for (const Controller &c : every)
		if (c.declares || std::find(c.axisHeaded.begin(), c.axisHeaded.end(), true) != c.axisHeaded.end()) declaring.push_back(&c);
	{
		/* a button left out gets the rule's guess: two columns of one pad come to share a letter */
		std::vector<std::string> wrong;
		for (const Controller *c : declaring)
			for (const std::string &b : c->buttons)
				if (letterOf(*c, b) == nullptr) wrong.push_back(c->core + " (" + c->where + "): " + b);
		report("a core that declares letters declares them all", wrong);
	}
	{
		std::vector<std::string> wrong;
		for (const Controller *c : declaring)
			for (const auto &[name, letter] : c->declared)
				if (letter.size() != 1 || letter[0] <= ' ' || letter[0] >= 0x7F || letter[0] == '.' || letter[0] == '|')
					wrong.push_back(c->core + " (" + c->where + "): " + name + " = \"" + letter + "\"");
		report("every declared letter can be written", wrong);
	}
	{
		std::vector<std::string> wrong;
		for (const Controller *c : declaring)
		{
			std::set<std::string> names(c->buttons.begin(), c->buttons.end());
			for (const std::string &b : c->buttons) names.insert(bare(b));
			for (const auto &[name, letter] : c->declared)
				if (!names.count(name)) wrong.push_back(c->core + " (" + c->where + "): " + name);
		}
		report("no declaration names a control that is not there", wrong);
	}
	{
		/* five characters is enough for P1GSX, the longest a shipped controller needs */
		std::vector<std::string> wrong;
		for (const Controller *c : declaring)
			for (size_t i = 0; i < c->axes.size(); i++)
			{
				const auto &[name, header] = c->axes[i];
				if (!c->axisHeaded[i] || header.empty() || header.size() > 5)
					wrong.push_back(c->core + " (" + c->where + "): " + name + " -> " + (c->axisHeaded[i] ? "\"" + header + "\"" : std::string("no header")));
			}
		report("axis headers are declared and short", wrong);
	}
	{
		/* per player, over controllers small enough for it (a PlayStation 2's L2 and R2 once shared 'L' and 'R') */
		std::vector<std::string> wrong;
		for (const Controller *c : declaring)
		{
			if (!c->declares) continue;
			std::map<std::string, std::vector<std::string>> players;
			std::vector<std::string> order;
			for (const std::string &b : c->buttons)
			{
				if (!players.count(playerOf(b))) order.push_back(playerOf(b));
				players[playerOf(b)].push_back(b);
			}
			for (const std::string &p : order)
			{
				if (players[p].size() > 40) continue;
				std::map<std::string, std::string> byChar;
				for (const std::string &b : players[p])
				{
					const std::string *ch = letterOf(*c, b);
					if (ch == nullptr) continue;
					if (auto it = byChar.find(*ch); it != byChar.end())
						wrong.push_back(c->core + " (" + c->where + ") " + p + ": '" + *ch + "' is both " + it->second + " and " + b);
					else
						byChar[*ch] = b;
				}
			}
		}
		report("no two controls of one player share a letter", wrong);
	}
	{
		/* chimera#225: a header tells a button from its neighbours */
		std::vector<std::string> wrong;
		for (const Controller &c : every)
		{
			if (c.headers.empty()) continue;
			std::set<std::string> names(c.buttons.begin(), c.buttons.end());
			for (const std::string &b : c.buttons) names.insert(bare(b));
			for (const auto &[name, h] : c.headers)
			{
				const bool printable = std::all_of(h.begin(), h.end(), [](char ch) { return ch >= ' ' && ch < 0x7F; });
				if (h.empty() || h.size() > 8 || h.front() == ' ' || h.back() == ' ' || !printable)
					wrong.push_back(c.core + " (" + c.where + "): " + name + " = \"" + h + "\" is not a header");
				if (!names.count(name)) wrong.push_back(c.core + " (" + c.where + "): a header for " + name + ", which is not a button");
			}
			std::map<std::string, std::vector<std::string>> players;
			for (const std::string &b : c.buttons) players[playerOf(b)].push_back(b);
			for (const auto &[p, group] : players)
				for (const std::string &b : group)
				{
					if (!c.headers.count(b) && !c.headers.count(bare(b))) continue;
					for (const std::string &other : group)
						if (other != b && headerOf(c, other) == headerOf(c, b))
						{
							wrong.push_back(c.core + " (" + c.where + ") " + p + ": \"" + headerOf(c, b) + "\" heads both " + b + " and " + other);
							break;
						}
				}
		}
		report("a declared header is usable and heads one column", wrong);
	}
}

/* ---- the rules on packages made up here ---- */

Json parsed(const char *text)
{
	Json j;
	std::string error;
	if (!chimera::vm::json::parse(text, j, error)) std::fprintf(stderr, "bad test JSON: %s\n", error.c_str());
	return j;
}

void selfChecks()
{
	std::vector<std::string> wrong;
	auto expect = [&](bool ok, const std::string &what) {
		if (!ok) wrong.push_back(what);
	};
	{
		const Json cfg = parsed(R"({"coreName": "rates", "systemId": "SYS", "audio": {"samplesPerFrame": 2048, "rate": 48000},
			"machines": [{"id": "A", "when": ["a"], "audioRate": 64000}, {"id": "B", "when": ["b"], "audioRate": 6400}, {"id": "C", "when": ["c"]}]})");
		const std::vector<std::string> found = soundRatesOutOfRange(cfg);
		expect(found == std::vector<std::string>{"machine B: audioRate is 6400"}, "a sound rate no chip has is found");
	}
	{
		const char *head = R"({"machineSetting": "machine",
			"machines": [{"id": "HOME", "when": ["home"]}, {"id": "ARCADE", "when": ["arcade"]}],
			"settings": [{"name": "machine", "type": "enum", "options": ["home", "arcade"], "default": "home"},)";
		const Json shownOnly = parsed((std::string(head) + R"(
			{"name": "bios", "type": "enum", "options": ["home-rom"], "default": "home-rom", "exposedWhen": {"setting": "machine", "is": "home"}},
			{"name": "bios", "type": "enum", "options": ["arcade-rom"], "default": "arcade-rom", "exposedWhen": {"setting": "machine", "is": "arcade"}}]})").c_str());
		const std::vector<std::string> found = settingsDeclaredTwice(shownOnly);
		expect(found.size() == 2 && found[0].find("'bios'") != std::string::npos, "two declarations told apart only by exposedWhen are found");
		const Json byMachine = parsed((std::string(head) + R"(
			{"name": "bios", "type": "enum", "options": ["home-rom"], "default": "home-rom", "when": ["home"], "exposedWhen": {"setting": "machine", "is": "home"}},
			{"name": "bios", "type": "enum", "options": ["arcade-rom"], "default": "arcade-rom", "when": ["arcade"], "exposedWhen": {"setting": "machine", "is": "arcade"}}]})").c_str());
		expect(settingsDeclaredTwice(byMachine).empty(), "two declarations told apart by their machines pass");
		/* and the window gives the home machine its own: the home rom (New
		 * Project chooses a machine as soon as the package is chosen) */
		SettingsForm form;
		form.declare(byMachine.find("settings")->items, "machine");
		form.declareMachines(byMachine.find("machines")->items);
		form.setSlots("{}");
		form.setValues(parsed(R"({"machine": "home"})"));
		form.evaluate();
		const Json recorded = form.recorded();
		const Json *bios = recorded.find("bios");
		expect(bios != nullptr && bios->text == "home-rom",
			"the window gives a machine its own declaration's default (it gave " + chimera::vm::json::write(recorded) + ")");
	}
	{
		const Json cfg = parsed(R"({"settings": [{"name": "bios", "type": "enum", "options": ["a-rom", "b-rom"], "default": "a-rom",
			"optionLabels": {"a-rom": "USA", "c-rom": "Japan"}}]})");
		expect(labelsOfNoValue(cfg) == std::vector<std::string>{"'bios' labels 'c-rom', which is not one of its values"}, "a label of no value is found");
	}
	{
		const Json cfg = parsed(R"({"settings": [{"name": "seed", "type": "unit", "default": 1}, {"name": "clock", "type": "datetime", "default": 946684800},
			{"name": "wide", "type": "ULong", "default": 1}, {"name": "guessed", "default": 3}]})");
		expect(unknownSettingTypes(cfg) == std::vector<std::string>{"'seed' is of type 'unit', which Chimera does not know"},
			"a setting type Chimera does not know is found");
	}
	{
		const Json cfg = parsed(R"({"tools": [{"id": "join", "inputs": [{"name": "rom"}, {"name": "settings"}], "outputs": [{"name": "joined"}]},
			{"id": "split", "inputs": [{"name": "joined"}], "outputs": [{"name": "tool"}]}]})");
		expect(toolFilesNamedAsChimeras(cfg) == std::vector<std::string>{
				"tool 'join' names a file 'settings', the name of a file Chimera gives every tool",
				"tool 'split' names a file 'tool', the name of a file Chimera gives every tool"},
			"a tool's file named as one Chimera gives every tool is found, and 'rom' is the tool's to use");
	}
	{
		expect(CoresViewModel::abiRefusal(parsed(R"({"abi": 1})")).empty() && CoresViewModel::abiRefusal(parsed("{}")).empty(),
			"guest ABI 1, said or not, runs");
		expect(CoresViewModel::abiRefusal(parsed(R"({"abi": 2})")).find("Update Chimera") != std::string::npos, "a newer guest ABI is refused, Chimera named");
		expect(CoresViewModel::abiRefusal(parsed(R"({"abi": 0})")).find("Update the core") != std::string::npos, "a retired guest ABI is refused, the core named");
	}
	{
		expect(runsOnProblems(parsed(R"({"runsOn": ["linux-x86_64", "macos-aarch64"]})")).empty(), "a package that lists two of the six runs on them");
		expect(runsOnProblems(parsed("{}")).size() == 1 && runsOnProblems(parsed(R"({"runsOn": []})")).size() == 1,
			"a package that does not say where it runs is found");
		expect(runsOnProblems(parsed(R"({"runsOn": ["linux-x64", "macos-aarch64"]})"))
				== std::vector<std::string>{"'runsOn' lists 'linux-x64', which is not one of the six"},
			"a name that is not one of the six is found");
	}
	{
		/* the key and gamepad names a binding is checked against */
		const std::set<std::string> keys = keyAndMouseButtons();
		expect(keys.count("Number1") && keys.count("Enter") && keys.count("Shift") && keys.count("WMouse L") && !keys.count("D1") && !keys.count("Return"),
			"the key names are the window's (Number1, Enter; not D1, Return)");
	}
	report("the rules, on packages made up here", wrong);
}

} // namespace

int main(int argc, char **argv)
{
	if (argc > 1 && std::strcmp(argv[1], "--self") == 0)
	{
		selfChecks();
		std::printf("%s: %d failed\n", failedChecks ? "FAIL" : "PASS", failedChecks);
		return failedChecks ? 1 : 0;
	}
	std::string folder = argc > 1 ? argv[1] : "";
	if (folder.empty())
		if (const char *env = std::getenv("CHIMERA_CORES_DIR")) folder = env;
	if (folder.empty()) folder = "build/Cores";
	const std::vector<Package> all = packagesIn(folder);
	if (all.empty())
	{
		std::printf("SKIP  no core packages in %s (a check with nothing to check proves nothing)\n", folder.c_str());
		return 77;
	}
	std::printf("%zu packages in %s\n", all.size(), folder.c_str());
	selfChecks();
	checkPackages(all);
	checkLetters(all);
	std::printf("%s: %d failed\n", failedChecks ? "FAIL" : "PASS", failedChecks);
	return failedChecks ? 1 : 0;
}
