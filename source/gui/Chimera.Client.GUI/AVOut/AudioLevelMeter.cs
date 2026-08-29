#nullable enable

using System;
using System.Drawing;
using System.Windows.Forms;

namespace Chimera.Client.GUI
{
	/// <summary>
	/// Two bars saying how loud the left and right channels are.
	///
	/// It exists because an encode plays the run SILENTLY - the machine runs as
	/// fast as it can and sound at that speed is noise, so the speakers are muted
	/// while the file is written. Muting the speakers removes the only evidence
	/// that anything is being recorded at all, and a video with no sound in it is
	/// worth knowing about before it is an hour long rather than after. These bars
	/// are that evidence: they move if there is audio going into the file.
	///
	/// The scale is decibels, not amplitude. Amplitude is useless to look at -
	/// ordinary game audio sits in the bottom fifth of the bar and never visibly
	/// leaves it - while dBFS from -60 up puts quiet sound in the middle, which is
	/// where a person can see it change. The peak marker holds and falls back the
	/// way every meter's does, because a peak that vanishes in one frame at 60fps
	/// is a peak nobody sees.
	/// </summary>
	public sealed class AudioLevelMeter : Control
	{
		/// <summary>Below this the bar is empty; ordinary game audio is well above it.</summary>
		private const double FloorDb = -60.0;

		/// <summary>How far the peak marker falls per second, in bar-widths.</summary>
		private const double PeakFallPerSecond = 0.6;

		private double _left, _right;
		private double _peakLeft, _peakRight;
		private DateTime _lastPeakAt = DateTime.UtcNow;

		public AudioLevelMeter()
		{
			SetStyle(ControlStyles.AllPaintingInWmPaint | ControlStyles.OptimizedDoubleBuffer
				| ControlStyles.ResizeRedraw | ControlStyles.UserPaint, true);
			TabStop = false;
		}

		/// <summary>
		/// The loudest sample in each channel since the last time this was asked,
		/// as a fraction of full scale. Anything outside 0..1 is clamped rather
		/// than argued with: a meter is not the place to discover a bad number.
		/// </summary>
		public void SetLevels(double left, double right)
		{
			_left = Clamp01(left);
			_right = Clamp01(right);

			var now = DateTime.UtcNow;
			var fall = (now - _lastPeakAt).TotalSeconds * PeakFallPerSecond;
			_lastPeakAt = now;
			_peakLeft = Math.Max(_left, _peakLeft - fall);
			_peakRight = Math.Max(_right, _peakRight - fall);
			Invalidate();
		}

		/// <summary>Back to silence, with no peaks held over from a previous run.</summary>
		public void Reset()
		{
			_left = _right = _peakLeft = _peakRight = 0;
			Invalidate();
		}

		/// <summary>What the bars are showing, 0 to 1 after the dB scaling.</summary>
		public double LeftFraction => Fraction(_left);

		public double RightFraction => Fraction(_right);

		protected override void OnPaint(PaintEventArgs e)
		{
			var g = e.Graphics;
			g.Clear(BackColor);

			var barHeight = Math.Max(4, (Height - 3) / 2);
			DrawChannel(g, new Rectangle(0, 0, Width, barHeight), _left, _peakLeft, "L");
			DrawChannel(g, new Rectangle(0, barHeight + 3, Width, barHeight), _right, _peakRight, "R");
		}

		private void DrawChannel(Graphics g, Rectangle bar, double level, double peak, string label)
		{
			using SolidBrush trough = new(SystemColors.ControlDark);
			g.FillRectangle(trough, bar);

			var fraction = Fraction(level);
			var filled = (int)Math.Round(bar.Width * fraction);
			if (filled > 0)
			{
				// Three zones, so "loud" and "about to clip" do not look the same.
				// The boundaries are the ones every meter uses: -12dB and -3dB.
				DrawZone(g, bar, 0, Math.Min(filled, Scaled(bar.Width, -12)), Color.ForestGreen);
				DrawZone(g, bar, Scaled(bar.Width, -12), Math.Min(filled, Scaled(bar.Width, -3)), Color.Goldenrod);
				DrawZone(g, bar, Scaled(bar.Width, -3), filled, Color.Firebrick);
			}

			var peakX = (int)Math.Round(bar.Width * Fraction(peak));
			if (peakX > 0)
			{
				using Pen pen = new(Color.White);
				peakX = Math.Min(peakX, bar.Width - 1);
				g.DrawLine(pen, bar.Left + peakX, bar.Top, bar.Left + peakX, bar.Bottom - 1);
			}

			using SolidBrush text = new(SystemColors.ControlLightLight);
			g.DrawString(label, Font, text, bar.Left + 2, bar.Top - 1);
		}

		private static void DrawZone(Graphics g, Rectangle bar, int from, int to, Color colour)
		{
			if (to <= from) return;
			using SolidBrush brush = new(colour);
			g.FillRectangle(brush, bar.Left + from, bar.Top, to - from, bar.Height);
		}

		/// <summary>Where a decibel value sits along the bar, in pixels.</summary>
		private static int Scaled(int width, double db) => (int)Math.Round(width * (db - FloorDb) / -FloorDb);

		/// <summary>An amplitude as a fraction of the bar, on the dBFS scale.</summary>
		private static double Fraction(double amplitude)
		{
			if (amplitude <= 0) return 0;
			var db = 20.0 * Math.Log10(amplitude);
			if (db <= FloorDb) return 0;
			return Clamp01((db - FloorDb) / -FloorDb);
		}

		private static double Clamp01(double v) => v < 0 ? 0 : v > 1 ? 1 : v;
	}
}
