#nullable enable

using System.Collections.Generic;
using System.Drawing;
using System.Linq;
using System.Runtime.CompilerServices;
using System.Windows.Forms;

using Chimera.Client.Common;
using Chimera.Common;

namespace Chimera.Client.GUI
{
	/// <summary>
	/// Paints the theme onto a window.
	///
	/// WinForms has no theming of its own, so this is the whole of it: one walk
	/// over a control tree, setting each control's colours from what KIND of
	/// control it is. A control that paints itself cannot be told that way, so it
	/// implements <see cref="IThemedControl"/> and is handed the theme instead; a
	/// control whose colour MEANS something (an error line, a firmware row) says
	/// so once with <see cref="SetForeRole"/> and is then re-coloured for free on
	/// every theme change.
	///
	/// The walk is called from <see cref="ThemedForm"/>, which every window in the
	/// frontend derives from, so a new window is themed without its author doing
	/// anything - and ThemingContractTests fails the build if a window is added
	/// that does not derive from it.
	/// </summary>
	public static class ThemeEngine
	{
		/// <summary>The colours everything is painted with right now.</summary>
		public static Theme Current => ThemeLibrary.Current;

		/// <summary>
		/// True when the theme says it IS the desktop's own colours. Then the walk
		/// sets nothing by control type: every control already has those colours,
		/// and assigning them again is not free - a Button stops using visual
		/// styles, a sunken border becomes a line, a menu gets a colour table
		/// instead of the system renderer. Same colours, different drawing. What
		/// still happens is everything the frontend declared for itself: a role put
		/// on a control by name, and a control that paints itself.
		/// </summary>
		public static bool IsSystemPalette(Theme theme) => theme.FollowsDesktop;

		private sealed class Overrides
		{
			public ThemeColorRole? Back;

			public ThemeColorRole? Fore;

			public bool Skip;

			public bool OwnerDrawn;

			/// <summary>The last column's width as somebody other than the walk left it.</summary>
			public int NaturalLastWidth;

			/// <summary>What the walk last set it to, so a change by anybody else can be told apart.</summary>
			public int AssignedLastWidth = -1;

			public bool Refilling;

			public bool Hooked;
		}

		private static readonly ConditionalWeakTable<Control, Overrides> Tagged = new();

		private static readonly ConditionalWeakTable<ToolStripItem, Overrides> TaggedItems = new();

		private static Overrides For(Control c) => Tagged.GetValue(c, static _ => new Overrides());

		/// <summary>This control's background is this role, whatever its type would have said.</summary>
		public static T SetBackRole<T>(this T control, ThemeColorRole role) where T : Control
		{
			For(control).Back = role;
			control.BackColor = Current[role];
			return control;
		}

		/// <summary>
		/// This control's text is this role. Use it wherever a colour SAYS something
		/// - <see cref="ThemeColorRole.AccentError"/> for a thing that is wrong,
		/// <see cref="ThemeColorRole.DisabledText"/> for a thing that is not there -
		/// so that the meaning survives a change of theme.
		/// </summary>
		public static T SetForeRole<T>(this T control, ThemeColorRole role) where T : Control
		{
			For(control).Fore = role;
			control.ForeColor = Current[role];
			return control;
		}

		/// <summary>Leave this control and everything in it exactly as it is.</summary>
		public static T SkipTheming<T>(this T control) where T : Control
		{
			For(control).Skip = true;
			return control;
		}

		/// <summary>The same, for an item on a tool strip.</summary>
		public static T SetItemForeRole<T>(this T item, ThemeColorRole role) where T : ToolStripItem
		{
			TaggedItems.GetValue(item, static _ => new Overrides()).Fore = role;
			item.ForeColor = Current[role];
			return item;
		}

		/// <summary>The background of an item on a tool strip - a button tinted to say something.</summary>
		public static T SetItemBackRole<T>(this T item, ThemeColorRole role) where T : ToolStripItem
		{
			TaggedItems.GetValue(item, static _ => new Overrides()).Back = role;
			item.BackColor = Current[role];
			return item;
		}

		/// <summary>The colour a role is right now. For a place that has to compute with it.</summary>
		public static Color Color(ThemeColorRole role) => Current[role];

