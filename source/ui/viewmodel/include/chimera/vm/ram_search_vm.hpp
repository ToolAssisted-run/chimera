#pragma once
/* RAM Search, as Chimera's: narrow a memory domain down to the addresses
 * that behave as a value in the game does. The search itself is the
 * engine's (candidates, previous values, change counts, undo, sorting, what
 * the next search would remove); this keeps it on the machine's thread,
 * updated after every frame, and lists the candidates as rows. Its settings
 * are Chimera's: what a search compares with and how, Detailed or Fast, the
 * size, byte order and display, what "previous" means, Auto-Search, the
 * preview, and lists of addresses kept in .wch files. */

#include <atomic>
#include <cstdint>
#include <functional>
#include <mutex>
#include <utility>
#include <vector>

#include "chimera/vm/session_vm.hpp"
#include "chimera/vm/view_model.hpp"
#include "chimera/vm/watch_values.hpp"

struct ce_ramsearch;

namespace chimera::vm {

class RamWatchViewModel;

class RamSearchViewModel : public ViewModel
{
public:
	RamSearchViewModel(Dispatcher &dispatcher, SessionViewModel &session);
	~RamSearchViewModel() override;
	/* RAM Watch: where Add to RAM Watch puts addresses, and whose watches
	 * Always Exclude RAM Watch List takes out of a search */
	void setRamWatch(RamWatchViewModel *watch) { ramWatch_ = watch; }

	/* the machine's memory domains, comma-separated; the one the next
	 * search is of, and the one the open search is of */
	Property<std::string> domains;
	Property<std::string> domain;
	Property<std::string> searchedDomain;
	/* bytes a value takes: 1, 2 or 4 (the open search is read again at a new size) */
	Property<int64_t> size{1};
	Property<bool> bigEndian{false};
	/* the ways a value of this size can be shown (a line each), and the one
	 * chosen, by its place there: values are shown, and typed, that way */
	Property<std::string> displays;
	Property<int64_t> display{0};
	/* Mode: Fast keeps no change counts (what a large domain needs) */
	Property<bool> fast{false};
	Property<bool> checkMisaligned{false};
	/* what the previous value is: 0 the first one, 1 the one at the last
	 * search, 2 the last frame's, 3 the one before the last change */
	Property<int64_t> previousType{1};
	/* Compare To / By: 0 the previous value, 1 a specific value, 2 a
	 * specific address, 3 specific changes, 4 a difference; each with its
	 * own box, typed as values are shown (the address in hex, the changes
	 * in decimal) */
	Property<int64_t> compare{0};
	Property<std::string> specificValue, specificAddress, specificChanges, difference;
	/* the Comparison Operator: 0 ==, 1 >, 2 >=, 3 <, 4 <=, 5 !=, 6 different by */
	Property<int64_t> op{0};
	Property<std::string> differentBy;
	/* candidates left, as a number and as Chimera writes it ("1,024 addresses") */
	Property<int64_t> count{0};
	Property<std::string> totalText{"0 addresses"};
	Property<bool> canUndo{false};
	Property<bool> canRedo{false};
	Property<bool> searching{false};
	/* what the search last had to say (Chimera's message line) */
	Property<std::string> status;
	/* a setting that needs a new search was changed (the reboot button) */
	Property<bool> rebootNeeded{false};
	/* candidates past the domain's end (the warning button) */
	Property<int64_t> outOfRange{0};
	/* rows picked, and whether they can be poked */
	Property<int64_t> selectedCount{0};
	Property<bool> canPoke{false};
	/* every row picked is frozen (Freeze Address reads Unfreeze Address; not
	 * asked past a thousand rows, as Chimera's), and any address is frozen */
	Property<bool> selectionFrozen{false};
	Property<bool> anyFrozen{false};
	/* the .wch file last opened or saved */
	Property<std::string> currentFile;
	/* the window is open: a search starts when it opens with none open, and
	 * again with each machine opened while it is, as Chimera's (issue 456) */
	Property<bool> windowOpen{false};

