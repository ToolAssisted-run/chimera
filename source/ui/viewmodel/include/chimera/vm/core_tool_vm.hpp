#pragma once
/* A core's own tool (Tools > Core-Specific Tools, owner 2026-11-01): a job
 * the core offers whose result is files - a compressed game decompressed, a
 * demo made into a project - offered for every installed core, whatever is
 * open. The core's package declares each ("tools" in its waterbox.config:
 * the menu item, what it does, the files it reads, its options and the files
 * it writes), and this form is drawn from that: the window knows nothing of
 * what any tool does. The core runs it in a process of its own, writing its
 * files straight where the person chose. */

#include "chimera/vm/cores_vm.hpp"
#include "chimera/vm/json.hpp"
#include "chimera/vm/rows.hpp"
#include "chimera/vm/view_model.hpp"

#include <functional>
#include <map>
#include <string>
#include <vector>

namespace chimera::vm {

class CoreToolViewModel : public ViewModel
{
public:
	CoreToolViewModel(Dispatcher &dispatcher, CoresViewModel &cores);

	/* the tool open: its core, its id, its name (the core's and the menu
	 * item's words) and what it says it does */
	Property<std::string> core, id, title, description;
	/* a run: going on; what the core said when it was done, or why it did not
	 * run or refused (in the error colour); a count that goes up with each
	 * run that ended, either way */
	Property<bool> running{false};
	Property<std::string> result, failure;
	Property<int64_t> runs{0};

	/* A file the tool reads or writes: its mount name and where it is. */
	struct File
	{
		std::string name, path;
	};
	/* Runs a tool away from the machine (a process holds one): the package,
	 * the tool, the files it reads and writes, its options as JSON; done gets
	 * the core's JSON answer or why there is none, on the view's thread. It
	 * hands back what stops the run. Without one the engine is asked here. */
	using Runner = std::function<std::function<void()>(const std::string &package, const std::string &tool,
		const std::vector<File> &inputs, const std::vector<File> &outputs, const std::string &options,
		std::function<void(std::string answer, std::string error)> done)>;
	void setRunner(Runner runner) { runner_ = std::move(runner); }

private:
	class InputRows : public RowModel
	{
	public:
		explicit InputRows(CoreToolViewModel &vm) : vm_(vm) {}
		int64_t rowCount() const override;
		int32_t columnCount() const override { return 6; }
		std::string columnName(int32_t column) const override;
		Value cell(int64_t row, int32_t column) const override;

	private:
		CoreToolViewModel &vm_;
	};
	class OptionRows : public RowModel
	{
	public:
		explicit OptionRows(CoreToolViewModel &vm) : vm_(vm) {}
		int64_t rowCount() const override;
		int32_t columnCount() const override { return 4; }
		std::string columnName(int32_t column) const override;
		Value cell(int64_t row, int32_t column) const override;

	private:
		CoreToolViewModel &vm_;
	};
	class OutputRows : public RowModel
	{
	public:
		explicit OutputRows(CoreToolViewModel &vm) : vm_(vm) {}
		int64_t rowCount() const override;
		int32_t columnCount() const override { return 4; }
		std::string columnName(int32_t column) const override;
		Value cell(int64_t row, int32_t column) const override;

	private:
		CoreToolViewModel &vm_;
	};

	const json::Json *list(const char *what) const;
	const json::Json *entry(const char *what, const std::string &name) const;
	bool open(const std::string &coreName, const std::string &toolId, std::string &error);
	/* the files the outputs are named from, suggested beside the input */
	void suggestOutputs(const std::string &input);
	void finish(const std::string &answer, const std::string &why);

	CoresViewModel &cores_;
	json::Json tool_;
	std::string package_;
	std::map<std::string, std::vector<std::string>> picked_;
	std::map<std::string, bool> options_;
	std::map<std::string, std::string> outputs_;
	/* the outputs the window named itself, which a later input may rename */
	std::map<std::string, bool> suggested_;
	uint64_t ticket_ = 0;
	std::function<void()> stop_;
	Runner runner_;
	InputRows inputRows_{*this};
	OptionRows optionRows_{*this};
	OutputRows outputRows_{*this};
};

} // namespace chimera::vm
