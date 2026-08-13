#nullable enable

using System.Collections.Generic;
using System.Diagnostics;
using System.Drawing;
using System.IO;
using System.Linq;
using System.Windows.Forms;

using BizHawk.Client.Common;

namespace BizHawk.Client.EmuHawk
{
	/// <summary>
	/// Lists the core packages this frontend found and what it did with each one.
	///
	/// The window is deliberately thin: every decision it displays - which packages
	/// exist, whether one is loaded, pending, disabled or broken - is computed by
	/// <see cref="CorePackageList"/> and tested without a UI. What is left here is
	/// rendering rows and forwarding four buttons, which is the part that has to be
	/// looked at rather than asserted.
	/// </summary>
	public sealed class CorePackagesForm : FormBase
	{
		private readonly ListView _list;
		private readonly Label _detail;
		private readonly Label _restartNote;
		private readonly Func<IReadOnlyList<CorePackageListEntry>> _fetch;
		private readonly Action _rescan;
		private readonly Action _addPackage;
		private readonly IReadOnlyList<string> _searchPaths;

		/// <summary>set while rows are being (re)built, so programmatic checks don't fire the user's handler</summary>
		private bool _populating;

		private List<CorePackageListEntry> _entries = new();

		protected override string WindowTitleStatic => "Core Packages";

		/// <param name="fetch">re-reads the current list (called on open and after every rescan)</param>
		/// <param name="rescan">rescans the search directories and loads anything newly enabled</param>
		/// <param name="addPackage">prompts for a package anywhere on disk and loads it (the file dialog belongs to the owner, not here)</param>
		public CorePackagesForm(
			Config config,
			Func<IReadOnlyList<CorePackageListEntry>> fetch,
			Action rescan,
			Action addPackage,
			IReadOnlyList<string> searchPaths)
		{
			Config = config;
			_fetch = fetch;
			_rescan = rescan;
			_addPackage = addPackage;
			_searchPaths = searchPaths;

			SuspendLayout();
			ClientSize = new(UIHelper.ScaleX(640), UIHelper.ScaleY(380));
			MinimumSize = new(UIHelper.ScaleX(480), UIHelper.ScaleY(280));
			StartPosition = FormStartPosition.CenterParent;
			ShowIcon = false;

			Label header = new()
			{
				AutoSize = true,
				Location = new(UIHelper.ScaleX(8), UIHelper.ScaleY(8)),
				Text = $"Packages are loaded at startup from: {string.Join("; ", searchPaths)}",
			};

			_list = new ListView
			{
				Anchor = AnchorStyles.Top | AnchorStyles.Bottom | AnchorStyles.Left | AnchorStyles.Right,
				CheckBoxes = true,
				FullRowSelect = true,
				HideSelection = false,
				Location = new(UIHelper.ScaleX(8), UIHelper.ScaleY(30)),
				MultiSelect = false,
				Size = new(UIHelper.ScaleX(624), UIHelper.ScaleY(250)),
				View = View.Details,
			};
			_list.Columns.Add("Core", UIHelper.ScaleX(140));
			_list.Columns.Add("System", UIHelper.ScaleX(60));
			_list.Columns.Add("Status", UIHelper.ScaleX(210));
			_list.Columns.Add("Package", UIHelper.ScaleX(130));
			_list.Columns.Add("SHA1", UIHelper.ScaleX(70));
			_list.ItemChecked += (_, e) =>
			{
				if (_populating) return;
				SetEnabled(e.Item.Index, e.Item.Checked);
			};
			_list.SelectedIndexChanged += (_, _) => UpdateDetail();

			_detail = new Label
			{
				Anchor = AnchorStyles.Bottom | AnchorStyles.Left | AnchorStyles.Right,
				AutoEllipsis = true,
				Location = new(UIHelper.ScaleX(8), UIHelper.ScaleY(288)),
				Size = new(UIHelper.ScaleX(624), UIHelper.ScaleY(16)),
			};
			_restartNote = new Label
			{
				Anchor = AnchorStyles.Bottom | AnchorStyles.Left | AnchorStyles.Right,
				AutoEllipsis = true,
				ForeColor = Color.FromArgb(0xB0, 0x60, 0x00),
				Location = new(UIHelper.ScaleX(8), UIHelper.ScaleY(306)),
				Size = new(UIHelper.ScaleX(624), UIHelper.ScaleY(16)),
			};

			Button addButton = MakeButton("Add Package...", 8, AddPackage);
			Button folderButton = MakeButton("Open Cores Folder", 120, OpenCoresFolder);
			Button rescanButton = MakeButton("Rescan", 260, Rescan);
			Button closeButton = MakeButton("Close", 552, Close);
			closeButton.Anchor = AnchorStyles.Bottom | AnchorStyles.Right;
			CancelButton = closeButton;

			Controls.AddRange([ header, _list, _detail, _restartNote, addButton, folderButton, rescanButton, closeButton ]);
			ResumeLayout();
			Populate();
		}