		/// <summary>
		/// A row colour nudged to say the pointer is on it. Away from the
		/// background, so it darkens on a light theme and lightens on a dark one -
		/// the old code only ever subtracted, and subtracting from a dark colour
		/// wrapped the byte round to bright.
		/// </summary>
		public static Color Hovered(Color c)
		{
			var step = Current.IsDark ? 24 : -24;
			static int Clamp(int v) => v < 0 ? 0 : v > 255 ? 255 : v;
			return System.Drawing.Color.FromArgb(Clamp(c.A - 24), Clamp(c.R + step), Clamp(c.G + step), Clamp(c.B + step));
		}

		// ---- the walk ------------------------------------------------------

		/// <summary>Paints <paramref name="root"/> and everything under it with the current theme.</summary>
		public static void Apply(Control root) => Apply(root, Current);

		/// <summary>Paints <paramref name="root"/> and everything under it.</summary>
		public static void Apply(Control root, Theme theme)
		{
			if (root is null) return;
			if (Tagged.TryGetValue(root, out var own) && own.Skip) return;

			// a control that paints itself takes the whole theme and is not told
			// anything further: the type table below would overwrite what it chose
			if (root is IThemedControl themed) themed.ApplyTheme(theme);
			else ApplyToOne(root, theme, own);

			foreach (Control child in root.Controls) Apply(child, theme);

			// A window builds most of itself before it is ever shown, but not all:
			// TAStudio's piano rolls are made when a project opens, long after the
			// walk ran, and a control made later would be the one control on the
			// window still painted white. Hooking the container means a control is
			// themed when it arrives, whenever that is.
			var state = own ?? For(root);
			if (!state.Hooked)
			{
				state.Hooked = true;
				root.ControlAdded += OnControlAdded;
			}

			// A form's context menu and a control's own context menu are not in
			// Controls, so the walk would never reach them.
			if (root.ContextMenuStrip is not null) ApplyStrip(root.ContextMenuStrip, theme);
			if (root is Form form && form.MainMenuStrip is not null && !form.Controls.Contains(form.MainMenuStrip))
			{
				ApplyStrip(form.MainMenuStrip, theme);
			}
		}

