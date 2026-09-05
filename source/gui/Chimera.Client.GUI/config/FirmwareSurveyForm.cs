#nullable enable

using System.Collections.Generic;
using System.Drawing;
using System.IO;
using System.Linq;
using System.Windows.Forms;

using Chimera.Client.Common;

namespace Chimera.Client.GUI
{
	/// <summary>
	/// Config > Firmware: every installed core asked what it needs, and what the
	/// person has for each - before any project, before any core is loaded.
	///
	/// Just in time, and nothing kept. The rows are built from the packages in the
	/// Cores folder when the window opens (<see cref="FirmwareSurvey"/>) and go away
	/// with it; the frontend has no list of firmware of its own and cannot, since it
	/// does not know what cores exist. Delete a core package and its rows are gone
	/// on the next open; what was chosen for it stays in the config for the day it
	/// is put back. What the person points at is remembered where it lives, never
	/// copied: firmware can be a
	/// PlayStation 3 update or an Xbox disk image, and a second copy is a second
	/// thing to drift.
	///
	/// Like <see cref="CoreFirmwareForm"/>, which shows what the RUNNING machine
	/// is using, this window is thin: the rows and their verdicts come from the
	/// model, which is tested without a UI, and choosing a file is handed to the
	/// owner.
	/// </summary>
	public sealed class FirmwareSurveyForm : FormBase
	{
		/// <summary>0 on hand and right, 1 on hand but not a dump the core knows, 2 gone or unusable, 3 nothing.</summary>
		private static readonly ImageList _marks = BuildMarks();

		/// <summary>An id declared more often than this shows only the dumps on hand until asked for all.</summary>
		public const int CollapseAbove = 8;

		private readonly ListView _list;
		private readonly Label _detail;
		private readonly Label _header;
		private readonly CheckBox _showAll;
		private readonly Button _setButton;
		private readonly Button _clearButton;
		private readonly Func<IReadOnlyList<FirmwareSurveyGroup>> _survey;
		private readonly Action<FirmwareSurveyRow, string?> _remember;
		private readonly Func<string, string?> _pickFile;
		private readonly Func<string?>? _pickFolder;
		private readonly Action<string>? _scanFolder;

		private List<FirmwareSurveyGroup> _groups = new();
		private List<FirmwareSurveyRow?> _rowOf = new(); // list index -> row, null for a "more" line

		protected override string WindowTitleStatic => "Firmware";

		/// <param name="survey">builds the rows from the packages present now (called on open and after every change)</param>
		/// <param name="remember">remembers (or, for null, forgets) the file chosen for one declaration</param>
		/// <param name="pickFile">shows a file picker titled for one row; the dialog belongs to the owner</param>
		/// <param name="pickFolder">shows a folder picker for Scan Folder; null hides the button</param>
		/// <param name="scanFolder">hashes a folder and remembers what it matched</param>
		public FirmwareSurveyForm(
			Func<IReadOnlyList<FirmwareSurveyGroup>> survey,
			Action<FirmwareSurveyRow, string?> remember,
			Func<string, string?> pickFile,
			Func<string?>? pickFolder = null,
			Action<string>? scanFolder = null)
		{
			_survey = survey;
			_remember = remember;
			_pickFile = pickFile;
			_pickFolder = pickFolder;
			_scanFolder = scanFolder;

			SuspendLayout();
			ClientSize = new(UIHelper.ScaleX(860), UIHelper.ScaleY(440));
			MinimumSize = new(UIHelper.ScaleX(640), UIHelper.ScaleY(300));
			StartPosition = FormStartPosition.CenterParent;
			ShowIcon = false;

			_header = new Label
			{
				AutoSize = false,
				Anchor = AnchorStyles.Top | AnchorStyles.Left | AnchorStyles.Right,
				Location = new(UIHelper.ScaleX(8), UIHelper.ScaleY(8)),
				Size = new(UIHelper.ScaleX(844), UIHelper.ScaleY(32)),
				Text = "Files your installed cores expect you to provide. A green row was found in the Firmware folder or where you last chose it; a project that needs it opens without asking.",
			};

			_list = new ListView
			{
				Anchor = AnchorStyles.Top | AnchorStyles.Bottom | AnchorStyles.Left | AnchorStyles.Right,
				FullRowSelect = true,
				HideSelection = false,
				Location = new(UIHelper.ScaleX(8), UIHelper.ScaleY(44)),
				MultiSelect = false,
				ShowGroups = true,
				Size = new(UIHelper.ScaleX(844), UIHelper.ScaleY(300)),
				SmallImageList = _marks,
				View = View.Details,
			};
			_list.Columns.Add("", UIHelper.ScaleX(26));
			_list.Columns.Add("Firmware", UIHelper.ScaleX(200));
			_list.Columns.Add("Release", UIHelper.ScaleX(150));
			_list.Columns.Add("Expected SHA1", UIHelper.ScaleX(95));
			_list.Columns.Add("Found", UIHelper.ScaleX(150));
			_list.Columns.Add("Where", UIHelper.ScaleX(110));
			_list.Columns.Add("Status", UIHelper.ScaleX(105));
			_list.SelectedIndexChanged += (_, _) => UpdateDetail();
			_list.DoubleClick += (_, _) => Browse();

			_detail = new Label
			{
				Anchor = AnchorStyles.Bottom | AnchorStyles.Left | AnchorStyles.Right,
				AutoEllipsis = true,
				Location = new(UIHelper.ScaleX(8), UIHelper.ScaleY(352)),
				Size = new(UIHelper.ScaleX(844), UIHelper.ScaleY(46)),
			};

			_setButton = MakeButton("Set File...", 8, Browse);
			_clearButton = MakeButton("Clear", 110, Clear);
			Button scanButton = MakeButton("Scan Folder...", 190, ScanFolder);
			scanButton.Visible = _pickFolder is not null && _scanFolder is not null;
			Button rescanButton = MakeButton("Rescan", 300, Populate);
			_showAll = new CheckBox
			{
				Anchor = AnchorStyles.Bottom | AnchorStyles.Left,
				AutoSize = true,
				Location = new(UIHelper.ScaleX(390), UIHelper.ScaleY(412)),
				Text = "Show every release",
			};
			_showAll.CheckedChanged += (_, _) => Render();
			Button closeButton = MakeButton("Close", 780, Close);
			closeButton.Anchor = AnchorStyles.Bottom | AnchorStyles.Right;
			CancelButton = closeButton;

			Controls.AddRange([ _header, _list, _detail, _setButton, _clearButton, scanButton, rescanButton, _showAll, closeButton ]);
			ResumeLayout();
			Populate();
		}

