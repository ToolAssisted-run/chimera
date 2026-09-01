using System.Drawing;
using System.Drawing.Imaging;

using Chimera.Emulation.Common;

namespace Chimera.Client.GUI
{
	/// <summary>
	/// What the screen shows when no project is open: the Chimera mark, very faint
	/// and colourless, glancing about and blinking now and then. It is the idle
	/// screen, so it must cost nothing - the frame is redrawn only while something
	/// is actually moving, and held otherwise.
	/// </summary>
	public sealed class IdleEyeVideo : IVideoProvider
	{
		private const int Width = 480;

		private const int Height = 360;

		/// <summary>how bright the mark gets at its brightest pixel, out of 255</summary>
		private const float Opacity = 0.10f;

		/// <summary>the mark's height as a fraction of the screen</summary>
		private const float MarkScale = 0.62f;

		/// <summary>
		/// The eye within the mark, in the source art: the central oval and the dark
		/// pupil inside it. It is what moves - as one piece, the way an eye moves -
		/// and the four slabs around it are the face it moves in. These bounds sit in
		/// the gap between the oval and those slabs, so the box holds the oval and
		/// nothing else.
		/// </summary>
		private const float EyeLeft = 62f / 160f;

		private const float EyeRight = 98f / 160f;

		private const float EyeTop = 41f / 160f;

		private const float EyeBottom = 118f / 160f;

		/// <summary>below this the art is pupil rather than iris</summary>
		private const float PupilDarkerThan = 0.35f;

		/// <summary>how far the oval may slide, in source pixels, before it crowds the slabs</summary>
		private const float MaxGaze = 12f;

		private readonly int[] _buffer = new int[Width * Height];

		private readonly Random _rng = new(Seed: 0x0E7E); // "eye"

		/// <summary>the mark without its eye, and the eye alone: source intensity, 0..1</summary>
		private float[] _still;

		private float[] _eye;

		private int _srcSize;

		/// <summary>1 is wide awake, 0 is shut</summary>
		private float _lid = 1f;

		/// <summary>-1 is hard left, +1 is hard right</summary>
		private float _gaze;

		/// <summary>where the eye was last told to look; it eases there on its own</summary>
		private float _gazeTarget;

		/// <summary>frames until the next thing the eye decides to do</summary>
		private int _wait = 120;

		private int _step;

		private int _stepsTotal;

		private Act _doing = Act.Waiting;

		private long _frame;

		private int _drawnKey = -1;

		private enum Act
		{
			Waiting,
			Closing,
			Opening,
		}

		/// <summary>
		/// Tells the eye where to look, -1 hard left to +1 hard right. The caller
		/// reports the pointer; the eye does the moving - a quick slide that
		/// settles, the way a glance lands. Off-screen positions just peg the
		/// gaze, which is what an eye watching someone leave the room does.
		/// </summary>
		public void LookAt(float gaze)
			=> _gazeTarget = Math.Max(-1f, Math.Min(1f, gaze));

		public int VirtualWidth => Width;

		public int VirtualHeight => Height;

		public int BufferWidth => Width;

		public int BufferHeight => Height;

		public int BackgroundColor => 0;

		public int VsyncNumerator => 60;

		public int VsyncDenominator => 1;

		public int[] GetVideoBuffer()
		{
			Animate();
			// the eye is still most of the time, and a still eye is the same picture:
			// only redraw when what would be drawn has actually changed
			var key = ((int) (_lid * 255f) << 16) | (((int) ((_gaze + 1f) * 127f) & 0xFF) << 8) | Breath();
			if (key != _drawnKey)
			{
				_drawnKey = key;
				Draw();
			}
			return _buffer;
		}

		/// <summary>A slow rise and fall in brightness, so a still eye is not a dead one. 0..15.</summary>
		private int Breath()
			=> (int) (7.5 * (1.0 + Math.Sin(_frame * 2.0 * Math.PI / 420.0)));

		private void Animate()
		{
			_frame++;
			// the pupil follows wherever it was last pointed, easing in so the
			// glance lands rather than teleports; once settled it snaps, the
			// redraw key stops changing, and the idle screen goes back to free
			var d = _gazeTarget - _gaze;
			_gaze = Math.Abs(d) < 0.004f ? _gazeTarget : _gaze + (d * 0.18f);
			switch (_doing)
			{
				case Act.Waiting:
					if (--_wait > 0) break;
					Begin(Act.Closing, _rng.Next(3, 6));
					break;
				case Act.Closing:
					_lid = 1f - Ease();
					if (Done()) Begin(Act.Opening, _rng.Next(4, 8));
					break;
				case Act.Opening:
					_lid = Ease();
					if (Done())
					{
						_lid = 1f;
						// blinks sometimes come in pairs, the way real ones do
						if (_rng.Next(100) < 22) Begin(Act.Closing, _rng.Next(3, 6));
						else Wait();
					}
					break;
			}
		}

		private void Begin(Act act, int steps)
		{
			_doing = act;
			_step = 0;
			_stepsTotal = Math.Max(1, steps);
		}

		private void Wait()
		{
			_doing = Act.Waiting;
			_wait = _rng.Next(90, 320);
		}

		private bool Done()
			=> ++_step >= _stepsTotal;