		private static void ApplyToOne(Control c, Theme theme, Overrides? own)
		{
			var systemPalette = IsSystemPalette(theme);

			if (own?.Back is { } backRole) c.BackColor = theme[backRole];
			if (own?.Fore is { } foreRole) c.ForeColor = theme[foreRole];

			// the desktop's own palette is already on every control; only what the
			// frontend asked for by name is set, and the rest is left as drawn
			if (systemPalette) return;
			FlattenBorder(c);

			switch (c)
			{
				case ToolStrip strip:
					ApplyStrip(strip, theme);
					return;
				case DataGridView grid:
					ApplyGrid(grid, theme);
					return;
				case LinkLabel link:
					Back(link, own, theme, ThemeColorRole.WindowBackground);
					Fore(link, own, theme, ThemeColorRole.WindowText);
					link.LinkColor = link.ActiveLinkColor = link.VisitedLinkColor = theme[ThemeColorRole.LinkText];
					return;
				case CheckBox cb:
					// the box itself is drawn by the OS; only its caption is ours
					cb.UseVisualStyleBackColor = false;
					Back(c, own, theme, ThemeColorRole.WindowBackground);
					Fore(c, own, theme, ThemeColorRole.WindowText);
					return;
				case RadioButton rb:
					rb.UseVisualStyleBackColor = false;
					Back(c, own, theme, ThemeColorRole.WindowBackground);
					Fore(c, own, theme, ThemeColorRole.WindowText);
					return;
				case ButtonBase bb:
					if (bb is Button plain) plain.UseVisualStyleBackColor = false;
					Back(c, own, theme, ThemeColorRole.ButtonBackground);
					Fore(c, own, theme, ThemeColorRole.ButtonText);
					bb.FlatStyle = FlatStyle.Flat;
					bb.FlatAppearance.BorderColor = theme[ThemeColorRole.ButtonBorder];
					bb.FlatAppearance.MouseOverBackColor = theme[ThemeColorRole.MenuSelectedBackground];
					bb.FlatAppearance.MouseDownBackColor = theme[ThemeColorRole.Selection];
					return;
				case TextBoxBase text:
					// not a special case for ReadOnly: WinForms does not colour a
					// read-only box differently either, and a box that should look
					// different says so with SetBackRole(ReadOnlyBackground)
					Back(text, own, theme, ThemeColorRole.InputBackground);
					Fore(text, own, theme, ThemeColorRole.InputText);
					return;
				case ComboBox combo:
					if (combo.FlatStyle is FlatStyle.Standard) combo.FlatStyle = FlatStyle.Flat;
					Back(combo, own, theme, ThemeColorRole.InputBackground);
					Fore(combo, own, theme, ThemeColorRole.InputText);
					return;
				case PropertyGrid properties:
					// the grid's own surfaces are separate properties; BackColor only
					// reaches the strip around them
					Back(properties, own, theme, ThemeColorRole.WindowBackground);
					Fore(properties, own, theme, ThemeColorRole.WindowText);
					properties.ViewBackColor = theme[ThemeColorRole.InputBackground];
					properties.ViewForeColor = theme[ThemeColorRole.InputText];
					properties.HelpBackColor = theme[ThemeColorRole.WindowBackground];
					properties.HelpForeColor = theme[ThemeColorRole.WindowText];
					properties.LineColor = theme[ThemeColorRole.GridLines];
					properties.CategoryForeColor = theme[ThemeColorRole.WindowText];
					return;
				case ListBox box:
					Back(box, own, theme, ThemeColorRole.InputBackground);
					Fore(box, own, theme, ThemeColorRole.InputText);
					if (box is not CheckedListBox) ApplyListBox(box);
					return;
				case TreeView or NumericUpDown or DateTimePicker:
					Back(c, own, theme, ThemeColorRole.InputBackground);
					Fore(c, own, theme, ThemeColorRole.InputText);
					return;
				case ListView list:
					Back(list, own, theme, ThemeColorRole.InputBackground);
					Fore(list, own, theme, ThemeColorRole.InputText);
					ApplyListView(list);
					return;
				case ProgressBar:
					// drawn entirely by the OS; setting colours on it does nothing but confuse
					return;
				case TabPage page:
					page.UseVisualStyleBackColor = false;
					Back(page, own, theme, ThemeColorRole.WindowBackground);
					Fore(page, own, theme, ThemeColorRole.WindowText);
					return;
				default:
					Back(c, own, theme, ThemeColorRole.WindowBackground);
					Fore(c, own, theme, ThemeColorRole.WindowText);
					return;
			}
		}

		/// <summary>
		/// A sunken 3D border is drawn by the toolkit out of the system's own light
		/// and shadow colours, so on a dark window it is a bright white frame round
		/// every list and text box. A single-line border is drawn in the control's
		/// own colours instead. Only away from the desktop's palette, where the 3D
		/// border is correct and is what the frontend has always had.
		/// </summary>
		private static void FlattenBorder(Control c)
		{
			switch (c)
			{
				case ListBox { BorderStyle: BorderStyle.Fixed3D } list: list.BorderStyle = BorderStyle.FixedSingle; break;
				case ListView { BorderStyle: BorderStyle.Fixed3D } view: view.BorderStyle = BorderStyle.FixedSingle; break;
				case TreeView { BorderStyle: BorderStyle.Fixed3D } tree: tree.BorderStyle = BorderStyle.FixedSingle; break;
				case TextBoxBase { BorderStyle: BorderStyle.Fixed3D } text: text.BorderStyle = BorderStyle.FixedSingle; break;
				case Panel { BorderStyle: BorderStyle.Fixed3D } panel: panel.BorderStyle = BorderStyle.FixedSingle; break;
			}
		}

		/// <summary>
		/// A control that was deliberately made transparent keeps its transparency:
		/// it is sitting on top of something that has already been themed, and
		/// giving it a colour of its own would hide that.
		/// </summary>
		private static void OnControlAdded(object sender, ControlEventArgs e)
		{
			if (e.Control is null) return;
			Apply(e.Control, Current);
		}

		private static void Back(Control c, Overrides? own, Theme theme, ThemeColorRole fallback)
		{
			if (own?.Back is null && c.BackColor != System.Drawing.Color.Transparent) c.BackColor = theme[fallback];
		}