		private static ImageList BuildMarks()
		{
			ImageList list = new() { ImageSize = new(16, 16), ColorDepth = ColorDepth.Depth32Bit };
			Bitmap Draw(Action<Graphics> paint)
			{
				Bitmap bmp = new(16, 16);
				using Graphics g = Graphics.FromImage(bmp);
				g.SmoothingMode = System.Drawing.Drawing2D.SmoothingMode.AntiAlias;
				paint(g);
				return bmp;
			}
			list.Images.Add(Draw(g =>
			{
				using Pen pen = new(Color.FromArgb(0, 140, 0), 2.4f);
				g.DrawLines(pen, new[] { new Point(3, 8), new Point(6, 12), new Point(13, 4) });
			}));
			list.Images.Add(Draw(g =>
			{
				using SolidBrush fill = new(Color.FromArgb(240, 173, 40));
				g.FillPolygon(fill, new[] { new Point(8, 1), new Point(15, 14), new Point(1, 14) });
				using SolidBrush ink = new(Color.FromArgb(60, 40, 0));
				g.FillRectangle(ink, 7, 5, 2, 5);
				g.FillRectangle(ink, 7, 11, 2, 2);
			}));
			list.Images.Add(Draw(g =>
			{
				using Pen pen = new(Color.FromArgb(190, 40, 40), 2.4f);
				g.DrawLine(pen, 4, 4, 12, 12);
				g.DrawLine(pen, 12, 4, 4, 12);
			}));
			list.Images.Add(Draw(g =>
			{
				using Pen pen = new(Color.FromArgb(150, 150, 150), 1.4f);
				g.DrawEllipse(pen, 3, 3, 10, 10);
			}));
			return list;
		}

		private static int MarkFor(CoreFirmwareState state) => state switch
		{
			CoreFirmwareState.Good => 0,
			CoreFirmwareState.Unrecognised or CoreFirmwareState.Custom => 1,
			CoreFirmwareState.Unreadable => 2,
			_ => 3,
		};

		private Button MakeButton(string text, int x, Action onClick)
		{
			Button b = new()
			{
				Anchor = AnchorStyles.Bottom | AnchorStyles.Left,
				AutoSize = true,
				Location = new(UIHelper.ScaleX(x), UIHelper.ScaleY(408)),
				Text = text,
			};
			b.Click += (_, _) => onClick();
			return b;
		}

		/// <summary>Surveys the packages again and redraws. Public so a test can drive it.</summary>
		public void Populate()
		{
			_groups = _survey().ToList();
			Render();
		}

		/// <summary>The groups as surveyed, for tests.</summary>
		public IReadOnlyList<FirmwareSurveyGroup> Groups => _groups;

