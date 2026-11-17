#include "chimera/vm/core_window_vm.hpp"

#include <cstdlib>

#include "chimera/vm/json.hpp"

namespace chimera::vm {

namespace {

/* a JSON value as a view model's: numbers as whole numbers where they are,
 * an array or an object as its JSON text (a window parses it) */
Value valueOf(const json::Json &j)
{
	switch (j.kind)
	{
	case json::Json::Kind::Bool: return j.boolean;
	case json::Json::Kind::Number:
		if (j.text.find_first_of(".eE") == std::string::npos)
		{
			char *end = nullptr;
			const long long v = std::strtoll(j.text.c_str(), &end, 10);
			if (end != nullptr && *end == '\0') return static_cast<int64_t>(v);
		}
		return j.number();
	case json::Json::Kind::String: return j.text;
	case json::Json::Kind::Array:
	case json::Json::Kind::Object: return json::write(j);
	default: return {};
	}
}

/* a view model's value as JSON, for a request */
std::string jsonOf(const Value &v)
{
	if (const bool *b = std::get_if<bool>(&v)) return *b ? "true" : "false";
	if (const int64_t *i = std::get_if<int64_t>(&v)) return std::to_string(*i);
	if (const double *d = std::get_if<double>(&v)) return json::write(json::Json::number(*d));
	if (const std::string *s = std::get_if<std::string>(&v)) return json::quote(*s);
	return "null";
}

} // namespace

Value CoreWindowViewModel::List::cell(int64_t row, int32_t column) const
{
	if (row < 0 || row >= rowCount() || column < 0) return {};
	const auto &r = cells[static_cast<size_t>(row)];
	return static_cast<size_t>(column) < r.size() ? r[static_cast<size_t>(column)] : Value{};
}

std::string CoreWindowViewModel::request(const std::string &op, const std::string &rest) const
{
	return "{\"op\":" + json::quote(op) + ",\"window\":" + json::quote(id_) + rest + "}";
}

std::string CoreWindowViewModel::getRequest() const { return request("get", ""); }

CoreWindowViewModel::CoreWindowViewModel(std::string id, const std::string &description, Dispatcher &dispatcher, Send send,
	SendFile sendFile)
	: ViewModel("coreWindow." + id, dispatcher), id_(std::move(id)), send_(std::move(send)), sendFile_(std::move(sendFile))
{
	expose("coreError", "what the core said of the last change or command it refused, \"\" for none", coreError);
	json::Json d;
	std::string err;
	if (!json::parse(description, d, err) || d.kind != json::Json::Kind::Object)
	{
		problem_ = tr("core_window.no_description", {id_});
		return;
	}
	if (const json::Json *e = d.find("error"))
	{
		problem_ = e->text;
		return;
	}
	if (const json::Json *ps = d.find("properties"))
		for (const json::Json &p : ps->items)
		{
			const json::Json *n = p.find("name");
			if (n == nullptr || n->text.empty() || n->text == "coreError" || n->text == "error") continue;
			Prop &prop = props_.emplace_back();
			prop.name = n->text;
			std::function<bool(const Value &)> set;
			if (const json::Json *w = p.find("writable"); w != nullptr && w->boolean)
				set = [this, name = prop.name](const Value &v) {
					send_(request("set", ",\"name\":" + json::quote(name) + ",\"value\":" + jsonOf(v)),
						[this](const std::string &answer) { answered(answer); });
					return true;
				};
			const std::string help = p.find("help") != nullptr ? p.find("help")->text : std::string();
			property(prop.name, help, [&prop] { return prop.value; }, std::move(set), prop.changed);
		}
	if (const json::Json *cs = d.find("commands"))
		for (const json::Json &c : cs->items)
		{
			const json::Json *n = c.find("name");
			if (n == nullptr || n->text.empty()) continue;
			std::vector<std::string> params;
			if (const json::Json *ps = c.find("params"))
				for (const json::Json &p : ps->items) params.push_back(p.text);
			const std::string help = c.find("help") != nullptr ? c.find("help")->text : std::string();
			command(n->text, help, params, [this, name = n->text](const Args &args, std::string &) -> Value {
				std::string list;
				for (const Value &a : args) list += (list.empty() ? "" : ",") + jsonOf(a);
				send_(request("run", ",\"command\":" + json::quote(name) + ",\"args\":[" + list + "]"),
					[this](const std::string &answer) { answered(answer); });
				return {};
			});
		}
	if (const json::Json *ls = d.find("lists"))
		for (const json::Json &l : ls->items)
		{
			const json::Json *n = l.find("name");
			if (n == nullptr || n->text.empty()) continue;
			auto &[name, list] = lists_.emplace_back();
			name = n->text;
			if (const json::Json *cols = l.find("columns"))
				for (const json::Json &c : cols->items) list.columns.push_back(c.text);
			rows(name, "", list);
		}
}

void CoreWindowViewModel::giveFile(const std::string &purpose, bool write, const std::string &path)
{
	if (!sendFile_) return;
	/* its own name only: what is after the last separator of either kind */
	const size_t cut = path.find_last_of("/\\");
	const std::string name = cut == std::string::npos ? path : path.substr(cut + 1);
	sendFile_(request("file", ",\"purpose\":" + json::quote(purpose) + ",\"mode\":" + json::quote(write ? "write" : "read") +
				  ",\"file\":" + json::quote(kFileName) + ",\"name\":" + json::quote(name)),
		path, write, [this](const std::string &answer) { answered(answer); });
}

void CoreWindowViewModel::answered(const std::string &answer)
{
	json::Json a;
	std::string err;
	if (answer.empty() || !json::parse(answer, a, err))
	{
		coreError.set(tr("core_window.no_answer"));
		return;
	}
	const json::Json *e = a.find("error");
	coreError.set(e != nullptr ? (e->text.empty() ? tr("core_window.no_answer") : e->text) : std::string());
}

void CoreWindowViewModel::show(const std::string &answer)
{
	json::Json a;
	std::string err;
	if (answer.empty() || !json::parse(answer, a, err) || a.kind != json::Json::Kind::Object) return;
	if (const json::Json *ps = a.find("properties"))
		for (Prop &p : props_)
			if (const json::Json *v = ps->find(p.name))
			{
				Value now = valueOf(*v);
				if (now != p.value)
				{
					p.value = std::move(now);
					p.changed.emit();
				}
			}
	if (const json::Json *ls = a.find("lists"))
		for (auto &[name, list] : lists_)
			if (const json::Json *rowsNow = ls->find(name))
			{
				const int64_t before = list.rowCount();
				list.cells.clear();
				for (const json::Json &r : rowsNow->items)
				{
					std::vector<Value> row;
					for (const json::Json &c : r.items) row.push_back(valueOf(c));
					list.cells.push_back(std::move(row));
				}
				list.refreshed(before);
			}
}

} // namespace chimera::vm
