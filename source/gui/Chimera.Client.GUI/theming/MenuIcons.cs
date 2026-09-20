#nullable enable

using System.Collections.Generic;
using System.Drawing;
using System.Drawing.Drawing2D;
using System.Linq;
using System.Reflection;

using Chimera.Client.GUI.Properties;

namespace Chimera.Client.GUI
{
	/// <summary>
	/// Every image that goes beside a menu item, in one place.
	///
	/// It is one place for two reasons. A menu where half the items have an icon
	/// looks broken, so the gaps have to be visible as gaps - and they are, by
	/// reading down this file. And an icon has to be legible on a WHITE menu and
	/// on a very dark one, which is not something anybody will check by eye every
	/// time; MenuIconTests walks these by reflection and measures them against
	/// every theme's menu colour, so an icon that cannot be seen is a failing
	/// test rather than a bug report.
	///
	/// Most are the images the frontend already had. The handful that had no
	/// sensible match are drawn here, the way CacheManagerForm draws its padlocks
	/// and FirmwareSurveyForm its ticks - flat, mid-toned and without a dark
	/// outline, because a dark outline is exactly what disappears on a dark menu.
	///
	/// Not everything gets one, on purpose:
	/// <list type="bullet">
	/// <item>A menu item that can be TICKED shows its tick in the image margin,
	/// so an image there fights the tick. Every Display-this toggle, every
	/// throttle mode, every window scale is deliberately bare.</item>
	/// <item>A label that is not clickable - "Loaded core: ...", the movie status
	/// line - is not a command and does not get a command's icon.</item>
	/// <item>Exit, which sits alone after a separator, and which no icon says
	/// anything useful about.</item>
	/// </list>
	/// </summary>
	public static class MenuIcons
	{
		// ---- the ones the frontend already had -----------------------------

		public static Image CloseProject => Resources.Close;

		public static Image EncodeVideo => Resources.Avi;

		public static Image Screenshot => Resources.Camera;

		public static Image Pause => Resources.Pause;

		public static Image RebootCore => Resources.Reboot;

		public static Image Fullscreen => Resources.Fullscreen;

		public static Image Controllers => Resources.GameController;

		public static Image Hotkeys => Resources.HotKeys;

		public static Image Display => Resources.TvIcon;

		public static Image Sound => Resources.Audio;

		public static Image Paths => Resources.CopyFolder;

		public static Image Messages => Resources.MessageConfig;

		public static Image Autofire => Resources.Lightning;

		public static Image SaveConfig => Resources.Save;

		public static Image LoadConfig => Resources.LoadConfig;

		public static Image RamWatch => Resources.Watch;

		public static Image RamSearch => Resources.Search;

		public static Image LuaConsole => Resources.TextDoc;

		public static Image TAStudio => Resources.TAStudio;

		public static Image HexEditor => Resources.Poke;

		public static Image Debugger => Resources.Bug;

		public static Image OnlineHelp => Resources.Help;

		public static Image About => Resources.ChimeraSmall;

		// ---- the gaps, filled with something the frontend already had ------

		public static Image NewProject => Resources.NewFile;

		public static Image OpenProject => Resources.OpenFile;

		public static Image RecentProjects => Resources.Recent;

		public static Image SaveProject => Resources.Save;

		public static Image SaveProjectAs => Resources.SaveAs;

		/// <summary>A backup is a second copy of the project, which is what this says.</summary>
		public static Image SaveProjectBackup => Resources.Duplicate;

		public static Image ScreenshotAs => Resources.SaveAs;

		public static Image LogWindow => Resources.TextDoc;

		/// <summary>The same picture the key-priority status bar icon has used for years.</summary>
		public static Image KeyPriority => Resources.Both;

		public static Image SpeedSkip => Resources.Clock;

		public static Image SaveConfigAs => Resources.SaveAs;

		public static Image LoadConfigFrom => Resources.OpenFile;

		/// <summary>A macro is a recorded run of inputs.</summary>
		public static Image MacroTool => Resources.Record;

		// ---- the gaps nothing already had, drawn here ----------------------
		//
		// Three of these had a picture in the frontend already and it was measured
		// and rejected: the monitor is 128 pixels square, which is a photograph in
		// a menu; the play triangle is twenty solid pixels of near-black; the
		// pencil is drawn almost entirely in outline. All three vanish on a dark
		// menu, which is the thing this whole exercise is about.

