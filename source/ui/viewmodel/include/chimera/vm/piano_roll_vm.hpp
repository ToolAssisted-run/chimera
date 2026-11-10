#pragma once
/* The piano roll: every frame a row, every button a column, a cell lit where
 * the button is held. It reads and edits the same inputs the session plays. */

#include <map>
#include <set>
#include <optional>

#include "chimera/vm/input_log.hpp"
#include "chimera/vm/session_vm.hpp"
#include "chimera/vm/view_model.hpp"

namespace chimera::vm {

class PianoRollViewModel : public ViewModel
{
public:
	/* a line read from its first '|', as Chimera reads one
	 * (chimera 10b9668): whitespace and the digits 0-9 before it dropped -
	 * the frame number Copy puts there when asked ("00002 |..U.....|"), an
	 * indent, a tab; any other line as it is */
	static std::string withoutFrameNumber(const std::string &line);
	/* Frames::rowFlags */
	static constexpr uint32_t kLag = 1, kGreen = 2, kMarker = 4, kStated = 8, kRan = 16;
	/* the theme role a row's frame number is drawn on, when it is not a
	 * marker's: a frame the greenzone holds takes Set Colors' "Frame# Column"
	 * colours, lag or not (TasGreenZone, TasLagZone), as Chimera's roll now
	 * does (owner, 2026-10-24); the current frame and a frame not run as it
	 * stands keep their row's, washed (TasFrameColumnWash) */
	static const char *frameColumnRole(bool current, bool inGreen, bool lagged)
	{
		return current || !inGreen ? "TasFrameColumnWash" : lagged ? "TasLagZone" : "TasGreenZone";
	}
	/* one of the run's own markers is on the frame */
	static constexpr uint32_t kPermanent = 32;
	/* a control's player, as Chimera counts them: "P2 A" is 2, anything
	 * else (the console's, past 255) 0 */
	static int playerOf(const std::string &name);
	/* where the roll draws a thick line between columns: for each of the
	 * columns' left edges and, last, the right edge of the last one, whether
	 * a player's group of columns starts or ends there - the first column
	 * shown, each column shown whose player (each column's, as the core
	 * declares; 0 the console's) is not the one shown before it, and the
	 * end. Hidden columns are passed over. */
	static std::vector<bool> playerEdges(const std::vector<int32_t> &players, const std::vector<bool> &shown);

	PianoRollViewModel(Dispatcher &dispatcher, InputLog &inputs, SessionViewModel &session);

