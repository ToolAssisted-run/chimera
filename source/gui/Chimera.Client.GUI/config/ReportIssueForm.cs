#nullable enable

using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Windows.Forms;

using Chimera.Client.Common;
using Chimera.Common;

namespace Chimera.Client.GUI
{
	/// <summary>
	/// Help &gt; Report an Issue (#243): the address of the report form, the text
	/// that fills in its first lines, and the files it asks for, which can be
	/// saved together as one zip. The window sends nothing. The link is handed
	/// to the system's browser, and the person writes and files the report there.
	/// </summary>
	public sealed class ReportIssueForm : FormBase
	{
		private const string PictureRow = "A picture of the game as it is now (" + IssueReport.PictureEntry + ")";

		private readonly TextBox _link;
		private readonly TextBox _report;
		private readonly CheckedListBox _files;
		private readonly Label _status;
		private readonly byte[]? _gamePicturePng;
		private readonly IReadOnlyList<string> _paths;

		protected override string WindowTitleStatic => "Report an Issue";

		/// <param name="reportText">what <see cref="IssueReport.Text"/> made of this session</param>
		/// <param name="filesToAttach">the files worth attaching that exist on this computer</param>
		/// <param name="gamePicturePng">the game's picture as a PNG, or null when no game is running</param>
		public ReportIssueForm(string reportText, IReadOnlyList<string> filesToAttach, byte[]? gamePicturePng = null)
		{
			_paths = filesToAttach;
			_gamePicturePng = gamePicturePng is { Length: > 0 } ? gamePicturePng : null;
			SuspendLayout();
			ClientSize = new(UIHelper.ScaleX(680), UIHelper.ScaleY(560));
			MinimumSize = new(UIHelper.ScaleX(520), UIHelper.ScaleY(460));
			StartPosition = FormStartPosition.CenterParent;
			MinimizeBox = false;
			var margin = UIHelper.ScaleX(10);
			var buttonWidth = UIHelper.ScaleX(120);
			var buttonHeight = UIHelper.ScaleY(26);
			var width = ClientSize.Width - (2 * margin);
			var y = UIHelper.ScaleY(10);

			Label linkLabel = Caption("1. Open the report form. It asks you to sign in to GitHub.", margin, y, width);
			y += UIHelper.ScaleY(22);
			_link = new TextBox
			{
				Anchor = AnchorStyles.Top | AnchorStyles.Left | AnchorStyles.Right,
				Location = new(margin, y + UIHelper.ScaleY(2)),
				ReadOnly = true,
				TabStop = false,
				Text = IssueReport.FormUrl,
				Width = width - (2 * (buttonWidth + margin)),
			};
			Button open = TopRightButton("Open in Browser", ClientSize.Width - margin - (2 * buttonWidth) - margin, y, buttonWidth, buttonHeight);
			open.Click += (_, _) => Util.OpenUrlExternal(IssueReport.FormUrl);
			Button copyLink = TopRightButton("Copy Link", ClientSize.Width - margin - buttonWidth, y, buttonWidth, buttonHeight);
			copyLink.Click += (_, _) => Copy(_link.Text, "The link is on the clipboard.");
			y += UIHelper.ScaleY(38);

			Label reportLabel = Caption("2. Choose \"Bug report\" there and paste this over the first lines of the form.", margin, y, width);
			y += UIHelper.ScaleY(22);
			var filesHeight = UIHelper.ScaleY(100);
			var bottom = UIHelper.ScaleY(46);
			var reportHeight = ClientSize.Height - y - UIHelper.ScaleY(36) - UIHelper.ScaleY(30) - filesHeight - bottom;
			_report = new TextBox
			{
				Anchor = AnchorStyles.Top | AnchorStyles.Bottom | AnchorStyles.Left | AnchorStyles.Right,
				Location = new(margin, y),
				Multiline = true,
				ReadOnly = true,
				TabStop = false,
				ScrollBars = ScrollBars.Vertical,
				Size = new(width, reportHeight),
				// a text box shows a line break only as the pair Windows writes
				Text = reportText.Replace("\r\n", "\n").Replace("\n", "\r\n"),
				WordWrap = true,
			};
			y += reportHeight + UIHelper.ScaleY(6);
			Button copyReport = new()
			{
				Anchor = AnchorStyles.Bottom | AnchorStyles.Left,
				Location = new(margin, y),
				Size = new(buttonWidth, buttonHeight),
				Text = "Copy Text",
			};
			copyReport.Click += (_, _) => Copy(ReportText, "The text is on the clipboard.");
			y += UIHelper.ScaleY(34);

			Label filesLabel = Caption("3. Tick what belongs to the problem. Save it as one zip and attach that, or attach the files one by one.", margin, y, width);
			filesLabel.Anchor = AnchorStyles.Bottom | AnchorStyles.Left | AnchorStyles.Right;
			y += UIHelper.ScaleY(22);
			_files = new CheckedListBox
			{
				Anchor = AnchorStyles.Bottom | AnchorStyles.Left | AnchorStyles.Right,
				CheckOnClick = true,
				HorizontalScrollbar = true,
				IntegralHeight = false,
				Location = new(margin, y),
				Size = new(width, filesHeight),
			};
			if (_gamePicturePng is not null) _files.Items.Add(PictureRow, isChecked: true);
			foreach (var path in filesToAttach) _files.Items.Add(path, isChecked: true);

			var lastRow = ClientSize.Height - UIHelper.ScaleY(36);
			Button saveZip = new()
			{
				Anchor = AnchorStyles.Bottom | AnchorStyles.Left,
				Location = new(margin, lastRow),
				Size = new(buttonWidth, buttonHeight),
				Text = "Save as Zip...",
			};
			saveZip.Click += (_, _) => AskWhereAndSaveZip();
			_status = new Label
			{
				Anchor = AnchorStyles.Bottom | AnchorStyles.Left | AnchorStyles.Right,
				AutoEllipsis = true,
				AutoSize = false,
				Location = new(margin + buttonWidth + margin, lastRow + UIHelper.ScaleY(5)),
				Size = new(width - buttonWidth - margin - UIHelper.ScaleX(100), UIHelper.ScaleY(18)),
				Text = _files.Items.Count is 0 ? "No saved project, crash note or log was found. The zip will hold the text." : "",
			};
			Button close = new()
			{
				Anchor = AnchorStyles.Bottom | AnchorStyles.Right,
				DialogResult = DialogResult.OK,
				Location = new(ClientSize.Width - margin - UIHelper.ScaleX(90), lastRow),
				Size = new(UIHelper.ScaleX(90), buttonHeight),
				Text = "Close",
			};
			Controls.AddRange([ linkLabel, _link, open, copyLink, reportLabel, _report, copyReport, filesLabel, _files, saveZip, _status, close ]);
			AcceptButton = close;
			CancelButton = close;
			ResumeLayout();
		}

