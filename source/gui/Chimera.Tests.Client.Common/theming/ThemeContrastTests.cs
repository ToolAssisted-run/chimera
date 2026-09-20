using System.Collections.Generic;
using System.Drawing;
using System.Linq;

using Chimera.Client.Common;

namespace Chimera.Tests.Client.Common
{
	/// <summary>
	/// Every place the frontend puts text on a background, the two have to be
	/// different enough to read. A theme file is ninety numbers somebody typed,
	/// and two of them being the same shade is not a thing anyone will spot by
	/// reading the file - it is a thing they find later, as a row of a list that
	/// looks blank.
	///
	/// Measured as the WCAG contrast ratio, which is what the accessibility world
	/// settled on and is the only one with agreed numbers behind it: 1 is the same
	/// colour, 21 is black on white, 3 is the floor for anything a person has to
	/// be able to pick out.
	///
	/// The theme that follows the desktop is exempt, and deliberately: those are
	/// the desktop's colours, they are what Chimera has always had, and they are
	/// not ours to refuse. Every other theme - Dark, and anything a contributor
	/// writes - answers for itself.
	/// </summary>
	[TestClass]
	public class ThemeContrastTests
	{
		/// <summary>The floor. Below this, text stops being something a person can pick out of its background.</summary>
		private const double Floor = 3.0;

		private static double Channel(int v)
		{
			var c = v / 255.0;
			return c <= 0.03928 ? c / 12.92 : Math.Pow((c + 0.055) / 1.055, 2.4);
		}

		private static double Luminance(Color c)
			=> (0.2126 * Channel(c.R)) + (0.7152 * Channel(c.G)) + (0.0722 * Channel(c.B));

		/// <summary>
		/// The WCAG ratio between two colours. A colour with alpha is a wash over
		/// the other one, which is how the frontend actually draws those two, so it
		/// is composited before being measured rather than compared as if it were
		/// opaque.
		/// </summary>
		private static double Ratio(Color ink, Color ground)
		{
			var over = Over(ink, ground);
			var a = Luminance(over);
			var b = Luminance(ground);
			return a > b ? (a + 0.05) / (b + 0.05) : (b + 0.05) / (a + 0.05);
		}

		private static Color Over(Color top, Color under)
		{
			if (top.A is 255) return top;
			var alpha = top.A / 255.0;
			return Color.FromArgb(
				255,
				(int) ((top.R * alpha) + (under.R * (1 - alpha))),
				(int) ((top.G * alpha) + (under.G * (1 - alpha))),
				(int) ((top.B * alpha) + (under.B * (1 - alpha))));
		}