	/* Settings, remembered (Auto-Search is not) */
	Property<bool> previewMode{true};
	Property<bool> autoSearch{false};
	Property<bool> autoSearchAccountForLag{true};
	Property<bool> alwaysExcludeRamWatch{false};
	Property<bool> useUndoHistory{true};
	Property<std::string> recentFiles;
	/* the columns not shown, by Chimera's names (Address, Value, Prev, Changes, Diff) */
	Property<std::string> hiddenColumns{"Diff"};
	/* the columns a person made wider or narrower, "Value:100", remembered (issue 523) */
	Property<std::string> columnWidths;

private:
	/* then: run on the window's thread once Mode is settled */
	void fastIfLarge(std::function<void()> then = {});
	class Results : public RowModel
	{
	public:
		explicit Results(RamSearchViewModel &vm) : vm_(vm) {}
		int64_t rowCount() const override { return vm_.rows_; }
		int32_t columnCount() const override { return 9; }
		std::string columnName(int32_t column) const override;
		Value cell(int64_t row, int32_t column) const override;

	private:
		RamSearchViewModel &vm_;
	};

	/* what a search compares with and how, as the engine takes it */
	struct Params
	{
		int32_t compare = 0, op = 0, display = 0;
		uint64_t value = 0, differentBy = 0;
		int32_t previousType = 1;
		/* the boxes read: a search can be made */
		bool ok = true;
	};
	/* the window's settings read into Params; false with why */
	bool params(Params &out, std::string &error) const;
	void paramsChanged();

	/* rows picked: first..last ranges, in order */
	std::vector<std::pair<int64_t, int64_t>> picked_;
	int64_t anchor_ = -1;
	bool isPicked(int64_t row) const;
	void pick(int64_t first, int64_t last);
	void unpick(int64_t row);
	void pickedChanged();
	std::vector<int64_t> pickedRows(size_t most = SIZE_MAX) const;

	/* on the machine's thread */
	void startOnMachine(ce_session *s, const std::string &domain, int32_t size, bool bigEndian, bool misaligned, bool detailed,
		int32_t previous, bool undo, const std::vector<uint64_t> &exclude);
	void searchOnMachine(const Params &p);
	void afterFrameOnMachine(ce_session *s);
	void dropOnMachine();
	void reportOnMachine(const std::string &text, bool keepPicks = false);
	/* a job on the open search, on the machine's thread; what it says is reported */
	void onSearch(std::function<std::string(ce_ramsearch *)> job, bool keepPicks = false);
	/* the RAM Watch list's addresses in the searched domain */
	std::vector<uint64_t> watchedAddresses() const;

	Value shown(uint64_t raw) const;
	/* the chosen way to show a value of the search's size */
	watch::Display way() const;
	void addRecent(const std::string &file);
	std::string addressText(uint64_t address) const;

	SessionViewModel &session_;
	RamWatchViewModel *ramWatch_ = nullptr;
	Results results_{*this};
	int hooks_ = 0;
	Connection freezesWatch_;
	void freezesShown();
	Connection opensWatch_, sizeWatch_, bigEndianWatch_, displayWatch_, previousWatch_, domainWatch_, fastWatch_, misalignedWatch_,
		undoWatch_, autoWatch_, lagWatch_, excludeWatch_, windowOpenWatch_;
	/* a new search of the domain picked, when a machine has memory to search */
	void startWhileOpen();
	std::vector<Connection> paramWatches_;
	int64_t domainSize_ = 0;
	bool domainWritable_ = false;
	std::string sortedBy_;
	bool sortReverse_ = false;
	/* the way values were shown before the last change of size or display */
	watch::Display lastWay_ = watch::Display::Unsigned;
	/* the preview is not shown between a search and the next frame, as Chimera's */
	bool previewCleared_ = false;
	/* what the preview compares with (the view's), and Auto-Search (the machine's) */
	Params preview_;
	std::mutex paramsMutex_;
	Params paramsM_;
	std::atomic<bool> autoM_{false}, lagAwareM_{true};

	/* the search, the machine's thread's; the window reads rows under rsM_ */
	mutable std::mutex rsM_;
	ce_ramsearch *rs_ = nullptr;
	int64_t rows_ = 0;
	std::atomic<bool> refreshPosted_{false};
	/* a search of a domain with no linear backing (a bus): the window's
	 * thread reads its pages from copies the machine's thread makes */
	std::atomic<bool> pagesPosted_{false};
	bool pagedM_ = false; /* rs_'s, under rsM_ */
	void pagesAsked();
	void pagesOnMachine(ce_session *s);
};

} // namespace chimera::vm
