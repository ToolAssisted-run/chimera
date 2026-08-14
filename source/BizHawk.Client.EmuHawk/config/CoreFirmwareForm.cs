#nullable enable

using System.Collections.Generic;
using System.Drawing;
using System.IO;
using System.Linq;
using System.Windows.Forms;

using BizHawk.Client.Common;

namespace BizHawk.Client.EmuHawk
{
	/// <summary>
	/// Where the user points the frontend at the files their cores cannot ship: a
	/// disk-system BIOS, a boot rom, a character set.
	///
	/// Every row exists because a loaded core package declared it. There is no
	/// built-in table of firmware for every console ever made - the frontend does
	/// not know what cores exist, so it cannot have one. It asks each loaded
	/// package what it wants, checks that what the user chose is the right size and
	/// a known dump, and remembers where it is.
	///
	/// Like <see cref="OpenCoreForm"/> this window is deliberately thin: the
	/// rows and their verdicts come from <see cref="CoreFirmwareStore"/>, which is
	/// tested without a UI, and choosing a file is handed back to the owner.
	/// </summary>
	public sealed class CoreFirmwareForm : FormBase
	{
		private readonly ListView _list;
		private readonly Label _detail;
		private readonly Func<IReadOnlyList<CoreFirmwareEntry>> _fetch;
		private readonly Action<CoreFirmwareEntry, string?> _setPath;

		private List<CoreFirmwareEntry> _entries = new();

		protected override string WindowTitleStatic => "Firmware";

		/// <param name="fetch">re-reads the rows (called on open and after every change)</param>
		/// <param name="setPath">remembers (or, for null, forgets) the file chosen for one declaration</param>
		public CoreFirmwareForm(Func<IReadOnlyList<CoreFirmwareEntry>> fetch, Action<CoreFirmwareEntry, string?> setPath)
		{
			_fetch = fetch;
			_setPath = setPath;

			SuspendLayout();
			ClientSize = new(UIHelper.ScaleX(660), UIHelper.ScaleY(340));
			MinimumSize = new(UIHelper.ScaleX(500), UIHelper.ScaleY(240));
			StartPosition = FormStartPosition.CenterParent;
			ShowIcon = false;

			Label header = new()
			{
				AutoSize = true,
				Location = new(UIHelper.ScaleX(8), UIHelper.ScaleY(8)),
				Text = "Files the loaded cores expect you to provide. A rom that needs one will not load until it is here.",
			};

			_list = new ListView
			{
				Anchor = AnchorStyles.Top | AnchorStyles.Bottom | AnchorStyles.Left | AnchorStyles.Right,
				FullRowSelect = true,
				HideSelection = false,
				Location = new(UIHelper.ScaleX(8), UIHelper.ScaleY(30)),
				MultiSelect = false,
				Size = new(UIHelper.ScaleX(644), UIHelper.ScaleY(230)),
				View = View.Details,
			};
			_list.Columns.Add("Core", UIHelper.ScaleX(130));
			_list.Columns.Add("Firmware", UIHelper.ScaleX(200));
			_list.Columns.Add("Status", UIHelper.ScaleX(180));
			_list.Columns.Add("File", UIHelper.ScaleX(130));
			_list.SelectedIndexChanged += (_, _) => UpdateDetail();
			_list.DoubleClick += (_, _) => Browse();

			_detail = new Label
			{
				Anchor = AnchorStyles.Bottom | AnchorStyles.Left | AnchorStyles.Right,
				AutoEllipsis = true,
				Location = new(UIHelper.ScaleX(8), UIHelper.ScaleY(268)),
				Size = new(UIHelper.ScaleX(644), UIHelper.ScaleY(32)),
			};

			Button setButton = MakeButton("Set File...", 8, Browse);
			Button clearButton = MakeButton("Clear", 110, Clear);
			Button closeButton = MakeButton("Close", 580, Close);
			closeButton.Anchor = AnchorStyles.Bottom | AnchorStyles.Right;
			CancelButton = closeButton;

			Controls.AddRange([ header, _list, _detail, setButton, clearButton, closeButton ]);
			ResumeLayout();
			Populate();
		}

		private Button MakeButton(string text, int x, Action onClick)
		{
			Button b = new()
			{
				Anchor = AnchorStyles.Bottom | AnchorStyles.Left,
				AutoSize = true,
				Location = new(UIHelper.ScaleX(x), UIHelper.ScaleY(306)),
				Text = text,
			};
			b.Click += (_, _) => onClick();
			return b;
		}

		/// <summary>(Re)builds the rows from the current config. Public so a test can drive it.</summary>
		public void Populate()
		{
			var selected = SelectedEntry;
			var selectedKey = selected is null ? null : CoreFirmwareStore.KeyFor(selected.CoreName, selected.Decl.Id);
			_entries = _fetch().ToList();
			_list.BeginUpdate();
			_list.Items.Clear();
			foreach (var entry in _entries)
			{
				ListViewItem item = new(entry.CoreName);
				item.SubItems.Add(entry.Decl.DisplayName);
				item.SubItems.Add(entry.StatusText);
				item.SubItems.Add(entry.Path is null ? "" : Path.GetFileName(entry.Path));
				item.ForeColor = entry.State switch
				{
					CoreFirmwareState.Good => Color.DarkGreen,
					CoreFirmwareState.Unrecognised => Color.DarkGoldenrod,
					CoreFirmwareState.Missing when !entry.Decl.Required => SystemColors.GrayText,
					CoreFirmwareState.Missing or CoreFirmwareState.Unreadable or CoreFirmwareState.WrongSize => Color.Firebrick,
					_ => SystemColors.ControlText,
				};
				if (CoreFirmwareStore.KeyFor(entry.CoreName, entry.Decl.Id) == selectedKey) item.Selected = true;
				_list.Items.Add(item);
			}
			if (_list.SelectedIndices.Count is 0 && _list.Items.Count is not 0) _list.Items[0].Selected = true;
			_list.EndUpdate();
			UpdateDetail();
		}

		/// <summary>The rows as displayed, top to bottom. For tests and for the detail line.</summary>
		public IReadOnlyList<CoreFirmwareEntry> Entries => _entries;

		public CoreFirmwareEntry? SelectedEntry
			=> _list.SelectedIndices.Count is 0 ? null : _entries[_list.SelectedIndices[0]];

		private void UpdateDetail()
		{
			var entry = SelectedEntry;
			if (entry is null)
			{
				_detail.Text = "No loaded core expects any firmware.";
				return;
			}
			// what the file is comes from the core; where it is (and what it hashed to)
			// is the part the user needs to see when a row is not what they expected
			var what = entry.Decl.Description ?? $"{entry.Decl.DisplayName}, {entry.Decl.Size} bytes";
			_detail.Text = entry.Path is null ? what : $"{what}\n{entry.Path}{(entry.Sha1 is null ? "" : $"  |  SHA1 {entry.Sha1}")}";
		}

		private void Browse()
		{
			var entry = SelectedEntry;
			if (entry is null) return;
			using OpenFileDialog ofd = new()
			{
				Title = $"{entry.CoreName}: {entry.Decl.DisplayName}",
				Filter = "All Files|*.*",
				FileName = entry.Path,
				InitialDirectory = entry.Path is null ? "" : Path.GetDirectoryName(entry.Path) ?? "",
			};
			if (ofd.ShowDialog(this) is not DialogResult.OK) return;
			_setPath(entry, ofd.FileName);
			Populate();
		}

		private void Clear()
		{
			var entry = SelectedEntry;
			if (entry is null) return;
			_setPath(entry, null);
			Populate();
		}
	}
}
