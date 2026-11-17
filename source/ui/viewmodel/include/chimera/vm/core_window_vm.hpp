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
#include <map>
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
	/* the same with a file the person picked in Chimera's own dialog, given
	 * to the core for that one request (mounted under kFileName, to read or
	 * to write): the core never opens a file of the computer on its own */
	using SendFile = std::function<void(std::string request, std::string path, int mode,
		std::function<void(const std::string &)> answered)>;
	/* a file kept for the window (a log): given to the core again after
	 * every frame to add to, until let go ("" for path) */
	using KeepFile = std::function<void(std::string purpose, std::string path, std::string name)>;
	/* how a file is given: to read, to write (emptied), or kept and added to */
	enum FileMode { Read = 0, Write = 1, Append = 2 };
	/* the name a file given to the core has in its sandbox */
	static constexpr const char *kFileName = "window-file";

	/* id: the window's, in the package; description: the core's answer to
	 * "describe" */
	CoreWindowViewModel(std::string id, const std::string &description, Dispatcher &dispatcher, Send send, SendFile sendFile = {},
		KeepFile keepFile = {});

	const std::string &id() const { return id_; }
	/* "" when the description could be used; else why not, said to the person */
	const std::string &problem() const { return problem_; }

	/* a picture's pixels, copied out of the core's memory where its answer said */
	struct Pixels
	{
		int32_t width = 0, height = 0;
		std::vector<uint32_t> pixels;
	};
	/* the core's answer to "get": what the window shows now, its pictures' pixels beside it */
	void show(const std::string &answer, const std::map<std::string, Pixels> &pictures = {});
	/* the pictures the core described, by name */
	std::vector<std::string> pictureNames() const;

	/* the request for what the window shows, as the session sends it */
	std::string getRequest() const;

	/* a file the person picked for the window (what the window asked it for:
	 * purpose, "read" or "write"), handed to the core: the core is told its
	 * own name, never where it is */
	void giveFile(const std::string &purpose, FileMode mode, const std::string &path);
	/* a file the person picked to be kept (a log): emptied and given to the
	 * core now, then again after every frame to add to, until let go */
	void keepFile(const std::string &purpose, const std::string &path);
	void releaseFile(const std::string &purpose);
	/* the request that gives a file: the core is told its name, never where it is */
	std::string fileRequest(const std::string &purpose, FileMode mode, const std::string &path) const;

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
		std::deque<std::vector<Value>> cells;
		int64_t rowCount() const override { return static_cast<int64_t>(cells.size()); }
		int32_t columnCount() const override { return static_cast<int32_t>(columns.size()); }
		std::string columnName(int32_t c) const override { return c >= 0 && c < columnCount() ? columns[static_cast<size_t>(c)] : ""; }
		Value cell(int64_t row, int32_t column) const override;
	};
	void answered(const std::string &answer);
	std::string request(const std::string &op, const std::string &rest) const;

	struct Pic
	{
		std::string name;
		Picture picture;
		Signal<> changed;
	};
	std::string id_, problem_;
	Send send_;
	SendFile sendFile_;
	KeepFile keepFile_;
	std::deque<Prop> props_;
	std::deque<Pic> pictures_;
	std::deque<std::pair<std::string, List>> lists_;
};

} // namespace chimera::vm
