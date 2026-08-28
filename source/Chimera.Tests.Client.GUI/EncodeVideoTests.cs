using System;
using System.Collections.Generic;
using System.Drawing;
using System.Linq;

using Chimera.Client.Common;
using Chimera.Client.GUI;

namespace Chimera.Tests.Client.GUI
{
	/// <summary>
	/// Encode Video, the window that turns a run into a file.
	///
	/// It replaced three commands that had to be assembled in the right order, so
	/// what these check is that nothing is left to assemble: the window opens on a
	/// whole run, the stretch is named by markers, the ffmpeg command decides the
	/// extension, and the encode belongs to the window rather than outliving it.
	/// </summary>
	[TestClass]
	public class EncodeVideoTests
	{
		private static List<TasMovieMarker> Markers()
			=> new()
			{
				new(0, "Run start", MarkerPermanence.RunStart),
				new(120, "the door"),
				new(900, "Last input", MarkerPermanence.LastInput),
				new(1000, "Run end", MarkerPermanence.RunEnd),
			};

		private sealed class Harness : IDisposable
		{
			internal readonly List<VideoEncodeRequest> Started = new();
			internal int Cancels;
			internal VideoEncodeProgress Progress = new(VideoEncodePhase.Idle, 0, 0, 0, 0, null, null);
			internal string Refusal;
			internal readonly EncodeVideoForm Form;

			internal Harness(IReadOnlyList<TasMovieMarker> markers = null, Config config = null)
			{
				Form = new(
					markers ?? Markers(),
					new Size(320, 240),
					config ?? new Config(),
					"/tmp/run.mp4",
					request =>
					{
						if (Refusal is not null) return Refusal;
						Started.Add(request);
						Progress = new(VideoEncodePhase.Encoding, 0, request.FrameCount, request.StartFrame, 0, null, null);
						return null;
					},
					() => Cancels++,
					() => Progress,
					current => current);
				Form.Show();
			}

			public void Dispose() => Form.Dispose();
		}

		[TestMethod]
		public void ItOpensOnTheWholeRun()
		{
			using Harness h = new();
			Assert.AreEqual(0, h.Form.StartFrame, "a run starts where it starts");
			Assert.AreEqual(1000, h.Form.EndFrame, "and a video of it ends where it ends");
		}

		[TestMethod]
		public void EveryMarkerIsOfferedAtBothEnds()
		{
			using Harness h = new();
			CollectionAssert.AreEqual(
				new[] { "0  Run start", "120  the door", "900  Last input", "1000  Run end" },
				h.Form.MarkerChoices,
				"the run's own markers and the user's, in frame order");
		}

		[TestMethod]
		public void TheStretchIsWhicheverTwoMarkersAreChosen()
		{
			using Harness h = new();
			h.Form.Choose(from: "120  the door", to: "900  Last input");
			Assert.AreEqual(120, h.Form.StartFrame);
			Assert.AreEqual(900, h.Form.EndFrame);
			Assert.IsTrue(h.Form.StartEnabled);
		}

		[TestMethod]
		public void AnEndBeforeAStartCannotBeEncoded()
		{
			using Harness h = new();
			h.Form.Choose(from: "1000  Run end", to: "120  the door");
			Assert.IsFalse(h.Form.StartEnabled, "there is nothing between them to encode");
		}

		[TestMethod]
		public void TheCommandDecidesTheExtension()
		{
			using Harness h = new();
			h.Form.Choose(format: "Matroska Lossless");
			StringAssert.EndsWith(h.Form.OutputPath, ".mkv",
				"the container the command names is the one the file should say it is");

			h.Form.Choose(format: "WebM");
			StringAssert.EndsWith(h.Form.OutputPath, ".webm");
		}

		[TestMethod]
		public void WhatItAsksForIsWhatWasFilledIn()
		{
			using Harness h = new();
			h.Form.Choose(from: "120  the door", to: "900  Last input", output: "/tmp/mine.mp4", format: "MP4");
			h.Form.StartEncode();

			var request = h.Started.Single();
			Assert.AreEqual("/tmp/mine.mp4", request.OutputPath);
			Assert.AreEqual(120, request.StartFrame);
			Assert.AreEqual(900, request.EndFrame);
			Assert.AreEqual(781, request.FrameCount, "both ends are in the video");
			StringAssert.Contains(request.FFmpegCommand, "libx264");
		}

		[TestMethod]
		public void ARefusedStartIsSaidRatherThanSwallowed()
		{
			using Harness h = new();
			h.Refusal = "There is no folder /nope.";
			h.Form.StartEncode();

			Assert.AreEqual(0, h.Started.Count);
			Assert.AreEqual("There is no folder /nope.", h.Form.StatusText);
			Assert.IsTrue(h.Form.StartEnabled, "and it can be tried again");
		}

		[TestMethod]
		public void WhileItRunsNothingCanBeChangedUnderIt()
		{
			using Harness h = new();
			h.Form.StartEncode();
			Assert.IsFalse(h.Form.StartEnabled, "no starting it twice");
			Assert.IsTrue(h.Form.StopEnabled, "and it can be stopped");
		}

		[TestMethod]
		public void ClosingTheWindowStopsTheEncode()
		{
			Harness h = new();
			h.Form.StartEncode();
			h.Form.Close();

			Assert.AreEqual(1, h.Cancels,
				"the encode lives inside this window; closing it does not leave one running");
			h.Dispose();
		}

		[TestMethod]
		public void ClosingWithoutEncodingStopsNothing()
		{
			Harness h = new();
			h.Form.Close();
			Assert.AreEqual(0, h.Cancels);
			h.Dispose();
		}

		/// <summary>A picture of the window, for eyes. Set CHIMERA_UI_SHOTS to get one.</summary>
		[TestMethod]
		public void PictureIt()
		{
			var dir = Environment.GetEnvironmentVariable("CHIMERA_UI_SHOTS");
			if (dir is null) { Assert.Inconclusive("set CHIMERA_UI_SHOTS to write screenshots"); return; }

			using Harness h = new();
			h.Form.Refresh();
			System.Windows.Forms.Application.DoEvents();
			using System.Drawing.Bitmap bmp = new(h.Form.Width, h.Form.Height);
			using (var g = System.Drawing.Graphics.FromImage(bmp))
			{
				g.CopyFromScreen(h.Form.Location, System.Drawing.Point.Empty, h.Form.Size);
			}
			System.IO.Directory.CreateDirectory(dir);
			bmp.Save(System.IO.Path.Combine(dir, "encode-video.png"), System.Drawing.Imaging.ImageFormat.Png);
		}

		[TestMethod]
		public void ACommandWithoutAContainerLeavesTheNameAlone()
		{
			Assert.IsNull(EncodeVideoForm.ExtensionOf("-c:v libx264"));
			Assert.AreEqual("mkv", EncodeVideoForm.ExtensionOf("-c:a pcm_s16le -f matroska"),
				"matroska is spelled mkv on a file");
			Assert.AreEqual("avi", EncodeVideoForm.ExtensionOf("-c:v utvideo -f avi"));
		}
	}
}
