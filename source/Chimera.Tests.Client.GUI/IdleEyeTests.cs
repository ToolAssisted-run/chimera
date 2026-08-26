using System.Collections.Generic;
using System.Drawing;
using System.Drawing.Imaging;
using System.IO;

using Chimera.Client.GUI;
using Microsoft.VisualStudio.TestTools.UnitTesting;

namespace Chimera.Tests.Client.GUI
{
	/// <summary>
	/// The idle screen is the first thing anyone sees, and it is pure drawing: no
	/// core, no window, just a buffer. So it is checked the way a buffer can be -
	/// that it is faint, colourless, and that the eye actually moves. Set
	/// CHIMERA_UI_SHOTS to also write a strip of frames to look at.
	/// </summary>
	[TestClass]
	public class IdleEyeTests
	{
		private static string ShotDir => Environment.GetEnvironmentVariable("CHIMERA_UI_SHOTS");

		private static int[] Frames(IdleEyeVideo eye, int count)
		{
			int[] last = null;
			for (var i = 0; i < count; i++) last = (int[]) eye.GetVideoBuffer().Clone();
			return last;
		}

		[TestMethod]
		public void TheIdleScreenIsFaintAndColourless()
		{
			IdleEyeVideo eye = new();
			var frame = Frames(eye, 1);
			var lit = 0;
			var brightest = 0;
			foreach (var pixel in frame)
			{
				var r = (pixel >> 16) & 0xFF;
				var g = (pixel >> 8) & 0xFF;
				var b = pixel & 0xFF;
				Assert.AreEqual(r, g, "the idle eye must have no colour");
				Assert.AreEqual(g, b, "the idle eye must have no colour");
				if (r is not 0) lit++;
				if (r > brightest) brightest = r;
			}
			Assert.IsTrue(lit > 1000, $"the mark should be drawn, but only {lit} pixels are lit");
			Assert.IsTrue(brightest is > 4 and < 48, $"the mark should be barely there, but its brightest pixel is {brightest}");
		}

		/// <summary>
		/// Over a few thousand frames it must both blink (the whole mark shrinks
		/// towards its midline) and glance (only the pupil moves). Without this the
		/// screen is a static logo, which is the thing it is not supposed to be.
		/// </summary>
		[TestMethod]
		public void TheEyeBlinksAndLooksAround()
		{
			IdleEyeVideo eye = new();
			var open = 0;
			var narrowest = int.MaxValue;
			var leftmost = int.MaxValue;
			var rightmost = 0;
			var frameLeft = new HashSet<int>();
			var frameRight = new HashSet<int>();
			for (var i = 0; i < 4000; i++)
			{
				var frame = eye.GetVideoBuffer();
				var (rows, centre) = Measure(frame);
				if (rows is 0) continue;
				if (rows > open) open = rows;
				if (rows < narrowest) narrowest = rows;
				if (centre >= 0)
				{
					if (centre < leftmost) leftmost = centre;
					if (centre > rightmost) rightmost = centre;
				}
				var (edgeL, edgeR) = Edges(frame);
				frameLeft.Add(edgeL);
				frameRight.Add(edgeR);
			}
			Assert.IsTrue(narrowest < open / 2, $"the eye never blinked (it was {narrowest}..{open} rows tall)");
			Assert.IsTrue(rightmost - leftmost > 8, $"the eye never looked aside (its eye stayed within {rightmost - leftmost}px)");

			// the WHOLE oval moves, not just the pupil inside it: the mark's outer
			// slabs must not have shifted with it
			Assert.AreEqual(1, frameLeft.Count, $"the mark's left edge moved with the eye ({string.Join(",", frameLeft)})");
			Assert.AreEqual(1, frameRight.Count, $"the mark's right edge moved with the eye ({string.Join(",", frameRight)})");
		}

		/// <summary>the outermost lit columns of the mark: its frame, which never moves</summary>
		private static (int Left, int Right) Edges(int[] frame)
		{
			const int W = 480;
			var left = -1;
			var right = -1;
			for (var x = 0; x < W; x++)
			{
				for (var y = 0; y < frame.Length / W; y++)
				{
					if ((frame[(y * W) + x] & 0xFF) is 0) continue;
					if (left < 0) left = x;
					right = x;
					break;
				}
			}
			return (left, right);
		}

		/// <summary>rows of the mark that are lit, and the x of the brightest run on the middle row</summary>
		private static (int Rows, int Centre) Measure(int[] frame)
		{
			const int W = 480;
			var height = frame.Length / W;
			var rows = 0;
			var first = -1;
			var last = -1;
			for (var y = 0; y < height; y++)
			{
				var any = false;
				for (var x = 0; x < W; x++)
				{
					if ((frame[(y * W) + x] & 0xFF) is not 0) { any = true; break; }
				}
				if (!any) continue;
				rows++;
				if (first < 0) first = y;
				last = y;
			}
			if (rows is 0) return (0, -1);

			// the pupil is the brightest thing on the mark's middle row
			var mid = (first + last) / 2;
			var best = 0;
			var bestX = -1;
			for (var x = 0; x < W; x++)
			{
				var v = frame[(mid * W) + x] & 0xFF;
				if (v > best) { best = v; bestX = x; }
			}
			return (rows, bestX);
		}

		[TestMethod]
		public void WriteFrameStrip()
		{
			if (ShotDir is null) { Assert.Inconclusive("set CHIMERA_UI_SHOTS to write screenshots"); return; }

			IdleEyeVideo eye = new();
			Directory.CreateDirectory(ShotDir);
			var wanted = new HashSet<int>();
			var shot = 0;
			var lastRows = -1;
			for (var i = 0; i < 4000 && shot < 12; i++)
			{
				var frame = eye.GetVideoBuffer();
				var (rows, _) = Measure(frame);
				if (rows != lastRows || wanted.Add(i / 137))
				{
					lastRows = rows;
					Save(frame, Path.Combine(ShotDir, $"idle-eye-{shot++:D2}.png"));
				}
			}
		}

		private static void Save(int[] frame, string path)
		{
			const int W = 480;
			var h = frame.Length / W;
			using Bitmap bmp = new(W, h, PixelFormat.Format32bppArgb);
			var data = bmp.LockBits(new Rectangle(0, 0, W, h), ImageLockMode.WriteOnly, PixelFormat.Format32bppArgb);
			System.Runtime.InteropServices.Marshal.Copy(frame, 0, data.Scan0, frame.Length);
			bmp.UnlockBits(data);
			bmp.Save(path, ImageFormat.Png);
		}
	}
}
