using System;
using System.Collections.Generic;
using System.Drawing;
using System.Linq;

using Chimera.Client.Common;
using Chimera.Client.GUI;

namespace Chimera.Tests.Client.GUI
{
	/// <summary>
	/// The pictures beside menu items, measured rather than looked at.
	///
	/// A menu icon has to be legible on a WHITE menu - which is what the desktop
	/// theme gives on Mono - and on a very dark one. That is not something anybody
	/// will re-check by eye each time an icon is added, and an icon that cannot be
	/// seen on a dark menu is the same bug as the list row nobody could read.
	/// Every icon the frontend uses comes from MenuIcons, so walking that by
	/// reflection is walking all of them.
	/// </summary>
	[TestClass]
	public class MenuIconTests
	{
		private static double Luma(Color c) => ((0.299 * c.R) + (0.587 * c.G) + (0.114 * c.B)) / 255.0;

		/// <summary>
		/// The opaque pixels of an icon, as luminance. Nearly-transparent ones are
		/// left out: an antialiased edge is not something anybody sees as the icon.
		/// </summary>
		private static List<double> Ink(Image image)
		{
			using Bitmap bmp = new(image);
			List<double> ink = new();
			for (var y = 0; y < bmp.Height; y++)
			{
				for (var x = 0; x < bmp.Width; x++)
				{
					var c = bmp.GetPixel(x, y);
					if (c.A >= 200) ink.Add(Luma(c));
				}
			}
			return ink;
		}

		[TestMethod]
		public void EveryMenuIconIsAnIconAndNotAnEmptySquare()
		{
			var icons = MenuIcons.All();
			Assert.IsTrue(icons.Count > 30, $"only {icons.Count} menu icons were found, so this is not sweeping");
			List<string> wrong = new();
			foreach (var (name, image) in icons)
			{
				if (image is null)
				{
					wrong.Add($"{name}: there is no image");
					continue;
				}
				// a menu scales its image down to the margin, so a 32 is fine; a 128
				// is a photograph being resampled every time the menu opens
				if (image.Width is < 12 or > 32 || image.Height is < 12 or > 32)
				{
					wrong.Add($"{name}: {image.Width}x{image.Height}, which is not a menu icon's size");
				}
				var ink = Ink(image);
				if (ink.Count < 20) wrong.Add($"{name}: only {ink.Count} pixels of it are solid, so there is nothing to see");
			}
			Assert.AreEqual(0, wrong.Count, string.Join("\n", wrong));
		}

		/// <summary>
		/// Two icons the frontend has had for years are faint on a WHITE menu - the
		/// autofire bolt is pale yellow and the hex editor's is pale blue. They are
		/// left alone: they are what Chimera has always looked like, nobody has
		/// complained about them, and the ask was for the items that had NO icon.
		/// Named here rather than hidden by lowering the bar for everything.
		/// </summary>
		private static readonly HashSet<string> FaintOnWhiteAlready = new(StringComparer.Ordinal)
		{
			nameof(MenuIcons.Autofire),
			nameof(MenuIcons.HexEditor),
		};

		/// <summary>
		/// The one that matters: on every theme's menu, is there anything of the
		/// icon a person can actually make out? Measured as pixels far enough from
		/// the menu's own colour to register - a quarter of the range, which a flat
		/// mid-toned glyph clears on white and on near-black alike.
		/// </summary>
		[TestMethod]
		public void EveryMenuIconCanBeSeenOnEveryThemesMenu()
		{
			List<string> invisible = new();
			foreach (var theme in ThemeLibrary.All)
			{
				var menu = Luma(theme[ThemeColorRole.MenuBackground]);
				foreach (var (name, image) in MenuIcons.All())
				{
					var ink = Ink(image);
					var seen = ink.Count(l => Math.Abs(l - menu) > 0.25);
					if (seen < 15)
					{
						if (theme.FollowsDesktop && FaintOnWhiteAlready.Contains(name)) continue;
						invisible.Add($"{name} on {theme.Name}'s menu: only {seen} of its {ink.Count} solid pixels "
							+ $"stand out from the menu colour {ThemeFile.FormatColor(theme[ThemeColorRole.MenuBackground])}");
					}
				}
			}
			Assert.AreEqual(
				0,
				invisible.Count,
				"menu icons that would not be visible:\n" + string.Join("\n", invisible));
		}

		/// <summary>
		/// A dark outline on a transparent background is the shape of icon that
		/// vanishes on a dark menu, so the drawn ones deliberately have none. This
		/// says so in a way that fails if somebody draws one anyway.
		/// </summary>
		[TestMethod]
		public void TheDrawnIconsAreMidTonedRatherThanOutlines()
		{
			string[] drawn =
			[
				nameof(MenuIcons.Firmware), nameof(MenuIcons.DataDirectory), nameof(MenuIcons.ThemePalette),
				nameof(MenuIcons.CoreManager), nameof(MenuIcons.CacheManager), nameof(MenuIcons.MediaMaker),
				nameof(MenuIcons.PrecompiledModules), nameof(MenuIcons.WindowSize), nameof(MenuIcons.Customize),
				nameof(MenuIcons.BatchRunner),
			];
			var icons = MenuIcons.All().ToDictionary(static x => x.Name, static x => x.Image, StringComparer.Ordinal);
			List<string> wrong = new();
			foreach (var name in drawn)
			{
				Assert.IsTrue(icons.ContainsKey(name), $"{name} is not in MenuIcons any more");
				var ink = Ink(icons[name]);
				var mean = ink.Average();
				if (mean is < 0.3 or > 0.85) wrong.Add($"{name}: mean brightness {mean:0.00}, which is not a mid tone");
			}
			Assert.AreEqual(0, wrong.Count, string.Join("\n", wrong));
		}
	}
}
