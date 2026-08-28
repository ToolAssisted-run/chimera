#nullable enable

using System;
using System.Collections.Generic;
using System.Drawing;
using System.IO;
using System.Linq;
using System.Windows.Forms;

using Chimera.Client.Common;

namespace Chimera.Client.GUI
{
	/// <summary>
	/// Encode Video: one window that turns a run into a video file.
	///
	/// It replaces three commands that had to be assembled in the right order -
	/// start the writer, play the movie, stop the writer - and which produced an
	/// empty or unfinished file when they were not. Everything the encode needs is
	/// answered here, the encode happens while the window is open, and it is over
	/// when the window closes. Closing early stops it; there is no state left
	/// running behind the dialog for someone to forget about.
	///
	/// The stretch to encode is given as two markers rather than two frame numbers,
	/// because a run already says where it starts, where its input stops and where
	/// it ends, and those are the answers people actually want.
	/// </summary>
	public sealed class EncodeVideoForm : FormBase
	{
		private readonly IReadOnlyList<TasMovieMarker> _markers;
		private readonly Func<VideoEncodeRequest, string?> _begin;
		private readonly Action _cancel;
		private readonly Func<VideoEncodeProgress> _poll;
		private readonly Func<string, string?> _pickOutputFile;
		private readonly Size _currentOutputSize;

		private readonly TextBox _output;
		private readonly Button _browse;
		private readonly TextBox _command;
		private readonly ComboBox _from;
		private readonly ComboBox _to;
		private readonly Label _range;
		private readonly CheckBox _resize;
		private readonly NumericUpDown _width;
		private readonly NumericUpDown _height;
		private readonly Button _useCurrentSize;
		private readonly Label _currentSize;
		private readonly CheckBox _pad;
		private readonly CheckBox _audioSync;
		private readonly CheckBox _captureOsd;
		private readonly CheckBox _captureLua;
		private readonly ProgressBar _progress;
		private readonly Label _status;
		private readonly Button _start;
		private readonly Button _stop;
		private readonly Button _close;
		private readonly Timer _tick;

		/// <summary>
		/// What fills the file when nobody has said otherwise: h264 and aac in an
		/// mp4, at a pixel format every player will take. A person who wants
		/// something else writes it, and it is theirs from then on.
		/// </summary>
		public const string DefaultCommand = "-c:v libx264 -c:a aac -pix_fmt yuv420p -f mp4";

		private bool _running;

		protected override string WindowTitleStatic => "Encode Video";

