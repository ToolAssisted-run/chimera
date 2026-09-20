#nullable enable

using System.Collections.Generic;
using System.Drawing;
using System.Linq;
using System.ComponentModel;
using System.Reflection;
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

			/// <summary>
			/// Puts this control back the way the toolkit had it, before any theme
			/// touched it. Built by <see cref="Prime"/>, which runs over the whole
			/// window before the walk paints any of it - being walked is too late,
			/// because by then this control's parent is already wearing the theme
			/// and a colour this control never set reads as the parent's.
			/// </summary>
			public Action? Restore;

			/// <summary>A list box's own draw mode, before it was taken over.</summary>
			public DrawMode ListBoxDrawMode = DrawMode.Normal;
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

		/// <summary>
		/// Paints <paramref name="root"/> and everything under it.
		///
		/// The control that was active is made active again at the end. Turning a
		/// list's owner drawing on or off recreates its handle, and a recreated
		/// control is no longer the form's active one - changing the theme is not
		/// an instruction to move somebody's cursor out of the box they were
		/// typing in.
		/// </summary>
		public static void Apply(Control root, Theme theme)
		{
			Prime(root);
			if (root is not Form window)
			{
				Walk(root, theme);
				return;
			}
			var focused = window.ActiveControl;
			Walk(window, theme);
			if (focused is not null && !focused.IsDisposed && focused.FindForm() == window)
			{
				window.ActiveControl = focused;
			}
		}

		/// <summary>
		/// Records what the toolkit gave every control on this window, before the
		/// walk paints a single one of them.
		///
		/// It has to be a pass of its own, and this is why: a control that was
		/// never given a colour of its own does not HAVE one - asking it returns
		/// its parent's. The walk paints a parent before it reaches the children,
		/// so a capture taken as each control is painted reads, for every child,
		/// the colour the theme has just put on the parent. Recording that as "what
		/// the toolkit gave it" and writing it back on the way out nails the theme
		/// on for good.
		///
		/// That is the bug this pass exists for. Chimera starts on Dark; every
		/// window was therefore built and captured wearing Dark; and choosing Light
		/// afterwards changed the title bar, which is set from the theme directly,
		/// and nothing else, which was all being "restored" to dark. It looked like
		/// a Windows-only fault because the tests all built their windows under
		/// Light, where the capture happens to be taken before anything is dark.
		///
		/// Capturing first means every value read is the one the toolkit really
		/// gave, inherited or not, so writing it back is right again.
		/// </summary>
		private static void Prime(Control root)
		{
			if (root is null) return;
			if (Tagged.TryGetValue(root, out var skip) && skip.Skip) return;
			For(root).Restore ??= Capture(root);
			foreach (Control child in root.Controls) Prime(child);
			if (root.ContextMenuStrip is not null) For(root.ContextMenuStrip).Restore ??= Capture((Control)root.ContextMenuStrip);
			if (root is Form form && form.MainMenuStrip is not null && !form.Controls.Contains(form.MainMenuStrip))
			{
				For(form.MainMenuStrip).Restore ??= Capture((Control)form.MainMenuStrip);
			}
		}

		private static void Walk(Control root, Theme theme)
		{
			if (root is null) return;
			if (Tagged.TryGetValue(root, out var own) && own.Skip) return;

			// a control that paints itself takes the whole theme and is not told
			// anything further: the type table below would overwrite what it chose
			if (root is IThemedControl themed) themed.ApplyTheme(theme);
			else ApplyToOne(root, theme, own);

			foreach (Control child in root.Controls) Walk(child, theme);

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

		/// <summary>
		/// A list is painted with its own events held off - see <see cref="Quietly"/>
		/// for why - and everything else is painted directly.
		/// </summary>
		private static void ApplyToOne(Control c, Theme theme, Overrides? own)
		{
			if (c is ListView list)
			{
				Quietly(list, () => Paint(c, theme, own));
				return;
			}
			Paint(c, theme, own);
		}

		/// <summary>
		/// The events a ListView raises when its handle is remade, which is not a
		/// thing that happened: the rows were not ticked and the selection did not
		/// move, the control was rebuilt underneath them.
		/// </summary>
		private static readonly string[] SpuriousListEvents =
		[
			// .NET Framework's names, then Mono's for the same three
			"EVENT_ITEMCHECKED", "EVENT_SELECTEDINDEXCHANGED", "EVENT_ITEMSELECTIONCHANGED",
			"ItemCheckedEvent", "SelectedIndexChangedEvent", "ItemSelectionChangedEvent",
		];

		private static PropertyInfo? _componentEvents;

		/// <summary>
		/// Paints a list with its tick and selection handlers unhooked, and hooks
		/// them back afterwards whatever happens.
		///
		/// Giving a ListView a different border recreates its handle, and WinForms
		/// rebuilds a recreated list by pushing every item back into it one at a
		/// time - raising ItemChecked for each, and moving the selection on the way.
		/// The window on the other end of those events has no way to know they are
		/// not real. Pre-Compiled Modules died of it on being opened: its handler
		/// walks ListView.Items, and a collection half way through being rebuilt is
		/// a NullReferenceException out of the toolkit's own enumerator.
		///
		/// A theme is a coat of paint. It must not be able to tell a window that
		/// somebody ticked a row.
		///
		/// The handlers are reached through the EventHandlerList every Component
		/// keeps, which is the only way to take a subscriber off an event you do not
		/// own. The key for each event is a private static field whose name differs
		/// between the two toolkits, so both names are tried; if neither is there,
		/// nothing is unhooked and the paint still happens. Failing to find it costs
		/// the suppression, never the painting.
		/// </summary>
		private static void Quietly(ListView list, Action paint)
		{
			List<(object Key, Delegate Handler)> held = [];
			EventHandlerList? events = null;
			try
			{
				_componentEvents ??= typeof(Component)
					.GetProperty("Events", BindingFlags.Instance | BindingFlags.NonPublic);
				events = _componentEvents?.GetValue(list) as EventHandlerList;
				if (events is not null)
				{
					foreach (var name in SpuriousListEvents)
					{
						var field = typeof(ListView).GetField(name, BindingFlags.Static | BindingFlags.NonPublic);
						if (field?.GetValue(null) is not { } key) continue;
						if (events[key] is not { } handler) continue;
						events.RemoveHandler(key, handler);
						held.Add((key, handler));
					}
				}
			}
			catch (Exception)
			{
				// a toolkit that does not keep its events where we looked; paint anyway
			}

			try
			{
				paint();
			}
			finally
			{
				foreach (var (key, handler) in held) events!.AddHandler(key, handler);
			}
		}

		private static void Paint(Control c, Theme theme, Overrides? own)
		{
			var state = own ?? For(c);
			// before anything is assigned, and only ever once: what the toolkit
			// gave this control. Without it there is nothing for the desktop theme
			// to put a window BACK to, which is how switching from Dark to Light
			// once changed the title bar and nothing else.
			state.Restore ??= Capture(c);

			if (IsSystemPalette(theme))
			{
				// the desktop's own palette IS what the toolkit had, so the whole
				// of it is the undo, plus the roles the frontend declared for
				// itself - which under this theme are the same colours again
				state.Restore!();
				if (state.Back is { } desktopBack) c.BackColor = theme[desktopBack];
				if (state.Fore is { } desktopFore) c.ForeColor = theme[desktopFore];
				switch (c)
				{
					case ToolStrip strip: ApplyStrip(strip, theme); break;
					case ListView list: ApplyListView(list, wanted: false); break;
					case CheckedListBox: break;
					case ListBox box: ApplyListBox(box, wanted: false); break;
				}
				return;
			}

			if (state.Back is { } backRole) c.BackColor = theme[backRole];
			if (state.Fore is { } foreRole) c.ForeColor = theme[foreRole];

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
					Back(link, state, theme, ThemeColorRole.WindowBackground);
					Fore(link, state, theme, ThemeColorRole.WindowText);
					link.LinkColor = link.ActiveLinkColor = link.VisitedLinkColor = theme[ThemeColorRole.LinkText];
					return;
				case CheckBox cb:
					// the box itself is drawn by the OS; only its caption is ours
					cb.UseVisualStyleBackColor = false;
					Back(c, state, theme, ThemeColorRole.WindowBackground);
					Fore(c, state, theme, ThemeColorRole.WindowText);
					return;
				case RadioButton rb:
					rb.UseVisualStyleBackColor = false;
					Back(c, state, theme, ThemeColorRole.WindowBackground);
					Fore(c, state, theme, ThemeColorRole.WindowText);
					return;
				case ButtonBase bb:
					if (bb is Button plain) plain.UseVisualStyleBackColor = false;
					Back(c, state, theme, ThemeColorRole.ButtonBackground);
					Fore(c, state, theme, ThemeColorRole.ButtonText);
					bb.FlatStyle = FlatStyle.Flat;
					bb.FlatAppearance.BorderColor = theme[ThemeColorRole.ButtonBorder];
					bb.FlatAppearance.MouseOverBackColor = theme[ThemeColorRole.MenuSelectedBackground];
					bb.FlatAppearance.MouseDownBackColor = theme[ThemeColorRole.Selection];
					return;
				case TextBoxBase text:
					// not a special case for ReadOnly: WinForms does not colour a
					// read-only box differently either, and a box that should look
					// different says so with SetBackRole(ReadOnlyBackground)
					Back(text, state, theme, ThemeColorRole.InputBackground);
					Fore(text, state, theme, ThemeColorRole.InputText);
					return;
				case ComboBox combo:
					if (combo.FlatStyle is FlatStyle.Standard) combo.FlatStyle = FlatStyle.Flat;
					Back(combo, state, theme, ThemeColorRole.InputBackground);
					Fore(combo, state, theme, ThemeColorRole.InputText);
					return;
				case PropertyGrid properties:
					// the grid's own surfaces are separate properties; BackColor only
					// reaches the strip around them
					Back(properties, state, theme, ThemeColorRole.WindowBackground);
					Fore(properties, state, theme, ThemeColorRole.WindowText);
					properties.ViewBackColor = theme[ThemeColorRole.InputBackground];
					properties.ViewForeColor = theme[ThemeColorRole.InputText];
					properties.HelpBackColor = theme[ThemeColorRole.WindowBackground];
					properties.HelpForeColor = theme[ThemeColorRole.WindowText];
					properties.LineColor = theme[ThemeColorRole.GridLines];
					properties.CategoryForeColor = theme[ThemeColorRole.WindowText];
					return;
				case ListBox box:
					Back(box, state, theme, ThemeColorRole.InputBackground);
					Fore(box, state, theme, ThemeColorRole.InputText);
					if (box is not CheckedListBox) ApplyListBox(box, wanted: true);
					return;
				case TreeView or NumericUpDown or DateTimePicker:
					Back(c, state, theme, ThemeColorRole.InputBackground);
					Fore(c, state, theme, ThemeColorRole.InputText);
					return;
				case ListView list:
					Back(list, state, theme, ThemeColorRole.InputBackground);
					Fore(list, state, theme, ThemeColorRole.InputText);
					ApplyListView(list, wanted: list.View is View.Details);
					return;
				case ProgressBar:
					// drawn entirely by the OS; setting colours on it does nothing but confuse
					return;
				case TabPage page:
					page.UseVisualStyleBackColor = false;
					Back(page, state, theme, ThemeColorRole.WindowBackground);
					Fore(page, state, theme, ThemeColorRole.WindowText);
					return;
				default:
					Back(c, state, theme, ThemeColorRole.WindowBackground);
					Fore(c, state, theme, ThemeColorRole.WindowText);
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
		/// Assigns only when it would change something. A theme that asks for the
		/// colour a control already has should touch nothing at all - that is what
		/// makes the desktop theme provably unable to alter a window it was the
		/// first theme applied to.
		/// </summary>
		private static void Set(Control c, Color value)
		{
			if (c.BackColor != value) c.BackColor = value;
		}

		/// <summary>
		/// Everything the walk is about to change on this control, remembered as a
		/// way of putting it back.
		///
		/// It is a closure rather than a record so that each thing is captured
		/// beside the line that restores it, and the two cannot drift apart. Only
		/// the properties this file actually writes are here; adding a write
		/// without adding it here is how a theme becomes one-way, which is exactly
		/// what happened.
		/// </summary>
		private static Action Capture(Control c)
		{
			var back = c.BackColor;
			var fore = c.ForeColor;
			Action restore = () =>
			{
				if (c.BackColor != back) c.BackColor = back;
				if (c.ForeColor != fore) c.ForeColor = fore;
			};

			switch (c)
			{
				case LinkLabel link:
				{
					var normal = link.LinkColor;
					var active = link.ActiveLinkColor;
					var visited = link.VisitedLinkColor;
					restore += () =>
					{
						link.LinkColor = normal;
						link.ActiveLinkColor = active;
						link.VisitedLinkColor = visited;
					};
					break;
				}
				case DataGridView grid:
				{
					var headers = grid.EnableHeadersVisualStyles;
					var background = grid.BackgroundColor;
					var lines = grid.GridColor;
					var styles = new[] { grid.DefaultCellStyle, grid.AlternatingRowsDefaultCellStyle, grid.ColumnHeadersDefaultCellStyle, grid.RowHeadersDefaultCellStyle }
						.Select(static style => (Style: style, style.BackColor, style.ForeColor, style.SelectionBackColor, style.SelectionForeColor))
						.ToArray();
					restore += () =>
					{
						grid.EnableHeadersVisualStyles = headers;
						grid.BackgroundColor = background;
						grid.GridColor = lines;
						foreach (var was in styles)
						{
							was.Style.BackColor = was.BackColor;
							was.Style.ForeColor = was.ForeColor;
							was.Style.SelectionBackColor = was.SelectionBackColor;
							was.Style.SelectionForeColor = was.SelectionForeColor;
						}
					};
					break;
				}
				case PropertyGrid properties:
				{
					var view = properties.ViewBackColor;
					var viewInk = properties.ViewForeColor;
					var help = properties.HelpBackColor;
					var helpInk = properties.HelpForeColor;
					var lines = properties.LineColor;
					var category = properties.CategoryForeColor;
					restore += () =>
					{
						properties.ViewBackColor = view;
						properties.ViewForeColor = viewInk;
						properties.HelpBackColor = help;
						properties.HelpForeColor = helpInk;
						properties.LineColor = lines;
						properties.CategoryForeColor = category;
					};
					break;
				}
				case ComboBox combo:
				{
					var flat = combo.FlatStyle;
					restore += () => combo.FlatStyle = flat;
					break;
				}
			}

			// the ones that are not exclusive: a CheckBox is a ButtonBase, a
			// ListBox has a border AND a draw mode
			if (c is ButtonBase button)
			{
				var flat = button.FlatStyle;
				var edge = button.FlatAppearance.BorderColor;
				var over = button.FlatAppearance.MouseOverBackColor;
				var down = button.FlatAppearance.MouseDownBackColor;
				restore += () =>
				{
					button.FlatStyle = flat;
					button.FlatAppearance.BorderColor = edge;
					button.FlatAppearance.MouseOverBackColor = over;
					button.FlatAppearance.MouseDownBackColor = down;
				};
			}
			switch (c)
			{
				case Button plain:
				{
					var styled = plain.UseVisualStyleBackColor;
					restore += () => plain.UseVisualStyleBackColor = styled;
					break;
				}
				case CheckBox tick:
				{
					var styled = tick.UseVisualStyleBackColor;
					restore += () => tick.UseVisualStyleBackColor = styled;
					break;
				}
				case RadioButton radio:
				{
					var styled = radio.UseVisualStyleBackColor;
					restore += () => radio.UseVisualStyleBackColor = styled;
					break;
				}
				case TabPage page:
				{
					var styled = page.UseVisualStyleBackColor;
					restore += () => page.UseVisualStyleBackColor = styled;
					break;
				}
			}

			// FlattenBorder's undo
			switch (c)
			{
				case ListBox list: restore += Border(list.BorderStyle, style => list.BorderStyle = style); break;
				case ListView view: restore += Border(view.BorderStyle, style => view.BorderStyle = style); break;
				case TreeView tree: restore += Border(tree.BorderStyle, style => tree.BorderStyle = style); break;
				case TextBoxBase text: restore += Border(text.BorderStyle, style => text.BorderStyle = style); break;
				case Panel panel: restore += Border(panel.BorderStyle, style => panel.BorderStyle = style); break;
			}

			if (c is ListBox box) For(box).ListBoxDrawMode = box.DrawMode;
			// a strip is reached both through the walk and directly, and it is the
			// renderer that paints it - miss this and a menu stays dark for ever
			// while its BackColor says otherwise
			if (c is ToolStrip strip) restore += Capture(strip);
			return restore;
		}

		private static Action Border(BorderStyle was, Action<BorderStyle> set) => () => set(was);

		/// <summary>
		/// The same, for something on a tool strip. It resets rather than records:
		/// an item's colour is the STRIP's until somebody sets one, and by the time
		/// the walk reaches the items the strip has already been recoloured - so
		/// recording what the item reads back gives you the strip's new colour, and
		/// "restoring" it writes the dark one in permanently. Resetting puts the
		/// item back to following its strip, which is what it was doing.
		///
		/// An item that had a colour of its own has a ROLE saying so, and
		/// RestoreItem puts that back afterwards; the two that exist (the RAM
		/// tools' error button) do.
		/// </summary>
		private static Action Capture(ToolStripItem item)
			=> () =>
			{
				item.ResetBackColor();
				item.ResetForeColor();
			};

		/// <summary>
		/// And for the strip itself, which is mostly its renderer: a ToolStrip
		/// paints its own background and border, so the renderer is the change and
		/// putting the old one back is the undo. Restoring a guess instead - the
		/// system renderer, say - is how a menu came back looking not quite like
		/// one that had never been dark.
		/// </summary>
		private static Action Capture(ToolStrip strip)
		{
			var back = strip.BackColor;
			var fore = strip.ForeColor;
			var mode = strip.RenderMode;
			var renderer = mode is ToolStripRenderMode.Custom ? strip.Renderer : null;
			return () =>
			{
				strip.BackColor = back;
				strip.ForeColor = fore;
				if (renderer is not null) strip.Renderer = renderer;
				else strip.RenderMode = mode;
			};
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
		private static void ApplyListView(ListView list, bool wanted)
		{
			var state = For(list);
			if (wanted == state.OwnerDrawn) return;
			state.OwnerDrawn = wanted;
			// Turning owner drawing on or off recreates the control's handle, and a
			// recreated list comes back with nothing selected and scrolled to the
			// top. Changing the theme is not an instruction to forget which row
			// somebody had picked.
			//
			// Only for a list that is already on screen, though, and that guard is
			// load-bearing: asking a ListView for SelectedIndices or TopItem CREATES
			// its handle, and creating a ListView's handle inserts its items into the
			// native control one at a time, each insertion raising ItemChecked. The
			// walk runs when the WINDOW's handle is made, which is before the
			// children have theirs, so this was manufacturing a burst of tick events
			// inside a window that had not finished opening. Pre-Compiled Modules
			// died of it: its handler walks ListView.Items, and ListView.Items is not
			// walkable half way through being rebuilt. A list with no handle has
			// nothing chosen and nothing scrolled, so there is nothing to preserve.
			var live = list.IsHandleCreated;
			var chosen = live ? list.SelectedIndices.Cast<int>().ToArray() : [];
			var top = live && list.View is View.Details ? list.TopItem?.Index ?? -1 : -1;
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
			foreach (var i in chosen)
			{
				if (i >= 0 && i < list.Items.Count) list.Items[i].Selected = true;
			}
			if (top > 0 && top < list.Items.Count)
			{
				try
				{
					list.TopItem = list.Items[top];
				}
				catch (InvalidOperationException)
				{
					// a list that cannot be scrolled yet; it is at the top anyway
				}
			}
			list.Invalidate();
		}

		/// <summary>
		/// Every state a row can be in. A list that is owner-drawn is drawn ENTIRELY
		/// by us, so a state nobody thought about is not "drawn by the toolkit
		/// instead", it is not drawn at all - which is how a row the pointer was on
		/// became an empty bar.
		///
		/// The order matters and is the order below: disabled beats everything,
		/// then the selection, then the pointer. A row that is both selected and
		/// under the pointer stays the selection, deliberately - the selection is
		/// the stronger statement and moving the mouse over the row you have
		/// already chosen should not change what it looks like.
		/// </summary>
		private static bool Hot(ListViewItemStates states) => (states & ListViewItemStates.Hot) is not 0;

		/// <summary>What a row's background is, in whichever state it is in.</summary>
		private static Color RowBackground(ListView list, ListViewItem item, Theme theme, ListViewItemStates states)
		{
			if (!list.Enabled) return theme[ThemeColorRole.DisabledBackground];
			if (item.Selected) return theme[list.Focused ? ThemeColorRole.Selection : ThemeColorRole.InactiveSelection];
			return Hot(states) ? theme[ThemeColorRole.HoverBackground] : item.BackColor;
		}

		/// <summary>
		/// And its text. A selected row's own colour is given up to the selection,
		/// which is what every list does - a colour that means something (a missing
		/// firmware file in red) is readable again the moment the row is not
		/// selected, and unreadable text on the highlight helps nobody.
		///
		/// A HOVERED row is the other way round: only its background changes, and
		/// the text keeps the colour that means something. So every text role has
		/// to stay readable on HoverBackground, which ThemeContrastTests checks.
		/// </summary>
		private static Color RowForeground(ListView list, ListViewItem item, ListViewItem.ListViewSubItem sub, Theme theme)
		{
			if (!list.Enabled) return theme[ThemeColorRole.DisabledText];
			return item.Selected
				? theme[list.Focused ? ThemeColorRole.SelectionText : ThemeColorRole.InactiveSelectionText]
				// a sub-item's own ForeColor is the LIST's until somebody turns
				// UseItemStyleForSubItems off; reading it instead of the item's is
				// how a whole window of red and green firmware rows came out grey
				: item.UseItemStyleForSubItems ? item.ForeColor : sub.ForeColor;
		}

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
			using SolidBrush back = new(RowBackground(list, e.Item, Current, e.State));
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

			// WinForms hands this event a null SubItem when it is redrawing a row
			// because the pointer moved onto it - the same event, with ItemIndex -1
			// and nothing in SubItem. The background was being painted from that and
			// the TEXT was being skipped, so pointing at a row wiped it: a coloured
			// bar with nothing written on it. The cell is identified by its COLUMN,
			// which is always there, so look the sub-item up rather than trusting it
			// to arrive.
			var sub = e.SubItem
				?? (e.ColumnIndex >= 0 && e.ColumnIndex < item.SubItems.Count ? item.SubItems[e.ColumnIndex] : item.SubItems[0]);

			using SolidBrush back = new(item.Selected || item.UseItemStyleForSubItems || Hot(e.ItemState) || !list.Enabled
				? RowBackground(list, item, theme, e.ItemState)
				: sub.BackColor);
			e.Graphics.FillRectangle(back, e.Bounds);

			var x = e.Bounds.Left + 2;
			if (e.ColumnIndex is 0)
			{
				if (list.CheckBoxes)
				{
					var side = Math.Min(13, e.Bounds.Height - 2);
					Rectangle box = new(x, e.Bounds.Top + ((e.Bounds.Height - side) / 2), side, side);
					DrawTick(e.Graphics, box, item.Checked, theme, list.Enabled);
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

			var text = sub.Text ?? "";
			if (text.Length is 0) return;
			var font = sub.Font ?? item.Font ?? list.Font;
			Rectangle textArea = new(x, e.Bounds.Top, Math.Max(0, e.Bounds.Right - x - 2), e.Bounds.Height);
			TextRenderer.DrawText(
				e.Graphics,
				text,
				font,
				textArea,
				RowForeground(list, item, sub, theme),
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
		private static void DrawTick(Graphics g, Rectangle box, bool ticked, Theme theme, bool enabled = true)
		{
			using SolidBrush fill = new(theme[enabled ? ThemeColorRole.InputBackground : ThemeColorRole.DisabledBackground]);
			g.FillRectangle(fill, box);
			using Pen edge = new(theme[enabled ? ThemeColorRole.Border : ThemeColorRole.DisabledText]);
			g.DrawRectangle(edge, box.X, box.Y, box.Width - 1, box.Height - 1);
			if (!ticked) return;
			using Pen tick = new(theme[enabled ? ThemeColorRole.GlyphForeground : ThemeColorRole.DisabledText], 2f);
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
		private static void ApplyListBox(ListBox box, bool wanted)
		{
			var state = For(box);
			if (wanted == state.OwnerDrawn) return;
			state.OwnerDrawn = wanted;
			// same as the list view: changing the draw mode recreates the handle
			var chosen = box.SelectedIndices.Cast<int>().ToArray();
			if (wanted)
			{
				box.DrawMode = DrawMode.OwnerDrawFixed;
				box.DrawItem += DrawListBoxItem;
			}
			else
			{
				box.DrawItem -= DrawListBoxItem;
				box.DrawMode = state.ListBoxDrawMode;
			}
			foreach (var i in chosen)
			{
				if (i >= 0 && i < box.Items.Count) box.SetSelected(i, true);
			}
			box.Invalidate();
		}

		private static void DrawListBoxItem(object sender, DrawItemEventArgs e)
		{
			var box = (ListBox) sender;
			var theme = Current;
			var selected = (e.State & DrawItemState.Selected) is not 0;
			// the same enumeration the ListView above has to do: a list box the
			// toolkit no longer draws has no state it can fall back on
			var off = !box.Enabled || (e.State & DrawItemState.Disabled) is not 0;
			using SolidBrush back = new(theme[off
				? ThemeColorRole.DisabledBackground
				: selected
					? box.Focused ? ThemeColorRole.Selection : ThemeColorRole.InactiveSelection
					: ThemeColorRole.InputBackground]);
			e.Graphics.FillRectangle(back, e.Bounds);
			if (e.Index < 0 || e.Index >= box.Items.Count) return;
			TextRenderer.DrawText(
				e.Graphics,
				box.GetItemText(box.Items[e.Index]),
				e.Font ?? box.Font,
				Rectangle.Inflate(e.Bounds, -2, 0),
				theme[off
					? ThemeColorRole.DisabledText
					: selected
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
			var state = For(strip);
			// the Control capture, not the ToolStrip one: it takes the colours AND
			// appends the renderer, so a strip reached only through here is undone
			// the same way as one the walk reached
			state.Restore ??= Capture((Control)strip);
			if (IsSystemPalette(theme))
			{
				state.Restore!();
				foreach (ToolStripItem item in strip.Items) RestoreItem(item);
				return;
			}
			strip.Renderer = ThemeToolStripRenderer.For(theme);
			var status = strip is StatusStrip;
			strip.BackColor = theme[status ? ThemeColorRole.StatusBarBackground : ThemeColorRole.ToolStripBackground];
			strip.ForeColor = theme[status ? ThemeColorRole.StatusBarText : ThemeColorRole.ToolStripText];
			foreach (ToolStripItem item in strip.Items) ApplyItem(item, theme, onDropDown: false);
		}

		/// <summary>Puts an item, and anything under it, back to the colours it had.</summary>
		private static void RestoreItem(ToolStripItem item)
		{
			if (TaggedItems.TryGetValue(item, out var state))
			{
				state.Restore?.Invoke();
				if (state.Back is { } back) item.BackColor = Current[back];
				if (state.Fore is { } fore) item.ForeColor = Current[fore];
			}
			switch (item)
			{
				case ToolStripDropDownItem drop:
					if (For(drop.DropDown).Restore is { } putBack) putBack();
					foreach (ToolStripItem child in drop.DropDownItems) RestoreItem(child);
					break;
				case ToolStripControlHost host when host.Control is not null:
					Apply(host.Control, Current);
					break;
			}
		}

		private static void ApplyItem(ToolStripItem item, Theme theme, bool onDropDown)
		{
			var own = TaggedItems.GetValue(item, static _ => new Overrides());
			if (own.Skip) return;
			own.Restore ??= Capture(item);
			item.BackColor = own.Back is { } back
				? theme[back]
				: theme[onDropDown ? ThemeColorRole.MenuBackground : ThemeColorRole.ToolStripBackground];
			item.ForeColor = own.Fore is { } role
				? theme[role]
				: theme[onDropDown ? ThemeColorRole.MenuText : ThemeColorRole.ToolStripText];

			switch (item)
			{
				case ToolStripDropDownItem drop:
				{
					var below = For(drop.DropDown);
					below.Restore ??= Capture((Control)drop.DropDown);
					drop.DropDown.Renderer = ThemeToolStripRenderer.For(theme);
					drop.DropDown.BackColor = theme[ThemeColorRole.MenuBackground];
					drop.DropDown.ForeColor = theme[ThemeColorRole.MenuText];
					foreach (ToolStripItem child in drop.DropDownItems) ApplyItem(child, theme, onDropDown: true);
					break;
				}
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