		private Button MakeButton(string text, int x, Action onClick)
		{
			Button b = new()
			{
				Anchor = AnchorStyles.Bottom | AnchorStyles.Left,
				AutoSize = true,
				Location = new(UIHelper.ScaleX(x), UIHelper.ScaleY(328)),
				Text = text,
			};
			b.Click += (_, _) => onClick();
			return b;
		}

		/// <summary>(Re)builds the rows from the current list. Public so a test can drive it after changing state.</summary>
		public void Populate()
		{
			var selectedPath = SelectedEntry?.Package.Path;
			_entries = _fetch().ToList();
			_populating = true;
			_list.BeginUpdate();
			_list.Items.Clear();
			foreach (var entry in _entries)
			{
				var pkg = entry.Package;
				ListViewItem item = new(pkg.Name) { Checked = entry.Enabled };
				item.SubItems.Add(string.Join(", ", pkg.Systems));
				item.SubItems.Add(entry.StatusText);
				item.SubItems.Add(pkg.IsDirectoryForm ? $"{Path.GetFileName(pkg.Path)} (folder)" : Path.GetFileName(pkg.Path));
				item.SubItems.Add(pkg.ShortSha1);
				if (entry.State is CorePackageState.Failed) item.ForeColor = Color.Firebrick;
				else if (!entry.Enabled) item.ForeColor = SystemColors.GrayText;
				if (pkg.Path == selectedPath) item.Selected = true;
				_list.Items.Add(item);
			}
			_list.EndUpdate();
			_populating = false;
			UpdateDetail();
			UpdateRestartNote();
		}

		/// <summary>The rows as displayed, top to bottom. For tests and for the detail line.</summary>
		public IReadOnlyList<CorePackageListEntry> Entries => _entries;

		public CorePackageListEntry? SelectedEntry
			=> _list.SelectedIndices.Count is 0 ? null : _entries[_list.SelectedIndices[0]];

		/// <summary>Switches a package on or off for the next launch. Public so a test can toggle without clicking.</summary>
		public void SetEnabled(int index, bool enabled)
		{
			if (index < 0 || index >= _entries.Count) return;
			var entry = _entries[index];
			if (entry.State is CorePackageState.Failed)
			{
				// nothing to enable: it could not be read. Undo the check so the row
				// does not claim a state the frontend cannot honour.
				_populating = true;
				_list.Items[index].Checked = false;
				_populating = false;
				return;
			}
			CorePackageDiscovery.SetEnabled(Config!, entry.Package, enabled);
			Populate();
		}

		private void UpdateDetail()
		{
			var entry = SelectedEntry;
			if (entry is null) { _detail.Text = ""; return; }
			var pkg = entry.Package;
			// the Status column truncates a long message; the selected row's full
			// reason belongs somewhere the user can actually read it
			var tail = entry.Error is not null
				? $"  |  {entry.Error}"
				: pkg.Extensions.Count is 0 ? "" : $"  |  {string.Join(" ", pkg.Extensions.Keys.OrderBy(static e => e))}";
			_detail.Text = $"{pkg.Path}{tail}";
		}

		private void UpdateRestartNote()
			=> _restartNote.Text = _entries.Any(static e => e.NeedsRestart)
				? "Changes marked \"restart\" take effect next launch - cores cannot be unloaded from a running session."
				: "";

		private void Rescan()
		{
			_rescan();
			Populate();
		}

		private void AddPackage()
		{
			_addPackage();
			Populate();
		}

		private void OpenCoresFolder()
		{
			var dir = _searchPaths.FirstOrDefault();
			if (dir is null) return;
			Directory.CreateDirectory(dir); // the folder is allowed not to exist until someone asks for it
			Process.Start(new ProcessStartInfo { FileName = dir, UseShellExecute = true });
		}
	}
}
