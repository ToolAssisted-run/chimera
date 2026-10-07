#nullable enable

using System;
using System.Collections.Generic;
using System.Linq;
using System.Windows.Forms;

using Chimera.Client.Common;
using Chimera.Emulation.Common;
using Chimera.WinForms.Controls;

namespace Chimera.Client.GUI
{
	/// <summary>
	/// RAM Watch &gt; Watches &gt; Add Game Properties: a game core's properties by name,
	/// grouped as the core groups them, to tick and add as watches (docs/game-cores.md).
	/// An array is listed element by element (<c>Guards.X[2]</c>), each a watch of its
	/// own. One already watched is shown in grey rather than offered twice.
	///
	/// A dynamic table (a Flash movie's variables) is the same list with two things more:
	/// it is read from the core when the window opens and again on Refresh, because what
	/// a movie keeps changes while it runs; and it can be long, so the box at the top
	/// narrows it to the names that hold what is typed. Ticks are kept while narrowing.
	/// </summary>
	public sealed class GamePropertyPicker : FormBase
	{
		/// <summary>The most rows listed at once; past it the list says how many more there are.</summary>
		public const int MaxRows = 2000;

		private readonly IGameProperties _properties;
		private readonly Func<GamePropertyElement, string> _valueOf;
		private readonly Func<GamePropertyElement, bool> _watched;
		private readonly ListView _list;
		private readonly TextBox _filter;
		private readonly Button _add;
		private readonly Dictionary<string, GamePropertyElement> _ticked = new(StringComparer.OrdinalIgnoreCase);
		private bool _filling;

		protected override string WindowTitleStatic => "Add Game Properties";

		/// <param name="properties">the core's properties</param>
		/// <param name="valueOf">an element's current value as a person reads it</param>
		/// <param name="watched">whether an element is already in the watch list</param>
		public GamePropertyPicker(IGameProperties properties, Func<GamePropertyElement, string> valueOf, Func<GamePropertyElement, bool> watched)
		{
			_properties = properties;
			_valueOf = valueOf;
			_watched = watched;
			// what a movie keeps now, not what it kept when the core was loaded
			if (properties.IsDynamic) properties.Refresh();

			SuspendLayout();
			ClientSize = new(UIHelper.ScaleX(720), UIHelper.ScaleY(450));
			MinimizeBox = false;
			StartPosition = FormStartPosition.CenterParent;
			var margin = UIHelper.ScaleX(10);

			Label intro = new()
			{
				Anchor = AnchorStyles.Top | AnchorStyles.Left | AnchorStyles.Right,
				Location = new(margin, UIHelper.ScaleY(8)),
				Size = new(ClientSize.Width - (2 * margin), UIHelper.ScaleY(20)),
				Text = properties.IsDynamic
					? "Tick the variables to watch. Each is watched by its name, wherever the game keeps it."
					: "Tick the properties to watch. Each is added under its own name, and a freeze of it keeps that name.",
			};

			var refreshWidth = properties.IsDynamic ? UIHelper.ScaleX(90) + UIHelper.ScaleX(6) : 0;
			Label find = new()
			{
				AutoSize = true,
				Location = new(margin, UIHelper.ScaleY(35)),
				Text = "Find:",
			};
			_filter = new TextBox
			{
				Anchor = AnchorStyles.Top | AnchorStyles.Left | AnchorStyles.Right,
				Location = new(margin + UIHelper.ScaleX(40), UIHelper.ScaleY(32)),
				Size = new(ClientSize.Width - (2 * margin) - UIHelper.ScaleX(40) - refreshWidth, UIHelper.ScaleY(22)),
			};
			_filter.TextChanged += (_, _) => Fill();
			Button refresh = new()
			{
				Anchor = AnchorStyles.Top | AnchorStyles.Right,
				Location = new(ClientSize.Width - margin - UIHelper.ScaleX(90), UIHelper.ScaleY(30)),
				Size = new(UIHelper.ScaleX(90), UIHelper.ScaleY(26)),
				Text = "Refresh",
				Visible = properties.IsDynamic,
			};
			refresh.Click += (_, _) => Reread();

			_list = new ListView
			{
				Anchor = AnchorStyles.Top | AnchorStyles.Bottom | AnchorStyles.Left | AnchorStyles.Right,
				CheckBoxes = true,
				FullRowSelect = true,
				HideSelection = false,
				Location = new(margin, UIHelper.ScaleY(62)),
				Size = new(ClientSize.Width - (2 * margin), ClientSize.Height - UIHelper.ScaleY(106)),
				View = View.Details,
			};
			_list.Columns.Add("Property", UIHelper.ScaleX(220));
			_list.Columns.Add("Type", UIHelper.ScaleX(70));
			_list.Columns.Add("Address", UIHelper.ScaleX(80));
			_list.Columns.Add("Value", UIHelper.ScaleX(100));
			_list.Columns.Add("Description", UIHelper.ScaleX(210));

			// only a property that is offered may be ticked
			_list.ItemCheck += (_, e) =>
			{
				if (e.Index >= 0 && e.Index < _list.Items.Count && _list.Items[e.Index].Tag is null) e.NewValue = CheckState.Unchecked;
			};
			_list.ItemChecked += (_, e) =>
			{
				if (_filling || e.Item.Tag is not GamePropertyElement element) return;
				if (e.Item.Checked) _ticked[element.Name] = element;
				else _ticked.Remove(element.Name);
				_add!.Enabled = _ticked.Count is not 0;
			};

			_add = new Button
			{
				Anchor = AnchorStyles.Bottom | AnchorStyles.Right,
				DialogResult = DialogResult.OK,
				Enabled = false,
				Location = new(ClientSize.Width - margin - UIHelper.ScaleX(180) - UIHelper.ScaleX(6), ClientSize.Height - UIHelper.ScaleY(36)),
				Size = new(UIHelper.ScaleX(90), UIHelper.ScaleY(26)),
				Text = "Add",
			};
			Button cancel = new()
			{
				Anchor = AnchorStyles.Bottom | AnchorStyles.Right,
				DialogResult = DialogResult.Cancel,
				Location = new(ClientSize.Width - margin - UIHelper.ScaleX(90), ClientSize.Height - UIHelper.ScaleY(36)),
				Size = new(UIHelper.ScaleX(90), UIHelper.ScaleY(26)),
				Text = "Cancel",
			};

			Controls.AddRange(new Control[] { intro, find, _filter, refresh, _list, _add, cancel });
			AcceptButton = _add;
			CancelButton = cancel;
			Fill();
			ResumeLayout();
		}