	/* the frame the machine is about to run */
	Property<int64_t> currentFrame{0};
	/* the frame of the marker the machine is at or past (Chimera's marker
	 * list colours it), -1 before the first */
	Property<int64_t> currentMarker{-1};
	/* the columns not shown, by control name, a line each (Chimera's
	 * defaults hide the console's power and reset, keyboard keys...) */
	Property<std::string> hiddenColumns;
	/* TAStudio's input rolls, as Chimera's: how many, the one the person
	 * last used (its columns are the ones the menus show and edits keep
	 * to), and whether they scroll together */
	Property<int64_t> rollCount{1}, activeRoll{0};
	Property<bool> scrollSync{true};
	/* the used roll's hidden columns, and all of them replaced at once, as
	 * Config > Controllers' TAStudio column boxes do (issue 391); a change
	 * marks the project changed */
	const std::set<std::string> &hiddenColumnSet() const { return hidden_; }
	void setHiddenColumns(const std::set<std::string> &names);
	/* how following the current frame scrolls, as TAStudio's "scroll to
	 * cursor method" does: "near" just enough to bring it in, "top",
	 * "bottom" or "center" putting it there when it is out of view (or
	 * every frame, with followAlways); and the frames a wheel's notch
	 * scrolls */
	Property<std::string> followScroll{"near"};
	Property<bool> followAlways{false};
	Property<int64_t> wheelRows{6};
	/* TAStudio's "allow editing hidden columns (with clear, record, paste,
	 * etc.)": off, an edit of frames changes only the columns shown - the
	 * hidden ones keep their inputs where they are, frames put in or taken
	 * out moving only the shown ones, as Chimera's multitrack editing */
	Property<bool> editHiddenColumns{true};
	/* the edits made now are a script's (Lua's tastudio.applyinputchanges):
	 * they leave recording mode on, where an edit by hand turns it off */
	Property<bool> scriptEditing{false};
	/* TAStudio's paint patterns: when painting a column follows its
	 * pattern - "never", "autofire" (on the columns given Auto-hold) or
	 * "always"; Alt held paints a button's pattern whatever this says -
	 * and which pattern a column's Auto-hold plays: "hold", "autofire" or
	 * "custom" (the column's own, kept in the project) */
	Property<std::string> patternMode{"never"};
	Property<std::string> patternKind{"hold"};
	/* what autoHoldQuestion offers: the analog control's range, and its
	 * value on the current frame */
	Property<int64_t> autoHoldMin{0}, autoHoldMax{0}, autoHoldInitial{0};
	/* TAStudio's appearance settings: a frame whose state the greenzone
	 * holds, and a marker's frame, told by colour and by an icon in the
	 * frame column; colours of the roll's own over the theme's (a JSON
	 * object, a theme role each: {"TasGreenZoneInput": "#c0f0c0"}); and
	 * the roll's font ({"family", "pointSize", "bold", "italic"}; "" the
	 * window's) */
	Property<bool> denoteStatesColor{true}, denoteStatesIcons{false};
	Property<bool> denoteMarkersColor{true}, denoteMarkersIcons{false};
	Property<std::string> rollColors{"{}"};
	Property<std::string> rollFont;
	/* TAStudio's lag frames hidden (the project's own settings, as
	 * Chimera's): of a run of lag frames, the first this many are not
	 * shown (0 shows them all); and frames that were lag before an edit
	 * took them out of the greenzone count as lag too */
	Property<int64_t> lagFramesToHide{0};
	Property<bool> hideWasLagFrames{false};
	/* TAStudio's Rotate: the frames across, the buttons down (the project's own) */
	Property<bool> horizontal{false};
	/* a script's own columns (Lua's tastudio.addcolumn), after the machine's: JSON
	 * [{"name", "text", "width"}]; and whether a script colours or writes the cells */
	Property<std::string> scriptColumns{"[]"};
	Property<bool> scriptQueries{false};
	/* the icons a script names read again when this moves (tastudio.clearIconCache) */
	Property<int64_t> iconEpoch{0};
	/* a frame not shown by those, and the frame `steps` shown frames on
	 * from one (back for a negative count) */
	bool frameHidden(int64_t frame) const;
	int64_t shownFrom(int64_t frame, int64_t steps) const;
	/* macros placed lately, newest first, a line each (eight kept) */
	Property<std::string> recentMacros;
	/* a macro file used, first in the recent ones */
	void addRecentMacro(const std::string &path);
	/* A macro as Chimera's .chimeraMacro has it, of frames first to last and
	 * these columns (none named: all), Overlay and Replace as given; "" with
	 * the reason when it cannot be. Its lines are the movie's own when it has
	 * every column, else made as Chimera makes a zone's */
	std::string macroText(int64_t first, int64_t last, const std::vector<std::string> &columns, bool overlay, bool replace,
		std::string &error) const;
	/* a macro's lines placed from this frame on (its own Overlay and Replace),
	 * one step of the undo history; `name` says where it came from */
	bool placeMacroLines(int64_t at, std::vector<std::string> lines, const std::string &name, std::string &error);
	/* the movie's columns (its LogKey), as written and as a macro names them */
	std::string movieKeyRaw() const;
	std::string movieKey() const;
	/* the most undo steps kept, as Chimera's TAStudio setting has it */
	Property<int64_t> maxUndoSteps{1000};
	/* TAStudio's "include frame numbers when copying input" */
	Property<bool> copyFrameNumbers{false};
	/* the frames last copied or cut, a line each as the movie writes them
	 * (with their numbers when asked), for the window to put on the
	 * system's clipboard, as Chimera's Copy does */
	Property<std::string> clipboardText;
	/* TAStudio's "markers keep states by default": a marker put on a frame
	 * keeps that frame's state in the greenzone */
	Property<bool> markersKeepStates{true};
	Property<int64_t> frameCount{0};
	/* keep the current frame in view while the machine runs */
	Property<bool> followPlayback{true};
	/* TAStudio's window comes along when the main one is moved, as Chimera's does at first */
	Property<bool> moveWithMainWindow{true};
	/* where TAStudio's two bars were left, as Chimera's settings keep them: the
	 * side's width and the Markers box's height (0: where Chimera puts them) */
	Property<int64_t> mainSplit{0};
	Property<int64_t> sideSplit{0};
	/* frames before this one were run with the inputs they have now: the greenzone */
	Property<int64_t> greenEnd{0};
	/* the first frame in view, and how many fit: the view says how many */
	Property<int64_t> topFrame{0};
	Property<int64_t> visibleFrames{0};
	/* the markers people placed (the run's own three not counted) */
	Property<int64_t> markerCount{0};
	/* the row, in the markers' list, of the marker last put on a frame, for
	 * the list to show it (issue 487); -1 in between, so that the same row
	 * twice is a change too */
	Property<int64_t> markerAddedRow{-1};
	/* the run's own markers, as Chimera gives every movie: Run start on frame
	 * 0, Last input on the last frame anything is pressed on (0 when none
	 * is), Run end on the movie's last frame. They follow every edit, are
	 * never saved, and cannot be moved, renamed or removed */
	Property<int64_t> lastInputFrame{0};
	Property<int64_t> runEndFrame{0};
	/* an edit can be taken back, or one taken back done again */
	Property<bool> canUndo{false};
	Property<bool> canRedo{false};
	/* the frames picked, first to last (-1: none) */
	Property<int64_t> selectFirst{-1};
	Property<int64_t> selectLast{-1};
	/* frames copied, ready to paste */
	Property<int64_t> clipboardFrames{0};
	/* markers move with the frames when frames are put in or taken out
	 * (off, as in Chimera, they stay on their frame numbers) */
	Property<bool> bindMarkers{false};
	/* the analog cell picked for editing: its frame and control (-1: none),
	 * the control's name and range, and the value there */
	Property<int64_t> pickedFrame{-1};
	Property<int64_t> pickedAxis{-1};
	Property<std::string> axisName;
	Property<int64_t> axisMin{0};
	Property<int64_t> axisMax{0};
	Property<int64_t> axisValue{0};

