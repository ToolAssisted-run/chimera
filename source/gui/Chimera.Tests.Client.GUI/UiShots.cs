using System.Collections.Generic;
using System.Drawing;
using System.Drawing.Imaging;
using System.IO;
using System.Linq;
using System.Runtime.InteropServices;
using System.Windows.Forms;

using Chimera.Client.Common;
using Chimera.Client.GUI;

namespace Chimera.Tests.Client.GUI
{
	/// <summary>
	/// Renders a window once per theme, looks at what came out, and writes it to a
	/// PNG if anybody asked for pictures.
	///
	/// It used to only write the pictures, for a person to look at. That was not
	/// enough: the core manager shipped with a chosen row that was a beige bar
	/// with invisible text on it, in a picture this very code had produced, while
	/// every property-reading test passed. A ListView paints its selected row, its
	/// column headers and the strip past its last column out of the desktop's
	/// colours and tells no property about it, so the only witness is the picture
	/// - and a picture nobody looks at is not a witness.
	///
	/// So every window photographed here is also put into the state that hid that
	/// bug - a row CHOSEN, under the dark theme - and the two places the toolkit
	/// used to leave its own colour behind are inspected: the band the column
	/// headers live in, all the way to the edge, and the band of a chosen row.
	/// </summary>
	internal static class UiShots
	{
		internal static string Dir => Environment.GetEnvironmentVariable("CHIMERA_UI_SHOTS");

		/// <summary>
		/// The toolkit's own beige, in the three shades it hands out. Not the
		/// whites: on Mono <c>SystemColors.Window</c>, <c>Menu</c> and
		/// <c>ControlLightLight</c> are all #FFFFFF, so looking for them is looking
		/// for white text. Text that has gone the colour of what is behind it is
		/// caught by measuring contrast instead.
		/// </summary>
		internal static readonly IReadOnlyList<(string Name, int Argb)> DesktopBeige =
		[
			("SystemColors.Control", SystemColors.Control.ToArgb()),
			("SystemColors.ControlLight", SystemColors.ControlLight.ToArgb()),
			("SystemColors.ActiveBorder", SystemColors.ActiveBorder.ToArgb()),
			("Color.WhiteSmoke", Color.WhiteSmoke.ToArgb()),
		];

		/// <summary>
		/// The pixels of a rectangle, as ARGB. Through LockBits and not GetPixel: a
		/// window is a million pixels and GetPixel is a call into GDI+ for each one,
		/// which turns a test run into a minute of nothing.
		/// </summary>
		internal static int[] Pixels(Bitmap bmp, Rectangle area)
		{
			area = Rectangle.Intersect(area, new Rectangle(0, 0, bmp.Width, bmp.Height));
			if (area.Width <= 0 || area.Height <= 0) return [];
			var data = bmp.LockBits(area, ImageLockMode.ReadOnly, PixelFormat.Format32bppArgb);
			try
			{
				var pixels = new int[area.Width * area.Height];
				for (var y = 0; y < area.Height; y++)
				{
					Marshal.Copy(data.Scan0 + (y * data.Stride), pixels, y * area.Width, area.Width);
				}
				return pixels;
			}
			finally
			{
				bmp.UnlockBits(data);
			}
		}

		/// <summary>How many of those pixels are each of the colours asked about.</summary>
		internal static Dictionary<string, int> CountOf(IReadOnlyList<int> pixels, IReadOnlyList<(string Name, int Argb)> wanted)
		{
			Dictionary<string, int> found = new(StringComparer.Ordinal);
			foreach (var argb in pixels)
			{
				foreach (var (name, value) in wanted)
				{
					if (argb != value) continue;
					found.TryGetValue(name, out var n);
					found[name] = n + 1;
				}
			}
			return found;
		}

		/// <summary>
		/// A block of one colour is a surface nobody themed; a handful of pixels is
		/// antialiasing on a glyph. Twenty is comfortably above the second, and the
		/// smallest real one of these - the strip past a last column - was eight
		/// hundred.
		/// </summary>
		private const int Block = 20;

