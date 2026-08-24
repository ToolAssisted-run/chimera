using System.Collections.Generic;
using System.Drawing;
using System.IO;
using System.Windows.Forms;

using Chimera.Client.Common;
using Chimera.Emulation.Common;

namespace Chimera.Client.GUI
{
	/// <summary>
	/// Shows the CPU trace a core produces (<see cref="ITraceable"/>): one line per
	/// instruction, split into what executed and the machine state around it.
	///
	/// The frontend does no disassembly and knows no instruction set - the core
	/// formats its own lines, and this window's only opinions are about volume.
	/// They have to be: a single frame of a 6502 at speed is around ten thousand
	/// lines, so the on-screen buffer keeps a bounded tail of the most recent lines
	/// and anything you actually want to keep goes straight to a file.
	/// </summary>
	public sealed class TraceLogger : ToolFormBase, IToolFormAutoConfig
	{
		[RequiredService]
		private ITraceable Tracer { get; set; }

		public static Icon ToolIcon
			=> Properties.Resources.TextDocIcon;

		protected override string WindowTitleStatic => "Trace Logger";

		private const string DisasmColumn = "Disassembly";
		private const string RegistersColumn = "Registers";

		private readonly InputRoll _view;
		private readonly ToolStripButton _loggingButton;
		private readonly ToolStripButton _fileButton;
		private readonly ToolStripStatusLabel _status;
		private readonly NumericUpDown _capacityBox;

		private readonly Sink _sink;
		private StreamWriter _file;
		private long _linesToFile;

		public TraceLogger()
		{
			SuspendLayout();
			Icon = ToolIcon;
			Size = new(620, 520);
			_sink = new Sink(this);

			MenuStrip menu = new();
			ToolStripMenuItem file = new("&File");
			file.DropDownItems.Add("&Save Visible Lines...", null, (_, _) => SaveVisible());
			menu.Items.Add(file);
			// (the "Settings" menu is added by ToolManager for IToolFormAutoConfig)
			MainMenuStrip = menu;

			_loggingButton = new("Start Logging") { CheckOnClick = true, DisplayStyle = ToolStripItemDisplayStyle.Text };
			_loggingButton.CheckedChanged += (_, _) => SetLogging(_loggingButton.Checked);
			_fileButton = new("Log to File...") { CheckOnClick = true, DisplayStyle = ToolStripItemDisplayStyle.Text };
			_fileButton.CheckedChanged += (_, _) => ToggleFileLog(_fileButton.Checked);

			_capacityBox = new()
			{
				Minimum = 100,
				Maximum = 1000000,
				Increment = 1000,
				Value = 10000,
				Width = UIHelper.ScaleX(80),
			};
			_capacityBox.ValueChanged += (_, _) => Trim();

			ToolStrip bar = new()
			{
				GripStyle = ToolStripGripStyle.Hidden,
				Items =
				{
					_loggingButton,
					_fileButton,
					new ToolStripButton("Clear", null, (_, _) => Clear()) { DisplayStyle = ToolStripItemDisplayStyle.Text },
					new ToolStripLabel("Keep:"),
					new ToolStripControlHost(_capacityBox),
					new ToolStripLabel("lines"),
				},
			};

			_view = new() { Dock = DockStyle.Fill, MultiSelect = false, AllowColumnReorder = false };
			_view.QueryItemText += View_QueryItemText;
			_view.AllColumns.Clear();
			_view.AllColumns.Add(new(name: DisasmColumn, widthUnscaled: 260, text: DisasmColumn));
			_view.AllColumns.Add(new(name: RegistersColumn, widthUnscaled: 320, text: RegistersColumn));

			_status = new() { Spring = true, TextAlign = ContentAlignment.MiddleLeft };
			StatusStrip statusBar = new() { Items = { _status } };

			Controls.Add(_view);
			Controls.Add(bar);
			Controls.Add(statusBar);
			Controls.Add(menu);
			ResumeLayout();
		}

		/// <summary>
		/// The bounded tail shown on screen. Older lines are dropped from the front:
		/// with a core producing lines faster than anyone can read them, the recent
		/// past is the part with any value, and the file log is there for the rest.
		/// </summary>
		private readonly List<TraceInfo> _lines = [ ];

