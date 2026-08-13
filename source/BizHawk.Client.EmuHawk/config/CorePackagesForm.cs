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
	/// Lists the core packages this frontend found and what became of each one.
	///
	/// It is a report, not a control panel: everything readable in a search
	/// directory is loaded, so there is nothing here to switch. What the window is
	/// for is answering "is my core actually being picked up, and if not, why not" —
	/// which before this existed meant reading the console, if you had one.
	///
	/// The window is deliberately thin: the rows are computed by
	/// <see cref="CorePackageList"/> and tested without a UI. What is left here is
	/// rendering them and forwarding three buttons, which is the part that has to
	/// be looked at rather than asserted.
	/// </summary>
	public sealed class CorePackagesForm : FormBase
	{
		private readonly ListView _list;
		private readonly Label _detail;
		private readonly Func<IReadOnlyList<CorePackageListEntry>> _fetch;
		private readonly Action _rescan;
		private readonly Action _addPackage;
		private readonly IReadOnlyList<string> _searchPaths;

		private List<CorePackageListEntry> _entries = new();

		protected override string WindowTitleStatic => "Core Packages";

		/// <param name="fetch">re-reads the current list (called on open and after every rescan)</param>
		/// <param name="rescan">rescans the search directories and loads anything newly present</param>
		/// <param name="addPackage">prompts for a package anywhere on disk and loads it (the file dialog belongs to the owner, not here)</param>
		public CorePackagesForm(
			Func<IReadOnlyList<CorePackageListEntry>> fetch,
			Action rescan,
			Action addPackage,
			IReadOnlyList<string> searchPaths)
		{
			_fetch = fetch;
			_rescan = rescan;
			_addPackage = addPackage;
			_searchPaths = searchPaths;

			SuspendLayout();
			ClientSize = new(UIHelper.ScaleX(640), UIHelper.ScaleY(360));
			MinimumSize = new(UIHelper.ScaleX(480), UIHelper.ScaleY(260));
			StartPosition = FormStartPosition.CenterParent;
			ShowIcon = false;

			Label header = new()
			{
				AutoSize = true,
				Location = new(UIHelper.ScaleX(8), UIHelper.ScaleY(8)),
				Text = $"Loaded at startup from: {string.Join("; ", searchPaths)}",
			};

			_list = new ListView
			{
				Anchor = AnchorStyles.Top | AnchorStyles.Bottom | AnchorStyles.Left | AnchorStyles.Right,
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
			_list.SelectedIndexChanged += (_, _) => UpdateDetail();

			_detail = new Label
			{
				Anchor = AnchorStyles.Bottom | AnchorStyles.Left | AnchorStyles.Right,
				AutoEllipsis = true,
				Location = new(UIHelper.ScaleX(8), UIHelper.ScaleY(288)),
				Size = new(UIHelper.ScaleX(624), UIHelper.ScaleY(16)),
			};

			Button addButton = MakeButton("Add Package...", 8, AddPackage);
			Button folderButton = MakeButton("Open Cores Folder", 120, OpenCoresFolder);
			Button rescanButton = MakeButton("Rescan", 260, Rescan);
			Button closeButton = MakeButton("Close", 552, Close);
			closeButton.Anchor = AnchorStyles.Bottom | AnchorStyles.Right;
			CancelButton = closeButton;

			Controls.AddRange([ header, _list, _detail, addButton, folderButton, rescanButton, closeButton ]);
			ResumeLayout();
			Populate();
		}

		private Button MakeButton(string text, int x, Action onClick)
		{
			Button b = new()
			{
				Anchor = AnchorStyles.Bottom | AnchorStyles.Left,
				AutoSize = true,
				Location = new(UIHelper.ScaleX(x), UIHelper.ScaleY(310)),
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
			_list.BeginUpdate();
			_list.Items.Clear();
			foreach (var entry in _entries)
			{
				var pkg = entry.Package;
				ListViewItem item = new(pkg.Name);
				item.SubItems.Add(string.Join(", ", pkg.Systems));
				item.SubItems.Add(entry.StatusText);
				item.SubItems.Add(pkg.IsDirectoryForm ? $"{Path.GetFileName(pkg.Path)} (folder)" : Path.GetFileName(pkg.Path));
				item.SubItems.Add(pkg.ShortSha1);
				if (entry.State is CorePackageState.Failed) item.ForeColor = Color.Firebrick;
				if (pkg.Path == selectedPath) item.Selected = true;
				_list.Items.Add(item);
			}
			_list.EndUpdate();
			UpdateDetail();
		}

		/// <summary>The rows as displayed, top to bottom. For tests and for the detail line.</summary>
		public IReadOnlyList<CorePackageListEntry> Entries => _entries;

		public CorePackageListEntry? SelectedEntry
			=> _list.SelectedIndices.Count is 0 ? null : _entries[_list.SelectedIndices[0]];

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
