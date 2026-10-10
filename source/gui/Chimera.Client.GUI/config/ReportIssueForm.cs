#nullable enable

using System;
using System.Collections.Generic;
using System.Windows.Forms;

using Chimera.Client.Common;
using Chimera.Common;

namespace Chimera.Client.GUI
{
	/// <summary>
	/// Help &gt; Report an Issue (#243): the address of the report form, the text
	/// that fills in its first lines, and the files it asks for, each with a
	/// button that copies it. The window sends nothing. The link is handed to the
	/// system's browser, and the person writes and files the report there.
	/// </summary>
	public sealed class ReportIssueForm : FormBase
	{
		private readonly TextBox _link;
		private readonly TextBox _report;
		private readonly TextBox _files;
		private readonly Label _status;

		protected override string WindowTitleStatic => "Report an Issue";

		/// <param name="reportText">what <see cref="IssueReport.Text"/> made of this session</param>
		/// <param name="filesToAttach">the files worth attaching that exist on this computer</param>
		public ReportIssueForm(string reportText, IReadOnlyList<string> filesToAttach)
		{
			SuspendLayout();
			ClientSize = new(UIHelper.ScaleX(680), UIHelper.ScaleY(520));
			MinimumSize = new(UIHelper.ScaleX(520), UIHelper.ScaleY(420));
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
			var filesHeight = UIHelper.ScaleY(84);
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
			_status = new Label
			{
				Anchor = AnchorStyles.Bottom | AnchorStyles.Left | AnchorStyles.Right,
				AutoSize = false,
				Location = new(margin + buttonWidth + margin, y + UIHelper.ScaleY(5)),
				Size = new(width - buttonWidth - margin, UIHelper.ScaleY(18)),
			};
			y += UIHelper.ScaleY(30);

			Label filesLabel = Caption("3. Attach these files if they belong to the problem, and a screenshot.", margin, y, width);
			filesLabel.Anchor = AnchorStyles.Bottom | AnchorStyles.Left | AnchorStyles.Right;
			y += UIHelper.ScaleY(22);
			_files = new TextBox
			{
				Anchor = AnchorStyles.Bottom | AnchorStyles.Left | AnchorStyles.Right,
				Location = new(margin, y),
				Multiline = true,
				ReadOnly = true,
				TabStop = false,
				ScrollBars = ScrollBars.Vertical,
				Size = new(width, filesHeight),
				Text = filesToAttach.Count is 0
					? "No saved project, crash note or sandbox log was found on this computer."
					: string.Join("\r\n", filesToAttach),
				WordWrap = false,
			};

			Button close = new()
			{
				Anchor = AnchorStyles.Bottom | AnchorStyles.Right,
				DialogResult = DialogResult.OK,
				Location = new(ClientSize.Width - margin - UIHelper.ScaleX(90), ClientSize.Height - UIHelper.ScaleY(36)),
				Size = new(UIHelper.ScaleX(90), buttonHeight),
				Text = "Close",
			};
			Controls.AddRange([ linkLabel, _link, open, copyLink, reportLabel, _report, copyReport, _status, filesLabel, _files, close ]);
			AcceptButton = close;
			CancelButton = close;
			ResumeLayout();
		}

		/// <summary>The link shown, for tests.</summary>
		public string LinkText => _link.Text;

		/// <summary>The text the Copy Text button copies: the report with plain line ends.</summary>
		public string ReportText => _report.Text.Replace("\r\n", "\n");

		/// <summary>The files listed, one a line, for tests.</summary>
		public string FilesText => _files.Text.Replace("\r\n", "\n");

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
	}
}