	/* the rows, for a view that draws its own cells (cells as bits, no Value) */
	Buttons frameBits(int64_t frame) const { return inputs_.at(frame); }
	void readBits(int64_t first, std::vector<Buttons> &out) const { inputs_.read(first, out); }
	int32_t buttonCount() const;
	RowModel &frames() { return frames_; }
	const std::map<int64_t, Marker> &markers() const { return markers_; }
	/* the inputs were replaced whole (a branch loaded), with these markers:
	 * the rows start again, and what could be undone is forgotten */
	void replaceAll(const std::vector<Marker> &markers);

private:
	class Frames : public RowModel
	{
	public:
		explicit Frames(PianoRollViewModel &vm) : vm_(vm) {}
		/* one row past the movie, empty, as Chimera's TAStudio has it: where
		 * the machine sits at the movie's end, where a click goes to that
		 * frame and a paint starts that makes the movie longer */
		int64_t rowCount() const override { return vm_.inputs_.bound() ? vm_.inputs_.size() + 1 : 0; }
		int32_t columnCount() const override { return vm_.buttonCount() + static_cast<int32_t>(vm_.session_.axes().size()); }
		bool columnIsNumber(int32_t column) const override { return column >= vm_.buttonCount(); }
		bool columnHidden(int32_t column) const override { return vm_.hidden_.count(columnName(column)) != 0; }
		bool columnHiddenIn(int64_t view, int32_t column) const override;
		bool columnEmphasized(int32_t column) const override { return vm_.autoHeld_.count(columnName(column)) != 0; }
		int32_t columnPlayer(int32_t column) const override;
		int64_t nextShown(int64_t row) const override;
		std::string columnName(int32_t column) const override;
		/* the button's letter in a movie's input log */
		std::string columnShortName(int32_t column) const override;
		Value cell(int64_t row, int32_t column) const override;
		uint32_t rowFlags(int64_t row) const override;
		std::string rowNote(int64_t row) const override;
		/* the row read last: a view asks for a row's cells one after another,
		 * and each read is the engine's (a line to parse). Forgotten on every
		 * change of the rows. */
		void forget() const { cachedRow_ = -1; }