		public EncodeVideoForm(
			IReadOnlyList<TasMovieMarker> markers,
			Size currentOutputSize,
			Config config,
			string suggestedOutputPath,
			Func<VideoEncodeRequest, string?> begin,
			Action cancel,
			Func<VideoEncodeProgress> poll,
			Func<string, string?> pickOutputFile)
		{
			_markers = markers;
			_currentOutputSize = currentOutputSize;
			_begin = begin;
			_cancel = cancel;
			_poll = poll;
			_pickOutputFile = pickOutputFile;
			Config = config;

			SuspendLayout();
			ClientSize = new(UIHelper.ScaleX(580), UIHelper.ScaleY(320));
			MinimumSize = new(UIHelper.ScaleX(520), UIHelper.ScaleY(320));
			StartPosition = FormStartPosition.CenterParent;
			ShowIcon = false;
			MaximizeBox = false;

			// ---- where it goes ---------------------------------------------------
			Controls.Add(MakeLabel("Output file:", 8, 18));
			_output = new TextBox
			{
				Anchor = AnchorStyles.Top | AnchorStyles.Left | AnchorStyles.Right,
				Location = Pt(124, 14),
				Text = suggestedOutputPath,
				Width = UIHelper.ScaleX(364),
			};
			Controls.Add(_output);
			_browse = new Button
			{
				Anchor = AnchorStyles.Top | AnchorStyles.Right,
				Location = Pt(494, 13),
				Size = new(UIHelper.ScaleX(78), UIHelper.ScaleY(23)),
				Text = "Browse...",
			};
			_browse.Click += (_, _) => Browse();
			Controls.Add(_browse);

			// ---- what writes it --------------------------------------------------
			// The command IS the choice. A list of canned formats sat here for a
			// while and only ever answered a question this box answers better: the
			// container it names decides the file's extension either way.
			Controls.Add(MakeLabel("FFmpeg Command:", 8, 50));
			_command = new TextBox
			{
				Anchor = AnchorStyles.Top | AnchorStyles.Left | AnchorStyles.Right,
				Location = Pt(124, 46),
				Width = UIHelper.ScaleX(448),
			};
			_command.TextChanged += (_, _) => UpdateSuggestedExtension();
			Controls.Add(_command);

			// ---- which frames ----------------------------------------------------
			Controls.Add(MakeLabel("From marker:", 8, 86));
			_from = new ComboBox
			{
				DropDownStyle = ComboBoxStyle.DropDownList,
				Location = Pt(124, 82),
				Width = UIHelper.ScaleX(176),
			};
			Controls.Add(_from);
			Controls.Add(MakeLabel("To marker:", 310, 86));
			_to = new ComboBox
			{
				DropDownStyle = ComboBoxStyle.DropDownList,
				Location = Pt(390, 82),
				Width = UIHelper.ScaleX(182),
			};
			Controls.Add(_to);
			_from.SelectedIndexChanged += (_, _) => ShowRange();
			_to.SelectedIndexChanged += (_, _) => ShowRange();
			_range = new Label
			{
				AutoSize = false,
				Location = Pt(124, 110),
				Size = new(UIHelper.ScaleX(448), UIHelper.ScaleY(18)),
			};
			Controls.Add(_range);

			// ---- what it looks like ----------------------------------------------
			_resize = new CheckBox
			{
				AutoSize = true,
				Location = Pt(8, 142),
				Text = "Resize to",
			};
			_resize.CheckedChanged += (_, _) => SyncSizeEnabled();
			Controls.Add(_resize);
			_width = MakeSize(100, 140);
			_height = MakeSize(172, 140);
			Controls.Add(_width);
			Controls.Add(MakeLabel("x", 156, 144));
			Controls.Add(_height);
			_useCurrentSize = new Button
			{
				Location = Pt(244, 139),
				Size = new(UIHelper.ScaleX(96), UIHelper.ScaleY(23)),
				Text = "Current size",
			};
			_useCurrentSize.Click += (_, _) => UseCurrentSize();
			Controls.Add(_useCurrentSize);
			_currentSize = MakeLabel($"now {currentOutputSize.Width}x{currentOutputSize.Height}", 348, 144);
			Controls.Add(_currentSize);
			_pad = new CheckBox
			{
				AutoSize = true,
				Location = Pt(444, 142),
				Text = "Pad",
			};
			Controls.Add(_pad);

			_audioSync = new CheckBox
			{
				AutoSize = true,
				Location = Pt(8, 170),
				Text = "Sync to audio",
			};
			Controls.Add(_audioSync);
			_captureOsd = new CheckBox
			{
				AutoSize = true,
				Location = Pt(140, 170),
				Text = "Capture OSD",
			};
			_captureOsd.CheckedChanged += (_, _) =>
			{
				// the OSD is drawn over the lua layer, so it cannot be had without it
				if (_captureOsd.Checked) _captureLua.Checked = true;
			};
			Controls.Add(_captureOsd);
			_captureLua = new CheckBox
			{
				AutoSize = true,
				Location = Pt(272, 170),
				Text = "Capture Lua",
			};
			_captureLua.CheckedChanged += (_, _) =>
			{
				if (!_captureLua.Checked) _captureOsd.Checked = false;
			};
			Controls.Add(_captureLua);

			// ---- how it is going -------------------------------------------------
			_progress = new ProgressBar
			{
				Anchor = AnchorStyles.Top | AnchorStyles.Left | AnchorStyles.Right,
				Location = Pt(8, 208),
				Maximum = 1,
				Size = new(UIHelper.ScaleX(564), UIHelper.ScaleY(20)),
			};
			Controls.Add(_progress);
			_status = new Label
			{
				Anchor = AnchorStyles.Top | AnchorStyles.Left | AnchorStyles.Right,
				AutoSize = false,
				Location = Pt(8, 234),
				Size = new(UIHelper.ScaleX(564), UIHelper.ScaleY(34)),
			};
			Controls.Add(_status);

			// ---- the three things you can do -------------------------------------
			_start = new Button
			{
				Anchor = AnchorStyles.Bottom | AnchorStyles.Right,
				Location = Pt(320, 280),
				Size = new(UIHelper.ScaleX(96), UIHelper.ScaleY(26)),
				Text = "Start Encode",
			};
			_start.Click += (_, _) => Start();
			Controls.Add(_start);
			_stop = new Button
			{
				Anchor = AnchorStyles.Bottom | AnchorStyles.Right,
				Enabled = false,
				Location = Pt(424, 280),
				Size = new(UIHelper.ScaleX(70), UIHelper.ScaleY(26)),
				Text = "Stop",
			};
			_stop.Click += (_, _) => _cancel();
			Controls.Add(_stop);
			_close = new Button
			{
				Anchor = AnchorStyles.Bottom | AnchorStyles.Right,
				Location = Pt(502, 280),
				Size = new(UIHelper.ScaleX(70), UIHelper.ScaleY(26)),
				Text = "Close",
			};
			_close.Click += (_, _) => Close();
			Controls.Add(_close);

			ResumeLayout();

			_tick = new Timer { Interval = 200 };
			_tick.Tick += (_, _) => ShowProgress(_poll());

			FillMarkers();
			FillFromConfig(config);
			ShowRange();
			SyncSizeEnabled();
			ShowProgress(new(VideoEncodePhase.Idle, 0, 0, 0, 0, null, null));
		}