		private static void Fore(Control c, Overrides? own, Theme theme, ThemeColorRole fallback)
		{
			if (own?.Fore is null) c.ForeColor = theme[fallback];
		}

		// ---- the surfaces a BackColor does not reach -----------------------

		private static void ApplyGrid(DataGridView grid, Theme theme)
		{
			grid.EnableHeadersVisualStyles = false;
			grid.BackgroundColor = theme[ThemeColorRole.WindowBackground];
			grid.GridColor = theme[ThemeColorRole.GridLines];
			grid.DefaultCellStyle.BackColor = theme[ThemeColorRole.InputBackground];
			grid.DefaultCellStyle.ForeColor = theme[ThemeColorRole.InputText];
			grid.DefaultCellStyle.SelectionBackColor = theme[ThemeColorRole.Selection];
			grid.DefaultCellStyle.SelectionForeColor = theme[ThemeColorRole.SelectionText];
			grid.AlternatingRowsDefaultCellStyle.BackColor = theme[ThemeColorRole.AlternateRowBackground];
			grid.AlternatingRowsDefaultCellStyle.ForeColor = theme[ThemeColorRole.InputText];
			grid.AlternatingRowsDefaultCellStyle.SelectionBackColor = theme[ThemeColorRole.Selection];
			grid.AlternatingRowsDefaultCellStyle.SelectionForeColor = theme[ThemeColorRole.SelectionText];
			grid.ColumnHeadersDefaultCellStyle.BackColor = theme[ThemeColorRole.HeaderBackground];
			grid.ColumnHeadersDefaultCellStyle.ForeColor = theme[ThemeColorRole.HeaderText];
			grid.ColumnHeadersDefaultCellStyle.SelectionBackColor = theme[ThemeColorRole.HeaderBackground];
			grid.ColumnHeadersDefaultCellStyle.SelectionForeColor = theme[ThemeColorRole.HeaderText];
			grid.RowHeadersDefaultCellStyle.BackColor = theme[ThemeColorRole.HeaderBackground];
			grid.RowHeadersDefaultCellStyle.ForeColor = theme[ThemeColorRole.HeaderText];
			grid.RowHeadersDefaultCellStyle.SelectionBackColor = theme[ThemeColorRole.Selection];
			grid.RowHeadersDefaultCellStyle.SelectionForeColor = theme[ThemeColorRole.SelectionText];
		}

		/// <summary>
		/// A ListView draws itself out of the desktop's colours and ignores almost
		/// everything set on it: the column headers, the highlight behind a
		/// selected row, and - the one that is easiest to miss - the strip to the
		/// right of the last column, which belongs to no cell and no header.
		/// Owner-drawing is the only way in, so in Details view the whole thing is
		/// taken over. Only away from the desktop's palette: under Light the list
		/// keeps exactly the drawing it has today.
		///
		/// What is reimplemented here is what those lists actually use - the tick
		/// box, the small image, per-item and per-subitem colours and fonts,
		/// alignment. Group headers are left to the toolkit, which still draws them
		/// under owner-draw.
		/// </summary>
		private static void ApplyListView(ListView list)
		{
			var state = For(list);
			var wanted = list.View is View.Details;
			if (wanted == state.OwnerDrawn) return;
			state.OwnerDrawn = wanted;
			if (wanted)
			{
				list.OwnerDraw = true;
				list.DrawColumnHeader += DrawHeader;
				list.DrawItem += DrawRow;
				list.DrawSubItem += DrawCell;
				list.Resize += RefillLastColumn;
				list.ColumnWidthChanged += RefillLastColumn;
			}
			else
			{
				list.DrawColumnHeader -= DrawHeader;
				list.DrawItem -= DrawRow;
				list.DrawSubItem -= DrawCell;
				list.Resize -= RefillLastColumn;
				list.ColumnWidthChanged -= RefillLastColumn;
				list.OwnerDraw = false;
			}
			FillLastColumn(list, wanted);
			list.Invalidate();
		}

		/// <summary>What a row's background is: the selection when it is one, otherwise whatever the row was given.</summary>
		private static Color RowBackground(ListView list, ListViewItem item, Theme theme)
			=> item.Selected
				? theme[list.Focused ? ThemeColorRole.Selection : ThemeColorRole.InactiveSelection]
				: item.BackColor;

