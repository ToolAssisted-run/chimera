#nullable enable

using System.Diagnostics;
using System.Drawing;
using System.Windows.Forms;

using Chimera.Client.Common;
using Chimera.Emulation.Common.Engine;

namespace Chimera.Client.GUI
{
	/// <summary>
	/// A small window that says what a long operation is doing and how far it
	/// has got - opening a project, creating one, saving one - and keeps the
	/// application answering meanwhile.
	///
	/// The work runs on the UI thread, as it always did (the emulator and the
	/// engine are not shared across threads), so this window is not modal and
	/// there is no worker: it listens to <see cref="EngineProgress"/>, which the
	/// engine and the frontend's own steps report into, and pumps the message
	/// loop a few times a second from those reports. The owner is disabled for
	/// the duration, so a click during the wait cannot start a second operation
	/// inside the first.
	///
	/// What it shows is what is known: a bar with a length when the stage has
	/// one (bytes of a file being hashed, of a greenzone being compressed), a
	/// count when it has that (compiled objects fetched), and an indefinite bar
	/// with a clock when it has neither (a machine starting up).
	/// </summary>
	public sealed class ProgressDialog : Form
	{
		private readonly Label _stage;
		private readonly Label _detail;
		private readonly ProgressBar _bar;
		private readonly Stopwatch _elapsed = Stopwatch.StartNew();
		private readonly Stopwatch _sincePump = Stopwatch.StartNew();
		private readonly Form? _owner;
		private readonly bool _ownerWasEnabled;
		private bool _shown;

		/// <summary>The stage as last reported, for tests.</summary>
		public string StageText => _stage.Text;
		public string DetailText => _detail.Text;
		public bool Determinate => _bar.Style is ProgressBarStyle.Blocks;
		public double Fraction => _bar.Maximum is 0 ? 0 : (double)_bar.Value / _bar.Maximum;

		/// <summary>
		/// Opens the window over <paramref name="owner"/> and starts listening.
		/// Dispose it when the operation is over. Headless runs get a silent one:
		/// nothing to draw for, and a gate must not wait on a window.
		/// </summary>
		public static ProgressDialog Begin(Form? owner, string title)
		{
			ProgressDialog dialog = new(owner, title);
			if (!HeadlessMode.Enabled)
			{
				dialog.Show(owner);
				dialog._shown = true;
				dialog.Pump(force: true);
			}
			return dialog;
		}

		private ProgressDialog(Form? owner, string title)
		{
			_owner = owner;
			_ownerWasEnabled = owner?.Enabled ?? false;

			Text = title;
			FormBorderStyle = FormBorderStyle.FixedDialog;
			MaximizeBox = false;
			MinimizeBox = false;
			ControlBox = false;
			ShowInTaskbar = false;
			StartPosition = owner is null ? FormStartPosition.CenterScreen : FormStartPosition.CenterParent;
			ClientSize = new(UIHelper.ScaleX(420), UIHelper.ScaleY(96));

			_stage = new Label
			{
				AutoEllipsis = true,
				Location = new(UIHelper.ScaleX(12), UIHelper.ScaleY(12)),
				Size = new(UIHelper.ScaleX(396), UIHelper.ScaleY(18)),
				Text = "Starting",
			};
			_bar = new ProgressBar
			{
				Location = new(UIHelper.ScaleX(12), UIHelper.ScaleY(36)),
				Size = new(UIHelper.ScaleX(396), UIHelper.ScaleY(20)),
				Style = ProgressBarStyle.Marquee,
				MarqueeAnimationSpeed = 30,
			};
			_detail = new Label
			{
				AutoEllipsis = true,
				ForeColor = SystemColors.GrayText,
				Location = new(UIHelper.ScaleX(12), UIHelper.ScaleY(64)),
				Size = new(UIHelper.ScaleX(396), UIHelper.ScaleY(18)),
			};
			Controls.AddRange([ _stage, _bar, _detail ]);

			EngineProgress.Reported += OnReport;
			if (owner is not null) owner.Enabled = false;
		}

		/// <summary>A step of the frontend's own, in the same stream as the engine's.</summary>
		public void Step(string stage) => OnReport(stage, 0, 0);

		private void OnReport(string stage, ulong done, ulong total)
		{
			if (IsDisposed) return;
			_stage.Text = Capitalise(stage);
			if (total is not 0)
			{
				if (_bar.Style is not ProgressBarStyle.Blocks) _bar.Style = ProgressBarStyle.Blocks;
				_bar.Maximum = 1000;
				_bar.Value = (int)Math.Min(1000UL, done * 1000 / total);
				_detail.Text = $"{Bytes(done)} of {Bytes(total)}  ·  {_elapsed.Elapsed.TotalSeconds:F0} s";
			}
			else
			{
				if (_bar.Style is not ProgressBarStyle.Marquee) _bar.Style = ProgressBarStyle.Marquee;
				_detail.Text = done is 0
					? $"{_elapsed.Elapsed.TotalSeconds:F0} s"
					: $"{done}  ·  {_elapsed.Elapsed.TotalSeconds:F0} s";
			}
			Pump(force: false);
		}

		/// <summary>Lets the windows repaint and the clock tick, at most thirty times a second.</summary>
		private void Pump(bool force)
		{
			if (!_shown) return;
			if (!force && _sincePump.ElapsedMilliseconds < 33) return;
			_sincePump.Restart();
			Application.DoEvents();
		}

		private static string Capitalise(string s)
			=> s.Length is 0 ? s : char.ToUpperInvariant(s[0]) + s[1..];

		/// <summary>"1.2 GB", "48 MB", "512 KB" - or a plain count when the stage is not bytes.</summary>
		private static string Bytes(ulong n)
			=> n >= 1UL << 30 ? $"{n / (double)(1UL << 30):F1} GB"
				: n >= 1UL << 20 ? $"{n / (double)(1UL << 20):F0} MB"
				: n >= 1UL << 10 ? $"{n / (double)(1UL << 10):F0} KB"
				: n.ToString();

		protected override void Dispose(bool disposing)
		{
			if (disposing)
			{
				EngineProgress.Reported -= OnReport;
				if (_owner is not null && !_owner.IsDisposed) _owner.Enabled = _ownerWasEnabled;
			}
			base.Dispose(disposing);
		}
	}
}