		private float Ease()
			=> Smooth(Math.Min(1f, (float) _step / _stepsTotal));

		private static float Smooth(float t)
			=> t * t * (3f - (2f * t));

		private void Draw()
		{
			Load();
			Array.Clear(_buffer, 0, _buffer.Length);
			if (_lid <= 0.01f) return;

			var mark = Height * MarkScale;
			var left = (Width - mark) / 2f;
			var top = (Height - mark) / 2f;
			var midline = top + (mark / 2f);
			var scale = _srcSize / mark;
			var gaze = _gaze * MaxGaze;
			var bright = 255f * Opacity * (0.85f + (Breath() / 100f));

			// the lid squashes the mark towards its midline, so more source rows fall
			// into each drawn row the further it closes: average them, or a closing eye
			// crawls with aliasing
			var rows = Math.Min(8, Math.Max(1, (int) Math.Ceiling(1f / _lid)));
			var y0 = Math.Max(0, (int) (midline - (mark * _lid / 2f)) - 1);
			var y1 = Math.Min(Height - 1, (int) (midline + (mark * _lid / 2f)) + 1);
			var x0 = Math.Max(0, (int) left - 1);
			var x1 = Math.Min(Width - 1, (int) (left + mark) + 1);

			for (var y = y0; y <= y1; y++)
			{
				var rowBase = y * Width;
				for (var x = x0; x <= x1; x++)
				{
					var sx = (x + 0.5f - left) * scale;
					var value = 0f;
					for (var s = 0; s < rows; s++)
					{
						var dy = y + ((s + 0.5f) / rows);
						var sy = ((dy - midline) / _lid) + (mark / 2f);
						sy *= scale;
						value += Sample(_still, sx, sy) + Sample(_eye, sx - gaze, sy);
					}
					value /= rows;
					if (value <= 0.004f) continue;

					var level = (int) (value * bright);
					if (level > 255) level = 255;
					_buffer[rowBase + x] = unchecked((int) 0xFF000000) | (level << 16) | (level << 8) | level;
				}
			}
		}

		/// <summary>Bilinear read of a source mask, in source pixels; outside is nothing.</summary>
		private float Sample(float[] mask, float x, float y)
		{
			x -= 0.5f;
			y -= 0.5f;
			var xi = (int) Math.Floor(x);
			var yi = (int) Math.Floor(y);
			if (xi < -1 || yi < -1 || xi >= _srcSize || yi >= _srcSize) return 0f;
			var fx = x - xi;
			var fy = y - yi;
			return (At(mask, xi, yi) * (1f - fx) * (1f - fy))
				+ (At(mask, xi + 1, yi) * fx * (1f - fy))
				+ (At(mask, xi, yi + 1) * (1f - fx) * fy)
				+ (At(mask, xi + 1, yi + 1) * fx * fy);
		}

		private float At(float[] mask, int x, int y)
			=> x < 0 || y < 0 || x >= _srcSize || y >= _srcSize ? 0f : mask[(y * _srcSize) + x];

		/// <summary>
		/// Reads the mark once and splits it in two: the eye - the central oval with
		/// its pupil - which moves, and everything else, which does not. Colour is
		/// dropped here: the idle screen is monochrome on purpose, so the mark reads
		/// as an absence of light rather than as a logo someone left on the screen.
		/// </summary>
		private void Load()
		{
			if (_still is not null) return;

			var art = Properties.Resources.Chimera;
			_srcSize = Math.Min(art.Width, art.Height);
			_still = new float[_srcSize * _srcSize];
			_eye = new float[_srcSize * _srcSize];

			var data = art.LockBits(
				new Rectangle(0, 0, _srcSize, _srcSize),
				ImageLockMode.ReadOnly,
				PixelFormat.Format32bppArgb);
			try
			{
				var row = new byte[data.Stride];
				for (var y = 0; y < _srcSize; y++)
				{
					System.Runtime.InteropServices.Marshal.Copy(data.Scan0 + (y * data.Stride), row, 0, data.Stride);
					var v = y / (float) _srcSize;
					for (var x = 0; x < _srcSize; x++)
					{
						var b = row[x * 4] / 255f;
						var g = row[(x * 4) + 1] / 255f;
						var r = row[(x * 4) + 2] / 255f;
						var a = row[(x * 4) + 3] / 255f;
						if (a <= 0f) continue;

						// keep some of the art's own shading, so the blocks do not
						// flatten into one silhouette
						var lum = (r + g + b) / 3f;
						// keep some of the art's own shading, so the blocks do not
						// flatten into one silhouette
						var shaded = a * (0.40f + (0.45f * lum));
						var u = x / (float) _srcSize;
						if (u is >= EyeLeft and <= EyeRight && v is >= EyeTop and <= EyeBottom)
						{
							// the pupil is the darkest part of the art, and dark cannot
							// be seen against black: on the idle screen it is the
							// BRIGHTEST part, so the oval does not slide about as a
							// hollow ring
							_eye[(y * _srcSize) + x] = lum < PupilDarkerThan ? a * 0.8f : shaded;
						}
						else
						{
							_still[(y * _srcSize) + x] = shaded;
						}
					}
				}
			}
			finally
			{
				art.UnlockBits(data);
			}
		}
	}
}