		/// <summary>
		/// And its text. A selected row's own colour is given up to the selection,
		/// which is what every list does - a colour that means something (a missing
		/// firmware file in red) is readable again the moment the row is not
		/// selected, and unreadable text on the highlight helps nobody.
		/// </summary>
		private static Color RowForeground(ListView list, ListViewItem item, ListViewItem.ListViewSubItem sub, Theme theme)
			=> item.Selected
				? theme[list.Focused ? ThemeColorRole.SelectionText : ThemeColorRole.InactiveSelectionText]
				// a sub-item's own ForeColor is the LIST's until somebody turns
				// UseItemStyleForSubItems off; reading it instead of the item's is
				// how a whole window of red and green firmware rows came out grey
				: item.UseItemStyleForSubItems ? item.ForeColor : sub.ForeColor;

		private static void DrawHeader(object sender, DrawListViewColumnHeaderEventArgs e)
		{
			var theme = Current;
			var bounds = e.Bounds;
			using SolidBrush back = new(theme[ThemeColorRole.HeaderBackground]);
			e.Graphics.FillRectangle(back, bounds);
			using Pen edge = new(theme[ThemeColorRole.Border]);
			e.Graphics.DrawLine(edge, e.Bounds.Right - 1, e.Bounds.Top, e.Bounds.Right - 1, e.Bounds.Bottom - 1);
			e.Graphics.DrawLine(edge, bounds.Left, bounds.Bottom - 1, bounds.Right - 1, bounds.Bottom - 1);
			TextRenderer.DrawText(
				e.Graphics,
				e.Header?.Text ?? "",
				e.Font ?? SystemFonts.DefaultFont,
				Rectangle.Inflate(e.Bounds, -4, 0),
				theme[ThemeColorRole.HeaderText],
				Align(e.Header?.TextAlign) | TextFormatFlags.VerticalCenter | TextFormatFlags.EndEllipsis);
		}

		/// <summary>
		/// The strip to the right of the last column belongs to no column, so no
		/// DrawColumnHeader is raised for it and nothing can paint over it: the
		/// toolkit fills it with the desktop's colour and keeps it there, which on
		/// a dark list is a bright block in the corner of every window. The only
		/// way to be rid of it is for there to be no strip, so the last column is
		/// grown to the edge.
		///
		/// It only ever GROWS a column, and only into space nothing else is using,
		/// so no horizontal scroll bar can appear because of it. The width the
		/// column would have had is remembered, and given back when the window is
		/// narrowed again or the theme goes back to the desktop's.
		/// </summary>
		private static void FillLastColumn(ListView list, bool wanted)
		{
			if (list.View is not View.Details || list.Columns.Count is 0) return;
			var state = For(list);
			var last = list.Columns[list.Columns.Count - 1];

			// somebody else set this width - the form as it populates, or a person
			// dragging the edge - so that is the width to give back
			if (state.AssignedLastWidth != last.Width) state.NaturalLastWidth = last.Width;

			var others = 0;
			for (var i = 0; i < list.Columns.Count - 1; i++) others += list.Columns[i].Width;
			var natural = state.NaturalLastWidth;
			var width = wanted ? Math.Max(natural, list.ClientSize.Width - others) : natural;
			if (last.Width != width) last.Width = width;
			state.AssignedLastWidth = last.Width;
			state.NaturalLastWidth = natural;
		}

		private static void RefillLastColumn(object sender, EventArgs e) => FillLastColumn((ListView) sender, wanted: true);

		private static void RefillLastColumn(object sender, ColumnWidthChangedEventArgs e)
		{
			var list = (ListView) sender;
			var state = For(list);
			if (state.Refilling) return;
			state.Refilling = true;
			try
			{
				FillLastColumn(list, wanted: true);
			}
			finally
			{
				state.Refilling = false;
			}
		}

