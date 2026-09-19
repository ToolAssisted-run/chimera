#nullable enable

using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Drawing;
using System.Linq;
using System.Windows.Forms;

using Chimera.Client.Common;
using Chimera.Common;

namespace Chimera.Client.GUI
{
	/// <summary>
	/// Tools &gt; Pre-Compiled Modules: the games a core has translated code for,
	/// and the only place that code is taken away (docs/compile-cache.md).
	///
	/// FILLING the cache is the wizard's - a project is not created until its
	/// game is compiled, because a project whose game is not compiled is a
	/// project that boots into a minutes-long stall. This window is the other
	/// half: what is in there, what it weighs, when it was made, and a way to
	/// remove one game without touching another.
	///
	/// One game, one directory, named by the game's own SHA1 (user-decided,
	/// 2026-09-17). Games do not share objects - each keeps its own copy of
	/// everything it needs - so removing a row is deleting one directory and
	/// can never leave another game short. That is what makes Remove safe to
	/// offer per row rather than as the all-or-nothing "clear everything" this
	/// replaces.
	///
	/// Thin over <see cref="PrecompiledCodeSurvey"/>, like the cache manager is
	/// over its survey: what is listed and what removing it costs are the
	/// model's, proved without a window.
	/// </summary>
	public sealed class PrecompiledModulesForm : FormBase
	{
		private readonly Func<IReadOnlyList<PrecompiledGame>> _survey;
		private readonly Action<PrecompiledGame> _remove;

		private readonly ListView _list;
		private readonly Label _header;
		private readonly Label _detail;
		private readonly Button _removeButton;
		private readonly Button _openFolder;

		/// <summary>
		/// The ticked rows by directory, which is unique and survives the list
		/// being rebuilt. A set rather than a walk of the ListView for the reason
		/// the cache manager records: the tick event arrives as a posted message,
		/// and by the time it lands the collection may be mid-rebuild.
		/// </summary>
		private readonly HashSet<string> _ticked = new(StringComparer.Ordinal);

		private bool _suppressCheckEvents;

		/// <summary>
		/// False until every control exists. A ListView raises ItemChecked while
		/// its handle is created, which on .NET Framework happens inside the
		/// constructor - before the buttons the handler enables are there. Mono
		/// does not, so a Linux test would never see it.
		/// </summary>
		private bool _ready;

		private List<PrecompiledGame> _items = new();

		protected override string WindowTitleStatic => "Pre-Compiled Modules";

		/// <param name="survey">what is on disk; called on open and after every removal</param>
		/// <param name="remove">takes one row away; the model's, so this window can be tested without a disk</param>
		public PrecompiledModulesForm(
			Func<IReadOnlyList<PrecompiledGame>> survey,
			Action<PrecompiledGame>? remove = null)
		{
			_survey = survey;
			_remove = remove ?? PrecompiledCodeSurvey.Remove;

			SuspendLayout();
			ClientSize = new(UIHelper.ScaleX(900), UIHelper.ScaleY(460));
			MinimumSize = new(UIHelper.ScaleX(620), UIHelper.ScaleY(340));
			StartPosition = FormStartPosition.CenterParent;
			ShowIcon = false;

			var margin = UIHelper.ScaleX(8);
			var listTop = UIHelper.ScaleY(52);
			var footer = UIHelper.ScaleY(88);

			_header = new Label
			{
				Anchor = AnchorStyles.Top | AnchorStyles.Left | AnchorStyles.Right,
				AutoSize = false,
				Location = new(margin, UIHelper.ScaleY(9)),
				Size = new(ClientSize.Width - (2 * margin), UIHelper.ScaleY(36)),
			};

			_list = new ListView
			{
				Anchor = AnchorStyles.Top | AnchorStyles.Bottom | AnchorStyles.Left | AnchorStyles.Right,
				CheckBoxes = true,
				FullRowSelect = true,
				HideSelection = false,
				Location = new(margin, listTop),
				MultiSelect = false,
				Size = new(ClientSize.Width - (2 * margin), ClientSize.Height - listTop - footer),
				UseCompatibleStateImageBehavior = false,
				View = View.Details,
			};
			_list.Columns.Add("Game", UIHelper.ScaleX(300));
			_list.Columns.Add("Compiled by", UIHelper.ScaleX(180));
			_list.Columns.Add("When", UIHelper.ScaleX(150));
			_list.Columns.Add("Modules", UIHelper.ScaleX(80), HorizontalAlignment.Right);
			_list.Columns.Add("Size", UIHelper.ScaleX(100), HorizontalAlignment.Right);
			_list.ItemChecked += (_, _) => Ticked();
			_list.SelectedIndexChanged += (_, _) => ShowDetail();

			_detail = new Label
			{
				Anchor = AnchorStyles.Bottom | AnchorStyles.Left | AnchorStyles.Right,
				AutoSize = false,
				Location = new(margin, ClientSize.Height - footer + UIHelper.ScaleY(6)),
				Size = new(ClientSize.Width - (2 * margin), UIHelper.ScaleY(34)),
			};

			var buttonWidth = UIHelper.ScaleX(150);
			var buttonHeight = UIHelper.ScaleY(26);
			var buttonTop = ClientSize.Height - buttonHeight - margin;

			_removeButton = new Button
			{
				Anchor = AnchorStyles.Bottom | AnchorStyles.Left,
				Enabled = false,
				Location = new(margin, buttonTop),
				Size = new(buttonWidth, buttonHeight),
				Text = "&Remove",
			};
			_removeButton.Click += (_, _) => RemoveTicked();

			_openFolder = new Button
			{
				Anchor = AnchorStyles.Bottom | AnchorStyles.Left,
				Enabled = false,
				Location = new(margin + buttonWidth + margin, buttonTop),
				Size = new(buttonWidth, buttonHeight),
				Text = "&Open folder",
			};
			_openFolder.Click += (_, _) => OpenSelectedFolder();

			Button close = new()
			{
				Anchor = AnchorStyles.Bottom | AnchorStyles.Right,
				DialogResult = DialogResult.OK,
				Location = new(ClientSize.Width - margin - buttonWidth, buttonTop),
				Size = new(buttonWidth, buttonHeight),
				Text = "&Close",
			};

			Controls.Add(_header);
			Controls.Add(_list);
			Controls.Add(_detail);
			Controls.Add(_removeButton);
			Controls.Add(_openFolder);
			Controls.Add(close);
			AcceptButton = close;
			CancelButton = close;
			ResumeLayout(performLayout: false);

			_ready = true;
			Refresh();
		}

