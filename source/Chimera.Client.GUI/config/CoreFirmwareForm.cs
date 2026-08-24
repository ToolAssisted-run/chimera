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
		/// <summary>0 satisfied, 1 usable but not a dump the core knows, 2 unusable, 3 nothing provided.</summary>
		private static readonly ImageList _marks = BuildMarks();

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
			ClientSize = new(UIHelper.ScaleX(760), UIHelper.ScaleY(360));
			MinimumSize = new(UIHelper.ScaleX(560), UIHelper.ScaleY(260));
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
				Size = new(UIHelper.ScaleX(744), UIHelper.ScaleY(245)),
				SmallImageList = _marks,
				View = View.Details,
			};
			// The mark is the whole answer at a glance: is this one satisfied, is it wrong,
			// or has nobody given it anything yet. The hashes underneath are the evidence.
			_list.Columns.Add("", UIHelper.ScaleX(26));
			_list.Columns.Add("Core", UIHelper.ScaleX(115));
			_list.Columns.Add("Firmware", UIHelper.ScaleX(175));
			_list.Columns.Add("Expected SHA1", UIHelper.ScaleX(95));
			_list.Columns.Add("Actual SHA1", UIHelper.ScaleX(95));
			_list.Columns.Add("Status", UIHelper.ScaleX(140));
			_list.Columns.Add("File", UIHelper.ScaleX(95));
			_list.SelectedIndexChanged += (_, _) => UpdateDetail();
			_list.DoubleClick += (_, _) => Browse();

			_detail = new Label
			{
				Anchor = AnchorStyles.Bottom | AnchorStyles.Left | AnchorStyles.Right,
				AutoEllipsis = true,
				Location = new(UIHelper.ScaleX(8), UIHelper.ScaleY(283)),
				Size = new(UIHelper.ScaleX(744), UIHelper.ScaleY(46)),
			};

			Button setButton = MakeButton("Set File...", 8, Browse);
			Button clearButton = MakeButton("Clear", 110, Clear);
			Button closeButton = MakeButton("Close", 680, Close);
			closeButton.Anchor = AnchorStyles.Bottom | AnchorStyles.Right;
			CancelButton = closeButton;

			Controls.AddRange([ header, _list, _detail, setButton, clearButton, closeButton ]);
			ResumeLayout();
			Populate();
		}

		/// <summary>
		/// The four marks, drawn rather than shipped as files: a tick, a warning triangle,
		/// a cross, and an empty circle for "nobody has given this one anything".
		/// </summary>
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
			CoreFirmwareState.Unrecognised => 1,
			CoreFirmwareState.WrongSize or CoreFirmwareState.Unreadable => 2,
			_ => 3,
		};

		private Button MakeButton(string text, int x, Action onClick)
		{
			Button b = new()
			{
				Anchor = AnchorStyles.Bottom | AnchorStyles.Left,
				AutoSize = true,
				Location = new(UIHelper.ScaleX(x), UIHelper.ScaleY(330)),
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
				ListViewItem item = new("", MarkFor(entry.State));
				item.SubItems.Add(entry.CoreName);
				item.SubItems.Add(entry.Decl.DisplayName);
				item.SubItems.Add(entry.ExpectedSha1.Count is 0 ? "(any)" : CoreFirmwareEntry.Short(entry.ExpectedSha1[0]));
				item.SubItems.Add(CoreFirmwareEntry.Short(entry.Sha1));
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
			// The full hashes live here rather than in the columns: comparing them is the one
			// thing a person actually does in this window, and eight characters is enough to
			// spot a difference but not enough to be sure of a match.
			var what = entry.Decl.Description ?? $"{entry.Decl.DisplayName}, {entry.Decl.Size} bytes";
			var expected = entry.ExpectedSha1.Count is 0
				? "expects any file of the right size"
				: $"expects {string.Join("  or  ", entry.ExpectedSha1.Select(static h => h.ToUpperInvariant()))}";
			var actual = entry.Sha1 is null
				? (entry.Path is null ? "nothing provided yet" : $"{entry.Path} could not be read")
				: $"yours is {entry.Sha1}";
			_detail.Text = $"{what}\n{expected}\n{actual}";
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