		/// <summary>
		/// The row's background, across the whole row INCLUDING the strip past the
		/// last column - the cells paint their own bounds, and that strip is not in
		/// any of them, which is how a selected row came out half themed and half
		/// the desktop's beige.
		/// </summary>
		private static void DrawRow(object sender, DrawListViewItemEventArgs e)
		{
			var list = (ListView) sender;
			if (list.View is not View.Details)
			{
				e.DrawDefault = true;
				return;
			}
			using SolidBrush back = new(RowBackground(list, e.Item, Current));
			var right = Math.Max(e.Bounds.Right, list.ClientRectangle.Right);
			e.Graphics.FillRectangle(back, new Rectangle(e.Bounds.Left, e.Bounds.Top, right - e.Bounds.Left, e.Bounds.Height));
		}

		/// <summary>One cell: its tick box and image if it is the first, then its text.</summary>
		private static void DrawCell(object sender, DrawListViewSubItemEventArgs e)
		{
			var list = (ListView) sender;
			var theme = Current;
			var item = e.Item;
			if (item is null) return;

			using SolidBrush back = new(item.Selected || item.UseItemStyleForSubItems
				? RowBackground(list, item, theme)
				: (e.SubItem ?? item.SubItems[0]).BackColor);
			e.Graphics.FillRectangle(back, e.Bounds);

			var x = e.Bounds.Left + 2;
			if (e.ColumnIndex is 0)
			{
				if (list.CheckBoxes)
				{
					var side = Math.Min(13, e.Bounds.Height - 2);
					Rectangle box = new(x, e.Bounds.Top + ((e.Bounds.Height - side) / 2), side, side);
					DrawTick(e.Graphics, box, item.Checked, theme);
					x = box.Right + 3;
				}
				var images = list.SmallImageList;
				var index = ImageIndexOf(images, item);
				if (images is not null && index >= 0)
				{
					var y = e.Bounds.Top + ((e.Bounds.Height - images.ImageSize.Height) / 2);
					images.Draw(e.Graphics, x, y, index);
					x += images.ImageSize.Width + 2;
				}
			}

			var text = e.SubItem?.Text ?? "";
			if (text.Length is 0) return;
			var font = e.SubItem?.Font ?? item.Font ?? list.Font;
			Rectangle textArea = new(x, e.Bounds.Top, Math.Max(0, e.Bounds.Right - x - 2), e.Bounds.Height);
			TextRenderer.DrawText(
				e.Graphics,
				text,
				font,
				textArea,
				RowForeground(list, item, e.SubItem ?? item.SubItems[0], theme),
				Align(e.Header?.TextAlign) | TextFormatFlags.VerticalCenter | TextFormatFlags.EndEllipsis);
		}

		private static int ImageIndexOf(ImageList? images, ListViewItem item)
		{
			if (images is null) return -1;
			if (item.ImageIndex >= 0 && item.ImageIndex < images.Images.Count) return item.ImageIndex;
			return string.IsNullOrEmpty(item.ImageKey) ? -1 : images.Images.IndexOfKey(item.ImageKey);
		}

		/// <summary>
		/// A tick box in the theme's colours. The one the toolkit draws is a white
		/// square whatever is around it, which on a dark list is the brightest
		/// thing on the window.
		/// </summary>
		private static void DrawTick(Graphics g, Rectangle box, bool ticked, Theme theme)
		{
			using SolidBrush fill = new(theme[ThemeColorRole.InputBackground]);
			g.FillRectangle(fill, box);
			using Pen edge = new(theme[ThemeColorRole.Border]);
			g.DrawRectangle(edge, box.X, box.Y, box.Width - 1, box.Height - 1);
			if (!ticked) return;
			using Pen tick = new(theme[ThemeColorRole.GlyphForeground], 2f);
			Point[] check =
			[
				new(box.Left + 3, box.Top + (box.Height / 2)),
				new(box.Left + (box.Width / 2) - 1, box.Bottom - 4),
				new(box.Right - 3, box.Top + 3),
			];
			g.DrawLines(tick, check);
		}

		private static TextFormatFlags Align(HorizontalAlignment? alignment) => alignment switch
		{
			HorizontalAlignment.Center => TextFormatFlags.HorizontalCenter,
			HorizontalAlignment.Right => TextFormatFlags.Right,
			_ => TextFormatFlags.Left,
		};

