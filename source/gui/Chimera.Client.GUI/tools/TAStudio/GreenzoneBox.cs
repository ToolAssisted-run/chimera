using System;
using System.Windows.Forms;

using Chimera.Client.Common;

namespace Chimera.Client.GUI
{
	/// <summary>
	/// How often the greenzone stores a frame: every frame, one in a few, one in many,
	/// or not at all (user request, 2026-09-23). Only the frequency changes; the engine
	/// keeps the greenzone the same way whichever is chosen. The two periods are
	/// TAStudio settings; the choice itself is not saved, so a project always opens
	/// storing every frame.
	/// </summary>
	public partial class GreenzoneBox : UserControl
	{
		public enum Level { EveryFrame, Sparse, Sparsest, Off }

		private Level _level = Level.EveryFrame;

		public TAStudio Tastudio { get; set; }

		public GreenzoneBox()
		{
			InitializeComponent();
			EveryFrameRadio.Click += (_, _) => Selected = Level.EveryFrame;
			SparseRadio.Click += (_, _) => Selected = Level.Sparse;
			SparsestRadio.Click += (_, _) => Selected = Level.Sparsest;
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

		/// <summary>The "Cycle Greenzone" hotkey: every frame, sparse, sparsest, off, and round again.</summary>
		public void Cycle() => Selected = (Level)(((int)_level + 1) % 4);

		private int PeriodOf(Level level) => level switch
		{
			Level.EveryFrame => 1,
			Level.Sparse => Math.Max(2, Tastudio.Settings.GreenzoneSparsePeriod),
			Level.Sparsest => Math.Max(2, Tastudio.Settings.GreenzoneSparsestPeriod),
			_ => 0,
		};

		/// <summary>The level or the settings changed: the labels, the buttons and the movie follow.</summary>
		public void Apply()
		{
			if (Tastudio?.CurrentTasMovie is not { } movie) return;
			ShowPeriods();
			movie.GreenzonePeriod = PeriodOf(_level);
			ShowLevel();
			Tastudio.RefreshDialog();
		}

		/// <summary>
		/// A movie that stores every frame while another level is chosen is one just
		/// opened - a project always opens storing every frame - so the box follows it.
		/// Anything else the box itself set, and it keeps its choice even while a new
		/// period from the settings is on its way to the movie.
		/// </summary>
		public void ShowMovie()
		{
			if (Tastudio?.CurrentTasMovie is not { } movie) return;
			if (movie.GreenzonePeriod == 1) _level = Level.EveryFrame;
			ShowLevel();
		}

		private void ShowPeriods()
		{
			SparseRadio.Text = $"Every {PeriodOf(Level.Sparse)} frames";
			SparsestRadio.Text = $"Every {PeriodOf(Level.Sparsest)} frames";
		}

		private void ShowLevel()
		{
			EveryFrameRadio.Checked = _level == Level.EveryFrame;
			SparseRadio.Checked = _level == Level.Sparse;
			SparsestRadio.Checked = _level == Level.Sparsest;
			OffRadio.Checked = _level == Level.Off;
		}

		public void UpdateHotkeyTooltips(Config config)
		{
			string raw = config.HotkeyBindings["Cycle Greenzone"];
			string hotkey = raw.Length == 0 ? "Hotkey (Cycle Greenzone): unbound" : $"Hotkey (Cycle Greenzone): {raw.Replace(",", " or ")}";

			toolTip1.SetToolTip(EveryFrameRadio, hotkey
				+ "\nThe greenzone stores every frame it can afford.");
			toolTip1.SetToolTip(SparseRadio, hotkey
				+ "\nThe greenzone stores one frame in this many, which runs faster;"
				+ "\nreaching a frame between costs replaying from the one before it."
				+ "\nThe number is set in TAStudio's settings.");
			toolTip1.SetToolTip(SparsestRadio, hotkey
				+ "\nThe greenzone stores one frame in this many, which runs faster still."
				+ "\nThe number is set in TAStudio's settings.");
			toolTip1.SetToolTip(OffRadio, hotkey
				+ "\nNo new greenzone at all; what is stored stays. Useful through parts"
				+ "\nthat need no re-recording, or when only branches are used."
				+ "\nTurning it on again stores a full state at the current frame.");
		}

		protected override void OnLoad(EventArgs e)
		{
			base.OnLoad(e);
			if (DesignMode || Tastudio is null) return;
			ShowPeriods();
			ShowMovie();
		}
	}
}