	private:
		PianoRollViewModel &vm_;
		mutable int64_t cachedRow_ = -1;
		mutable Buttons cachedBits_;
		mutable std::vector<int32_t> cachedAxes_;
	};

	/* the columns, for a menu to show and hide them: name, label, shown,
	 * autoHold, and the group Chimera's Columns menu puts it in ("" the
	 * console's, "Player 1"..., or "Keys (A - M)" for keyboard keys, thirty
	 * a group) */
	class ColumnRows : public RowModel
	{
	public:
		explicit ColumnRows(PianoRollViewModel &vm) : vm_(vm) {}
		int64_t rowCount() const override { return vm_.frames_.columnCount(); }
		int32_t columnCount() const override { return 5; }
		std::string columnName(int32_t column) const override;
		Value cell(int64_t row, int32_t column) const override;

	private:
		PianoRollViewModel &vm_;
	};

	/* a column's own pattern: values in turn, back to `loop` after the last; lag
	 * frames passed over (the value before kept) when skipLag */
	struct Pattern
	{
		std::vector<int32_t> values;
		bool skipLag = true;
		int64_t loop = 0;
	};
	class PatternRows : public RowModel
	{
	public:
		explicit PatternRows(PianoRollViewModel &vm) : vm_(vm) {}
		int64_t rowCount() const override
		{
			return static_cast<int64_t>(vm_.buttonPatterns_.size() + vm_.axisPatterns_.size());
		}
		int32_t columnCount() const override { return 8; }
		std::string columnName(int32_t column) const override;
		Value cell(int64_t row, int32_t column) const override;

	private:
		PianoRollViewModel &vm_;
	};

	class MarkerRows : public RowModel
	{
	public:
		explicit MarkerRows(PianoRollViewModel &vm) : vm_(vm) {}
		int64_t rowCount() const override { return static_cast<int64_t>(vm_.markerList_.size()); }
		int32_t columnCount() const override { return 5; }
		std::string columnName(int32_t column) const override;
		Value cell(int64_t row, int32_t column) const override;

	private:
		PianoRollViewModel &vm_;
	};

	/* One edit, for undo: each frame it changed, as it was and as it became,
	 * and the length before and after. */
	struct Step
	{
		std::map<int64_t, std::pair<Buttons, Buttons>> frames;
		int64_t sizeBefore = 0, sizeAfter = 0;
		/* frames put in (shiftCount > 0) or taken out (< 0) at shiftAt,
		 * before the changes above; what was taken out, to put back */
		int64_t shiftAt = -1, shiftCount = 0;
		std::vector<Buttons> removedButtons;
		std::vector<std::vector<int32_t>> removedAxes;
		/* analog values a step set: frame -> (before, after) */
		std::map<int64_t, std::pair<std::vector<int32_t>, std::vector<int32_t>>> axes;
		/* the markers, when the step moved them */
		std::optional<std::map<int64_t, Marker>> markersBefore, markersAfter;
		/* what it was, as Chimera's undo history names it ("Set P1 A(On): 5") */
		std::string name;
	};
	/* the next step's name, set by the command that makes it */
	std::string naming_;
	static std::string describe(const Step &step);
	void keepMarkers(std::map<int64_t, Marker> before, std::string name);
	/* the undo history, in order: the steps done, then those undone */
	class HistoryRows : public RowModel
	{
	public:
		explicit HistoryRows(PianoRollViewModel &vm) : vm_(vm) {}
		int64_t rowCount() const override { return static_cast<int64_t>(vm_.undo_.size() + vm_.redo_.size()); }
		int32_t columnCount() const override { return 2; }
		std::string columnName(int32_t column) const override;
		Value cell(int64_t row, int32_t column) const override;