		/// <summary>
		/// A list box has the same problem in miniature: the highlight behind the
		/// chosen line is the desktop's, not the theme's. It has no images and no
		/// tick boxes, so taking it over is a fill and a string.
		/// </summary>
		private static void ApplyListBox(ListBox box)
		{
			var state = For(box);
			if (state.OwnerDrawn) return;
			state.OwnerDrawn = true;
			box.DrawMode = DrawMode.OwnerDrawFixed;
			box.DrawItem += DrawListBoxItem;
			box.Invalidate();
		}

		private static void DrawListBoxItem(object sender, DrawItemEventArgs e)
		{
			var box = (ListBox) sender;
			var theme = Current;
			var selected = (e.State & DrawItemState.Selected) is not 0;
			using SolidBrush back = new(theme[selected
				? box.Focused ? ThemeColorRole.Selection : ThemeColorRole.InactiveSelection
				: ThemeColorRole.InputBackground]);
			e.Graphics.FillRectangle(back, e.Bounds);
			if (e.Index < 0 || e.Index >= box.Items.Count) return;
			TextRenderer.DrawText(
				e.Graphics,
				box.GetItemText(box.Items[e.Index]),
				e.Font ?? box.Font,
				Rectangle.Inflate(e.Bounds, -2, 0),
				theme[selected
					? box.Focused ? ThemeColorRole.SelectionText : ThemeColorRole.InactiveSelectionText
					: ThemeColorRole.InputText],
				TextFormatFlags.Left | TextFormatFlags.VerticalCenter | TextFormatFlags.EndEllipsis);
		}


		/// <summary>
		/// A menu bar, tool bar, status bar or context menu, and everything on it.
		///
		/// A ToolStrip paints its own background, border and hover highlight and
		/// ignores BackColor for all three, so the only way in is a renderer. Under
		/// the desktop's own palette it is left exactly as the frontend has always
		/// left it - the system renderer on a Unix host, the manager's renderer on
		/// Windows - because a colour table built from system colours still does
		/// not draw what those two draw.
		/// </summary>
		public static void ApplyStrip(ToolStrip strip, Theme theme)
		{
			if (IsSystemPalette(theme)) return;
			strip.Renderer = ThemeToolStripRenderer.For(theme);
			var status = strip is StatusStrip;
			strip.BackColor = theme[status ? ThemeColorRole.StatusBarBackground : ThemeColorRole.ToolStripBackground];
			strip.ForeColor = theme[status ? ThemeColorRole.StatusBarText : ThemeColorRole.ToolStripText];
			foreach (ToolStripItem item in strip.Items) ApplyItem(item, theme, onDropDown: false);
		}

		private static void ApplyItem(ToolStripItem item, Theme theme, bool onDropDown)
		{
			if (TaggedItems.TryGetValue(item, out var own) && own.Skip) return;
			item.BackColor = own?.Back is { } back
				? theme[back]
				: theme[onDropDown ? ThemeColorRole.MenuBackground : ThemeColorRole.ToolStripBackground];
			item.ForeColor = own?.Fore is { } role
				? theme[role]
				: theme[onDropDown ? ThemeColorRole.MenuText : ThemeColorRole.ToolStripText];

			switch (item)
			{
				case ToolStripDropDownItem drop:
					drop.DropDown.Renderer = ThemeToolStripRenderer.For(theme);
					drop.DropDown.BackColor = theme[ThemeColorRole.MenuBackground];
					drop.DropDown.ForeColor = theme[ThemeColorRole.MenuText];
					foreach (ToolStripItem child in drop.DropDownItems) ApplyItem(child, theme, onDropDown: true);
					break;
				case ToolStripControlHost host when host.Control is not null:
					Apply(host.Control, theme);
					break;
			}
		}

		// ---- applying it to what is already open ---------------------------

		/// <summary>Repaints every window that is open. Called when the theme changes.</summary>
		public static void ApplyToOpenForms(Theme theme)
		{
			foreach (var form in Application.OpenForms.Cast<Form>().ToList())
			{
				try
				{
					if (form.IsDisposed) continue;
					if (form is ThemedForm themed) themed.RaiseThemeChanged(theme);
					else Apply(form, theme);
					form.Invalidate(invalidateChildren: true);
				}
				catch (ObjectDisposedException)
				{
					// a window that closed while we were walking it; nothing to paint
				}
			}
		}
	}
}
