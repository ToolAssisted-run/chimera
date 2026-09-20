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

		private static Bitmap Capture(Form form)
		{
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
			foreach (var themeName in ThemeLibrary.All.Select(static t => t.Name).ToList())
			{
				// Select, not just Apply: half the frontend asks ThemeLibrary.Current
				// for a colour as it paints, so a picture taken with a theme that is
				// not the current one is a picture of two themes at once
				var theme = ThemeLibrary.Select(themeName);
				ThemeEngine.Apply(form, theme);
				if (dir is null) continue;
				// the window is real and on a (headless) screen, so grab it from there:
				// DrawToBitmap skips the non-client area and mis-renders ListViews on Mono
				using var bmp = Capture(form);
				Directory.CreateDirectory(dir);
				var suffix = theme.FollowsDesktop ? "" : "." + theme.Name.ToLowerInvariant();
				bmp.Save(Path.Combine(dir, $"{name}{suffix}.png"), ImageFormat.Png);
			}

			var complaints = ListsUnderDark(form, name);

			ThemeEngine.Apply(form, ThemeLibrary.Select(ThemeLibrary.FallbackThemeName));
			form.Refresh();
			Application.DoEvents();
			Assert.AreEqual(0, complaints.Count, string.Join("\n", complaints));
		}

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
