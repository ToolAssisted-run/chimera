#pragma once
/* RAM Watch: addresses a person keeps an eye on, each read after every
 * frame and shown as its kind of value, with the value before it and how
 * many times it changed. The list is kept in Chimera's .wch files. The
 * reading is the machine's thread's; the list, and what the window shows,
 * the view's. */

#include <atomic>
#include <cstdint>
#include <map>
#include <mutex>
#include <string>
#include <vector>

#include "chimera/vm/session_vm.hpp"
#include "chimera/vm/view_model.hpp"
#include "chimera/vm/watch_values.hpp"

namespace chimera::vm {

class RamWatchViewModel : public ViewModel
{
public:
	RamWatchViewModel(Dispatcher &dispatcher, SessionViewModel &session);
	~RamWatchViewModel() override;

	struct Watch
	{
		/* a separator is a line with only a note */
		bool separator = false;
		/* a watch on a game core's property (docs/game-cores.md), known by
		 * its name, never its address: the engine reads it and writes it as
		 * text, as a script and Chimera's watch see it. Its file line is
		 * kept from the file, for when no machine has it to write afresh. */
		std::string property, propertyLine;
		std::string valueText, previousText;
		std::string domain;
		int64_t address = 0;
		int size = 1;
		watch::Display display = watch::Display::Unsigned;
		bool bigEndian = false;
		std::string note;
		bool onScreen = false;
		/* as last read: the value, the one before it, changes counted, and
		 * whether the address is in its domain at all */
		uint64_t value = 0, previous = 0;
		int64_t changes = 0;
		bool readable = false, seen = false;
		int64_t domainSize = 0;
		/* picked in the list (the view's) */
		bool selected = false;
	};

	Property<int64_t> count{0};
	/* the machine's memory domains, comma-separated */
	Property<std::string> domains;
	/* what "previous" means: 0 the value when the watch was made, 2 the
	 * last frame's, 3 the value before the last change (Chimera's numbers) */
	Property<int64_t> previousType{2};
	/* the .wch file the list was opened from or saved to, and whether it has
	 * changed since */
	Property<std::string> path;
	Property<bool> changed{false};
	Property<std::string> status;
	/* the watches marked to be shown over the picture, "note: value" a line
	 * each ("" while showOnScreen is off), as Chimera shows them */
	Property<bool> showOnScreen{true};
	Property<std::string> onScreenText;

	/* "3 watches", as Chimera counts them (separators not) */
	Property<std::string> countText{"0 watches"};
	/* rows picked, and of them the watches (not separators) */
	Property<int64_t> selectedCount{0};
	Property<int64_t> selectedWatches{0};
	/* Split Watch and Poke Address can work on every watch picked */
	Property<bool> canSplit{false};
	Property<bool> canPoke{false};
	/* every watch picked is frozen (Freeze Address reads Unfreeze Address),
	 * and any address is frozen at all (Unfreeze All is offered) */
	Property<bool> selectionFrozen{false};
	Property<bool> anyFrozen{false};
	/* watches whose address is past their domain's end (Chimera's warning) */
	Property<int64_t> invalidCount{0};
	/* the domain a new watch starts in (Watches > Default Domain) */
	Property<std::string> defaultDomain;
	/* On Double-Clicking a Watch: poke it (else edit it), remembered */
	Property<bool> doubleClickToPoke{true};
	/* .wch files opened or saved, newest first, a line each, remembered */
	Property<std::string> recentFiles;
	/* the columns not shown, by Chimera's names, comma-separated, remembered */
	Property<std::string> hiddenColumns{"Prev,Diff,Type"};

	/* the watches as the window shows them (the view's thread) */
	const std::vector<Watch> &watches() const { return watches_; }

private:
	class Rows : public RowModel
	{
	public:
		explicit Rows(RamWatchViewModel &vm) : vm_(vm) {}
		int64_t rowCount() const override { return static_cast<int64_t>(vm_.watches_.size()); }
		int32_t columnCount() const override { return 15; }
		std::string columnName(int32_t column) const override;
		Value cell(int64_t row, int32_t column) const override;

	private:
		RamWatchViewModel &vm_;
	};

	bool index(const Args &a, size_t at, size_t &out, std::string &error) const;
	std::vector<size_t> selectedRows() const;
	/* the picks changed: the counts and what may be done with them */
	void selectionChanged();
	/* a watch as a line of a .wch file (and of the clipboard) */
	std::string lineOf(const Watch &w) const;
	/* the watches of .wch text, those marked on screen marked */
	std::vector<Watch> readList(std::istream &in) const;
	/* Chimera's status line: the file's name, " *" after it while changed */
	void showFileState(bool saved);
	void addRecent(const std::string &file);
	bool invalid(const Watch &w) const;
	/* a value written into a watch's memory, as its display reads text */
	bool poke(const Watch &w, const std::string &text, std::string &error);
	void refreshOnScreen();
	/* the list changed: the machine gets the new one, the window is told */
	void listChanged(bool edit);
	bool load(const std::string &file, bool append, std::string &error);
	bool save(const std::string &file, std::string &error);

	/* the machine's domains and system, for the list and its file */
	void refreshDomains();
	/* the machine's thread */
	void readOnMachine(ce_session *s, bool fresh);
	void readProperty(ce_session *s, Watch &w, bool fresh);

	SessionViewModel &session_;
	std::vector<Watch> watches_;
	Rows rows_{*this};
	int hooks_ = 0;
	Connection opensWatch_, previousTypeWatch_, showWatch_, freezesWatch_;
	bool frozen(const Watch &w) const;
	std::string systemId_;
	/* each domain's size, as the machine last said (the view's thread) */
	std::map<std::string, int64_t> domainSizes_;
	/* and whether it can be written */
	std::map<std::string, bool> domainWritable_;
	int64_t anchor_ = -1;
	std::string sortedBy_;
	bool sortReverse_ = false;
	void sizeFromDomain(Watch &w) const;

	/* the machine's copy, and a serial so values read for an older list are
	 * not shown against a newer one */
	std::vector<Watch> watchesM_;
	int previousTypeM_ = 2;
	uint64_t serial_ = 0, serialM_ = 0;
	std::atomic<bool> postedM_{false};
	std::mutex valuesM_;
	std::vector<Watch> valuesOut_;
	uint64_t valuesSerial_ = 0;
};

} // namespace chimera::vm
