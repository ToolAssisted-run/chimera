#nullable enable

using System.Diagnostics;
using System.Drawing;
using System.Threading;
using System.Windows.Forms;

using Chimera.Client.Common;
using Chimera.Emulation.Common.Engine;

namespace Chimera.Client.GUI
{
	/// <summary>
	/// A small window that says what a long operation is doing and how far it
	/// has got - opening a project, creating one, saving one - and keeps moving
	/// even while the operation does not report.
	///
	/// The work runs on the caller's thread, as it must (the emulator and the
	/// engine are not shared across threads), and some of it is one long call
	/// into the sandbox that reports nothing while it runs - a core compiling
	/// its machine on Init. If the window lived on that thread its bar would
	/// freeze exactly when the wait is longest. So the WINDOW gets its own
	/// thread and its own message loop: the marquee animates there no matter
	/// what the caller's thread is doing, and a person can see the application
	/// is alive.
	///
	/// The caller stays on its own thread throughout. <see cref="EngineProgress"/>
	/// fires there; this records the report in plain fields at once (so the
	/// stage and fraction are known synchronously) and posts a repaint to the
	/// window's thread. The owner is disabled for the duration, so a click
	/// during the wait cannot start a second operation inside the first.
	///
	/// What it shows is what is known: a bar with a length when the stage has
	/// one (bytes of a file being hashed, of a greenzone being compressed), a
	/// count when it has that (compiled objects fetched), and an animated bar
	/// with a clock when it has neither (a machine starting up).
	/// </summary>
	public sealed class ProgressDialog : IDisposable
	{
		private readonly Form? _owner;
		private readonly bool _ownerWasEnabled;
		private readonly Stopwatch _elapsed = Stopwatch.StartNew();

		private Thread? _thread;
		private Form? _window;
		private Label? _stageLabel;
		private Label? _detailLabel;
		private ProgressBar? _bar;
		private readonly ManualResetEventSlim _ready = new(false);
		private bool _disposed;

		// what was reported, kept here so the answer is synchronous and does not
		// depend on the window's thread having drawn yet
		private readonly object _lock = new();
		private string _stage = "Starting";
		private string _detail = "";
		private bool _determinate;
		private double _fraction;

		public string StageText { get { lock (_lock) return _stage; } }
		public string DetailText { get { lock (_lock) return _detail; } }
		public bool Determinate { get { lock (_lock) return _determinate; } }
		public double Fraction { get { lock (_lock) return _fraction; } }

		/// <summary>
		/// Opens the window over <paramref name="owner"/> on its own thread and
		/// starts listening. Dispose it when the operation is over. Headless runs
		/// get a silent one: nothing to draw for, and a gate must not wait on a
		/// window.
		/// </summary>
		public static ProgressDialog Begin(Form? owner, string title)
		{
			ProgressDialog dialog = new(owner, title);
			EngineProgress.Reported += dialog.OnReport;
			if (!HeadlessMode.Enabled) dialog.StartWindow(title, owner);
			return dialog;
		}

		private ProgressDialog(Form? owner, string title)
		{
			_owner = owner;
			_ownerWasEnabled = owner?.Enabled ?? false;
			_stage = Capitalise(title);
			if (owner is not null) owner.Enabled = false;
		}

		private void StartWindow(string title, Form? owner)
		{
			// the owner's bounds are read here, on its own thread, and the window
			// centres itself on them from its thread
			var center = owner is { IsDisposed: false }
				? new Point(owner.Left + owner.Width / 2, owner.Top + owner.Height / 2)
				: (Point?)null;

			_thread = new Thread(() => WindowThread(title, center))
			{
				IsBackground = true,
				Name = "chimera-progress",
			};
			_thread.SetApartmentState(ApartmentState.STA);
			_thread.Start();
			_ready.Wait(2000); // the window's handle exists past this; if it timed out we simply post to nothing
		}

