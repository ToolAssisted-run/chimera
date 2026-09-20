#nullable enable

namespace Chimera.Client.Common
{
	/// <summary>
	/// Every colour the frontend is allowed to paint with. A theme is a value for
	/// each of these and nothing else, which is the whole point: a role added here
	/// is a role every theme has to answer for, and a colour that is not a role
	/// cannot be themed, so it cannot quietly stay light when the rest of the
	/// window goes dark.
	/// </summary>
	/// <remarks>
	/// The names are the keys in a theme's JSON file, spelled exactly as they are
	/// here. Do not rename one casually: a theme somebody wrote names it. Adding
	/// one is fine - a theme that is <c>basedOn</c> another inherits the new role,
	/// and the built-ins are updated in the same commit.
	/// </remarks>
	public enum ThemeColorRole
	{
		// ---- window chrome -------------------------------------------------

		/// <summary>The window itself, and any plain panel, tab page or group box in it.</summary>
		WindowBackground,

		/// <summary>Ordinary text on <see cref="WindowBackground"/>: labels, group box captions, check box captions.</summary>
		WindowText,

		/// <summary>Text that is there but cannot be used.</summary>
		DisabledText,

		/// <summary>The background of a control that is there but cannot be used.</summary>
		DisabledBackground,

		/// <summary>Text that is present but secondary: a detail line, a domain that cannot be written.</summary>
		MutedText,

		/// <summary>A line drawn to separate things: a group box edge, a panel border, a separator.</summary>
		Border,

		/// <summary>A hyperlink.</summary>
		LinkText,

		/// <summary>An inset area darker than the window: the backing behind a meter or a preview.</summary>
		ShadedBackground,

		/// <summary>A glyph a control draws itself: a drop-down arrow, a check mark.</summary>
		GlyphForeground,

		/// <summary>The same glyph when it means "no": a button not in the macro, a disabled arrow.</summary>
		GlyphShadow,

		// ---- things you type in --------------------------------------------

		/// <summary>Text box, combo box, list box, tree, grid: the writing surface.</summary>
		InputBackground,

		/// <summary>Text on <see cref="InputBackground"/>.</summary>
		InputText,

		/// <summary>A text box showing something it will not let you change.</summary>
		ReadOnlyBackground,

		/// <summary>A key-binding field that is waiting for you to press something.</summary>
		InputAwaitingBackground,

		// ---- buttons -------------------------------------------------------

		ButtonBackground,

		ButtonText,

		/// <summary>The line around a button.</summary>
		ButtonBorder,

		// ---- menus and strips ----------------------------------------------

		MenuBackground,

		MenuText,

		/// <summary>The item the pointer or the keyboard is on.</summary>
		MenuSelectedBackground,

		MenuSelectedText,

		/// <summary>The edge of an open drop-down.</summary>
		MenuBorder,

		/// <summary>The rule between groups of menu items.</summary>
		MenuSeparator,

		/// <summary>A tool strip or menu bar's own background (not its drop-downs).</summary>
		ToolStripBackground,

		ToolStripText,

		StatusBarBackground,

		StatusBarText,

		// ---- selection -----------------------------------------------------

		/// <summary>The highlight behind a selected row or a selected run of text.</summary>
		Selection,

		SelectionText,

		/// <summary>The same, in a control that does not have the focus.</summary>
		InactiveSelection,

		InactiveSelectionText,

		// ---- lists, grids, headers -----------------------------------------

		/// <summary>The rules between cells in a grid or a list.</summary>
		GridLines,

		/// <summary>A column header.</summary>
		HeaderBackground,

		HeaderText,

		/// <summary>Every other row, where a list stripes itself.</summary>
		AlternateRowBackground,

		// ---- what a colour is used to SAY ----------------------------------

		/// <summary>Text saying something is in order: a firmware file that matches, a need satisfied.</summary>
		AccentGood,

		/// <summary>Text saying something is already there: a module already compiled.</summary>
		AccentReady,

		/// <summary>Text saying something is off but survivable: a file that is not the known dump.</summary>
		AccentWarning,

		/// <summary>Text saying something is wrong: a missing file, an invalid value.</summary>
		AccentError,

		/// <summary>A field tinted to say the same as <see cref="AccentWarning"/>.</summary>
		AccentWarningBackground,

		/// <summary>The tick in a "this is the right file" icon.</summary>
		GlyphGood,

		/// <summary>The body of a warning triangle.</summary>
		GlyphWarning,

		/// <summary>The exclamation mark inside that triangle.</summary>
		GlyphWarningInk,

		/// <summary>The cross in a "this file is missing" icon.</summary>
		GlyphError,

		/// <summary>An icon for something that is simply absent and need not be.</summary>
		GlyphNeutral,

		// ---- the piano roll (InputRoll), independent of what it is showing ---

		/// <summary>The roll's paper.</summary>
		RollBackground,

		/// <summary>Text in a roll cell, and in a column header.</summary>
		RollText,

		/// <summary>The lines between rows and columns of the roll.</summary>
		RollGridLines,

		/// <summary>The roll's column headers.</summary>
		RollColumnBackground,

		/// <summary>The line the roll draws between column headers.</summary>
		RollColumnBorder,

		/// <summary>A selected cell in the roll, and a hovered column header.</summary>
		RollSelection,

