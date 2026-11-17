#include "chimera/vm/script_vm.hpp"
#include "chimera/vm/tr.hpp"

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>

#include "chimera/vm/watch_values.hpp"

namespace chimera::vm {

GuiOverlayViewModel::GuiOverlayViewModel(Dispatcher &dispatcher) : ViewModel("gui", dispatcher)
{
	expose("shapes", "what scripts drew over the picture this frame, as JSON: rect, line, ellipse and text in the "
		"machine's pixels, screentext in the window's", shapes, true);
	expose("message", "the last message a script asked to show", message);
	command("addMessage", "a message shown over the picture a moment, as the window's own are", {"text"},
		[this](const Args &a, std::string &error) -> Value {
			std::string text;
			if (!arg(a, 0, text, error)) return {};
			/* said again even when it is the same as the last */
			message.set("");
			message.set(text);
			return {};
		});
}

RegistersViewModel::RegistersViewModel(Dispatcher &dispatcher, SessionViewModel &session) : ViewModel("registers", dispatcher), session_(session)
{
	expose("cycles", "the machine's cycles run, when the core counts them", cycles);
	expose("hasCycles", "the core counts the cycles it runs", hasCycles);
	rows("registers", "the core's registers: name, value, hex (as many digits as the core says), bits", rows_);
	command("refresh", "the registers read again", {}, [this](const Args &, std::string &) -> Value {
		refresh();
		return static_cast<int64_t>(registers_.size());
	});
	command("registerList", "the registers now, a line each: name=value", {}, [this](const Args &, std::string &) -> Value {
		refresh();
		std::string out;
		for (const SessionViewModel::Register &r : registers_) out += r.name + "=" + std::to_string(r.value) + "\n";
		return out;
	});
	command("roleRegister", "the register the core names for a role (pc, sp or flags), \"\" when it names none", {"role"},
		[this](const Args &a, std::string &error) -> Value {
			std::string role;
			if (!arg(a, 0, role, error)) return {};
			return session_.registerForRole(role);
		});
	command("setRegisterNamed", "a register set by its name", {"name", "value"}, [this](const Args &a, std::string &error) -> Value {
		std::string name;
		int64_t value = 0;
		if (!arg(a, 0, name, error) || !arg(a, 1, value, error)) return {};
		refresh();
		for (size_t i = 0; i < registers_.size(); i++)
			if (registers_[i].name == name)
			{
				if (!session_.writeRegister(static_cast<int32_t>(i), value))
				{
					error = tr("trace.not_taken", {name});
					return {};
				}
				refresh();
				return {};
			}
		error = tr("trace.no_register", {name});
		return {};
	});
	command("setRegister", "a register set, by its place in the list: a number, or hex with 0x", {"index", "value"},
		[this](const Args &a, std::string &error) -> Value {
			int64_t index = 0;
			if (!arg(a, 0, index, error)) return {};
			/* a number, or its text ("0x1F", "31") */
			int64_t value = 0;
			std::string text;
			if (a.size() < 2)
			{
				error = tr("trace.no_value");
				return {};
			}
			if (!fromValue(a[1], value))
			{
				if (!fromValue(a[1], text))
				{
					error = tr("trace.value_number");
					return {};
				}
				try
				{
					size_t used = 0;
					value = static_cast<int64_t>(std::stoull(text, &used, 0));
					if (used != text.size()) throw std::invalid_argument(text);
				}
				catch (...)
				{
					error = tr("trace.not_number", {text});
					return {};
				}
			}
			if (!session_.writeRegister(static_cast<int32_t>(index), value))
			{
				error = tr("trace.register_not_taken");
				return {};
			}
			refresh();
			return {};
		}, &session_.hasRegisters);
}

void RegistersViewModel::refresh()
{
	int64_t c = 0;
	bool counted = false;
	registers_ = session_.readRegisters(c, counted);
	cycles.set(c);
	hasCycles.set(counted);
	rows_.reset.emit();
}

std::string RegistersViewModel::Rows::columnName(int32_t column) const
{
	static const char *const names[] = {"name", "value", "hex", "bits"};
	return column >= 0 && column < 4 ? names[column] : "";
}

Value RegistersViewModel::Rows::cell(int64_t row, int32_t column) const
{
	if (row < 0 || row >= rowCount()) return {};
	const SessionViewModel::Register &r = vm_.registers_[static_cast<size_t>(row)];
	switch (column)
	{
	case 0: return r.name;
	case 1: return r.value;
	case 2:
	{
		const int digits = std::clamp((r.bits + 3) / 4, 1, 16);
		char text[24];
		std::snprintf(text, sizeof text, "%0*llX", digits, static_cast<unsigned long long>(r.value) & (digits >= 16 ? ~0ull : ((1ull << (digits * 4)) - 1)));
		return std::string(text);
	}
	case 3: return static_cast<int64_t>(r.bits);
	default: return {};
	}
}

} // namespace chimera::vm