		// ---- the state a caller or a test asks about ------------------------------

		/// <summary>The markers offered as a starting point, in the order shown.</summary>
		public string[] MarkerChoices => _from.Items.Cast<string>().ToArray();

		public int StartFrame => FrameOf(_from);

		public int EndFrame => FrameOf(_to);

		public string FFmpegCommand => _command.Text;

		public string OutputPath => _output.Text;

		public bool StartEnabled => _start.Enabled;

		public bool StopEnabled => _stop.Enabled;

		public string StatusText => _status.Text;

		/// <summary>Fills in the dialog as if a person had, for tests and for callers.</summary>
		public void Choose(string? from = null, string? to = null, string? output = null, string? command = null)
		{
			if (from is not null) _from.SelectedIndex = Array.IndexOf(MarkerChoices, from);
			if (to is not null) _to.SelectedIndex = Array.IndexOf(MarkerChoices, to);
			if (output is not null) _output.Text = output;
			if (command is not null) _command.Text = command;
		}

		/// <summary>Presses Start Encode.</summary>
		public void StartEncode() => Start();

		// ---- filling it in --------------------------------------------------------

		private void FillMarkers()
		{
			foreach (var marker in _markers.OrderBy(static m => m.Frame).ThenBy(static m => m.Frame))
			{
				var label = Describe(marker);
				_from.Items.Add(label);
				_to.Items.Add(label);
			}

			if (_from.Items.Count is 0) return;

			// A run says where it starts and where it ends, so those are what the
			// dialog opens on: the whole movie, which is what most encodes are.
			_from.SelectedIndex = IndexOfPermanent(MarkerPermanence.RunStart) ?? 0;
			_to.SelectedIndex = IndexOfPermanent(MarkerPermanence.RunEnd) ?? _to.Items.Count - 1;
		}

		private static string Describe(TasMovieMarker marker) => $"{marker.Frame}  {marker.Message}";

		private int? IndexOfPermanent(MarkerPermanence kind)
		{
			var ordered = _markers.OrderBy(static m => m.Frame).ToList();
			var index = ordered.FindIndex(m => m.Permanence == kind);
			return index >= 0 ? index : null;
		}

		private int FrameOf(ComboBox box)
		{
			if (box.SelectedIndex < 0) return 0;
			var ordered = _markers.OrderBy(static m => m.Frame).ToList();
			return ordered[box.SelectedIndex].Frame;
		}

		private void FillFromConfig(Config config)
		{
			_command.Text = string.IsNullOrWhiteSpace(config.FFmpegCustomCommand)
				? DefaultCommand
				: config.FFmpegCustomCommand;

			_audioSync.Checked = config.VideoWriterAudioSyncEffective;
			_pad.Checked = config.AVWriterPad;
			_captureOsd.Checked = config.AviCaptureOsd;
			_captureLua.Checked = config.AviCaptureLua || config.AviCaptureOsd;
			if (config.AVWriterResizeWidth > 0 && config.AVWriterResizeHeight > 0)
			{
				_resize.Checked = true;
				_width.Value = Math.Min(_width.Maximum, config.AVWriterResizeWidth);
				_height.Value = Math.Min(_height.Maximum, config.AVWriterResizeHeight);
			}
			else
			{
				UseCurrentSize();
			}
		}

		/// <summary>
		/// Follows the container named in the command, so the file gets the extension
		/// its bytes deserve. Only ever changes the extension - the folder and the
		/// name are the person's.
		/// </summary>
		private void UpdateSuggestedExtension()
		{
			var extension = ExtensionOf(_command.Text);
			if (extension is null || string.IsNullOrWhiteSpace(_output.Text)) return;
			var current = Path.GetExtension(_output.Text);
			if (string.Equals(current, $".{extension}", StringComparison.OrdinalIgnoreCase)) return;
			_output.Text = Path.ChangeExtension(_output.Text, extension);
		}

		/// <summary>The container an ffmpeg command asks for, from its own -f flag.</summary>
		internal static string? ExtensionOf(string command)
		{
			var parts = command.Split(new[] { ' ' }, StringSplitOptions.RemoveEmptyEntries);
			for (var i = 0; i < parts.Length - 1; i++)
			{
				if (parts[i] is not "-f") continue;
				return parts[i + 1] is "matroska" ? "mkv" : parts[i + 1];
			}

			return null;
		}

		private void ShowRange()
		{
			var frames = EndFrame - StartFrame + 1;
			_range.Text = frames > 0
				? $"frames {StartFrame} to {EndFrame} ({frames:N0} frames)"
				: "the end marker is before the start marker";
			_range.ForeColor = frames > 0 ? SystemColors.GrayText : Color.Firebrick;
			if (!_running) _start.Enabled = frames > 0;
		}

