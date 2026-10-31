#pragma once
/* Chimera's Trace Logger (Tools): the core's own lines for each instruction
 * its CPU runs - what ran, then the machine around it after a tab - as the
 * core formats them; the window knows no instruction set. A frame can be
 * ten thousand lines, so the window keeps a bounded tail (capacity, 10,000
 * at first) and what is to be kept goes to a file as it comes. Tracing is
 * on only while logging: a core tracing for nobody pays for it. */

#include <cstdio>
#include <deque>
#include <string>

#include "chimera/vm/session_vm.hpp"
#include "chimera/vm/view_model.hpp"

namespace chimera::vm {

class TraceLoggerViewModel : public ViewModel
{
public:
	TraceLoggerViewModel(Dispatcher &dispatcher, SessionViewModel &session);
	~TraceLoggerViewModel() override;

	Property<bool> logging{false};
	Property<int64_t> capacity{10000};
	/* the file lines go to, "" for none, and how many went there */
	Property<std::string> file;
	Property<int64_t> linesToFile{0}, lineCount{0};
	Property<std::string> status;

	struct Line
	{
		std::string disassembly, registers;
	};
	const std::deque<Line> &lines() const { return lines_; }

private:
	class Rows : public RowModel
	{
	public:
		explicit Rows(TraceLoggerViewModel &vm) : vm_(vm) {}
		int64_t rowCount() const override { return static_cast<int64_t>(vm_.lines_.size()); }
		int32_t columnCount() const override { return 2; }
		std::string columnName(int32_t column) const override { return column == 0 ? "disassembly" : column == 1 ? "registers" : ""; }
		Value cell(int64_t row, int32_t column) const override;

	private:
		TraceLoggerViewModel &vm_;
	};
	void take(std::vector<std::string> lines);
	void trim();
	void sayStatus();
	void closeFile();
	SessionViewModel &session_;
	std::deque<Line> lines_;
	Rows rows_{*this};
	std::FILE *out_ = nullptr;
	Connection loggingWatch_, capacityWatch_, openWatch_;
};

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