		internal static double Luma(Color c) => ((0.299 * c.R) + (0.587 * c.G) + (0.114 * c.B)) / 255.0;

		/// <summary>
		/// A picture with nothing chosen in any list, and everything put back
		/// afterwards. The one surface whose drawing depends on the keyboard focus
		/// is a chosen row, so taking it out makes two pictures of the same window
		/// comparable whatever the focus has done in between.
		/// </summary>
		private static Bitmap CaptureWithNothingChosen(Form form)
		{
			List<ListView> views = new();
			List<ListBox> boxes = new();
			Collect(form, views, boxes);
			var wasChosen = views.ToDictionary(static v => v, static v => v.SelectedIndices.Cast<int>().ToArray());
			var wasIndex = boxes.ToDictionary(static b => b, static b => b.SelectedIndex);
			try
			{
				foreach (var view in views) view.SelectedIndices.Clear();
				foreach (var box in boxes) box.SelectedIndex = -1;
				return Capture(form);
			}
			finally
			{
				foreach (var view in views)
				{
					foreach (var i in wasChosen[view])
					{
						if (i >= 0 && i < view.Items.Count) view.Items[i].Selected = true;
					}
				}
				foreach (var box in boxes) box.SelectedIndex = wasIndex[box];
			}
		}

		private static Bitmap Capture(Form form)
		{
			// nothing active, in every picture: a list draws its chosen row in the
			// active highlight or the duller one depending on the focus, and a
			// control whose handle has been recreated - which is what turning owner
			// drawing off does - does not get it back. Taking the focus out of the
			// picture altogether is what makes two pictures of the same window
			// comparable.
			form.ActiveControl = null;
			form.Refresh();
			Application.DoEvents();
			Bitmap bmp = new(form.Width, form.Height);
			using (var g = Graphics.FromImage(bmp)) g.CopyFromScreen(form.Location, Point.Empty, form.Size);
			return bmp;
		}

		/// <summary>
		/// Shows the window, saves a picture of it per theme if CHIMERA_UI_SHOTS
		/// says where (<c>&lt;name&gt;.png</c> for the desktop theme,
		/// <c>&lt;name&gt;.&lt;theme&gt;.png</c> for the rest), and then checks its
		/// lists under the dark theme with something chosen in them.
		///
		/// The pictures are taken with nothing chosen, as they always were, so that
		/// they stay comparable with the ones from before any of this existed. The
		/// checking pass is separate and saves nothing.
		///
		/// The window is left showing and back in the Light theme.
		/// </summary>
		internal static void Shoot(Form form, string name)
		{
			var dir = Dir;
			form.Show();
			// The window as it looks having never worn anything but the desktop's
			// colours - the thing a switch back to that theme has to reproduce.
			// Taken with nothing chosen in any list, and compared against another
			// taken the same way: a chosen row is drawn in the active highlight or
			// the duller one depending on the keyboard focus, which a control whose
			// handle has been recreated does not keep. That is worth knowing and is
			// not what this is asking.
			ThemeEngine.Apply(form, ThemeLibrary.Select(FallbackName));
			var neverDark = CaptureWithNothingChosen(form);
			foreach (var themeName in ThemeLibrary.All.Select(static t => t.Name).ToList())
			{
				// Select, not just Apply: half the frontend asks ThemeLibrary.Current
				// for a colour as it paints, so a picture taken with a theme that is
				// not the current one is a picture of two themes at once
				var theme = ThemeLibrary.Select(themeName);
				ThemeEngine.Apply(form, theme);
				// the window is real and on a (headless) screen, so grab it from there:
				// DrawToBitmap skips the non-client area and mis-renders ListViews on Mono
				if (dir is null) continue;
				using var bmp = Capture(form);
				Directory.CreateDirectory(dir);
				var suffix = theme.FollowsDesktop ? "" : "." + theme.Name.ToLowerInvariant();
				bmp.Save(Path.Combine(dir, $"{name}{suffix}.png"), ImageFormat.Png);
			}

			var complaints = ListsUnderDark(form, name);
			complaints.AddRange(ComesBack(form, name, neverDark));
			neverDark.Dispose();

			ThemeEngine.Apply(form, ThemeLibrary.Select(ThemeLibrary.FallbackThemeName));
			form.Refresh();
			Application.DoEvents();
			Assert.AreEqual(0, complaints.Count, string.Join("\n", complaints));
		}