		/// <summary>
		/// Lists what the box at the top lets through: the core's groups in the order it
		/// first names them, each under a grey row (Mono's ListView ignores groups in
		/// Details view, so a divide is a row here as in every other list).
		/// </summary>
		private void Fill()
		{
			var filter = _filter.Text.Trim();
			_filling = true;
			_list.BeginUpdate();
			_list.Items.Clear();
			var more = 0;
			foreach (var group in _properties.Properties.Where(static p => p.Listed).GroupBy(static p => p.Group))
			{
				var elements = group.SelectMany(static p => p.Elements)
					.Where(e => filter.Length is 0 || e.Name.IndexOf(filter, StringComparison.OrdinalIgnoreCase) >= 0)
					.ToList();
				if (elements.Count is 0) continue;
				// a heading with nothing under it says nothing: it needs room for one row too
				var heading = group.Key.Length is not 0 ? 1 : 0;
				var room = MaxRows - _list.Items.Count - heading;
				if (room <= 0)
				{
					more += elements.Count;
					continue;
				}
				if (heading is not 0)
				{
					ListViewItem divide = new(group.Key) { Tag = null, ForeColor = ThemeEngine.Color(ThemeColorRole.DisabledText) };
					for (var i = 1; i < _list.Columns.Count; i++) divide.SubItems.Add("");
					_list.Items.Add(divide);
				}
				more += Math.Max(0, elements.Count - room);
				foreach (var element in elements.Take(room))
				{
					var property = element.Property;
					var already = _watched(element);
					ListViewItem row = new(element.Name)
					{
						Tag = already ? null : element,
						ToolTipText = property.Description,
						ForeColor = ThemeEngine.Color(already || !property.Writable ? ThemeColorRole.DisabledText : ThemeColorRole.InputText),
					};
					row.SubItems.Add(property.TypeName
						+ (property.Type is GamePropertyType.String or GamePropertyType.Bytes ? $"({property.Size})" : "")
						+ (property.IsBitField ? $":{property.Bits}" : ""));
					row.SubItems.Add(element.Offset.ToString("X"));
					row.SubItems.Add(_valueOf(element));
					row.SubItems.Add(already ? "(already watched)" : property.Writable ? property.Description : $"{property.Description} (read-only)".TrimStart());
					row.Checked = !already && _ticked.ContainsKey(element.Name);
					_list.Items.Add(row);
				}
			}
			if (more is not 0)
			{
				ListViewItem rest = new($"... and {more} more: type part of a name above") { Tag = null, ForeColor = ThemeEngine.Color(ThemeColorRole.DisabledText) };
				for (var i = 1; i < _list.Columns.Count; i++) rest.SubItems.Add("");
				_list.Items.Add(rest);
			}
			_list.EndUpdate();
			_filling = false;
		}

		/// <summary>Has the core list again; a tick stays on what the new list still has.</summary>
		private void Reread()
		{
			_properties.Refresh();
			var kept = _ticked.Keys.Select(name => _properties.Find(name)).Where(static e => e is { Property.Listed: true }).ToList();
			_ticked.Clear();
			foreach (var element in kept) _ticked[element!.Name] = element;
			_add.Enabled = _ticked.Count is not 0;
			Fill();
		}

		/// <summary>What is ticked, in the core's order - whether or not the box at the top shows it now.</summary>
		public IReadOnlyList<GamePropertyElement> Chosen
			=> _ticked.Values.OrderBy(static e => e.Property.Index).ThenBy(static e => e.Index).ToList();

		/// <summary>The rows as listed, a group's heading by its name: for tests.</summary>
		public IReadOnlyList<string> Rows => _list.Items.Cast<ListViewItem>().Select(static i => i.Text).ToList();

		/// <summary>A row's cells, by the property's name: for tests.</summary>
		public IReadOnlyList<string> Cells(string name)
			=> _list.Items.Cast<ListViewItem>().Where(i => i.Text == name).SelectMany(static i => i.SubItems.Cast<ListViewItem.ListViewSubItem>().Select(static c => c.Text)).ToList();

		/// <summary>Ticks a property by name, as a person would: for tests.</summary>
		public void Tick(string name)
		{
			foreach (ListViewItem item in _list.Items)
			{
				if (item.Text == name) item.Checked = true;
			}
		}

		/// <summary>Types into the box at the top: for tests.</summary>
		public void Narrow(string text) => _filter.Text = text;

		/// <summary>Presses Refresh: for tests.</summary>
		public void PressRefresh() => Reread();
	}
}
