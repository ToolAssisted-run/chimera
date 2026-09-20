using System.Collections.Generic;
using System.Drawing;
using System.Linq;

using Chimera.Client.Common;
using Chimera.Common;

namespace Chimera.Tests.Client.Common
{
	/// <summary>
	/// The Light theme is not a new palette. It is the palette Chimera had before
	/// there were themes, written down - so this file is that palette, copied out
	/// of the code it used to live in, role by role, and compared against what the
	/// theme says. If somebody edits light.json, this fails and names the colour.
	///
	/// The expected values here are deliberately NOT read from the theme or from
	/// anything that reads the theme: they are the literals and the SystemColors
	/// members the old code used. The one thing they do share is the rule about
	/// SystemColors.Control, which Mono answers with an ugly beige and which every
	/// window in the frontend therefore replaced with WhiteSmoke; it is repeated
	/// here rather than called, so that changing the rule in one place fails here.
	/// </summary>
	[TestClass]
	public class LightThemeBaselineTests
	{
		/// <summary>What <c>SystemColors.Control</c> meant to the frontend before themes.</summary>
		private static Color Control => OSTailoredCode.IsUnixHost ? Color.WhiteSmoke : SystemColors.Control;

		private static IReadOnlyDictionary<ThemeColorRole, Color> Baseline() => new Dictionary<ThemeColorRole, Color>
		{
			// FormBase.FixBackColorOnControls, and every Designer default
			[ThemeColorRole.WindowBackground] = Control,
			[ThemeColorRole.WindowText] = SystemColors.ControlText,
			[ThemeColorRole.DisabledText] = SystemColors.GrayText,
			[ThemeColorRole.DisabledBackground] = Control,
			[ThemeColorRole.MutedText] = SystemColors.ControlDarkDark, // HexEditor, a domain that cannot be written
			[ThemeColorRole.Border] = SystemColors.ControlDark,
			[ThemeColorRole.LinkText] = SystemColors.HotTrack,
			[ThemeColorRole.ShadedBackground] = SystemColors.ControlDarkDark, // EncodeVideoForm, behind the level meter
			[ThemeColorRole.GlyphForeground] = SystemColors.ControlText, // MenuButton's arrow, CustomCheckBox's tick
			[ThemeColorRole.GlyphShadow] = SystemColors.ButtonShadow, // MacroInput, a button not in the macro

			[ThemeColorRole.InputBackground] = SystemColors.Window,
			[ThemeColorRole.InputText] = SystemColors.WindowText,
			[ThemeColorRole.ReadOnlyBackground] = Control,
			[ThemeColorRole.InputAwaitingBackground] = Color.FromArgb(unchecked((int) 0xFFC0FFFF)), // InputWidget

			[ThemeColorRole.ButtonBackground] = Control,
			[ThemeColorRole.ButtonText] = SystemColors.ControlText,
			[ThemeColorRole.ButtonBorder] = SystemColors.ControlDark,

			[ThemeColorRole.MenuBackground] = SystemColors.Menu,
			[ThemeColorRole.MenuText] = SystemColors.MenuText,
			[ThemeColorRole.MenuSelectedBackground] = SystemColors.Highlight,
			[ThemeColorRole.MenuSelectedText] = SystemColors.HighlightText,
			[ThemeColorRole.MenuBorder] = SystemColors.ControlDark,
			[ThemeColorRole.MenuSeparator] = SystemColors.ControlDark,
			[ThemeColorRole.ToolStripBackground] = Control,
			[ThemeColorRole.ToolStripText] = SystemColors.ControlText,
			[ThemeColorRole.StatusBarBackground] = Control,
			[ThemeColorRole.StatusBarText] = SystemColors.ControlText,

			[ThemeColorRole.Selection] = SystemColors.Highlight,
			[ThemeColorRole.SelectionText] = SystemColors.HighlightText,
			[ThemeColorRole.InactiveSelection] = Control,
			[ThemeColorRole.InactiveSelectionText] = SystemColors.ControlText,

			// Light never paints this one: it is the desktop's palette, so its lists
			// are drawn by the toolkit and the toolkit draws its own hover. The value
			// is here because every role must have one, and it is ControlLight
			// because that is what the desktop would tint a row with if it were ever
			// asked to.
			[ThemeColorRole.HoverBackground] = SystemColors.ControlLight,

			[ThemeColorRole.GridLines] = SystemColors.ControlLight,
			[ThemeColorRole.HeaderBackground] = Control,
			[ThemeColorRole.HeaderText] = SystemColors.ControlText,
			[ThemeColorRole.AlternateRowBackground] = SystemColors.Window,

			// CoreFirmwareForm, FirmwareSurveyForm, NewProjectWizard, EncodeVideoForm
			[ThemeColorRole.AccentGood] = Color.DarkGreen,
			[ThemeColorRole.AccentReady] = Color.ForestGreen,
			[ThemeColorRole.AccentWarning] = Color.DarkGoldenrod,
			[ThemeColorRole.AccentError] = Color.Firebrick,
			[ThemeColorRole.AccentWarningBackground] = Color.NavajoWhite, // the RAM tools' error button
			[ThemeColorRole.GlyphGood] = Color.FromArgb(0, 140, 0),
			[ThemeColorRole.GlyphWarning] = Color.FromArgb(240, 173, 40),
			[ThemeColorRole.GlyphWarningInk] = Color.FromArgb(60, 40, 0),
			[ThemeColorRole.GlyphError] = Color.FromArgb(190, 40, 40),
			[ThemeColorRole.GlyphNeutral] = Color.FromArgb(150, 150, 150),

			// InputRoll's own chrome
			[ThemeColorRole.RollBackground] = Color.White,
			[ThemeColorRole.RollText] = Color.Black,
			[ThemeColorRole.RollGridLines] = SystemColors.ControlLight,
			[ThemeColorRole.RollColumnBackground] = SystemColors.ControlLight,
			[ThemeColorRole.RollColumnBorder] = Color.Black,
			[ThemeColorRole.RollSelection] = SystemColors.Highlight,
			[ThemeColorRole.RollSelectionText] = SystemColors.HighlightText,
			[ThemeColorRole.RollHintText] = SystemColors.GrayText,
			[ThemeColorRole.RollEmphasisColumn] = SystemColors.ActiveBorder,

			// TAStudioPalette.Default, verbatim
			[ThemeColorRole.TasCurrentFrame] = Color.FromArgb(0xB5, 0xE7, 0xF7),
			[ThemeColorRole.TasGreenZone] = Color.FromArgb(0xDD, 0xFF, 0xDD),
			[ThemeColorRole.TasGreenZoneInput] = Color.FromArgb(0xD2, 0xF9, 0xD3),
			[ThemeColorRole.TasGreenZoneInputStated] = Color.FromArgb(0xC4, 0xF7, 0xC8),
			[ThemeColorRole.TasGreenZoneInputInvalidated] = Color.FromArgb(0xE0, 0xFB, 0xE0),
			[ThemeColorRole.TasLagZone] = Color.FromArgb(0xFF, 0xDC, 0xDD),
			[ThemeColorRole.TasLagZoneInput] = Color.FromArgb(0xF4, 0xDA, 0xDA),
			[ThemeColorRole.TasLagZoneInputStated] = Color.FromArgb(0xF0, 0xD0, 0xD2),
			[ThemeColorRole.TasLagZoneInputInvalidated] = Color.FromArgb(0xF7, 0xE5, 0xE5),
			[ThemeColorRole.TasMarker] = Color.FromArgb(0xF7, 0xFF, 0xC9),
			[ThemeColorRole.TasPermanentMarker] = Color.FromArgb(0xC9, 0xE4, 0xFF),
			[ThemeColorRole.TasAnalogEdit] = Color.FromArgb(0x90, 0x90, 0x70),
			// ...and the four TAStudio.ListView drew without going through the palette
			[ThemeColorRole.TasDefaultRow] = Color.FromArgb(0xFF, 0xFE, 0xEE),
			[ThemeColorRole.TasCursorColumn] = Color.FromArgb(0xFE, 0xFF, 0xFF),
			[ThemeColorRole.TasFrameColumnWash] = Color.FromArgb(0x60, 0xFF, 0xFF, 0xFF),
			[ThemeColorRole.TasAlternatePlayer] = Color.FromArgb(0x0D, 0x00, 0x00, 0x00),
			// ...and the five icons it draws once into bitmaps
			[ThemeColorRole.TasIconPlayback] = Color.FromArgb(83, 217, 255),
			[ThemeColorRole.TasIconRecording] = Color.FromArgb(0, 194, 64),
			[ThemeColorRole.TasIconMarker] = Color.FromArgb(252, 209, 55),
			[ThemeColorRole.TasIconAnchor] = Color.FromArgb(0, 222, 98),
			[ThemeColorRole.TasIconLagAnchor] = Color.FromArgb(255, 96, 100),

			// RAM Search, RAM Watch, the Lua console, breakpoints, the disassembler
			[ThemeColorRole.RowDefault] = Color.White,
			[ThemeColorRole.RowInvalid] = Color.PeachPuff,
			[ThemeColorRole.RowActive] = Color.LightCyan,
			[ThemeColorRole.RowPaused] = Color.LightPink,
			[ThemeColorRole.RowExcluded] = Color.Pink,
			[ThemeColorRole.RowExcludedActive] = Color.Lavender,

			// HexEditor.ColorConfig's defaults
			[ThemeColorRole.HexBackground] = Control,
			[ThemeColorRole.HexText] = SystemColors.ControlText,
			[ThemeColorRole.HexMenuBar] = Control,
			[ThemeColorRole.HexFreeze] = Color.LightBlue,
			[ThemeColorRole.HexHighlight] = Color.Pink,
			[ThemeColorRole.HexHighlightFreeze] = Color.Violet,

			// AnalogRangeConfig
			[ThemeColorRole.AnalogRangeBackground] = Color.Gray,
			[ThemeColorRole.AnalogRangeField] = Control,
			[ThemeColorRole.AnalogRangeDot] = Color.White,
			[ThemeColorRole.AnalogRangeAxis] = Color.Black,
			[ThemeColorRole.AnalogRangeLimit] = Color.Cyan,

			// AudioLevelMeter
			[ThemeColorRole.MeterTrough] = SystemColors.ControlDark,
			[ThemeColorRole.MeterLow] = Color.ForestGreen,
			[ThemeColorRole.MeterMid] = Color.Goldenrod,
			[ThemeColorRole.MeterHigh] = Color.Firebrick,
			[ThemeColorRole.MeterPeak] = Color.White,
			[ThemeColorRole.MeterText] = SystemColors.ControlLightLight,

			// PresentationPanel, ViewportPanel, SurfaceViewer, the fullscreen hack
			[ThemeColorRole.EmulatorViewport] = Color.Black,

			// DefaultMessagePositions, and the two OSDManager had as literals
			[ThemeColorRole.OsdMessage] = Color.FromArgb(unchecked((int) 0xFF_FF_FF_FF)),
			[ThemeColorRole.OsdAlert] = Color.FromArgb(unchecked((int) 0xFF_FF_00_00)),
			[ThemeColorRole.OsdLastInput] = Color.FromArgb(unchecked((int) 0xFF_FF_A5_00)),
			[ThemeColorRole.OsdMovieInput] = Color.FromArgb(unchecked((int) 0xFF_80_80_80)),
			[ThemeColorRole.OsdStickyInput] = Color.Pink,
			[ThemeColorRole.OsdCurrentAndPreviousInput] = Color.PeachPuff,
			[ThemeColorRole.OsdAutoHold] = Color.White,
		};