		/// <summary>
		/// Turns the desktop theme back on and checks the window looks the way it
		/// did before it ever went dark - the same window, photographed with
		/// nothing focused both times, so the only thing that can differ is what
		/// the theme did.
		///
		/// This is the check that was missing when switching from Dark to Light
		/// changed the title bar and nothing else: the theme was being applied and
		/// a repaint was happening, and the result was still dark.
		/// </summary>
		private static List<string> ComesBack(Form form, string name, Bitmap settled)
		{
			List<string> complaints = new();
			ThemeEngine.Apply(form, ThemeLibrary.Select(FallbackName));
			using var again = CaptureWithNothingChosen(form);
			var was = Pixels(settled, new Rectangle(0, 0, settled.Width, settled.Height));
			var now = Pixels(again, new Rectangle(0, 0, again.Width, again.Height));
			if (was.Length != now.Length)
			{
				complaints.Add($"{name}: the window is a different size after a theme has been on and off");
				return complaints;
			}
			var differences = 0;
			for (var i = 0; i < was.Length; i++)
			{
				if (was[i] != now[i]) differences++;
			}
			if (differences > Allowed(name))
			{
				if (Dir is not null)
				{
					Directory.CreateDirectory(Dir);
					settled.Save(Path.Combine(Dir, $"{name}.back.was.png"), ImageFormat.Png);
					again.Save(Path.Combine(Dir, $"{name}.back.now.png"), ImageFormat.Png);
					using Bitmap map = new(again.Width, again.Height);
					for (var y = 0; y < again.Height; y++)
					{
						for (var x = 0; x < again.Width; x++)
						{
							map.SetPixel(x, y, was[(y * again.Width) + x] == now[(y * again.Width) + x] ? Color.White : Color.Red);
						}
					}
					map.Save(Path.Combine(Dir, $"{name}.back.where.png"), ImageFormat.Png);
				}
				complaints.Add($"{name}: after Dark and back, {differences} pixels do not match the window as it was "
					+ "under the desktop theme - switching back has to put every surface back, not only the ones "
					+ "that theme assigns");
			}
			return complaints;
		}

		private const string FallbackName = "Light";

		/// <summary>
		/// What is still not coming back, window by window, as a budget rather than
		/// an exemption: these two are known and measured, and a regression that
		/// makes them worse still fails.
		///
		/// The wizard's settings step is a PropertyGrid, whose rules and left
		/// margin do not return to the shade they had. The encoder has two small
		/// controls that do not either. Both are a few thousand pixels of detail on
		/// windows whose colours otherwise round-trip exactly; neither is the bug
		/// this check was written for, and neither is fixed. See the theming notes.
		/// </summary>
		private static readonly IReadOnlyDictionary<string, int> KnownResidue = new Dictionary<string, int>(StringComparer.Ordinal)
		{
			["wizard-3-settings"] = 4600,
			["encode-video"] = 2200,
		};

		private static int Allowed(string name) => KnownResidue.TryGetValue(name, out var budget) ? budget : 40;

