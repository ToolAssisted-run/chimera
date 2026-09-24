using System;
using System.Windows.Forms;

using Chimera.Client.Common;

namespace Chimera.Client.GUI
{
	/// <summary>
	/// How often the greenzone stores a frame: every frame, one in N, or not at all
	/// (user request, 2026-09-23/24). Only the frequency changes; the engine keeps the
	/// greenzone the same way whichever is chosen. N is kept in TAStudio's settings;
	/// the choice itself is not saved, so a project always opens storing every frame.
	/// </summary>
	public partial class GreenzoneBox : UserControl
	{
		public enum Level { EveryFrame, EveryN, Off }

		private Level _level = Level.EveryFrame;
		private bool _loading = true;

		public TAStudio Tastudio { get; set; }

		public GreenzoneBox()
		{
			InitializeComponent();
			EveryFrameRadio.Click += (_, _) => Selected = Level.EveryFrame;
			EveryNRadio.Click += (_, _) => Selected = Level.EveryN;
			OffRadio.Click += (_, _) => Selected = Level.Off;
		}

		public Level Selected
		{
			get => _level;
			set
			{
				_level = value;
				Apply();
			}
		}

		/// <summary>The "Cycle Greenzone" hotkey: every frame, every N, off, and round again.</summary>
		public void Cycle() => Selected = (Level)(((int)_level + 1) % 3);

		private int PeriodOf(Level level) => level switch
		{
			Level.EveryFrame => 1,
			Level.EveryN => (int)PeriodNum.Value,
			_ => 0,
		};

		/// <summary>The level or N changed: the buttons and the movie follow.</summary>
		private void Apply()
		{
			if (Tastudio?.CurrentTasMovie is not { } movie) return;
			movie.GreenzonePeriod = PeriodOf(_level);
			ShowLevel();
			Tastudio.RefreshDialog();
		}

		/// <summary>
		/// A movie that stores every frame while another level is chosen is one just
		/// opened - a project always opens storing every frame - so the box follows it.
		/// </summary>
		public void ShowMovie()
		{
			if (Tastudio?.CurrentTasMovie is not { } movie) return;
			if (movie.GreenzonePeriod == 1) _level = Level.EveryFrame;
			ShowLevel();
		}

		private void ShowLevel()
		{
			EveryFrameRadio.Checked = _level == Level.EveryFrame;
			EveryNRadio.Checked = _level == Level.EveryN;
			OffRadio.Checked = _level == Level.Off;
		}

		private void PeriodNum_ValueChanged(object sender, EventArgs e)
		{
			if (_loading) return;
			Tastudio.Settings.GreenzonePeriod = (int)PeriodNum.Value;
			if (_level == Level.EveryN) Apply();
		}

		public void UpdateHotkeyTooltips(Config config)
		{
			string raw = config.HotkeyBindings["Cycle Greenzone"];
			string hotkey = raw.Length == 0 ? "Hotkey (Cycle Greenzone): unbound" : $"Hotkey (Cycle Greenzone): {raw.Replace(",", " or ")}";

			toolTip1.SetToolTip(EveryFrameRadio, hotkey
				+ "\nThe greenzone stores every frame it can afford.");
			toolTip1.SetToolTip(EveryNRadio, hotkey
				+ "\nThe greenzone stores one frame in this many, which runs faster;"
				+ "\nreaching a frame between costs replaying from the one before it.");
			toolTip1.SetToolTip(PeriodNum, "How many frames apart the greenzone stores one (2 to 999).");
			toolTip1.SetToolTip(OffRadio, hotkey
				+ "\nNo new greenzone at all; what is stored stays. Useful through parts"
				+ "\nthat need no re-recording, or when only branches are used."
				+ "\nTurning it on again stores a full state at the current frame.");
		}

		protected override void OnLoad(EventArgs e)
		{
			base.OnLoad(e);
			if (DesignMode || Tastudio is null) return;
			PeriodNum.Value = Math.Min(Math.Max(Tastudio.Settings.GreenzonePeriod, (int)PeriodNum.Minimum), (int)PeriodNum.Maximum);
			_loading = false;
			ShowMovie();
		}
	}
}
