#nullable enable

using System;
using System.IO;
using System.Text;
using System.Windows.Forms;

using Chimera.Client.Common;
using Chimera.Common;
using Chimera.Common.PathExtensions;
using Chimera.Emulation.Common.Engine;

namespace Chimera.Client.GUI
{
	/// <summary>
	/// Tools &gt; MCP Server... (docs/mcp-server.md): where a person starts and stops the
	/// server that lets their AI agent work on the open project, allows it to make
	/// changes, and reads what it did. The server is the engine's (EngineMcp); this
	/// window only shows it. It stays open beside the main window, and closing it
	/// does not stop the server.
	/// </summary>
	public sealed class McpServerForm : FormBase
	{
		private readonly Label _state;
		private readonly Button _startStop;
		private readonly CheckBox _allowChanges;
		private readonly TextBox _setup;
		private readonly TextBox _log;
		private readonly Label _note;
		private readonly Timer _refresh;
		private ulong _logShown;
		private string _startError = "";

		protected override string WindowTitleStatic => "MCP Server";

		/// <summary>Raised after the server was started or stopped from this window.</summary>
		public event Action? ServerToggled;

		public McpServerForm()
		{
			SuspendLayout();
			ClientSize = new(UIHelper.ScaleX(700), UIHelper.ScaleY(520));
			MinimumSize = new(UIHelper.ScaleX(560), UIHelper.ScaleY(420));
			StartPosition = FormStartPosition.CenterParent;
			MinimizeBox = false;
			var margin = UIHelper.ScaleX(10);
			var buttonWidth = UIHelper.ScaleX(110);
			var buttonHeight = UIHelper.ScaleY(26);
			var width = ClientSize.Width - (2 * margin);
			var y = UIHelper.ScaleY(10);

			Label intro = Caption("An AI agent can look at the project you have open, and change it if you allow that, while this server is started.", margin, y, width);
			y += UIHelper.ScaleY(26);

			_state = new Label
			{
				Anchor = AnchorStyles.Top | AnchorStyles.Left | AnchorStyles.Right,
				AutoEllipsis = true,
				AutoSize = false,
				Location = new(margin, y + UIHelper.ScaleY(5)),
				Size = new(width - buttonWidth - margin, UIHelper.ScaleY(18)),
			};
			_startStop = new Button
			{
				Anchor = AnchorStyles.Top | AnchorStyles.Right,
				Location = new(ClientSize.Width - margin - buttonWidth, y),
				Size = new(buttonWidth, buttonHeight),
			};
			_startStop.Click += (_, _) => Toggle();
			y += UIHelper.ScaleY(36);

			_allowChanges = new CheckBox
			{
				Anchor = AnchorStyles.Top | AnchorStyles.Left | AnchorStyles.Right,
				AutoSize = false,
				Location = new(margin, y),
				Size = new(width, UIHelper.ScaleY(20)),
				Text = "Allow the agent to make changes (edit input, move the playback position, write memory)",
			};
			_allowChanges.CheckedChanged += (_, _) =>
			{
				if (EngineMcp.State is not EngineMcp.ServerState.Stopped) EngineMcp.ChangesAllowed = _allowChanges.Checked;
			};
			y += UIHelper.ScaleY(22);
			Label allowNote = Caption("Off every time the server is started. While it is off the agent can only look.", margin + UIHelper.ScaleX(18), y, width - UIHelper.ScaleX(18));
			y += UIHelper.ScaleY(30);

			Label setupLabel = Caption("To set up an agent, once: give it this command. This one is for Claude Code; other programs run the same file as an MCP server.", margin, y, width);
			y += UIHelper.ScaleY(22);
			_setup = new TextBox
			{
				Anchor = AnchorStyles.Top | AnchorStyles.Left | AnchorStyles.Right,
				Location = new(margin, y + UIHelper.ScaleY(2)),
				ReadOnly = true,
				TabStop = false,
				Text = SetupCommand,
				Width = width - buttonWidth - margin,
			};
			Button copy = new()
			{
				Anchor = AnchorStyles.Top | AnchorStyles.Right,
				Location = new(ClientSize.Width - margin - buttonWidth, y),
				Size = new(buttonWidth, buttonHeight),
				Text = "Copy",
			};
			copy.Click += (_, _) => Copy(_setup.Text);
			y += UIHelper.ScaleY(38);

			Label logLabel = Caption("What happened, one line a call:", margin, y, width);
			y += UIHelper.ScaleY(22);
			var lastRow = ClientSize.Height - UIHelper.ScaleY(36);
			_log = new TextBox
			{
				Anchor = AnchorStyles.Top | AnchorStyles.Bottom | AnchorStyles.Left | AnchorStyles.Right,
				Location = new(margin, y),
				Multiline = true,
				ReadOnly = true,
				ScrollBars = ScrollBars.Both,
				Size = new(width, lastRow - y - UIHelper.ScaleY(8)),
				TabStop = false,
				WordWrap = false,
			};

			Button save = new()
			{
				Anchor = AnchorStyles.Bottom | AnchorStyles.Left,
				Location = new(margin, lastRow),
				Size = new(buttonWidth, buttonHeight),
				Text = "Save Log...",
			};
			save.Click += (_, _) => AskWhereAndSaveLog();
			_note = new Label
			{
				Anchor = AnchorStyles.Bottom | AnchorStyles.Left | AnchorStyles.Right,
				AutoEllipsis = true,
				AutoSize = false,
				Location = new(margin + buttonWidth + margin, lastRow + UIHelper.ScaleY(5)),
				Size = new(width - buttonWidth - margin - UIHelper.ScaleX(100), UIHelper.ScaleY(18)),
				Text = "Closing this window does not stop the server.",
			};
			Button close = new()
			{
				Anchor = AnchorStyles.Bottom | AnchorStyles.Right,
				Location = new(ClientSize.Width - margin - UIHelper.ScaleX(90), lastRow),
				Size = new(UIHelper.ScaleX(90), buttonHeight),
				Text = "Close",
			};
			close.Click += (_, _) => Close();
			Controls.AddRange([ intro, _state, _startStop, _allowChanges, allowNote, setupLabel, _setup, copy, logLabel, _log, save, _note, close ]);
			CancelButton = close;
			ResumeLayout();

			// the server lives in the engine and changes without this window: an agent
			// connects, a call is run. So the window asks a few times a second.
			_refresh = new Timer { Interval = 250 };
			_refresh.Tick += (_, _) => RefreshNow();
			_refresh.Start();
			RefreshNow();
		}