		private void SyncSizeEnabled()
		{
			_width.Enabled = _height.Enabled = _pad.Enabled = _useCurrentSize.Enabled = _resize.Checked && !_running;
		}

		private void UseCurrentSize()
		{
			_width.Value = Math.Min(_width.Maximum, Math.Max(_width.Minimum, _currentOutputSize.Width));
			_height.Value = Math.Min(_height.Maximum, Math.Max(_height.Minimum, _currentOutputSize.Height));
		}

		private void Browse()
		{
			var chosen = _pickOutputFile(_output.Text);
			if (chosen is not null) _output.Text = chosen;
		}

		// ---- running it -----------------------------------------------------------

		private void Start()
		{
			if (_running) return;

			var request = BuildRequest();
			var error = _begin(request);
			if (error is not null)
			{
				_status.Text = error;
				_status.ForeColor = Color.Firebrick;
				return;
			}

			RememberChoices();
			_running = true;
			SetInputsEnabled(false);
			_tick.Start();
			ShowProgress(_poll());
		}

		/// <summary>What the dialog is asking for, exactly as it stands.</summary>
		public VideoEncodeRequest BuildRequest()
			=> new()
			{
				OutputPath = _output.Text.Trim(),
				FFmpegCommand = _command.Text.Trim(),
				StartFrame = StartFrame,
				EndFrame = EndFrame,
				Width = _resize.Checked ? (int)_width.Value : 0,
				Height = _resize.Checked ? (int)_height.Value : 0,
				Pad = _pad.Checked,
				AudioSync = _audioSync.Checked,
				CaptureOsd = _captureOsd.Checked,
				CaptureLua = _captureLua.Checked || _captureOsd.Checked,
			};

		private void RememberChoices()
		{
			if (Config is null) return;
			Config.FFmpegCustomCommand = _command.Text;
		}

		private void SetInputsEnabled(bool enabled)
		{
			foreach (var control in new Control[]
				{ _output, _browse, _command, _from, _to, _resize, _audioSync, _captureOsd, _captureLua })
			{
				control.Enabled = enabled;
			}

			_start.Enabled = enabled;
			_stop.Enabled = !enabled;
			SyncSizeEnabled();
		}

		private void ShowProgress(VideoEncodeProgress progress)
		{
			_progress.Maximum = Math.Max(1, progress.FramesTotal);
			_progress.Value = Math.Min(_progress.Maximum, Math.Max(0, progress.FramesDone));

			_status.ForeColor = progress.Phase is VideoEncodePhase.Failed ? Color.Firebrick : SystemColors.ControlText;
			_status.Text = progress.Phase switch
			{
				VideoEncodePhase.Idle => "",
				VideoEncodePhase.Seeking => $"Winding the run to frame {StartFrame} (now at {progress.CurrentFrame})...",
				VideoEncodePhase.Encoding =>
					$"frame {progress.FramesDone:N0} / {progress.FramesTotal:N0}"
					+ $"   {progress.FramesPerSecond:N0} fps"
					+ (progress.Remaining is { } left ? $"   {Describe(left)} remaining" : ""),
				_ => progress.Message ?? "",
			};

			if (_running && progress.Phase is not (VideoEncodePhase.Seeking or VideoEncodePhase.Encoding))
			{
				_running = false;
				_tick.Stop();
				SetInputsEnabled(true);
				ShowRange();
			}
		}

		private static string Describe(TimeSpan span)
			=> span.TotalHours >= 1
				? $"about {(int)span.TotalHours}h {span.Minutes}m"
				: span.TotalMinutes >= 1 ? $"about {(int)span.TotalMinutes}m {span.Seconds}s" : $"about {span.Seconds}s";

		protected override void OnFormClosing(FormClosingEventArgs e)
		{
			// the encode lives and dies with this window: nothing keeps running
			// behind it for someone to wonder about later
			if (_running) _cancel();
			_tick.Stop();
			base.OnFormClosing(e);
		}

		protected override void Dispose(bool disposing)
		{
			if (disposing) _tick.Dispose();
			base.Dispose(disposing);
		}

		// ---- layout ---------------------------------------------------------------

		private static Point Pt(int x, int y) => new(UIHelper.ScaleX(x), UIHelper.ScaleY(y));

		private static Label MakeLabel(string text, int x, int y)
			=> new() { AutoSize = true, Location = Pt(x, y), Text = text };

		private static NumericUpDown MakeSize(int x, int y)
			=> new()
			{
				Location = Pt(x, y),
				Maximum = 8192,
				Minimum = 1,
				Value = 1,
				Width = UIHelper.ScaleX(52),
			};
	}
}