		/// <summary>Window size: a screen with a corner being pulled.</summary>
		public static Image WindowSize { get; } = Draw(static g =>
		{
			using SolidBrush frame = new(Color.FromArgb(0x7E, 0x8A, 0x9A));
			using SolidBrush screen = new(Color.FromArgb(0xBC, 0xD4, 0xE8));
			using SolidBrush grip = new(Color.FromArgb(0xD8, 0xA8, 0x50));
			g.FillRectangle(frame, 4, 8, 56, 42);
			g.FillRectangle(screen, 9, 13, 46, 32);
			g.FillRectangle(frame, 24, 50, 16, 6);
			Point[] corner = [ new(38, 46), new(58, 46), new(58, 26) ];
			g.FillPolygon(grip, corner);
		});

		/// <summary>Customize: the sliders of a settings panel.</summary>
		public static Image Customize { get; } = Draw(static g =>
		{
			using SolidBrush rail = new(Color.FromArgb(0x8A, 0x94, 0xA0));
			using SolidBrush knob = new(Color.FromArgb(0x60, 0xA8, 0xD8));
			int[] rows = [ 14, 32, 50 ];
			int[] knobs = [ 40, 18, 32 ];
			for (var i = 0; i < 3; i++)
			{
				g.FillRectangle(rail, 4, rows[i] - 3, 56, 6);
				g.FillRectangle(knob, knobs[i], rows[i] - 9, 10, 18);
			}
		});

		/// <summary>The batch runner: a list of runs, played through.</summary>
		public static Image BatchRunner { get; } = Draw(static g =>
		{
			using SolidBrush line = new(Color.FromArgb(0x9A, 0xA4, 0xB0));
			using SolidBrush go = new(Color.FromArgb(0x5C, 0xB8, 0x6C));
			for (var i = 0; i < 3; i++)
			{
				g.FillRectangle(line, 4, 10 + (i * 14), 34, 7);
			}
			Point[] play = [ new(30, 34), new(58, 47), new(30, 60) ];
			g.FillPolygon(go, play);
		});


		/// <summary>Firmware: a chip.</summary>
		public static Image Firmware { get; } = Draw(static g =>
		{
			using SolidBrush body = new(Color.FromArgb(0x5A, 0x8C, 0xC8));
			using SolidBrush pins = new(Color.FromArgb(0xC8, 0xC0, 0x78));
			for (var i = 0; i < 3; i++)
			{
				var y = 16 + (i * 16);
				g.FillRectangle(pins, 2, y, 12, 6);
				g.FillRectangle(pins, 50, y, 12, 6);
			}
			g.FillRectangle(body, 12, 12, 40, 40);
			using SolidBrush dot = new(Color.FromArgb(0xB8, 0xD8, 0xF8));
			g.FillEllipse(dot, 18, 18, 8, 8);
		});

		/// <summary>The data directory: a disk platter.</summary>
		public static Image DataDirectory { get; } = Draw(static g =>
		{
			using SolidBrush body = new(Color.FromArgb(0x8A, 0x92, 0x9E));
			using SolidBrush face = new(Color.FromArgb(0xD2, 0xD8, 0xE0));
			using SolidBrush hole = new(Color.FromArgb(0x50, 0x58, 0x64));
			g.FillEllipse(body, 2, 6, 60, 52);
			g.FillEllipse(face, 8, 12, 48, 40);
			g.FillEllipse(hole, 26, 26, 12, 12);
		});

		/// <summary>The theme: a paint palette.</summary>
		public static Image ThemePalette { get; } = Draw(static g =>
		{
			using SolidBrush body = new(Color.FromArgb(0xC8, 0xA0, 0x60));
			g.FillEllipse(body, 2, 6, 60, 52);
			using SolidBrush thumb = new(Color.FromArgb(0x30, 0x30, 0x30));
			g.FillEllipse(thumb, 40, 34, 16, 16);
			Color[] blobs = [ Color.FromArgb(0xD0, 0x50, 0x50), Color.FromArgb(0x50, 0xB0, 0x60), Color.FromArgb(0x60, 0x90, 0xD8) ];
			int[] xs = [ 10, 22, 36 ];
			int[] ys = [ 26, 14, 14 ];
			for (var i = 0; i < 3; i++)
			{
				using SolidBrush blob = new(blobs[i]);
				g.FillEllipse(blob, xs[i], ys[i], 14, 14);
			}
		});