		private sealed class Sink(TraceLogger owner) : ITraceSink
		{
			public void Put(TraceInfo info) => owner.Put(info);
		}

		private void Put(TraceInfo info)
		{
			if (_file is not null)
			{
				_file.Write(info.Disassembly);
				if (!string.IsNullOrEmpty(info.RegisterInfo))
				{
					_file.Write("  ");
					_file.Write(info.RegisterInfo);
				}
				_file.Write('\n');
				_linesToFile++;
			}

			_lines.Add(info);
			// amortised: trimming one line per Put would memmove the whole list
			// hundreds of thousands of times a second
			if (_lines.Count > Capacity * 2) Trim();
		}

		private int Capacity => (int)_capacityBox.Value;

		private void Trim()
		{
			int excess = _lines.Count - Capacity;
			if (excess > 0) _lines.RemoveRange(0, excess);
		}

		private void SetLogging(bool on)
		{
			_loggingButton.Text = on ? "Stop Logging" : "Start Logging";
			// Attaching the sink is what turns tracing on in the core; a core that
			// traces when nobody is listening is just paying for it.
			Tracer.Sink = on ? _sink : null;
			UpdateStatus();
		}

		private void ToggleFileLog(bool on)
		{
			if (!on)
			{
				_file?.Dispose();
				_file = null;
				UpdateStatus();
				return;
			}

			var path = this.ShowFileSaveDialog(
				initDir: Config!.PathEntries.LogAbsolutePath(),
				fileExt: ".log",
				filter: new FilesystemFilterSet(new FilesystemFilter("Log Files", [ "log" ])),
				initFileName: $"{Game.FilesystemSafeName()}.log");
			if (path is null)
			{
				_fileButton.Checked = false; // re-enters here with on == false
				return;
			}

			_file = new StreamWriter(path) { AutoFlush = false };
			_linesToFile = 0;
			UpdateStatus();
		}

		private void Clear()
		{
			_lines.Clear();
			UpdateView();
		}

		private void SaveVisible()
		{
			var path = this.ShowFileSaveDialog(
				initDir: Config!.PathEntries.LogAbsolutePath(),
				fileExt: ".log",
				filter: new FilesystemFilterSet(new FilesystemFilter("Log Files", [ "log" ])),
				initFileName: $"{Game.FilesystemSafeName()}.log");
			if (path is null) return;
			using var w = new StreamWriter(path);
			foreach (var line in _lines)
			{
				w.WriteLine(string.IsNullOrEmpty(line.RegisterInfo) ? line.Disassembly : $"{line.Disassembly}  {line.RegisterInfo}");
			}
		}

		private void View_QueryItemText(InputRoll sender, int index, RollColumn column, out string text, ref int offsetX, ref int offsetY)
		{
			text = "";
			if ((uint)index >= (uint)_lines.Count) return;
			var line = _lines[index];
			text = column.Name == DisasmColumn ? line.Disassembly : line.RegisterInfo;
		}

		private void UpdateView()
		{
			Trim();
			_view.RowCount = _lines.Count;
			if (_lines.Count > 0) _view.ScrollToIndex(_lines.Count - 1);
			UpdateStatus();
		}

		private void UpdateStatus()
		{
			// The core's header describes what its lines contain; it belongs where it
			// can be read in full, not squeezed into a column heading.
			var buffered = $"{_header} - {_lines.Count:N0} lines buffered";
			_status.Text = _file is null
				? buffered
				: $"{buffered} - {_linesToFile:N0} written to file";
		}

		private string _header = "";

		public override void Restart()
		{
			// A new core means a new tracer; re-attach if the user left logging on.
			_lines.Clear();
			if (_loggingButton.Checked) Tracer.Sink = _sink;
			_header = Tracer.Header ?? "";
			UpdateView();
		}

		protected override void UpdateAfter() => UpdateView();

		protected override void GeneralUpdate() => UpdateView();

		protected override void OnClosing(System.ComponentModel.CancelEventArgs e)
		{
			// Leaving the sink attached would keep the core tracing for a window that
			// no longer exists.
			if (Tracer is not null && ReferenceEquals(Tracer.Sink, _sink)) Tracer.Sink = null;
			_file?.Dispose();
			_file = null;
			base.OnClosing(e);
		}
	}
}
