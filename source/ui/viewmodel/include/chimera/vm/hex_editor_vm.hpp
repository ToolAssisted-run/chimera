#pragma once
/* The Hex Editor, as Chimera's: a memory domain sixteen bytes a line, shown
 * 1, 2 or 4 bytes at a time, with the text the bytes spell (as ASCII, or as
 * a .tbl file maps them). Only the lines on screen are held - a domain can
 * be gigabytes - read on the machine's thread after every frame. One address
 * is highlighted and others beside it (Ctrl and Shift, as Chimera's); hex
 * digits typed are written into it once a value's worth is in; the keys move
 * as Chimera's do. Find looks for bytes (or text) after or before it; Copy,
 * Export and Paste, Increment and Decrement, Poke, Add to RAM Watch, saving
 * a domain as binary or text, importing a file into one, and "File on Disk"
 * (the open game's file, edited in memory and saved back) are Chimera's. */

#include <atomic>
#include <cstdint>
#include <map>
#include <mutex>
#include <set>
#include <string>
#include <vector>

#include "chimera/vm/session_vm.hpp"
#include "chimera/vm/view_model.hpp"

namespace chimera::vm {

class RamWatchViewModel;

class HexEditorViewModel : public ViewModel
{
public:
	HexEditorViewModel(Dispatcher &dispatcher, SessionViewModel &session);
	~HexEditorViewModel() override;
	/* RAM Watch, where Add to RAM Watch puts the addresses highlighted */
	void setRamWatch(RamWatchViewModel *watch) { ramWatch_ = watch; }

	/* the machine's memory domains (and "File on Disk" with a game open by
	 * itself), comma-separated, and the one shown */
	Property<std::string> domains;
	Property<std::string> domain;
	/* bytes a value takes: 1, 2 or 4; and which end comes first */
	Property<int64_t> size{1};
	Property<bool> bigEndian{false};
	/* lines of 16 bytes the domain has, the first one shown, and how many
	 * are shown */
	Property<int64_t> lineCount{0};
	Property<int64_t> topLine{0};
	Property<int64_t> visibleLines{32};
	/* read again after each frame (the window is open) */
	Property<bool> watching{false};
	/* the address highlighted (-1 none), and how many others are */
	Property<int64_t> picked{-1};
	Property<int64_t> secondaryCount{0};
	Property<bool> writable{false};
	/* the address highlighted is frozen (Freeze Address reads Unfreeze
	 * Address), and any address is frozen at all */
	Property<bool> pickedFrozen{false};
	Property<bool> anyFrozen{false};
	Property<std::string> status;
	/* hex digits typed into the highlighted address so far */
	Property<std::string> nibbles;
	/* Chimera's window title ("Hex Editor - Editing Address 0x0010"), the
	 * box's ("Synth RAM  -  0x1000 addresses  Typing: (A)"), and the column
	 * numbers over the values */
	Property<std::string> title{"Hex Editor"};
	Property<std::string> boxTitle;
	Property<std::string> header;
	/* what Find Next and Find Prev look for (hex), and whether the Find
	 * window last looked for text */
	Property<std::string> findString;
	Property<bool> findText{false};
	/* a .tbl file read (its path), and the recent ones, remembered */
	Property<std::string> tableFile;
	Property<std::string> recentTables;
	/* Custom Colors, remembered, as JSON {"Background", "Foreground",
	 * "MenuBar", "Freeze", "Highlight", "HighlightFreeze"}: "#RRGGBB" each,
	 * "{}" for the theme's */
	Property<std::string> colors{"{}"};

private:
	class Lines : public RowModel
	{
	public:
		explicit Lines(HexEditorViewModel &vm) : vm_(vm) {}
		int64_t rowCount() const override;
		/* address; a value and whether it changed for each of the 16 / size
		 * places (v0, c0, v1, c1, ...); the text; each place's highlight
		 * (h0.., 0 none, 1 the address, 2 one beside it); each byte's
		 * character (t0..) and its highlight (th0..); each place frozen (f0..) */
		int32_t columnCount() const override { return 98; }
		std::string columnName(int32_t column) const override;
		Value cell(int64_t row, int32_t column) const override;

	private:
		HexEditorViewModel &vm_;
	};

	/* the window asked for moved, or the domain or size changed: read again */
	void reread();
	/* the machine's thread */
	void readOnMachine(ce_session *s);
	void refreshDomains();
	/* bytes of the domain shown, read now (the machine waited for) */
	bool readNow(int64_t from, int64_t count, std::vector<uint8_t> &out);
	/* bytes written into the domain shown; false with why */
	bool writeNow(int64_t at, const std::vector<uint8_t> &bytes, std::string &error);
	uint32_t valueAt(int64_t address);
	bool writeValue(int64_t address, uint32_t value, std::string &error);
	/* every address highlighted, the first one first */
	std::vector<int64_t> highlightedAll() const;
	/* going to an address and highlighting it: in the domain, and brought into view */
	void highlight(int64_t address);
	void titles();
	int highlightKind(int64_t address) const;
	char charOf(uint8_t b) const;
	/* the next or last place bytes are, from the highlighted address */
	int64_t findBytes(const std::vector<uint8_t> &want, bool forward, int64_t from);
	void found(int64_t at, size_t length);
	void addRecent(const std::string &file);
	int digits() const;

	SessionViewModel &session_;
	RamWatchViewModel *ramWatch_ = nullptr;
	Lines lines_{*this};
	/* how many lines the view was last told there are: the lines read again
	 * are as many as that, or the whole list again (the window's height
	 * known only after it opened left it empty, issue 471) */
	int64_t linesAnnounced_ = 0;
	Connection linesResetWatch_;
	int hooks_ = 0;
	Connection opensWatch_, domainWatch_, topWatch_, visibleWatch_, sizeWatch_, pickedWatch_, endianWatch_, freezesWatch_, pickedFrozenWatch_,
		watchingWatch_;
	std::atomic<bool> watchingM_{false};
	/* any byte of the place at address frozen, in the domain shown */
	bool placeFrozen(int64_t address) const;
	void freezesShown();
	/* the addresses highlighted beside the first */
	std::set<int64_t> secondary_;
	std::map<int, char> table_;
	/* "File on Disk": the game file's bytes, edited here */
	/* what the window shows (the view's thread): the bytes from `shownFrom_`,
	 * which of them changed at the last read, and the domain's size */
	std::vector<uint8_t> shown_, changedShown_;
	int64_t shownFrom_ = 0, domainSize_ = 0;
	std::map<std::string, int64_t> sizes_;
	std::map<std::string, bool> canWrite_;
	/* the machine's: what to read, and what it read last time */
	std::mutex wantM_;
	std::string wantDomain_;
	int64_t wantFrom_ = 0, wantBytes_ = 0;
	std::vector<uint8_t> lastM_;
	int64_t lastFromM_ = -1;
	std::string lastDomainM_;
	std::atomic<bool> postedM_{false};
	std::mutex outM_;
	std::vector<uint8_t> out_, outChanged_;
	int64_t outFrom_ = 0;
};

} // namespace chimera::vm
