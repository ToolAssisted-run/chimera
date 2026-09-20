using System.Drawing;
using System.Linq;
using System.Drawing.Imaging;
using System.IO;
using System.Windows.Forms;

using Chimera.Client.Common;
using Chimera.Client.GUI;

namespace Chimera.Tests.Client.GUI
{
	/// <summary>
	/// Rendering a window to a PNG so a person can look at it, once per theme.
	///
	/// Every automated check in this project asserts behaviour, which is the part
	/// a machine can judge. Whether a window is legible in the dark theme is not,
	/// so a shot is taken in each theme on offer - <c>&lt;name&gt;.png</c> for the
	/// Light one, <c>&lt;name&gt;.dark.png</c> for the rest - and a person compares
	/// them.
	/// </summary>
	internal static class UiShots
	{
		internal static string Dir => Environment.GetEnvironmentVariable("CHIMERA_UI_SHOTS");

		/// <summary>
		/// Shows the window and captures it once per theme. The window is left
		/// showing and in the Light theme, so a caller that goes on to drive it
		/// sees what it saw before.
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
				form.Refresh();
				Application.DoEvents();
				// the window is real and on a (headless) screen, so grab it from there:
				// DrawToBitmap skips the non-client area and mis-renders ListViews on Mono
				using Bitmap bmp = new(form.Width, form.Height);
				using (var g = Graphics.FromImage(bmp))
				{
					g.CopyFromScreen(form.Location, Point.Empty, form.Size);
				}
				Directory.CreateDirectory(dir);
				var suffix = theme.Name.Equals(ThemeLibrary.DefaultThemeName, StringComparison.OrdinalIgnoreCase)
					? ""
					: "." + theme.Name.ToLowerInvariant();
				bmp.Save(Path.Combine(dir, $"{name}{suffix}.png"), ImageFormat.Png);
			}
			ThemeEngine.Apply(form, ThemeLibrary.Select(ThemeLibrary.DefaultThemeName));
			form.Refresh();
			Application.DoEvents();
		}
	}
}
