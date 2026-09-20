#nullable enable

using System.Collections.Generic;
using System.Drawing;
using System.Windows.Forms;

using Chimera.Client.Common;

namespace Chimera.Client.GUI
{
	/// <summary>
	/// The colours a <see cref="ToolStripProfessionalRenderer"/> asks for, answered
	/// from a theme. This is the only way to colour a menu bar: a ToolStrip paints
	/// its own background, borders and hover highlight and ignores BackColor for
	/// all three.
	/// </summary>
	public sealed class ThemeColorTable : ProfessionalColorTable
	{
		private readonly Theme _theme;

		public ThemeColorTable(Theme theme)
		{
			_theme = theme;
			UseSystemColors = false;
		}

		private Color Menu => _theme[ThemeColorRole.MenuBackground];

		private Color Strip => _theme[ThemeColorRole.ToolStripBackground];

		private Color Selected => _theme[ThemeColorRole.MenuSelectedBackground];

		private Color Edge => _theme[ThemeColorRole.MenuBorder];

		public override Color MenuStripGradientBegin => Strip;
		public override Color MenuStripGradientEnd => Strip;
		public override Color ToolStripGradientBegin => Strip;
		public override Color ToolStripGradientMiddle => Strip;
		public override Color ToolStripGradientEnd => Strip;
		public override Color ToolStripContentPanelGradientBegin => Strip;
		public override Color ToolStripContentPanelGradientEnd => Strip;
		public override Color ToolStripPanelGradientBegin => Strip;
		public override Color ToolStripPanelGradientEnd => Strip;
		public override Color StatusStripGradientBegin => _theme[ThemeColorRole.StatusBarBackground];
		public override Color StatusStripGradientEnd => _theme[ThemeColorRole.StatusBarBackground];

		public override Color ToolStripDropDownBackground => Menu;
		public override Color ImageMarginGradientBegin => Menu;
		public override Color ImageMarginGradientMiddle => Menu;
		public override Color ImageMarginGradientEnd => Menu;
		public override Color ImageMarginRevealedGradientBegin => Menu;
		public override Color ImageMarginRevealedGradientMiddle => Menu;
		public override Color ImageMarginRevealedGradientEnd => Menu;

		public override Color MenuItemSelected => Selected;
		public override Color MenuItemSelectedGradientBegin => Selected;
		public override Color MenuItemSelectedGradientEnd => Selected;
		public override Color MenuItemPressedGradientBegin => Menu;
		public override Color MenuItemPressedGradientMiddle => Menu;
		public override Color MenuItemPressedGradientEnd => Menu;
		public override Color ButtonSelectedGradientBegin => Selected;
		public override Color ButtonSelectedGradientMiddle => Selected;
		public override Color ButtonSelectedGradientEnd => Selected;
		public override Color ButtonPressedGradientBegin => Selected;
		public override Color ButtonPressedGradientMiddle => Selected;
		public override Color ButtonPressedGradientEnd => Selected;
		public override Color ButtonCheckedGradientBegin => Selected;
		public override Color ButtonCheckedGradientMiddle => Selected;
		public override Color ButtonCheckedGradientEnd => Selected;
		public override Color CheckBackground => Selected;
		public override Color CheckSelectedBackground => Selected;
		public override Color CheckPressedBackground => Selected;

		public override Color MenuItemBorder => Edge;
		public override Color MenuBorder => Edge;
		public override Color ButtonSelectedBorder => Edge;
		public override Color ToolStripBorder => Edge;
		public override Color SeparatorDark => _theme[ThemeColorRole.MenuSeparator];
		public override Color SeparatorLight => _theme[ThemeColorRole.MenuSeparator];
		public override Color GripDark => _theme[ThemeColorRole.Border];
		public override Color GripLight => _theme[ThemeColorRole.Border];
		public override Color OverflowButtonGradientBegin => Strip;
		public override Color OverflowButtonGradientMiddle => Strip;
		public override Color OverflowButtonGradientEnd => Strip;
	}

	/// <summary>
	/// Draws menu bars, tool bars, status bars and context menus in a theme's
	/// colours. One renderer per theme, shared: a ToolStrip only holds a reference
	/// to it, and building one per strip would be a few hundred objects.
	/// </summary>
	public sealed class ThemeToolStripRenderer : ToolStripProfessionalRenderer
	{
		private static readonly Dictionary<Theme, ThemeToolStripRenderer> Cache = new();

		private static readonly Lock Sync = new();

		/// <summary>
		/// The renderer for this theme. For the desktop's own palette this is still
		/// a professional renderer rather than the system one the frontend used
		/// before, because a system renderer cannot be told any colours at all -
		/// and the colours it is told here are the desktop's, so it draws what it
		/// drew.
		/// </summary>
		public static ThemeToolStripRenderer For(Theme theme)
		{
			lock (Sync)
			{
				if (!Cache.TryGetValue(theme, out var renderer)) Cache[theme] = renderer = new(theme);
				return renderer;
			}
		}

		private readonly Theme _theme;

		private ThemeToolStripRenderer(Theme theme) : base(new ThemeColorTable(theme))
		{
			_theme = theme;
			RoundedEdges = false;
		}

		/// <summary>
		/// A disabled item's text. The base renderer greys it by blending towards
		/// the system's control colour, which on a dark strip makes it brighter
		/// than the enabled items rather than dimmer.
		/// </summary>
		protected override void OnRenderItemText(ToolStripItemTextRenderEventArgs e)
		{
			if (!e.Item.Enabled) e.TextColor = _theme[ThemeColorRole.DisabledText];
			else if (e.Item.Selected && e.Item.IsOnDropDown) e.TextColor = _theme[ThemeColorRole.MenuSelectedText];
			base.OnRenderItemText(e);
		}

		/// <summary>The arrow on a submenu, which the base renderer draws in the system's text colour.</summary>
		protected override void OnRenderArrow(ToolStripArrowRenderEventArgs e)
		{
			e.ArrowColor = e.Item?.Enabled is false
				? _theme[ThemeColorRole.DisabledText]
				: _theme[ThemeColorRole.MenuText];
			base.OnRenderArrow(e);
		}

		/// <summary>The tick beside a checked menu item, drawn in the system's colours by the base.</summary>
		protected override void OnRenderItemCheck(ToolStripItemImageRenderEventArgs e)
		{
			if (e.Image is not null)
			{
				base.OnRenderItemCheck(e);
				return;
			}
			var r = e.ImageRectangle;
			using Pen pen = new(_theme[ThemeColorRole.MenuText], 2f);
			Point[] tick =
			[
				new(r.Left + 3, r.Top + (r.Height / 2)),
				new(r.Left + (r.Width / 2) - 1, r.Bottom - 4),
				new(r.Right - 3, r.Top + 3),
			];
			e.Graphics.DrawLines(pen, tick);
		}
	}
}