		/// <summary>
		/// Puts a row in every list, turns the dark theme on, and looks at the two
		/// bands the toolkit used to paint itself: the column headers, and a chosen
		/// row. Puts everything back afterwards.
		/// </summary>
		private static List<string> ListsUnderDark(Form form, string name)
		{
			List<string> complaints = new();
			List<ListView> views = new();
			List<ListBox> boxes = new();
			Collect(form, views, boxes);
			if (views.Count is 0 && boxes.Count is 0) return complaints;

			var wasHiding = views.ToDictionary(static v => v, static v => v.HideSelection);
			var wasChosen = views.ToDictionary(static v => v, static v => v.SelectedIndices.Cast<int>().ToArray());
			var wasIndex = boxes.ToDictionary(static b => b, static b => b.SelectedIndex);
			var theme = ThemeLibrary.Select("Dark");
			try
			{
				foreach (var view in views.Where(static v => v.Items.Count > 0))
				{
					view.HideSelection = false;
					view.Items[0].Selected = true;
				}
				foreach (var box in boxes.Where(static b => b.Items.Count > 0)) box.SelectedIndex = 0;
				ThemeEngine.Apply(form, theme);
				using var bmp = Capture(form);

				foreach (var view in views.Where(static v => v.View is View.Details && v.Items.Count > 0))
				{
					var gutter = SystemInformation.VerticalScrollBarWidth + 2;
					var width = Math.Max(0, view.ClientSize.Width - gutter);
					var headerHeight = Math.Max(0, view.Items[0].Bounds.Top);
					if (headerHeight > 0)
					{
						Beige(bmp, form, view, new Rectangle(0, 0, width, headerHeight), $"{name}: the column header band", complaints);
					}
					var row = view.Items[0].Bounds;
					Beige(bmp, form, view, new Rectangle(0, row.Top, width, row.Height), $"{name}: the chosen row", complaints);
					if (view.Columns.Count > 1 && view.Items[0].SubItems.Count > 1)
					{
						Readable(bmp, form, view, view.Items[0].SubItems[1].Bounds, $"{name}: the chosen row's text", complaints);
					}
				}
				foreach (var box in boxes.Where(static b => b.Items.Count > 0))
				{
					Beige(bmp, form, box, box.GetItemRectangle(0), $"{name}: the chosen line", complaints);
				}
			}
			finally
			{
				foreach (var view in views)
				{
					view.SelectedIndices.Clear();
					foreach (var i in wasChosen[view]) view.Items[i].Selected = true;
					view.HideSelection = wasHiding[view];
				}
				foreach (var box in boxes) box.SelectedIndex = wasIndex[box];
			}
			return complaints;
		}

		private static void Collect(Control root, List<ListView> views, List<ListBox> boxes)
		{
			if (root is ListView view) views.Add(view);
			else if (root is ListBox box) boxes.Add(box);
			foreach (Control child in root.Controls) Collect(child, views, boxes);
		}

		/// <summary>A rectangle of a control, in the picture's coordinates.</summary>
		private static Rectangle InPicture(Form form, Control control, Rectangle area)
		{
			var screen = control.RectangleToScreen(area);
			screen.Offset(-form.Left, -form.Top);
			return screen;
		}

		private static void Beige(Bitmap bmp, Form form, Control control, Rectangle area, string what, List<string> complaints)
		{
			var box = Rectangle.Intersect(InPicture(form, control, area), new Rectangle(0, 0, bmp.Width, bmp.Height));
			if (box.Width < 8 || box.Height < 2) return;
			foreach (var kvp in CountOf(Pixels(bmp, box), DesktopBeige).Where(static kvp => kvp.Value > Block))
			{
				complaints.Add($"{what} is still {kvp.Key} on {kvp.Value} pixels under the dark theme");
			}
		}

		private static void Readable(Bitmap bmp, Form form, Control control, Rectangle area, string what, List<string> complaints)
		{
			var box = Rectangle.Intersect(InPicture(form, control, area), new Rectangle(0, 0, bmp.Width, bmp.Height));
			if (box.Width < 20 || box.Height < 4) return;
			double darkest = 1, brightest = 0;
			foreach (var argb in Pixels(bmp, box))
			{
				var l = Luma(Color.FromArgb(argb));
				if (l < darkest) darkest = l;
				if (l > brightest) brightest = l;
			}
			if (brightest - darkest <= 0.2)
			{
				complaints.Add($"{what} is {brightest - darkest:0.00} of contrast from end to end, "
					+ "which is text the same colour as what it is written on");
			}
		}
	}
}
