#pragma once
/* View models of Chimera's scripts' libraries: what the gui library draws
 * over the picture, and the registers Lua's emu library reads and sets (the
 * core's own exports). A core's tools are its own windows (issue 610):
 * Chimera has no Trace Logger or CPU Registers window of its own. */

#include <cstdio>
#include <deque>
#include <string>

#include "chimera/vm/session_vm.hpp"
#include "chimera/vm/view_model.hpp"

namespace chimera::vm {

/* What Chimera's Lua gui library draws over the picture: the drawings of
 * the frame, as JSON the window draws (in the machine's pixels; "screen"
 * text in the window's), and messages for the window to show. */
class GuiOverlayViewModel : public ViewModel
{
public:
	explicit GuiOverlayViewModel(Dispatcher &dispatcher);
	/* [{"k": "rect"|"line"|"ellipse"|"text"|"screentext", ...}] */
	Property<std::string> shapes{"[]"};
	/* the last message a script asked to show (said each time it is set) */
	Property<std::string> message;
};

class RegistersViewModel : public ViewModel
{
public:
	RegistersViewModel(Dispatcher &dispatcher, SessionViewModel &session);

	/* the machine's cycles run, when the core counts them */
	Property<int64_t> cycles{0};
	Property<bool> hasCycles{false};
	const std::vector<SessionViewModel::Register> &registers() const { return registers_; }

private:
	class Rows : public RowModel
	{
	public:
		explicit Rows(RegistersViewModel &vm) : vm_(vm) {}
		int64_t rowCount() const override { return static_cast<int64_t>(vm_.registers_.size()); }
		int32_t columnCount() const override { return 4; }
		std::string columnName(int32_t column) const override;
		Value cell(int64_t row, int32_t column) const override;

	private:
		RegistersViewModel &vm_;
	};
	void refresh();
	SessionViewModel &session_;
	std::vector<SessionViewModel::Register> registers_;
	Rows rows_{*this};
};

} // namespace chimera::vm