	private:
		PianoRollViewModel &vm_;
	};
	HistoryRows historyRows_{*this};
	/* the steps the history list was last told it has */
	int64_t historyTold_ = 0;
	void historyChanged();
	struct Row
	{
		Buttons buttons;
		std::vector<int32_t> axes;
	};
	/* the most steps kept (Chimera's default, a setting) */
	void trimHistory();

	/* held: set to it; nullopt: flip. join: part of the stroke under way */
	bool paint(int64_t first, int64_t last, int64_t button, std::optional<bool> held, bool join, std::string &error);
	void keep(Step step, bool join);
	/* frames in and out, and rows written, as one undoable step */
	Row readRow(int64_t frame) const;
	/* the patterns, a button's then an analog control's, in the machine's
	 * order; Chimera's defaults (on one frame, off one; an axis at its
	 * most) for a machine just opened */
	std::vector<Pattern> buttonPatterns_, axisPatterns_;
	PatternRows patternRows_{*this};
	void defaultPatterns();
	void storePatterns();
	void loadPatterns(const std::map<std::string, std::string> &members);
	const Pattern *patternOf(int64_t column) const;
	static std::string runsText(const Pattern &p, bool isButton);
	/* a pattern's value for each frame first..last, counted from first */
	std::vector<int32_t> patternValues(const Pattern &p, int64_t first, int64_t last) const;
	/* a stroke painting a column's pattern: the frame it began on, the
	 * column, and whether it presses (the pressed cell was not held) */
	bool patternStroke_ = false, patternOn_ = false;
	int64_t patternFrom_ = 0, patternColumn_ = -1;
	bool paintPattern(int64_t first, int64_t last, bool join, std::string &error);
	Connection patternsWatch_, projectGoneWatch_, statesWatch_, viewWatch_, viewCountWatch_, lagHideWatch_, wasLagWatch_, horizontalWatch_;
	/* the lag settings, to save with the project */
	void storeLagHiding();
	bool laggy(int64_t frame) const;
	/* a project's TAStudio part, kept until its machine's controls are
	 * known (they are told after it); and the controls the patterns are for */
	std::optional<std::map<std::string, std::string>> pendingMembers_;
	std::vector<std::string> patternNames_;
	std::vector<std::string> controlNames() const;
	/* each column's group in the Columns menu, by column */
	std::vector<std::string> columnGroups() const;
	/* the groups, in the menu's order: the players', then the keys' */
	class GroupRows : public RowModel
	{
	public:
		explicit GroupRows(PianoRollViewModel &vm) : vm_(vm) {}
		int64_t rowCount() const override { return static_cast<int64_t>(vm_.groupNames().size()); }
		int32_t columnCount() const override { return 4; }
		std::string columnName(int32_t column) const override;
		Value cell(int64_t row, int32_t column) const override;