		/// <summary>
		/// If this fails, a role was added without adding the colour the frontend
		/// used for it before there were themes - which means nothing is checking
		/// that the Light theme has not changed the look of that surface.
		/// </summary>
		[TestMethod]
		public void EveryRoleHasABaseline()
		{
			var baseline = Baseline();
			var missing = Theme.AllRoles.Where(r => !baseline.ContainsKey(r)).Select(static r => r.ToString()).ToList();
			Assert.AreEqual(0, missing.Count, "no baseline colour recorded for: " + string.Join(", ", missing));
		}

		[TestMethod]
		public void TheLightThemeIsThePaletteChimeraAlreadyHad()
		{
			var light = ThemeLibrary.Find("Light");
			Assert.IsNotNull(light, "there is no Light theme");
			List<string> wrong = new();
			foreach (var (role, expected) in Baseline())
			{
				var actual = light![role];
				if (actual.ToArgb() != expected.ToArgb())
				{
					wrong.Add($"{role}: light.json says {ThemeFile.FormatColor(actual)}, the frontend used {ThemeFile.FormatColor(expected)}");
				}
			}
			Assert.AreEqual(0, wrong.Count, "the Light theme is no longer the palette Chimera had:\n" + string.Join("\n", wrong));
		}

		/// <summary>
		/// Dark has to be complete and it has to be dark; a role left at a light
		/// value is the bug this whole exercise is about.
		/// </summary>
		[TestMethod]
		public void TheDarkThemeIsDarkWhereItPaintsChrome()
		{
			var dark = ThemeLibrary.Find("Dark");
			Assert.IsNotNull(dark);
			ThemeColorRole[] surfaces =
			[
				ThemeColorRole.WindowBackground,
				ThemeColorRole.InputBackground,
				ThemeColorRole.ButtonBackground,
				ThemeColorRole.MenuBackground,
				ThemeColorRole.ToolStripBackground,
				ThemeColorRole.StatusBarBackground,
				ThemeColorRole.HeaderBackground,
				ThemeColorRole.RollBackground,
				ThemeColorRole.RollColumnBackground,
				ThemeColorRole.HexBackground,
				ThemeColorRole.TasDefaultRow,
				ThemeColorRole.RowDefault,
			];
			foreach (var role in surfaces)
			{
				var c = dark![role];
				var luma = ((0.299 * c.R) + (0.587 * c.G) + (0.114 * c.B)) / 255.0;
				Assert.IsTrue(luma < 0.4, $"Dark's {role} is {ThemeFile.FormatColor(c)}, which is not a dark surface");
			}

			ThemeColorRole[] inks = [ThemeColorRole.WindowText, ThemeColorRole.InputText, ThemeColorRole.MenuText, ThemeColorRole.RollText];
			foreach (var role in inks)
			{
				var c = dark![role];
				var luma = ((0.299 * c.R) + (0.587 * c.G) + (0.114 * c.B)) / 255.0;
				Assert.IsTrue(luma > 0.6, $"Dark's {role} is {ThemeFile.FormatColor(c)}, which will not read on a dark surface");
			}
		}
	}
}