		private void WindowThread(string title, Point? center)
		{
			_window = new Form
			{
				Text = title,
				FormBorderStyle = FormBorderStyle.FixedDialog,
				MaximizeBox = false,
				MinimizeBox = false,
				ControlBox = false,
				ShowInTaskbar = false,
				StartPosition = FormStartPosition.CenterScreen,
				ClientSize = new(UIHelper.ScaleX(420), UIHelper.ScaleY(96)),
			};
			_stageLabel = new Label
			{
				AutoEllipsis = true,
				Location = new(UIHelper.ScaleX(12), UIHelper.ScaleY(12)),
				Size = new(UIHelper.ScaleX(396), UIHelper.ScaleY(18)),
				Text = _stage,
			};
			_bar = new ProgressBar
			{
				Location = new(UIHelper.ScaleX(12), UIHelper.ScaleY(36)),
				Size = new(UIHelper.ScaleX(396), UIHelper.ScaleY(20)),
				Style = ProgressBarStyle.Marquee,
				MarqueeAnimationSpeed = 30,
			};
			_detailLabel = new Label
			{
				AutoEllipsis = true,
				ForeColor = SystemColors.GrayText,
				Location = new(UIHelper.ScaleX(12), UIHelper.ScaleY(64)),
				Size = new(UIHelper.ScaleX(396), UIHelper.ScaleY(18)),
			};
			_window.Controls.AddRange([ _stageLabel, _bar, _detailLabel ]);
			if (center is { } c)
			{
				_window.StartPosition = FormStartPosition.Manual;
				_window.Location = new(c.X - _window.Width / 2, c.Y - _window.Height / 2);
			}

			// a clock, ticking on THIS thread, so the elapsed time keeps moving
			// even when the caller's thread has reported nothing for a while
			System.Windows.Forms.Timer clock = new() { Interval = 250 };
			clock.Tick += (_, _) => Repaint();
			clock.Start();

			_window.HandleCreated += (_, _) => _ready.Set();
			_window.Shown += (_, _) => _ready.Set();
			Application.Run(_window);
			clock.Dispose();
		}

		/// <summary>A step of the frontend's own, in the same stream as the engine's.</summary>
		public void Step(string stage) => OnReport(stage, 0, 0);

		private void OnReport(string stage, ulong done, ulong total)
		{
			lock (_lock)
			{
				_stage = Capitalise(stage);
				if (total is not 0)
				{
					_determinate = true;
					_fraction = (double)done / total;
					_detail = $"{Bytes(done)} of {Bytes(total)}";
				}
				else
				{
					_determinate = false;
					_fraction = 0;
					_detail = done is 0 ? "" : done.ToString();
				}
			}
			Repaint();
		}

		private void Repaint()
		{
			var window = _window;
			if (window is null || window.IsDisposed || !window.IsHandleCreated) return;
			try
			{
				window.BeginInvoke((Action)Apply);
			}
			catch (System.Exception)
			{
				// the window's thread is tearing down; nothing to paint
			}
		}

		private void Apply()
		{
			if (_window is null || _window.IsDisposed || _bar is null || _stageLabel is null || _detailLabel is null) return;
			string stage, detail;
			bool determinate;
			double fraction;
			lock (_lock)
			{
				stage = _stage;
				detail = _detail;
				determinate = _determinate;
				fraction = _fraction;
			}
			_stageLabel.Text = stage;
			if (determinate)
			{
				if (_bar.Style is not ProgressBarStyle.Blocks) _bar.Style = ProgressBarStyle.Blocks;
				_bar.Maximum = 1000;
				_bar.Value = (int)System.Math.Min(1000, System.Math.Round(fraction * 1000));
			}
			else if (_bar.Style is not ProgressBarStyle.Marquee)
			{
				_bar.Style = ProgressBarStyle.Marquee;
			}
			var seconds = _elapsed.Elapsed.TotalSeconds;
			_detailLabel.Text = detail.Length is 0 ? $"{seconds:F0} s" : $"{detail}  ·  {seconds:F0} s";
		}

		private static string Capitalise(string s)
			=> s.Length is 0 ? s : char.ToUpperInvariant(s[0]) + s[1..];

		/// <summary>"1.2 GB", "48 MB", "512 KB" - or a plain count when the stage is not bytes.</summary>
		private static string Bytes(ulong n)
			=> n >= 1UL << 30 ? $"{n / (double)(1UL << 30):F1} GB"
				: n >= 1UL << 20 ? $"{n / (double)(1UL << 20):F0} MB"
				: n >= 1UL << 10 ? $"{n / (double)(1UL << 10):F0} KB"
				: n.ToString();

		public void Dispose()
		{
			if (_disposed) return;
			_disposed = true;
			EngineProgress.Reported -= OnReport;

			var window = _window;
			if (window is not null)
			{
				try
				{
					if (window.IsHandleCreated && !window.IsDisposed)
					{
						window.BeginInvoke((Action)(() => { if (!window.IsDisposed) window.Close(); }));
					}
				}
				catch (System.Exception)
				{
				}
				_thread?.Join(2000);
			}
			_ready.Dispose();
			if (_owner is not null && !_owner.IsDisposed) _owner.Enabled = _ownerWasEnabled;
		}
	}
}
