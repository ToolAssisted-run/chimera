#include "chimera/vm/view_model.hpp"
#include "chimera/vm/tr.hpp"

namespace chimera::vm {

ViewModel::ViewModel(std::string name, Dispatcher &dispatcher)
	: dispatcher_(dispatcher), name_(std::move(name))
{
}

ViewModel::~ViewModel() = default;

std::optional<Value> ViewModel::get(std::string_view property) const
{
	for (const auto &p : properties_)
		if (p.name == property) return p.get();
	return std::nullopt;
}

bool ViewModel::set(std::string_view property, const Value &v, std::string &error)
{
	for (const auto &p : properties_)
	{
		if (p.name != property) continue;
		if (!p.set)
		{
			error = tr("view_model.read_only", {name_ + "." + p.name});
			return false;
		}
		if (!p.set(v))
		{
			error = tr("view_model.cannot_be", {name_ + "." + p.name, describe(v)});
			return false;
		}
		return true;
	}
	error = tr("view_model.no_property", {name_, std::string(property)});
	return false;
}

std::optional<Value> ViewModel::run(std::string_view command, const Args &args, std::string &error)
{
	for (const auto &c : commands_)
	{
		if (c.name != command) continue;
		if (c.enabled && !c.enabled->get())
		{
			error = tr("view_model.cannot_run", {name_ + "." + c.name});
			return std::nullopt;
		}
		error.clear();
		commandRunning.emit(c.name);
		Value v = c.run(args, error);
		if (!error.empty()) return std::nullopt;
		return v;
	}
	error = tr("view_model.no_command", {name_, std::string(command)});
	return std::nullopt;
}

void ViewModel::command(std::string name, std::string help, std::vector<std::string> params,
	std::function<Value(const Args &, std::string &)> run, Property<bool> *enabled)
{
	commands_.push_back({std::move(name), std::move(help), std::move(params), std::move(run), enabled});
}

void ViewModel::scriptSideProperty(std::string_view name, std::function<Value()> get)
{
	for (auto &p : properties_)
		if (p.name == name) p.scriptSide = std::move(get);
}

void ViewModel::scriptSideCommand(std::string_view name)
{
	for (auto &c : commands_)
		if (c.name == name) c.scriptSide = true;
}

void ViewModel::property(std::string name, std::string help, std::function<Value()> get,
	std::function<bool(const Value &)> set, Signal<> &changed)
{
	properties_.push_back({std::move(name), std::move(help), std::move(get), std::move(set), &changed});
}

void ViewModel::rows(std::string name, std::string help, RowModel &model)
{
	rows_.push_back({std::move(name), std::move(help), &model});
}

void ViewModel::picture(std::string name, std::string help, Picture &picture, Signal<> &changed)
{
	pictures_.push_back({std::move(name), std::move(help), &picture, &changed});
}

void ViewModel::later(std::function<void()> fn)
{
	std::weak_ptr<int> alive = alive_;
	dispatcher_.post([alive, fn = std::move(fn)] {
		if (alive.lock()) fn();
	});
}

} // namespace chimera::vm
