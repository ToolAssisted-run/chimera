using System.Drawing;
using System.IO;
using System.Windows.Forms;

namespace Chimera.Client.EmuHawk.Properties
{
	internal static class Resources
	{
		/// <param name="filename">Dir separator is '<c>.</c>'. Filename is relative to <c>&lt;NS>/images</c> and omits <c>.png</c> extension.</param>
		private static Bitmap ReadEmbeddedBitmap(string filename) => new Bitmap(ReflectionCache.EmbeddedResourceStream($"images.{filename}.png"));

		/// <param name="filename">Dir separator is '<c>.</c>'. Filename is relative to <c>&lt;NS>/images</c> and omits <c>.ico</c> extension.</param>
		private static Icon ReadEmbeddedIcon(string filename) => new Icon(ReflectionCache.EmbeddedResourceStream($"images.{filename}.ico"));

		/// <param name="filename">Dir separator is '<c>.</c>'. Filename is relative to <c>&lt;NS>/images</c> and omits <c>.ico</c> extension.</param>
		private static Bitmap ReadEmbeddedIconAsBitmap(string filename) => new Bitmap(ReflectionCache.EmbeddedResourceStream($"images.{filename}.ico"));


		internal static readonly Bitmap Add = ReadEmbeddedBitmap("add");
		internal static readonly Bitmap AddEdit = ReadEmbeddedBitmap("AddEdit");
		internal static readonly Bitmap AddWatch = ReadEmbeddedIconAsBitmap("addWatch");
		internal static readonly Bitmap ArrowBlackDown = ReadEmbeddedBitmap("arrow_black_down");
		internal static readonly Bitmap Audio = ReadEmbeddedBitmap("AudioHS");
		internal static readonly Bitmap AudioMuted = ReadEmbeddedBitmap("AudioMuted");
		internal static readonly Bitmap AutoSearch = ReadEmbeddedBitmap("AutoSearch");
		internal static readonly Bitmap Avi = ReadEmbeddedBitmap("AVI");
		internal static readonly Bitmap Back = ReadEmbeddedBitmap("Back");
		internal static readonly Bitmap BackMore = ReadEmbeddedBitmap("BackMore");
		internal static readonly Bitmap Blank = ReadEmbeddedBitmap("Blank");
		internal static readonly Lazy<Cursor> BlankCursor = new(static () => new(ReflectionCache.EmbeddedResourceStream("images.BlankCursor.cur")));
		internal static readonly Bitmap BlueDown = ReadEmbeddedBitmap("BlueDown");
		internal static readonly Bitmap BlueUp = ReadEmbeddedBitmap("BlueUp");
		internal static readonly Bitmap Both = ReadEmbeddedBitmap("Both");
		internal static readonly Icon BugIcon = ReadEmbeddedIcon("Bug");
		internal static readonly Bitmap Bug = ReadEmbeddedBitmap("Bug");
		internal static readonly Bitmap C64Symbol = ReadEmbeddedBitmap("C64Symbol");
		internal static readonly Bitmap Camera = ReadEmbeddedBitmap("camera");
		internal static readonly Bitmap Cheat = ReadEmbeddedBitmap("Freeze");
		internal static readonly Icon CheatIcon = ReadEmbeddedIcon("Freeze");
		internal static readonly Bitmap Checkbox = ReadEmbeddedBitmap("checkbox");
		internal static readonly Bitmap Circle = ReadEmbeddedBitmap("Circle");
		internal static readonly Bitmap ClearConsole = ReadEmbeddedBitmap("clear_console");
		internal static readonly Bitmap Close = ReadEmbeddedBitmap("Close");
		internal static readonly Icon CommandWindow = ReadEmbeddedIcon("commandWindow");
		internal static readonly Bitmap Connect16X16 = ReadEmbeddedBitmap("connect_16x16");
		internal static readonly Bitmap CopyFolder = ReadEmbeddedBitmap("CopyFolderHS");
		internal static readonly Bitmap Chimera = ReadEmbeddedBitmap("chimera");
		internal static readonly Bitmap ChimeraSmall = ReadEmbeddedBitmap("ChimeraSmall");
		internal static readonly Bitmap Cross = ReadEmbeddedBitmap("Cross");
		internal static readonly Bitmap Cut = ReadEmbeddedBitmap("CutHS");
		internal static readonly Bitmap Debugger = ReadEmbeddedBitmap("Debugger");
		internal static readonly Bitmap Delete = ReadEmbeddedBitmap("Delete");
		internal static readonly Bitmap Duplicate = ReadEmbeddedBitmap("Duplicate");
		internal static readonly Bitmap ENE = ReadEmbeddedBitmap("ENE");
		internal static readonly Bitmap Erase = ReadEmbeddedBitmap("Erase");
		internal static readonly Bitmap ESE = ReadEmbeddedBitmap("ESE");
		internal static readonly Bitmap ExclamationRed = ReadEmbeddedBitmap("ExclamationRed");
		internal static readonly Bitmap FastForward = ReadEmbeddedBitmap("FastForward");
		internal static readonly Bitmap Find = ReadEmbeddedBitmap("FindHS");
		internal static readonly Bitmap Forward = ReadEmbeddedBitmap("Forward");
		internal static readonly Bitmap Freeze = ReadEmbeddedBitmap("Freeze");
		internal static readonly Bitmap Fullscreen = ReadEmbeddedBitmap("Fullscreen");
		internal static readonly Icon GameControllerIcon = ReadEmbeddedIcon("GameController");
		internal static readonly Bitmap GameController = ReadEmbeddedBitmap("GameController");
		internal static readonly Bitmap GreenCheck = ReadEmbeddedBitmap("GreenCheck");
		internal static readonly Bitmap Hack = ReadEmbeddedBitmap("Hack");
		internal static readonly Bitmap Help = ReadEmbeddedBitmap("Help");
		internal static readonly Bitmap HomeBrew = ReadEmbeddedBitmap("HomeBrew");
		internal static readonly Icon HotKeysIcon = ReadEmbeddedIcon("HotKeys");
		internal static readonly Bitmap HotKeys = ReadEmbeddedBitmap("HotKeys");
		internal static readonly Bitmap InsertSeparator = ReadEmbeddedBitmap("InsertSeparator");
		internal static readonly Bitmap JumpTo = ReadEmbeddedBitmap("JumpTo");
		internal static readonly Bitmap KitchenSink = ReadEmbeddedBitmap("kitchensink");
		internal static readonly Icon LightningIcon = ReadEmbeddedIcon("Lightning");
		internal static readonly Bitmap Lightning = ReadEmbeddedBitmap("Lightning");
		internal static readonly Bitmap LightOff = ReadEmbeddedBitmap("LightOff");
		internal static readonly Bitmap LightOn = ReadEmbeddedBitmap("LightOn");
		internal static readonly Bitmap LoadConfig = ReadEmbeddedBitmap("LoadConfig");
		internal static readonly Icon Logo = ReadEmbeddedIcon("logo");
		internal static readonly Bitmap LuaPictureBox = ReadEmbeddedBitmap("luaPictureBox");
		internal static readonly Bitmap MessageConfig = ReadEmbeddedBitmap("MessageConfig");
		internal static readonly Icon MonitorIcon = ReadEmbeddedIcon("monitor");
		internal static readonly Bitmap MoveBottom = ReadEmbeddedBitmap("MoveBottom");
		internal static readonly Bitmap MoveDown = ReadEmbeddedBitmap("MoveDown");
		internal static readonly Bitmap MoveLeft = ReadEmbeddedBitmap("MoveLeft");
		internal static readonly Bitmap MoveRight = ReadEmbeddedBitmap("MoveRight");
		internal static readonly Bitmap MoveTop = ReadEmbeddedBitmap("MoveTop");
		internal static readonly Bitmap MoveUp = ReadEmbeddedBitmap("MoveUp");
		internal static readonly Icon MsgBoxIcon = ReadEmbeddedIcon("MsgBox");
		internal static readonly Bitmap NE = ReadEmbeddedBitmap("NE");
		internal static readonly Bitmap NewFile = ReadEmbeddedBitmap("NewFile");
		internal static readonly Bitmap NNE = ReadEmbeddedBitmap("NNE");
		internal static readonly Bitmap NNW = ReadEmbeddedBitmap("NNW");
		internal static readonly Bitmap NoConnect16X16 = ReadEmbeddedBitmap("noconnect_16x16");
		internal static readonly Bitmap NW = ReadEmbeddedBitmap("NW");
		internal static readonly Bitmap OpenFile = ReadEmbeddedBitmap("OpenFile");
		internal static readonly Bitmap Paste = ReadEmbeddedBitmap("Paste");
		internal static readonly Bitmap Pause = ReadEmbeddedBitmap("Pause");
		internal static readonly Bitmap LuaRunning = ReadEmbeddedBitmap("arrow_green");
		internal static readonly Bitmap Pencil = ReadEmbeddedBitmap("pencil");
		internal static readonly Bitmap Clock = ReadEmbeddedBitmap("Clock");
		internal static readonly Bitmap Play = ReadEmbeddedBitmap("Play");
		internal static readonly Bitmap Placeholder = ReadEmbeddedBitmap("placeholder_bitmap");
		internal static readonly Icon PokeIcon = ReadEmbeddedIcon("poke");
		internal static readonly Bitmap Poke = ReadEmbeddedBitmap("poke");
		internal static readonly Bitmap Previous = ReadEmbeddedBitmap("Previous");
		internal static readonly Bitmap ReadOnly = ReadEmbeddedBitmap("ReadOnly");
		internal static readonly Bitmap Reboot = ReadEmbeddedBitmap("reboot");
		internal static readonly Bitmap Recent = ReadEmbeddedBitmap("Recent");
		internal static readonly Bitmap Record = ReadEmbeddedBitmap("RecordHS");
		internal static readonly Bitmap Redo = ReadEmbeddedBitmap("redo");
		internal static readonly Bitmap Refresh = ReadEmbeddedBitmap("Refresh");
		internal static readonly Bitmap Restart = ReadEmbeddedBitmap("restart");
		internal static readonly Bitmap RetroQuestion = ReadEmbeddedBitmap("RetroQuestion");
		internal static readonly Bitmap RewindRecord = ReadEmbeddedBitmap("RewindRecord");
		internal static readonly Bitmap Save = ReadEmbeddedBitmap("Save");
		internal static readonly Bitmap SaveAs = ReadEmbeddedBitmap("SaveAs");
		internal static readonly Bitmap Scan = ReadEmbeddedBitmap("Scan");
		internal static readonly Bitmap ScrollTo = ReadEmbeddedBitmap("ScrollTo");
		internal static readonly Bitmap SE = ReadEmbeddedBitmap("SE");
		internal static readonly Bitmap Search = ReadEmbeddedBitmap("search");
		internal static readonly Icon SearchIcon = ReadEmbeddedIcon("search");
		internal static readonly Bitmap Split = ReadEmbeddedBitmap("Split");
		internal static readonly Bitmap Square = ReadEmbeddedBitmap("Square");
		internal static readonly Bitmap SSE = ReadEmbeddedBitmap("SSE");
		internal static readonly Bitmap SSW = ReadEmbeddedBitmap("SSW");
		internal static readonly Bitmap Stop = ReadEmbeddedBitmap("Stop");
		internal static readonly Bitmap SW = ReadEmbeddedBitmap("SW");
		internal static readonly Icon TAStudioIcon = ReadEmbeddedIcon("TAStudio");
		internal static readonly Bitmap TAStudio = ReadEmbeddedBitmap("TAStudio");
		internal static readonly Bitmap TextDoc = ReadEmbeddedBitmap("textdoc");
		internal static readonly Icon TextDocIcon = ReadEmbeddedIcon("textdoc");
		internal static readonly Icon ToolBoxIcon = ReadEmbeddedIcon("ToolBox");
		internal static readonly Bitmap ToolBox = ReadEmbeddedBitmap("ToolBox");
		internal static readonly Bitmap Translation = ReadEmbeddedBitmap("Translation");
		internal static readonly Bitmap Triangle = ReadEmbeddedBitmap("Triangle");
		internal static readonly Bitmap TruncateFromFile = ReadEmbeddedBitmap("TruncateFromFile");
		internal static readonly Bitmap TvIcon = ReadEmbeddedBitmap("tvIcon");
		internal static readonly Bitmap Undo = ReadEmbeddedBitmap("undo");
		internal static readonly Bitmap Unfreeze = ReadEmbeddedBitmap("Unfreeze");
		internal static readonly Bitmap UpdateBranch = ReadEmbeddedBitmap("updateBranch");
		internal static readonly Bitmap UpdateWithText = ReadEmbeddedBitmap("updateWithText");
		internal static readonly Bitmap Watch = ReadEmbeddedIconAsBitmap("watch");
		internal static readonly Icon WatchIcon = ReadEmbeddedIcon("watch");
		internal static readonly Bitmap WNW = ReadEmbeddedBitmap("WNW");
		internal static readonly Bitmap WSW = ReadEmbeddedBitmap("WSW");
		internal static readonly Bitmap YellowDown = ReadEmbeddedBitmap("YellowDown");
		internal static readonly Bitmap YellowLeft = ReadEmbeddedBitmap("YellowLeft");
		internal static readonly Bitmap YellowRight = ReadEmbeddedBitmap("YellowRight");
		internal static readonly Bitmap YellowUp = ReadEmbeddedBitmap("YellowUp");

		internal static Stream GetNotHawkCallSFX()
			=> ReflectionCache.EmbeddedResourceStream("Resources.nothawk.wav");
	}
}