		/// <summary>The core manager: a package.</summary>
		public static Image CoreManager { get; } = Draw(static g =>
		{
			using SolidBrush body = new(Color.FromArgb(0xC0, 0x96, 0x5A));
			using SolidBrush lid = new(Color.FromArgb(0xD8, 0xB4, 0x80));
			using SolidBrush tape = new(Color.FromArgb(0x8A, 0x66, 0x38));
			g.FillRectangle(body, 6, 20, 52, 38);
			g.FillRectangle(lid, 4, 10, 56, 12);
			g.FillRectangle(tape, 28, 10, 8, 48);
		});

		/// <summary>The cache manager: a stack of what is being kept.</summary>
		public static Image CacheManager { get; } = Draw(static g =>
		{
			using SolidBrush top = new(Color.FromArgb(0x7C, 0xB0, 0x88));
			using SolidBrush side = new(Color.FromArgb(0x4E, 0x80, 0x5C));
			for (var i = 2; i >= 0; i--)
			{
				var y = 8 + (i * 17);
				g.FillEllipse(side, 4, y + 6, 56, 18);
				g.FillRectangle(side, 4, y + 6, 56, 9);
				g.FillEllipse(top, 4, y, 56, 18);
			}
		});

		/// <summary>The media maker: a disc.</summary>
		public static Image MediaMaker { get; } = Draw(static g =>
		{
			using SolidBrush body = new(Color.FromArgb(0x9E, 0xAE, 0xC4));
			using SolidBrush sheen = new(Color.FromArgb(0xE0, 0xE8, 0xF4));
			using SolidBrush hole = new(Color.FromArgb(0x40, 0x46, 0x50));
			g.FillEllipse(body, 2, 2, 60, 60);
			g.FillPie(sheen, 2, 2, 60, 60, 200, 80);
			g.FillEllipse(hole, 24, 24, 16, 16);
		});

		/// <summary>Pre-compiled modules: a cog, for work already done.</summary>
		public static Image PrecompiledModules { get; } = Draw(static g =>
		{
			using SolidBrush teeth = new(Color.FromArgb(0x92, 0x9C, 0xA8));
			using SolidBrush body = new(Color.FromArgb(0xC4, 0xCC, 0xD6));
			using SolidBrush hole = new(Color.FromArgb(0x4A, 0x52, 0x5C));
			for (var i = 0; i < 8; i++)
			{
				var state = g.Save();
				g.TranslateTransform(32, 32);
				g.RotateTransform(i * 45f);
				g.FillRectangle(teeth, -7, -32, 14, 20);
				g.Restore(state);
			}
			g.FillEllipse(body, 8, 8, 48, 48);
			g.FillEllipse(hole, 24, 24, 16, 16);
		});

		/// <summary>Every icon above, by name, for the test that measures them.</summary>
		public static IReadOnlyList<(string Name, Image Image)> All()
			=> typeof(MenuIcons).GetProperties(BindingFlags.Public | BindingFlags.Static)
				.Where(static p => p.PropertyType == typeof(Image))
				.Select(static p => (p.Name, (Image) p.GetValue(null)!))
				.OrderBy(static x => x.Name, StringComparer.Ordinal)
				.ToList();

		/// <summary>
		/// Renders a glyph at four times the size with antialiasing and brings it
		/// back down, which is the difference between a 16-pixel icon that looks
		/// drawn and one that looks like a mistake.
		/// </summary>
		private static Image Draw(Action<Graphics> glyph)
		{
			const int Big = 64;
			const int Small = 16;
			using Bitmap large = new(Big, Big);
			using (var g = Graphics.FromImage(large))
			{
				g.SmoothingMode = SmoothingMode.AntiAlias;
				g.Clear(Color.Transparent);
				glyph(g);
			}
			Bitmap small = new(Small, Small);
			using (var g = Graphics.FromImage(small))
			{
				g.InterpolationMode = InterpolationMode.HighQualityBicubic;
				g.PixelOffsetMode = PixelOffsetMode.HighQuality;
				g.Clear(Color.Transparent);
				g.DrawImage(large, new Rectangle(0, 0, Small, Small), 0, 0, Big, Big, GraphicsUnit.Pixel);
			}
			return small;
		}
	}
}