		/// <summary>What the list shows right now, for tests: label, size and whether it is complete.</summary>
		public IReadOnlyList<(string Label, long Bytes, bool Complete)> Rows
			=> _items.Select(static i => (i.Label, i.Bytes, i.Complete)).ToList();

		/// <summary>Ticks a row by its label, for tests driving the window without a mouse.</summary>
		public void Tick(string label, bool ticked = true)
		{
			foreach (ListViewItem row in _list.Items)
			{
				if (row.Text == label) row.Checked = ticked;
			}
		}

		/// <summary>Removes what is ticked without asking, for tests.</summary>
		public void RemoveTickedForTest() => RemoveTicked(confirm: false);

		private new void Refresh()
		{
			_items = _survey().ToList();
			_suppressCheckEvents = true;
			_list.BeginUpdate();
			_list.Items.Clear();
			foreach (var game in _items)
			{
				ListViewItem row = new(game.Label)
				{
					Checked = _ticked.Contains(game.Path),
					Tag = game,
					// a row nothing can name, and one missing pieces, are the two
					// worth an eye: neither is an error, both are worth seeing
					ForeColor = game.Unknown || game.Legacy
						? SystemColors.GrayText
						: game.Complete ? SystemColors.WindowText : Color.Firebrick,
				};
				row.SubItems.Add(game.By);
				row.SubItems.Add(game.Compiled is { } when ? when.ToLocalTime().ToString("yyyy-MM-dd HH:mm") : "");
				row.SubItems.Add(game.Modules is 0 ? "" : game.Modules.ToString());
				row.SubItems.Add(SizeText(game.Bytes));
				_list.Items.Add(row);
			}
			_list.EndUpdate();
			_suppressCheckEvents = false;

			var total = PrecompiledCodeSurvey.TotalBytes(_items);
			_header.Text = _items.Count is 0
				? "No game has compiled code yet. A core that translates its game's code fills this when a project is created."
				: $"{_items.Count} game{(_items.Count is 1 ? "" : "s")}, {SizeText(total)}. "
					+ "Removing one means its game is compiled again the next time a project needs it, which costs minutes and never work.";
			Ticked();
			ShowDetail();
		}

		private void Ticked()
		{
			if (!_ready || _suppressCheckEvents) return;
			_ticked.Clear();
			foreach (ListViewItem row in _list.Items)
			{
				if (row.Checked && row.Tag is PrecompiledGame game) _ticked.Add(game.Path);
			}
			var bytes = _items.Where(i => _ticked.Contains(i.Path)).Sum(static i => i.Bytes);
			_removeButton.Enabled = _ticked.Count is not 0;
			_removeButton.Text = _ticked.Count is 0 ? "&Remove" : $"&Remove ({SizeText(bytes)})";
		}

		private void ShowDetail()
		{
			if (!_ready) return;
			var selected = Selected();
			_openFolder.Enabled = selected is not null;
			_detail.Text = selected?.Note ?? "";
		}

		private PrecompiledGame? Selected()
			=> _list.SelectedItems.Count is 0 ? null : _list.SelectedItems[0].Tag as PrecompiledGame;

		private void RemoveTicked(bool confirm = true)
		{
			var going = _items.Where(i => _ticked.Contains(i.Path)).ToList();
			if (going.Count is 0) return;

			if (confirm)
			{
				var what = going.Count is 1 ? going[0].Label : $"{going.Count} games' compiled code";
				var answer = MessageBox.Show(
					this,
					$"Remove {what}?\n\n{SizeText(going.Sum(static g => g.Bytes))} will be freed. "
						+ "Each game is compiled again the next time a project needs it.",
					"Pre-Compiled Modules",
					MessageBoxButtons.YesNo,
					MessageBoxIcon.Question);
				if (answer is not DialogResult.Yes) return;
			}

			foreach (var game in going)
			{
				try
				{
					_remove(game);
				}
				catch (Exception ex)
				{
					MessageBox.Show(this, $"{game.Label} could not be removed: {ex.Message}", "Pre-Compiled Modules",
						MessageBoxButtons.OK, MessageBoxIcon.Warning);
				}
				_ticked.Remove(game.Path);
			}
			Refresh();
		}

		private void OpenSelectedFolder()
		{
			if (Selected() is not { Path.Length: > 0 } game) return;
			try
			{
				Process.Start(new ProcessStartInfo(game.Path) { UseShellExecute = true });
			}
			catch (Exception)
			{
				// a folder that will not open is not worth a dialog of its own
			}
		}

		private static string SizeText(long bytes)
		{
			var gb = bytes / (1024.0 * 1024 * 1024);
			if (gb >= 1.0) return $"{gb:0.00} GB";
			var mb = bytes / (1024.0 * 1024);
			return mb >= 1.0 ? $"{mb:0.0} MB" : $"{bytes / 1024.0:0} KB";
		}
	}
}