	private:
		PianoRollViewModel &vm_;
	};
	std::vector<std::string> groupNames() const;
	GroupRows groupRows_{*this};
	/* editing hidden columns is off and some are hidden: the buttons shown
	 * (a bit each) and the analog controls shown */
	bool limited() const;
	Buttons shownButtons() const;
	std::vector<bool> shownAxes() const;
	/* the session told which columns recording may change */
	void pushRecordMask();
	/* frames the machine recorded: Record Frame steps, a run's frames one
	 * step while the machine runs on, as Chimera merges them */
	void framesRecorded(const std::vector<SessionViewModel::RecordedFrame> &frames);
	bool recordingStep_ = false, lastWasRecord_ = false;
	/* `to` with the shown columns of `from` */
	Row mixShown(const Row &to, const Row &from) const;
	/* a frame with nothing held */
	Row emptyRow() const;
	/* frames put in, taken out, or cleared to the end, the shown columns alone */
	void putInShown(int64_t at, const std::vector<Row> &rows);
	void takeOutShown(int64_t first, int64_t last);
	void truncateShown(int64_t frame);
	void writeRow(int64_t frame, const Row &row);
	void putIn(int64_t at, const std::vector<Row> &rows);
	void takeOut(int64_t first, int64_t last);
	void overwrite(int64_t at, const std::vector<Row> &rows);
	/* markers moved as frames move, when they are bound to them */
	void moveMarkers(Step &step, int64_t at, int64_t by);
	bool range(const Args &a, int64_t &first, int64_t &last, std::string &error) const;
	/* an analog control's value on frames first..last, as one step (join:
	 * part of the stroke under way) */
	bool paintAxis(int64_t first, int64_t last, int64_t axis, int64_t value, bool join, std::string &error);
	void pickAxis(int64_t frame, int64_t axis);
	int64_t dragAxisValue_ = 0;
	/* a press on an analog cell edits nothing: the drag's first cell starts
	 * the step, the rest join it */
	bool axisJoin_ = false;
	void undoOrRedo(bool back);
	void edited(int64_t first, int64_t last, int64_t oldCount);
	void markersEdited(int64_t frame);
	/* markerAddedRow at the person's marker on a frame */
	void showMarker(int64_t frame);
	/* after an edit at a frame, the last
	 * frame with anything pressed, walking back from the end no further than
	 * the edit unless the edit could have erased it; then the run's own
	 * markers moved where the movie says, and the marker list made again */
	/* the run's own markers worked out again after frames `frame` to `through`
	 * changed (to the movie's end when not said) */
	void refreshRun(int64_t frame, int64_t through = INT64_MAX);
	bool frameEmpty(int64_t frame) const;
	bool permanentAt(int64_t frame) const { return frame == 0 || frame == lastInputFrame.get() || frame == runEndFrame.get(); }
	void listMarkers();
	void pickCurrentMarker();
	/* the marker list as shown: the run's own first on a shared frame, in run order */
	struct ListedMarker
	{
		int64_t frame;
		std::string text;
		bool keepState, permanent;
	};
	std::vector<ListedMarker> markerList_;
	Connection runOpensWatch_;
	void follow();
	void clampTop();

	InputLog &inputs_;
	std::vector<Row> clipboard_;
	/* where the frames copied came from, to pick them again */
	int64_t copiedFrom_ = -1;
	SessionViewModel &session_;
	Frames frames_{*this};
	/* the columns hidden, by control name, and the list of them all */
	std::set<std::string> hidden_;
	/* each input roll's hidden columns (the active one's are hidden_, kept
	 * back here when another roll becomes active) */
	std::vector<std::set<std::string>> rolls_{1};
	/* the columns given Auto-hold (Chimera's column "emphasis"), by name;
	 * the value an analog one holds (chimera issue 298: asked for when its
	 * Auto-hold is turned on; its most when none was given); and what each
	 * presses while the game plays, handed to the session */
	std::set<std::string> autoHeld_;
	std::map<std::string, int32_t> axisHoldValues_;
	void pushColumnHolds();
	void autoHeldChanged();
	std::vector<Connection> holdWatches_;
	ColumnRows columnRows_{*this};
	void hiddenChanged();
	Connection hiddenWatch_, maxStepsWatch_, editHiddenWatch_;
	std::map<int64_t, Marker> markers_;
	MarkerRows markerRows_{*this};
	Connection markersWatch_;
	std::vector<Connection> forgetWatches_;
	Connection frameWatch_, buttonsWatch_, changedWatch_, appendedWatch_, topWatch_, greenWatch_, resetWatch_, recordedWatch_,
		followWatch_;
	/* what a drag paints: the state the pressed cell was given */
	bool dragHeld_ = false;
	/* a press began a stroke that drags extend */
	bool stroke_ = false;
	/* an MCP agent's edit: every column, hidden or not (Chimera's agent writes whole frames) */
	bool agentEditing_ = false;
	/* Chimera's right-button drags on the frame numbers: the input taken up
	 * (the frames picked, or the frame pressed), where it came from, the keys
	 * held, the rows the drag reached, and whether its one undo step is made */
	std::vector<Row> rightInput_;
	int64_t rightFrame_ = -1, rightStart_ = -1, rightLow_ = -1, rightHigh_ = -1;
	bool rightCtrl_ = false, rightShift_ = false, rightAlt_ = false, rightStepMade_ = false;
	/* the drag gone to a frame: its whole effect from where it began, one undo step */
	void rightDragOnTo(int64_t frame);
	std::vector<Step> undo_, redo_;
};

} // namespace chimera::vm