		/// <summary>The link shown, for tests.</summary>
		public string LinkText => _link.Text;

		/// <summary>The text the Copy Text button copies: the report with plain line ends.</summary>
		public string ReportText => _report.Text.Replace("\r\n", "\n");

		/// <summary>The rows of the list, one a line, for tests.</summary>
		public string FilesText => string.Join("\n", _files.Items.Cast<object>().Select(static row => row.ToString()));

		/// <summary>What the window last said under the list.</summary>
		public string StatusText => _status.Text;

		/// <summary>Ticks or unticks a row of the list, for tests.</summary>
		public void SetTicked(int row, bool ticked) => _files.SetItemChecked(row, ticked);

		private static Label Caption(string text, int x, int y, int width) => new()
		{
			Anchor = AnchorStyles.Top | AnchorStyles.Left | AnchorStyles.Right,
			AutoSize = false,
			Location = new(x, y),
			Size = new(width, UIHelper.ScaleY(18)),
			Text = text,
		};

		private static Button TopRightButton(string text, int x, int y, int width, int height) => new()
		{
			Anchor = AnchorStyles.Top | AnchorStyles.Right,
			Location = new(x, y),
			Size = new(width, height),
			Text = text,
		};

		private void Copy(string text, string done)
		{
			try
			{
				Clipboard.SetText(text);
				_status.Text = done;
			}
			catch (System.Runtime.InteropServices.ExternalException)
			{
				// another program has the clipboard open; nothing was copied
				_status.Text = "The clipboard is busy: nothing was copied. Try again.";
			}
		}

		private void AskWhereAndSaveZip()
		{
			using SaveFileDialog dialog = new()
			{
				AddExtension = true,
				DefaultExt = "zip",
				FileName = $"chimera-report-{DateTime.Now:yyyy-MM-dd-HHmm}.zip",
				Filter = "Zip archive (*.zip)|*.zip",
				OverwritePrompt = true,
				Title = "Save the report's files as one zip",
			};
			var desktop = Environment.GetFolderPath(Environment.SpecialFolder.DesktopDirectory);
			if (!string.IsNullOrEmpty(desktop) && Directory.Exists(desktop)) dialog.InitialDirectory = desktop;
			if (dialog.ShowDialog(this) is not DialogResult.OK) return;
			SaveZip(dialog.FileName);
		}

		/// <summary>
		/// Writes the zip: the text, and every ticked row of the list. Says under
		/// the list what happened, including a zip too large for GitHub to take.
		/// </summary>
		public void SaveZip(string zipPath)
		{
			var ticked = _files.CheckedItems.Cast<object>().Select(static row => row.ToString()).ToList();
			var picture = ticked.Contains(PictureRow) ? _gamePicturePng : null;
			var files = _paths.Where(ticked.Contains).ToList();
			try
			{
				var result = IssueReport.WriteZip(zipPath, ReportText, picture, files);
				var megabytes = result.Bytes / (1024.0 * 1024.0);
				var said = $"Saved {Path.GetFileName(zipPath)} ({megabytes:0.0} MB).";
				if (result.NotRead.Count > 0) said += $" Could not read: {string.Join(", ", result.NotRead.Select(Path.GetFileName))}.";
				if (result.Bytes > IssueReport.AttachmentLimit) said += " GitHub takes up to 25 MB: save again without the largest file.";
				_status.Text = said;
			}
			catch (IOException e)
			{
				_status.Text = $"The zip was not saved: {e.Message}";
			}
		}
	}
}
