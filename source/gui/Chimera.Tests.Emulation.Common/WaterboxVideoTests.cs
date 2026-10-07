using Chimera.Emulation.Common.Waterbox;

namespace Chimera.Tests.Emulation.Common
{
	/// <summary>
	/// The video declaration's two GPU answers, as this side reads them. Both
	/// are silently harmless when wrong - a core that should keep drawing and
	/// says nothing simply shows an older screen at the end of a seek, which is
	/// exactly the bug it exists for - so the names are pinned rather than
	/// trusted. drawEveryFrame is ACTED on in the engine, which reads the same
	/// key out of the same file; this pins that the two agree on its name.
	/// </summary>
	[TestClass]
	public class WaterboxVideoTests
	{
		[TestMethod]
		public void ADeclaredDrawEveryFrameIsReadFromThePackage()
		{
			WaterboxConfig declared = new() { Video = new() { DrawEveryFrame = true } };
			Assert.IsTrue(declared.Video.DrawEveryFrame);

			// what a package that says nothing means, which is almost all of them
			WaterboxConfig silent = new() { Video = new() };
			Assert.IsFalse(silent.Video.DrawEveryFrame,
				"a core that does not ask to keep drawing must not be made to");
		}

		[TestMethod]
		public void AMissingVideoBlockDrawsLikeEveryOtherCore()
		{
			WaterboxConfig none = new();
			Assert.IsNull(none.Video);
		}

		/// <summary>
		/// The package is JSON and the property names are the contract; a rename
		/// on either side is a declaration that quietly becomes false.
		/// </summary>
		[TestMethod]
		public void TheAnswersAreReadFromTheNamesThePackageUses()
		{
			var video = VideoOf("{\"video\":{\"drawEveryFrame\":true,\"gpuStatesSurviveTheContext\":true}}");
			Assert.IsTrue(video.DrawEveryFrame);
			Assert.IsTrue(video.GpuStatesSurviveTheContext);
		}

		/// <summary>
		/// A core that says nothing about rebuilding on a state load keeps the
		/// behaviour every core had before the question existed (issue #43: a
		/// restore looks like a new context, so a renderer builds its objects
		/// again). Only a core that declares FALSE is changed.
		///
		/// This is the guarantee the whole declaration rests on: adding it must
		/// not move a single other core.
		/// </summary>
		[TestMethod]
		public void ACoreThatSaysNothingStillRebuildsOnAStateLoad()
		{
			Assert.IsTrue(VideoOf("{\"video\":{\"width\":640,\"height\":480}}").RebuildOnStateLoad,
				"a package that never mentions it must behave exactly as before");

			Assert.IsFalse(VideoOf("{\"video\":{\"rebuildOnStateLoad\":false}}").RebuildOnStateLoad,
				"and one that declines is taken at its word");

			Assert.IsTrue(VideoOf("{\"video\":{\"rebuildOnStateLoad\":true}}").RebuildOnStateLoad);
		}

		/// <summary>
		/// The video block of the package this test wrote. A package that did not
		/// parse, or that has no video block after all, is the test's own mistake
		/// and says so rather than failing as a null dereference.
		/// </summary>
		private static WaterboxConfig.VideoConfig VideoOf(string json)
		{
			var cfg = Newtonsoft.Json.JsonConvert.DeserializeObject<WaterboxConfig>(json);
			Assert.IsNotNull(cfg, "the package this test declares did not parse");
			Assert.IsNotNull(cfg.Video, "the package this test declares has no video block");
			return cfg.Video;
		}
	}
}