		/// <summary>
		/// Every pair the frontend actually draws: the text role, and the
		/// background role it lands on. Adding a role that carries text means
		/// adding it here, or nothing is checking that it can be read.
		/// </summary>
		private static IEnumerable<(ThemeColorRole Ink, ThemeColorRole Ground)> Pairs()
		{
			// window chrome
			yield return (ThemeColorRole.WindowText, ThemeColorRole.WindowBackground);
			yield return (ThemeColorRole.DisabledText, ThemeColorRole.WindowBackground);
			yield return (ThemeColorRole.DisabledText, ThemeColorRole.DisabledBackground);
			yield return (ThemeColorRole.MutedText, ThemeColorRole.WindowBackground);
			yield return (ThemeColorRole.LinkText, ThemeColorRole.WindowBackground);
			yield return (ThemeColorRole.GlyphForeground, ThemeColorRole.WindowBackground);
			yield return (ThemeColorRole.GlyphShadow, ThemeColorRole.WindowBackground);

			// fields, buttons, strips
			yield return (ThemeColorRole.InputText, ThemeColorRole.InputBackground);
			yield return (ThemeColorRole.MutedText, ThemeColorRole.ReadOnlyBackground);
			yield return (ThemeColorRole.InputText, ThemeColorRole.InputAwaitingBackground);
			yield return (ThemeColorRole.InputText, ThemeColorRole.AlternateRowBackground);
			yield return (ThemeColorRole.ButtonText, ThemeColorRole.ButtonBackground);
			yield return (ThemeColorRole.MenuText, ThemeColorRole.MenuBackground);
			yield return (ThemeColorRole.MenuSelectedText, ThemeColorRole.MenuSelectedBackground);
			yield return (ThemeColorRole.ToolStripText, ThemeColorRole.ToolStripBackground);
			yield return (ThemeColorRole.StatusBarText, ThemeColorRole.StatusBarBackground);
			yield return (ThemeColorRole.HeaderText, ThemeColorRole.HeaderBackground);

			// a chosen row, which is where the frontend once painted text it had
			// already made the same colour as what was behind it
			yield return (ThemeColorRole.SelectionText, ThemeColorRole.Selection);
			yield return (ThemeColorRole.InactiveSelectionText, ThemeColorRole.InactiveSelection);

			// A HOVERED row keeps the text colour it already had - only the
			// background moves - so every colour a row's text is ever given has to
			// be readable on this one too. The row under the pointer is the third
			// state of the same control, after normal and chosen, and it is the one
			// that shipped as an empty bar.
			foreach (var ink in new[]
			{
				ThemeColorRole.InputText, ThemeColorRole.MutedText, ThemeColorRole.DisabledText,
				ThemeColorRole.AccentGood, ThemeColorRole.AccentReady,
				ThemeColorRole.AccentWarning, ThemeColorRole.AccentError,
			})
			{
				yield return (ink, ThemeColorRole.HoverBackground);
			}

			// and the same colours on a row of a list that has been switched off
			yield return (ThemeColorRole.DisabledText, ThemeColorRole.DisabledBackground);

			// colours that say something
			yield return (ThemeColorRole.AccentGood, ThemeColorRole.WindowBackground);
			yield return (ThemeColorRole.AccentGood, ThemeColorRole.InputBackground);
			yield return (ThemeColorRole.AccentReady, ThemeColorRole.WindowBackground);
			yield return (ThemeColorRole.AccentReady, ThemeColorRole.InputBackground);
			yield return (ThemeColorRole.AccentWarning, ThemeColorRole.WindowBackground);
			yield return (ThemeColorRole.AccentWarning, ThemeColorRole.InputBackground);
			yield return (ThemeColorRole.AccentError, ThemeColorRole.WindowBackground);
			yield return (ThemeColorRole.AccentError, ThemeColorRole.InputBackground);
			yield return (ThemeColorRole.WindowText, ThemeColorRole.AccentWarningBackground);
			yield return (ThemeColorRole.GlyphWarningInk, ThemeColorRole.GlyphWarning);

			// the piano roll, and every row state it draws text on
			yield return (ThemeColorRole.RollText, ThemeColorRole.RollBackground);
			yield return (ThemeColorRole.RollText, ThemeColorRole.RollColumnBackground);
			yield return (ThemeColorRole.RollHintText, ThemeColorRole.RollBackground);
			yield return (ThemeColorRole.RollSelectionText, ThemeColorRole.RollSelection);
			foreach (var row in new[]
			{
				ThemeColorRole.TasDefaultRow, ThemeColorRole.TasCurrentFrame,
				ThemeColorRole.TasGreenZone, ThemeColorRole.TasGreenZoneInput,
				ThemeColorRole.TasGreenZoneInputStated, ThemeColorRole.TasGreenZoneInputInvalidated,
				ThemeColorRole.TasLagZone, ThemeColorRole.TasLagZoneInput,
				ThemeColorRole.TasLagZoneInputStated, ThemeColorRole.TasLagZoneInputInvalidated,
				ThemeColorRole.TasMarker, ThemeColorRole.TasPermanentMarker, ThemeColorRole.TasAnalogEdit,
				ThemeColorRole.RowDefault, ThemeColorRole.RowInvalid, ThemeColorRole.RowActive,
				ThemeColorRole.RowPaused, ThemeColorRole.RowExcluded, ThemeColorRole.RowExcludedActive,
			})
			{
				yield return (ThemeColorRole.RollText, row);
			}

			// the hex editor and the level meter
			yield return (ThemeColorRole.HexText, ThemeColorRole.HexBackground);
			yield return (ThemeColorRole.HexText, ThemeColorRole.HexFreeze);
			yield return (ThemeColorRole.HexText, ThemeColorRole.HexHighlight);
			yield return (ThemeColorRole.HexText, ThemeColorRole.HexHighlightFreeze);
			yield return (ThemeColorRole.MeterText, ThemeColorRole.MeterTrough);
			yield return (ThemeColorRole.MeterText, ThemeColorRole.ShadedBackground);
		}

		[TestMethod]
		public void EveryThemeOfItsOwnPutsReadableTextOnEveryBackground()
		{
			var pairs = Pairs().ToList();
			Assert.IsTrue(pairs.Count > 40, $"only {pairs.Count} pairs are being checked, so this is not checking much");

			List<string> bad = new();
			var looked = 0;
			foreach (var theme in ThemeLibrary.All.Where(static t => !t.FollowsDesktop))
			{
				looked++;
				foreach (var (ink, ground) in pairs)
				{
					var ratio = Ratio(theme[ink], theme[ground]);
					if (ratio < Floor)
					{
						bad.Add($"{theme.Name}: {ink} on {ground} is {ratio:0.0}:1 "
							+ $"({ThemeFile.FormatColor(theme[ink])} on {ThemeFile.FormatColor(theme[ground])})");
					}
				}
			}
			Assert.IsTrue(looked > 0, "no theme of its own was checked");
			Assert.AreEqual(
				0,
				bad.Count,
				$"text that cannot be read off its background (the floor is {Floor:0.0}:1):\n" + string.Join("\n", bad));
		}

		/// <summary>
		/// The measure itself, against the two ends everybody agrees on - otherwise
		/// a mistake in the arithmetic would make the test above pass everything.
		/// </summary>
		[TestMethod]
		public void TheMeasureIsTheOneEverybodyElseUses()
		{
			Assert.AreEqual(21.0, Ratio(Color.Black, Color.White), 0.05);
			Assert.AreEqual(1.0, Ratio(Color.FromArgb(0x33, 0x44, 0x55), Color.FromArgb(0x33, 0x44, 0x55)), 0.001);
			// a half-transparent white over black is a mid grey, not a white
			Assert.AreEqual(5.3, Ratio(Color.FromArgb(0x80, 0xFF, 0xFF, 0xFF), Color.Black), 0.3);
		}
	}
}