		private void Render()
		{
			var selected = SelectedRow;
			_list.BeginUpdate();
			_list.Items.Clear();
			_list.Groups.Clear();
			_rowOf.Clear();
			foreach (var group in _groups)
			{
				ListViewGroup lvg = new($"{group.CoreName}  ·  {group.Summary}");
				_list.Groups.Add(lvg);
				if (group.Rows.Count is 0)
				{
					ListViewItem none = new("", 3) { Group = lvg, ForeColor = SystemColors.GrayText };
					none.SubItems.Add("(needs no firmware)");
					_list.Items.Add(none);
					_rowOf.Add(null);
					continue;
				}
				foreach (var byId in group.Rows.GroupBy(static r => r.Decl.Id))
				{
					var rows = byId.ToList();
					var collapse = !_showAll.Checked && rows.Count > CollapseAbove;
					var hidden = 0;
					foreach (var row in rows)
					{
						if (collapse && !row.OnHand && row.State is not CoreFirmwareState.Unreadable)
						{
							hidden++;
							continue;
						}
						ListViewItem item = new("", MarkFor(row.State)) { Group = lvg };
						item.SubItems.Add(row.Decl.DisplayName);
						item.SubItems.Add(row.Decl.Label ?? (row.Condition.Length is 0 ? "" : $"when {row.Condition}"));
						item.SubItems.Add(string.IsNullOrEmpty(row.Decl.Sha1) ? "(any)" : CoreFirmwareEntry.Short(row.Decl.Sha1));
						item.SubItems.Add(row.Path is null ? "" : Path.GetFileName(row.Path));
						item.SubItems.Add(row.Where is FirmwareWhere.Chosen ? "chosen" : row.WhereText);
						item.SubItems.Add(row.StatusText);
						item.ForeColor = row.State switch
						{
							CoreFirmwareState.Good => Color.DarkGreen,
							CoreFirmwareState.Unrecognised or CoreFirmwareState.Custom => Color.DarkGoldenrod,
							CoreFirmwareState.Unreadable => Color.Firebrick,
							_ => SystemColors.GrayText,
						};
						if (ReferenceEquals(selected, row) || (selected is not null && SameRow(selected, row))) item.Selected = true;
						_list.Items.Add(item);
						_rowOf.Add(row);
					}
					if (hidden > 0)
					{
						ListViewItem more = new("", 3) { Group = lvg, ForeColor = SystemColors.GrayText };
						more.SubItems.Add($"… {hidden} more releases of {rows[0].Decl.DisplayName} not on hand; tick \"Show every release\" to see them");
						_list.Items.Add(more);
						_rowOf.Add(null);
					}
				}
			}
			_list.EndUpdate();
			UpdateDetail();
		}

		private static bool SameRow(FirmwareSurveyRow a, FirmwareSurveyRow b)
			=> a.CoreName == b.CoreName && a.Decl.Id == b.Decl.Id && string.Equals(a.Decl.Sha1, b.Decl.Sha1, StringComparison.OrdinalIgnoreCase)
				&& a.Decl.Label == b.Decl.Label;

		public FirmwareSurveyRow? SelectedRow
			=> _list.SelectedIndices.Count is 0 || _list.SelectedIndices[0] >= _rowOf.Count ? null : _rowOf[_list.SelectedIndices[0]];

		/// <summary>The rows as displayed, top to bottom, for tests ("more" lines excluded).</summary>
		public IReadOnlyList<FirmwareSurveyRow> DisplayedRows => _rowOf.Where(static r => r is not null).Select(static r => r!).ToList();

		private void UpdateDetail()
		{
			var row = SelectedRow;
			_setButton.Enabled = row is not null;
			_clearButton.Enabled = row is not null && row.Where is FirmwareWhere.Chosen;
			if (row is null)
			{
				_detail.Text = _groups.Count is 0 ? "No core package is installed." : "";
				return;
			}
			var what = row.Decl.Description ?? $"{row.Decl.DisplayName}, {row.Decl.Size} bytes";
			var expected = string.IsNullOrEmpty(row.Decl.Sha1)
				? (string.IsNullOrEmpty(row.Decl.Name) ? "any file" : $"a file called {row.Decl.Name}, any bytes")
				: $"expects {row.Decl.Sha1!.ToUpperInvariant()}";
			var when = row.Condition.Length is 0 ? "" : $" · needed when {row.Condition}";
			var actual = row.Path is null ? "nothing yet"
				: row.Sha1 is null ? $"{row.Path} could not be read"
				: $"{row.Path} is {row.Sha1}";
			_detail.Text = $"{what}\n{expected}{when}\n{actual}";
		}

		private void Browse()
		{
			var row = SelectedRow;
			if (row is null) return;
			var path = _pickFile($"{row.CoreName}: {row.Decl.DisplayName}{(row.Decl.Label is null ? "" : $" - {row.Decl.Label}")}");
			if (path is null) return;
			_remember(row, path);
			Populate();
		}

		private void Clear()
		{
			var row = SelectedRow;
			if (row is null) return;
			_remember(row, null);
			Populate();
		}

		private void ScanFolder()
		{
			var folder = _pickFolder?.Invoke();
			if (folder is null || _scanFolder is null) return;
			UseWaitCursor = true;
			try
			{
				_scanFolder(folder);
			}
			finally
			{
				UseWaitCursor = false;
			}
			Populate();
		}
	}
}