		RollSelectionText,

		/// <summary>The ghost of a column's name shown in an empty cell under the pointer.</summary>
		RollHintText,

		/// <summary>The tint over a column the roll has been told to emphasise.</summary>
		RollEmphasisColumn,

		// ---- TAStudio's row states -----------------------------------------

		/// <summary>The input log on the frame the emulator is sitting on.</summary>
		TasCurrentFrame,

		/// <summary>A greenzone frame's frame-number column.</summary>
		TasGreenZone,

		/// <summary>A greenzone frame's input.</summary>
		TasGreenZoneInput,

		/// <summary>A greenzone frame whose state is actually held.</summary>
		TasGreenZoneInputStated,

		/// <summary>A greenzone frame an edit behind it has invalidated.</summary>
		TasGreenZoneInputInvalidated,

		/// <summary>A lag frame's frame-number column.</summary>
		TasLagZone,

		TasLagZoneInput,

		TasLagZoneInputStated,

		TasLagZoneInputInvalidated,

		/// <summary>A frame somebody put a marker on.</summary>
		TasMarker,

		/// <summary>The run's own three markers: start, last input, end.</summary>
		TasPermanentMarker,

		/// <summary>An analog value being typed over.</summary>
		TasAnalogEdit,

		/// <summary>A row nothing else has anything to say about.</summary>
		TasDefaultRow,

		/// <summary>The column the edit cursor is in.</summary>
		TasCursorColumn,

		/// <summary>The wash laid over the frame-number column, which shows the row colour through it.</summary>
		TasFrameColumnWash,

		/// <summary>The stripe that tells one player's buttons from the next, laid over the row colour.</summary>
		TasAlternatePlayer,

		/// <summary>The arrow marking where playback is.</summary>
		TasIconPlayback,

		/// <summary>The arrow marking where recording is.</summary>
		TasIconRecording,

		/// <summary>The marker pin drawn in the frame column.</summary>
		TasIconMarker,

		/// <summary>The anchor icon on a frame holding a state.</summary>
		TasIconAnchor,

		/// <summary>The same anchor on a lag frame.</summary>
		TasIconLagAnchor,

		// ---- lists of things with a state (RAM search, Lua console, breakpoints) ---

		/// <summary>A row nothing is wrong with.</summary>
		RowDefault,

		/// <summary>A row whose address does not exist any more.</summary>
		RowInvalid,

		/// <summary>A row that is doing something: a running script, an armed breakpoint, a live cheat.</summary>
		RowActive,

		/// <summary>A row that is doing something but is held: a paused script.</summary>
		RowPaused,

		/// <summary>A row the next search will throw away.</summary>
		RowExcluded,

		/// <summary>A row the next search will throw away that is also doing something.</summary>
		RowExcludedActive,

		// ---- the hex editor ------------------------------------------------

		HexBackground,

		HexText,

		/// <summary>The hex editor's own menu bar, which it colours itself.</summary>
		HexMenuBar,

		/// <summary>An address frozen at its value.</summary>
		HexFreeze,

		/// <summary>An address under the cursor.</summary>
		HexHighlight,

		/// <summary>An address that is both.</summary>
		HexHighlightFreeze,

		// ---- the analog-range widget ---------------------------------------

		/// <summary>The square the analog widget draws in.</summary>
		AnalogRangeBackground,

		/// <summary>The area inside it the stick can reach.</summary>
		AnalogRangeField,

		/// <summary>The dot showing where the stick is.</summary>
		AnalogRangeDot,

		/// <summary>The cross through the middle.</summary>
		AnalogRangeAxis,

		/// <summary>The outline of the range the stick is allowed.</summary>
		AnalogRangeLimit,

		// ---- the recording level meter -------------------------------------

		/// <summary>The unfilled part of a level bar.</summary>
		MeterTrough,

		/// <summary>A level that is fine.</summary>
		MeterLow,

		/// <summary>A level that is getting close.</summary>
		MeterMid,

		/// <summary>A level that is clipping.</summary>
		MeterHigh,

		/// <summary>The line left behind at the loudest recent level.</summary>
		MeterPeak,

		/// <summary>The L and R beside the bars.</summary>
		MeterText,

		// ---- the emulated picture ------------------------------------------

		/// <summary>
		/// Behind and around the emulated picture. Black in both built-in themes:
		/// it is not chrome, it is what a screen looks like with nothing on it,
		/// and anything else shows up as a border around the game.
		/// </summary>
		EmulatorViewport,

		// ---- the on-screen display, drawn over the emulated picture ---------

		/// <summary>An ordinary OSD line.</summary>
		OsdMessage,

		/// <summary>An OSD line that wants attention: the frame counter while seeking, the lag counter.</summary>
		OsdAlert,

		/// <summary>The previous frame's input, in the input display.</summary>
		OsdLastInput,

		/// <summary>A movie's input, in the input display.</summary>
		OsdMovieInput,

		/// <summary>An input being held down by an autohold.</summary>
		OsdStickyInput,

		/// <summary>An input that is both pressed now and was pressed last frame.</summary>
		OsdCurrentAndPreviousInput,

		/// <summary>The list of held buttons drawn in the corner.</summary>
		OsdAutoHold,
	}
}
