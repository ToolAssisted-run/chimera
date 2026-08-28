#nullable enable

using System.Drawing;
using System.Linq;
using System.Windows.Forms;

using Chimera.Emulation.Common.Engine;

namespace Chimera.Client.GUI
{
	/// <summary>
	/// Where a project's files are found for THIS session. The project stores
	/// only names and hashes, never paths (docs/project.md): whatever did not
	/// resolve beside the project file is located here, each file's hash checked
	/// the moment it is provided. A differing file name on disk is fine - the
	/// hash is the identity - and a differing HASH is a per-file warning the
	/// user may knowingly override, which the project then records truthfully.
	/// </summary>
	public sealed class ProjectResolutionForm : FormBase
	{
		private readonly EngineProject _project;
		private readonly Func<string, string?> _locateFile;
		private readonly ListView _list;
		private readonly Label _detail;
		private readonly Button _openButton;
		private readonly Button _locateButton;

		protected override string WindowTitleStatic => "Locate Project Files";

		/// <param name="project">opened and auto-resolved by the caller; this form finishes the job</param>
		/// <param name="locateFile">shows a file picker titled for one file; the dialog belongs to the owner</param>
		public ProjectResolutionForm(EngineProject project, Func<string, string?> locateFile)
		{
			_project = project;
			_locateFile = locateFile;

			SuspendLayout();
			ClientSize = new(UIHelper.ScaleX(560), UIHelper.ScaleY(320));
			MinimumSize = new(UIHelper.ScaleX(440), UIHelper.ScaleY(240));
			StartPosition = FormStartPosition.CenterParent;
			ShowIcon = false;

			Label header = new()
			{
				AutoSize = true,
				Location = new(UIHelper.ScaleX(8), UIHelper.ScaleY(8)),
				Text = "The project names these files. Locate any that were not found beside it:",
			};

			_list = new ListView
			{
				Anchor = AnchorStyles.Top | AnchorStyles.Bottom | AnchorStyles.Left | AnchorStyles.Right,
				FullRowSelect = true,
				HideSelection = false,
				Location = new(UIHelper.ScaleX(8), UIHelper.ScaleY(30)),
				MultiSelect = false,
				Size = new(UIHelper.ScaleX(544), UIHelper.ScaleY(210)),
				View = View.Details,
			};
			_list.Columns.Add("File", UIHelper.ScaleX(180));
			_list.Columns.Add("Slot", UIHelper.ScaleX(80));
			_list.Columns.Add("Status", UIHelper.ScaleX(260));
			_list.SelectedIndexChanged += (_, _) => UpdateDetail();
			_list.DoubleClick += (_, _) => Locate();

			_detail = new Label
			{
				Anchor = AnchorStyles.Bottom | AnchorStyles.Left | AnchorStyles.Right,
				AutoEllipsis = true,
				Location = new(UIHelper.ScaleX(8), UIHelper.ScaleY(248)),
				Size = new(UIHelper.ScaleX(544), UIHelper.ScaleY(16)),
			};

			_locateButton = MakeButton("Locate...", 8, Locate);
			_openButton = MakeButton("Open", 380, Confirm);
			_openButton.Anchor = AnchorStyles.Bottom | AnchorStyles.Right;
			Button cancelButton = MakeButton("Cancel", 472, () => { DialogResult = DialogResult.Cancel; Close(); });
			cancelButton.Anchor = AnchorStyles.Bottom | AnchorStyles.Right;
			AcceptButton = _openButton;
			CancelButton = cancelButton;

			Controls.AddRange([ header, _list, _detail, _locateButton, _openButton, cancelButton ]);
			ResumeLayout();
			Populate();
		}

		private Button MakeButton(string text, int x, Action onClick)
		{
			Button b = new()
			{
				Anchor = AnchorStyles.Bottom | AnchorStyles.Left,
				AutoSize = true,
				Location = new(UIHelper.ScaleX(x), UIHelper.ScaleY(270)),
				Text = text,
			};
			b.Click += (_, _) => onClick();
			return b;
		}

		/// <summary>(Re)builds the rows from the project's resolution state. Public so a test can drive it.</summary>
		public void Populate()
		{
			var selected = _list.SelectedIndices.Count is 0 ? -1 : _list.SelectedIndices[0];
			_list.BeginUpdate();
			_list.Items.Clear();
			for (var i = 0; i < _project.FileCount; i++)
			{
				ListViewItem item = new(_project.FileName(i));
				item.SubItems.Add(_project.FileSlot(i));
				item.SubItems.Add(StatusText(i));
				if (_project.FileStatus(i) is 2) item.ForeColor = Color.Firebrick;
				else if (_project.FileStatus(i) is 1) item.ForeColor = Color.DarkGoldenrod;
				_list.Items.Add(item);
			}
			if (selected >= 0 && selected < _list.Items.Count) _list.Items[selected].Selected = true;
			_list.EndUpdate();
			UpdateButtons();
			UpdateDetail();
		}

		private string StatusText(int index) => _project.FileStatus(index) switch
		{
			0 => "found, hash matches",
			2 => "HASH MISMATCH (not the recorded bytes)",
			_ => "not found",
		};

		/// <summary>The Status column of every row, top to bottom, for tests.</summary>
		public string[] StatusRows
			=> Enumerable.Range(0, _project.FileCount).Select(StatusText).ToArray();

		public bool CanOpen => _openButton.Enabled;
		public string OpenButtonText => _openButton.Text;

		private void UpdateButtons()
		{
			var anyMissing = false;
			var anyMismatch = false;
			for (var i = 0; i < _project.FileCount; i++)
			{
				if (_project.FileStatus(i) is 1) anyMissing = true;
				if (_project.FileStatus(i) is 2) anyMismatch = true;
			}

			// a missing file cannot be overridden into existence; a mismatch can
			// be knowingly accepted, and the project will record what actually ran
			_openButton.Enabled = !anyMissing;
			_openButton.Text = anyMismatch ? "Open Anyway" : "Open";
		}

		private void UpdateDetail()
		{
			var index = _list.SelectedIndices.Count is 0 ? -1 : _list.SelectedIndices[0];
			_locateButton.Enabled = index >= 0;
			if (index < 0)
			{
				_detail.Text = "";
				return;
			}
			_detail.Text = _project.FileStatus(index) switch
			{
				2 => $"recorded {_project.FileSha1(index)}, provided {_project.FileActualSha1(index)}",
				0 => $"SHA1 {_project.FileSha1(index)}",
				_ => "double-click, or press Locate, to point at this file",
			};
		}

		/// <summary>Points one row at a file on disk; the hash verdict lands in the row immediately.</summary>
		public void Locate()
		{
			var index = _list.SelectedIndices.Count is 0 ? -1 : _list.SelectedIndices[0];
			if (index < 0) return;
			var path = _locateFile($"Locate {_project.FileName(index)}");
			if (path is null) return;
			try
			{
				_project.FileResolve(index, path);
			}
			catch (InvalidOperationException ex)
			{
				MessageBox.Show(this, ex.Message, "Cannot read that file", MessageBoxButtons.OK, MessageBoxIcon.Error);
			}
			Populate();
		}

		private void Confirm()
		{
			DialogResult = DialogResult.OK;
			Close();
		}
	}
}
