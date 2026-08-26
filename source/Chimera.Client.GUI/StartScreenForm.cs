#nullable enable

using System.Collections.Generic;
using System.Drawing;
using System.IO;
using System.Linq;
using System.Windows.Forms;

namespace Chimera.Client.GUI
{
	/// <summary>
	/// Chimera's front door. There is one use case - work on a TAS project -
	/// so this is the whole menu: make a new project, open one, or pick up a
	/// recent one (docs/project.md). No rom ever loads without a project.
	/// </summary>
	public sealed class StartScreenForm : FormBase
	{
		private readonly Func<string?> _newProject;
		private readonly Func<string?> _openProject;
		private readonly ListView _recents;
		private readonly IReadOnlyList<string> _recentPaths;

		/// <summary>the project the user chose, ready for the owner to load; null = quit</summary>
		public string? ChosenProjectPath { get; private set; }

		protected override string WindowTitleStatic => "Chimera";

		/// <param name="recentPaths">most recent first</param>
		/// <param name="newProject">runs the creation wizard; the new project's path, or null when cancelled</param>
		/// <param name="openProject">shows the open-file picker; the chosen path, or null</param>
		public StartScreenForm(
			IReadOnlyList<string> recentPaths,
			Func<string?> newProject,
			Func<string?> openProject)
		{
			_recentPaths = recentPaths;
			_newProject = newProject;
			_openProject = openProject;

			SuspendLayout();
			ClientSize = new(UIHelper.ScaleX(440), UIHelper.ScaleY(320));
			FormBorderStyle = FormBorderStyle.FixedDialog;
			MaximizeBox = false;
			MinimizeBox = false;
			StartPosition = FormStartPosition.CenterParent;
			ShowIcon = false;

			Label banner = new()
			{
				AutoSize = true,
				Font = new Font(Font.FontFamily, Font.Size * 1.6f, FontStyle.Bold),
				Location = Pt(12, 12),
				Text = "Chimera",
			};
			Label tagline = new()
			{
				AutoSize = true,
				Location = Pt(14, 44),
				Text = "A project holds the whole TAS: core, files, settings, inputs.",
			};

			Button newButton = Big("New Chimera Project...", 12, 76, () => Finish(_newProject()));
			Button openButton = Big("Open Project...", 226, 76, () => Finish(_openProject()));

			Label recentLabel = new() { AutoSize = true, Location = Pt(12, 122), Text = "Recent projects (double-click to open):" };
			_recents = new ListView
			{
				FullRowSelect = true,
				HeaderStyle = ColumnHeaderStyle.None,
				HideSelection = false,
				Location = Pt(12, 142),
				MultiSelect = false,
				Size = new(UIHelper.ScaleX(416), UIHelper.ScaleY(130)),
				View = View.Details,
			};
			_recents.Columns.Add("", UIHelper.ScaleX(410));
			foreach (var path in recentPaths)
			{
				ListViewItem item = new($"{Path.GetFileNameWithoutExtension(path)}    ({path})");
				if (!File.Exists(path)) item.ForeColor = Color.Gray;
				_recents.Items.Add(item);
			}
			_recents.DoubleClick += (_, _) => OpenSelectedRecent();

			Button quitButton = new()
			{
				AutoSize = true,
				Location = Pt(370, 284),
				Text = "Quit",
			};
			quitButton.Click += (_, _) => { DialogResult = DialogResult.Cancel; Close(); };
			CancelButton = quitButton;

			Controls.AddRange([ banner, tagline, newButton, openButton, recentLabel, _recents, quitButton ]);
			ResumeLayout();
		}

		private static Point Pt(int x, int y) => new(UIHelper.ScaleX(x), UIHelper.ScaleY(y));

		private Button Big(string text, int x, int y, Action onClick)
		{
			Button b = new()
			{
				Location = Pt(x, y),
				Size = new(UIHelper.ScaleX(202), UIHelper.ScaleY(34)),
				Text = text,
			};
			b.Click += (_, _) => onClick();
			return b;
		}

		/// <summary>the rows shown, for tests</summary>
		public string[] RecentRows => _recents.Items.OfType<ListViewItem>().Select(static i => i.Text).ToArray();

		public void OpenSelectedRecent()
		{
			if (_recents.SelectedIndices.Count is 0) return;
			var path = _recentPaths[_recents.SelectedIndices[0]];
			if (!File.Exists(path))
			{
				MessageBox.Show(this, path, "That project is not there any more", MessageBoxButtons.OK, MessageBoxIcon.Warning);
				return;
			}
			Finish(path);
		}

		private void Finish(string? path)
		{
			if (path is null) return; // cancelled inside; stay on the door
			ChosenProjectPath = path;
			DialogResult = DialogResult.OK;
			Close();
		}
	}
}
