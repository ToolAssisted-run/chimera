#pragma once
/* A core's own window (issue 610, user-decided 2026-11-17): the core extends
 * Chimera's window with windows of its own, their design QML in its package
 * and the core itself their view model. This is that view model on Chimera's
 * side: what the core said the window has when it opened (its properties,
 * its commands, its lists), each as Chimera's own view models have them, so
 * the window binds to it through the one generic adapter. Chimera knows
 * nothing of what any of it means.
 *
 * Every read and change is the core's: a property's value is what the core
 * last answered, a change or a command is sent to the core, and what the
 * window shows is asked again after each frame, after each change, and while
 * the core holds its frame (docs/porting-a-core.md, "A core's own windows"). */

#include <deque>
#include <functional>
#include <string>
#include <vector>

#include "chimera/vm/view_model.hpp"

namespace chimera::vm {

class CoreWindowViewModel : public ViewModel
{
public:
	/* sends a request to the core on the machine's thread; answered is
	 * called on the view's thread with the core's answer ("" for none) */
	using Send = std::function<void(std::string request, std::function<void(const std::string &)> answered)>;

	/* id: the window's, in the package; description: the core's answer to
	 * "describe" */
	CoreWindowViewModel(std::string id, const std::string &description, Dispatcher &dispatcher, Send send);

	const std::string &id() const { return id_; }
	/* "" when the description could be used; else why not, said to the person */
	const std::string &problem() const { return problem_; }

	/* the core's answer to "get": what the window shows now */
	void show(const std::string &answer);

	/* the request for what the window shows, as the session sends it */
	std::string getRequest() const;

	/* the last error the core gave for a change or a command, "" for none */
	Property<std::string> coreError{""};

private:
	struct Prop
	{
		std::string name;
		Value value;
		Signal<> changed;
	};
	class List final : public RowModel
	{
	public:
		std::vector<std::string> columns;
		std::vector<std::vector<Value>> cells;
		int64_t rowCount() const override { return static_cast<int64_t>(cells.size()); }
		int32_t columnCount() const override { return static_cast<int32_t>(columns.size()); }
		std::string columnName(int32_t c) const override { return c >= 0 && c < columnCount() ? columns[static_cast<size_t>(c)] : ""; }
		Value cell(int64_t row, int32_t column) const override;
	};
	void answered(const std::string &answer);
	std::string request(const std::string &op, const std::string &rest) const;

	std::string id_, problem_;
	Send send_;
	std::deque<Prop> props_;
	std::deque<std::pair<std::string, List>> lists_;
};

} // namespace chimera::vm