		protected override void Dispose(bool disposing)
		{
			if (disposing) _refresh.Dispose();
			base.Dispose(disposing);
		}

		/// <summary>The program an agent's program starts: beside the engine's library.</summary>
		public static string ProgramPath
			=> Path.Combine(PathUtils.DllDirectoryPath, OSTailoredCode.IsUnixHost ? "chimera-mcp" : "chimera-mcp.exe");

		/// <summary>The command that adds Chimera to Claude Code.</summary>
		public static string SetupCommand => $"claude mcp add chimera -- \"{ProgramPath}\"";

		/// <summary>What the window says the server is doing.</summary>
		public string StateText => _state.Text;

		/// <summary>The text of the Start / Stop button.</summary>
		public string StartStopText => _startStop.Text;

		/// <summary>The tick box that allows the agent to make changes.</summary>
		public bool AllowChanges
		{
			get => _allowChanges.Checked;
			set => _allowChanges.Checked = value;
		}

		/// <summary>Whether the tick box can be ticked now (only while the server is started).</summary>
		public bool AllowChangesEnabled => _allowChanges.Enabled;

		/// <summary>The log as shown, with plain line ends.</summary>
		public string LogText => _log.Text.Replace("\r\n", "\n");

		/// <summary>The line under the log.</summary>
		public string NoteText => _note.Text;

		/// <summary>Starts the server when it is stopped, stops it when it is started.</summary>
		public void Toggle()
		{
			_startError = "";
			if (EngineMcp.State is EngineMcp.ServerState.Stopped)
			{
				if (!EngineMcp.Start(out var error)) _startError = error;
			}
			else
			{
				EngineMcp.Stop();
			}
			RefreshNow();
			ServerToggled?.Invoke();
		}

		/// <summary>Brings the window up to date with the engine: the state, the tick box, new log lines.</summary>
		public void RefreshNow()
		{
			var state = EngineMcp.State;
			var stateText = state switch
			{
				EngineMcp.ServerState.Connected => $"Started. Connected: {EngineMcp.Client}.",
				EngineMcp.ServerState.Waiting => "Started. No agent is connected.",
				_ => _startError.Length is 0 ? "Stopped." : $"Not started: {_startError}.",
			};
			if (_state.Text != stateText) _state.Text = stateText;
			var buttonText = state is EngineMcp.ServerState.Stopped ? "Start" : "Stop";
			if (_startStop.Text != buttonText) _startStop.Text = buttonText;

			// the engine's word is the truth: a new start begins with changes off
			var started = state is not EngineMcp.ServerState.Stopped;
			if (_allowChanges.Enabled != started) _allowChanges.Enabled = started;
			var allowed = started && EngineMcp.ChangesAllowed;
			if (_allowChanges.Checked != allowed) _allowChanges.Checked = allowed;

			var total = EngineMcp.LogTotal;
			if (total == _logShown) return;
			StringBuilder added = new();
			for (var i = _logShown; i < total; i++)
			{
				var line = EngineMcp.LogLine(i);
				// a line the engine no longer keeps (more than 5000 since we last looked)
				if (line.Length is 0) continue;
				added.Append(line).Append("\r\n");
			}
			_logShown = total;
			_log.AppendText(added.ToString());
		}

		/// <summary>Writes the log as shown to a file. For the button, and for tests.</summary>
		public void SaveLog(string path)
		{
			try
			{
				File.WriteAllText(path, LogText);
				_note.Text = $"Saved {Path.GetFileName(path)}.";
			}
			catch (Exception ex) when (ex is IOException or UnauthorizedAccessException)
			{
				_note.Text = $"The log was not saved: {ex.Message}";
			}
		}

		private void AskWhereAndSaveLog()
		{
			using SaveFilePicker dialog = new()
			{
				AddExtension = true,
				DefaultExt = "txt",
				FileName = $"chimera-mcp-log-{DateTime.Now:yyyy-MM-dd-HHmm}.txt",
				Filter = "Text file (*.txt)|*.txt",
				OverwritePrompt = true,
				Title = "Save the MCP server's log",
			};
			if (dialog.ShowDialog(this) is DialogResult.OK) SaveLog(dialog.FileName);
		}

		private void Copy(string text)
		{
			try
			{
				Clipboard.SetText(text);
				_note.Text = "The command is on the clipboard.";
			}
			catch (System.Runtime.InteropServices.ExternalException)
			{
				// another program has the clipboard open; nothing was copied
				_note.Text = "The clipboard is busy: nothing was copied. Try again.";
			}
		}

		private static Label Caption(string text, int x, int y, int width) => new()
		{
			Anchor = AnchorStyles.Top | AnchorStyles.Left | AnchorStyles.Right,
			AutoEllipsis = true,
			AutoSize = false,
			Location = new(x, y),
			Size = new(width, UIHelper.ScaleY(18)),
			Text = text,
		};
	}
}
