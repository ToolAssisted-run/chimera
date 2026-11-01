#include "chimera/vm/core_tool_vm.hpp"
#include "chimera/vm/tr.hpp"

#include <chimera/engine.h>

#include <algorithm>
#include <cctype>
#include <filesystem>

namespace chimera::vm {

namespace fs = std::filesystem;
using json::Json;

namespace {

std::string baseName(const std::string &path)
{
	const size_t cut = path.find_last_of("/\\");
	return cut == std::string::npos ? path : path.substr(cut + 1);
}

std::string lower(std::string s)
{
	for (char &c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
	return s;
}

std::string textOf(const Json &j, const char *key)
{
	const Json *v = j.find(key);
	return v != nullptr ? v->text : std::string();
}

bool flagOf(const Json &j, const char *key)
{
	const Json *v = j.find(key);
	return v != nullptr && v->boolean;
}

/* "*.nsz *.xcz" for a file dialog, from a declaration's "extensions" */
std::string filterOf(const Json &j)
{
	std::string filter;
	if (const Json *ex = j.find("extensions"))
		for (const Json &e : ex->items)
			filter += (filter.empty() ? "" : " ") + std::string("*.") + lower(!e.text.empty() && e.text[0] == '.' ? e.text.substr(1) : e.text);
	return filter.empty() ? std::string("*") : filter;
}

/* the menu item's words as a window's title: "Decompress NSZ..." is
 * "Decompress NSZ", and a mnemonic's & is not shown */
std::string titleOf(std::string menu)
{
	menu.erase(std::remove(menu.begin(), menu.end(), '&'), menu.end());
	while (!menu.empty() && (menu.back() == '.' || std::isspace(static_cast<unsigned char>(menu.back())))) menu.pop_back();
	return menu;
}

} // namespace

CoreToolViewModel::CoreToolViewModel(Dispatcher &dispatcher, CoresViewModel &cores)
	: ViewModel("coreTool", dispatcher), cores_(cores)
{
	expose("core", "the core whose tool is open", core);
	expose("id", "the tool open, by the id its package gives it (\"\" when none is)", id);
	expose("title", "the tool's name, its core's and its menu item's words (\"Eden: Decompress NSZ\")", title);
	expose("description", "what the tool does, in its package's words", description);
	expose("running", "the tool is running", running);
	expose("result", "what the core said when the last run was done", result);
	expose("failure", "why the last run did not happen, or what the core refused (\"\" when it went)", failure);
	expose("runs", "goes up with each run that ended, done or not", runs);
	rows("inputs", "the files the tool reads: name, label, filter, required, more than one, the files picked (their names, a line each)", inputRows_);
	rows("options", "the tool's options: name, label, description, ticked", optionRows_);
	rows("outputs", "the files the tool writes: name, label, extension, where it is written", outputRows_);
	command("open", "an installed core's tool, by the core's name and the tool's id: the form starts empty", {"core", "id"},
		[this](const Args &a, std::string &error) -> Value {
			std::string coreName, toolId;
			if (!arg(a, 0, coreName, error) || !arg(a, 1, toolId, error)) return {};
			open(coreName, toolId, error);
			return {};
		});
	command("addFile", "a file the tool reads, for one of its inputs (an input that takes one has it replaced)", {"input", "path"},
		[this](const Args &a, std::string &error) -> Value {
			std::string input, path;
			if (!arg(a, 0, input, error) || !arg(a, 1, path, error)) return {};
			const Json *in = entry("inputs", input);
			std::error_code ec;
			if (in == nullptr || !fs::is_regular_file(path, ec))
			{
				error = in == nullptr ? tr("core_tool.no_input", {input}) : tr("new_project.not_a_file", {path});
				return {};
			}
			std::vector<std::string> &files = picked_[input];
			if (!flagOf(*in, "multiple")) files.clear();
			/* the core reads several by their own names: two of one name cannot both be */
			const std::string name = lower(baseName(path));
			for (const auto &[other, list] : picked_)
				for (const std::string &p : list)
					if (lower(baseName(p)) == name)
					{
						inputRows_.reset.emit();
						error = tr("new_project.two_files_named", {baseName(path)});
						return {};
					}
			files.push_back(path);
			suggestOutputs(input);
			inputRows_.reset.emit();
			return {};
		});
	command("removeFile", "takes a file out of an input", {"input", "index"}, [this](const Args &a, std::string &error) -> Value {
		std::string input;
		int64_t i = 0;
		if (!arg(a, 0, input, error) || !arg(a, 1, i, error)) return {};
		auto it = picked_.find(input);
		if (it == picked_.end() || i < 0 || i >= static_cast<int64_t>(it->second.size()))
		{
			error = tr("session.no_such_file");
			return {};
		}
		it->second.erase(it->second.begin() + static_cast<long>(i));
		inputRows_.reset.emit();
		return {};
	});
	command("moveFile", "moves a file within an input that takes several (Up and Down): they are read in this order", {"input", "from", "to"},
		[this](const Args &a, std::string &error) -> Value {
			std::string input;
			int64_t from = 0, to = 0;
			if (!arg(a, 0, input, error) || !arg(a, 1, from, error) || !arg(a, 2, to, error)) return {};
			auto it = picked_.find(input);
			const int64_t n = it == picked_.end() ? 0 : static_cast<int64_t>(it->second.size());
			if (from < 0 || from >= n || to < 0 || to >= n)
			{
				error = tr("session.no_such_file");
				return {};
			}
			std::string moved = it->second[static_cast<size_t>(from)];
			it->second.erase(it->second.begin() + static_cast<long>(from));
			it->second.insert(it->second.begin() + static_cast<long>(to), moved);
			inputRows_.reset.emit();
			return {};
		});
	command("setOption", "ticks one of the tool's options, or not", {"name", "on"}, [this](const Args &a, std::string &error) -> Value {
		std::string name;
		bool on = false;
		if (!arg(a, 0, name, error) || !arg(a, 1, on, error)) return {};
		if (entry("options", name) == nullptr)
		{
			error = tr("core_tool.no_option", {name});
			return {};
		}
		options_[name] = on;
		optionRows_.reset.emit();
		return {};
	});
	command("setOutput", "where a file the tool writes goes (it is made, or replaced)", {"output", "path"},
		[this](const Args &a, std::string &error) -> Value {
			std::string output, path;
			if (!arg(a, 0, output, error) || !arg(a, 1, path, error)) return {};
			if (entry("outputs", output) == nullptr)
			{
				error = tr("core_tool.no_output", {output});
				return {};
			}
			outputs_[output] = path;
			suggested_[output] = false;
			outputRows_.reset.emit();
			return {};
		});
	command("run", "runs the tool on the files picked; its answer, when it comes, is the result or the failure", {},
		[this](const Args &, std::string &error) -> Value {
			if (tool_.kind != Json::Kind::Object)
			{
				error = tr("core_tool.no_tool", {id.get()});
				return {};
			}
			if (running.get()) return {};
			/* what is missing, in the tool's own words */
			std::vector<File> inputs, outputs;
			if (const Json *list = this->list("inputs"))
				for (const Json &in : list->items)
				{
					const std::string name = textOf(in, "name");
					const auto it = picked_.find(name);
					const bool none = it == picked_.end() || it->second.empty();
					if (none && flagOf(in, "required"))
					{
						const std::string label = textOf(in, "label");
						error = tr("core_tool.choose_input", {label.empty() ? name : label});
						failure.set(error);
						return {};
					}
					if (none) continue;
					/* several are read as name.0, name.1, ... in the order picked */
					if (flagOf(in, "multiple"))
						for (size_t i = 0; i < it->second.size(); i++) inputs.push_back({name + "." + std::to_string(i), it->second[i]});
					else
						inputs.push_back({name, it->second.front()});
				}
			if (const Json *list = this->list("outputs"))
				for (const Json &out : list->items)
				{
					const std::string name = textOf(out, "name");
					const auto it = outputs_.find(name);
					if (it == outputs_.end() || it->second.empty())
					{
						const std::string label = textOf(out, "label");
						error = tr("core_tool.choose_output", {label.empty() ? name : label});
						failure.set(error);
						return {};
					}
					outputs.push_back({name, it->second});
				}
			Json options;
			options.kind = Json::Kind::Object;
			for (const auto &[name, on] : options_) options.members.emplace_back(name, Json::boolean_(on));
			const std::string optionsText = json::write(options);

			result.set("");
			failure.set("");
			running.set(true);
			/* an answer that comes after the run was let go (the window closed,
			 * another tool opened) is not taken */
			const uint64_t ticket = ++ticket_;
			auto done = [this, ticket](std::string answer, std::string why) {
				if (ticket != ticket_) return;
				stop_ = nullptr;
				finish(answer, why);
			};
			if (runner_)
			{
				stop_ = runner_(package_, id.get(), inputs, outputs, optionsText, done);
				return {};
			}
			/* no process to run it in (a test): the engine, here */
			std::vector<const char *> inNames, inPaths, outNames, outPaths;
			for (const File &f : inputs)
			{
				inNames.push_back(f.name.c_str());
				inPaths.push_back(f.path.c_str());
			}
			for (const File &f : outputs)
			{
				outNames.push_back(f.name.c_str());
				outPaths.push_back(f.path.c_str());
			}
			const char *why = nullptr;
			uint64_t len = 0;
			const char *answer = ce_package_run_tool(package_.c_str(), id.get().c_str(), inNames.data(), inPaths.data(),
				static_cast<int32_t>(inNames.size()), outNames.data(), outPaths.data(), static_cast<int32_t>(outNames.size()),
				optionsText.c_str(), &len, &why);
			done(answer != nullptr ? std::string(answer, static_cast<size_t>(len)) : std::string(), why != nullptr ? why : "");
			return {};
		});
	command("cancel", "stops the tool running; what it wrote is left as it was", {}, [this](const Args &, std::string &) -> Value {
		++ticket_;
		if (stop_) stop_();
		stop_ = nullptr;
		running.set(false);
		return {};
	});
}

const Json *CoreToolViewModel::list(const char *what) const
{
	const Json *l = tool_.find(what);
	return l != nullptr && l->kind == Json::Kind::Array ? l : nullptr;
}

const Json *CoreToolViewModel::entry(const char *what, const std::string &name) const
{
	if (const Json *l = list(what))
		for (const Json &e : l->items)
			if (textOf(e, "name") == name) return &e;
	return nullptr;
}

bool CoreToolViewModel::open(const std::string &coreName, const std::string &toolId, std::string &error)
{
	++ticket_;
	if (stop_) stop_();
	stop_ = nullptr;
	Json cores;
	std::string jerr;
	json::parse(cores_.coreTools.get(), cores, jerr);
	const Json *found = nullptr;
	package_.clear();
	for (const Json &c : cores.items)
	{
		if (textOf(c, "core") != coreName) continue;
		if (const Json *tools = c.find("tools"))
			for (const Json &t : tools->items)
				if (textOf(t, "id") == toolId)
				{
					found = &t;
					package_ = textOf(c, "package");
				}
	}
	tool_ = found != nullptr ? *found : Json();
	picked_.clear();
	options_.clear();
	outputs_.clear();
	suggested_.clear();
	running.set(false);
	result.set("");
	failure.set("");
	if (found == nullptr)
	{
		core.set("");
		id.set("");
		title.set("");
		description.set("");
		error = tr("core_tool.no_tool", {coreName, toolId});
	}
	else
	{
		core.set(coreName);
		id.set(toolId);
		title.set(coreName + ": " + titleOf(textOf(*found, "menu")));
		description.set(textOf(*found, "description"));
		/* an option starts as the package says, else not ticked */
		if (const Json *opts = list("options"))
			for (const Json &o : opts->items) options_[textOf(o, "name")] = flagOf(o, "default");
	}
	inputRows_.reset.emit();
	optionRows_.reset.emit();
	outputRows_.reset.emit();
	return found != nullptr;
}

void CoreToolViewModel::suggestOutputs(const std::string &input)
{
	const auto it = picked_.find(input);
	if (it == picked_.end() || it->second.empty()) return;
	const std::string &first = it->second.front();
	const fs::path from(std::u8string(first.begin(), first.end()));
	bool changed = false;
	if (const Json *outs = list("outputs"))
		for (const Json &out : outs->items)
		{
			const std::string name = textOf(out, "name");
			if (textOf(out, "nameFrom") != input) continue;
			/* a place the person chose stays theirs */
			const auto had = outputs_.find(name);
			if (had != outputs_.end() && !had->second.empty() && !suggested_[name]) continue;
			std::string ext = textOf(out, "extension");
			if (!ext.empty() && ext[0] == '.') ext = ext.substr(1);
			const std::u8string to = (from.parent_path() / from.stem()).u8string();
			outputs_[name] = std::string(to.begin(), to.end()) + (ext.empty() ? std::string() : "." + ext);
			suggested_[name] = true;
			changed = true;
		}
	if (changed) outputRows_.reset.emit();
}

void CoreToolViewModel::finish(const std::string &answer, const std::string &why)
{
	running.set(false);
	Json a;
	std::string jerr;
	if (answer.empty())
		failure.set(why.empty() ? tr("core_tool.no_answer") : why);
	else if (!json::parse(answer, a, jerr) || a.kind != Json::Kind::Object)
		failure.set(tr("core_tool.answer_unreadable", {jerr.empty() ? answer.substr(0, 80) : jerr}));
	else if (const Json *refused = a.find("refused"))
		failure.set(refused->text.empty() ? tr("core_tool.no_answer") : refused->text);
	else if (const Json *done = a.find("done"))
		result.set(done->text);
	else
		failure.set(tr("core_tool.answer_unreadable", {answer.substr(0, 80)}));
	runs.set(runs.get() + 1);
}

int64_t CoreToolViewModel::InputRows::rowCount() const
{
	const Json *l = vm_.list("inputs");
	return l != nullptr ? static_cast<int64_t>(l->items.size()) : 0;
}

std::string CoreToolViewModel::InputRows::columnName(int32_t column) const
{
	static const char *const names[] = {"name", "label", "filter", "required", "multiple", "files"};
	return column >= 0 && column < 6 ? names[column] : "";
}

Value CoreToolViewModel::InputRows::cell(int64_t row, int32_t column) const
{
	if (row < 0 || row >= rowCount()) return {};
	const Json &in = vm_.list("inputs")->items[static_cast<size_t>(row)];
	switch (column)
	{
	case 0: return textOf(in, "name");
	case 1: return textOf(in, "label");
	case 2: return filterOf(in);
	case 3: return flagOf(in, "required");
	case 4: return flagOf(in, "multiple");
	case 5:
	{
		std::string names;
		const auto it = vm_.picked_.find(textOf(in, "name"));
		if (it != vm_.picked_.end())
			for (const std::string &p : it->second) names += (names.empty() ? "" : "\n") + baseName(p);
		return names;
	}
	default: return {};
	}
}

int64_t CoreToolViewModel::OptionRows::rowCount() const
{
	const Json *l = vm_.list("options");
	return l != nullptr ? static_cast<int64_t>(l->items.size()) : 0;
}

std::string CoreToolViewModel::OptionRows::columnName(int32_t column) const
{
	static const char *const names[] = {"name", "label", "help", "on"};
	return column >= 0 && column < 4 ? names[column] : "";
}

Value CoreToolViewModel::OptionRows::cell(int64_t row, int32_t column) const
{
	if (row < 0 || row >= rowCount()) return {};
	const Json &o = vm_.list("options")->items[static_cast<size_t>(row)];
	const std::string name = textOf(o, "name"), label = textOf(o, "display");
	switch (column)
	{
	case 0: return name;
	case 1: return label.empty() ? name : label;
	case 2: return textOf(o, "description");
	case 3:
	{
		const auto it = vm_.options_.find(name);
		return it != vm_.options_.end() && it->second;
	}
	default: return {};
	}
}

int64_t CoreToolViewModel::OutputRows::rowCount() const
{
	const Json *l = vm_.list("outputs");
	return l != nullptr ? static_cast<int64_t>(l->items.size()) : 0;
}

std::string CoreToolViewModel::OutputRows::columnName(int32_t column) const
{
	static const char *const names[] = {"name", "label", "extension", "path"};
	return column >= 0 && column < 4 ? names[column] : "";
}

Value CoreToolViewModel::OutputRows::cell(int64_t row, int32_t column) const
{
	if (row < 0 || row >= rowCount()) return {};
	const Json &out = vm_.list("outputs")->items[static_cast<size_t>(row)];
	const std::string name = textOf(out, "name");
	switch (column)
	{
	case 0: return name;
	case 1: return textOf(out, "label");
	case 2:
	{
		std::string ext = textOf(out, "extension");
		return !ext.empty() && ext[0] == '.' ? ext.substr(1) : ext;
	}
	case 3:
	{
		const auto it = vm_.outputs_.find(name);
		return it != vm_.outputs_.end() ? it->second : std::string();
	}
	default: return {};
	}
}

} // namespace chimera::vm
