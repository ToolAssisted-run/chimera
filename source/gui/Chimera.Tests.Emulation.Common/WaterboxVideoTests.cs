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
			var cfg = Newtonsoft.Json.JsonConvert.DeserializeObject<WaterboxConfig>(
				"{\"video\":{\"drawEveryFrame\":true,\"gpuStatesSurviveTheContext\":true}}");
			Assert.IsTrue(cfg.Video.DrawEveryFrame);
			Assert.IsTrue(cfg.Video.GpuStatesSurviveTheContext);
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
			var silent = Newtonsoft.Json.JsonConvert.DeserializeObject<WaterboxConfig>(
				"{\"video\":{\"width\":640,\"height\":480}}");
			Assert.IsTrue(silent.Video.RebuildOnStateLoad,
				"a package that never mentions it must behave exactly as before");

			var declined = Newtonsoft.Json.JsonConvert.DeserializeObject<WaterboxConfig>(
				"{\"video\":{\"rebuildOnStateLoad\":false}}");
			Assert.IsFalse(declined.Video.RebuildOnStateLoad, "and one that declines is taken at its word");

			var asked = Newtonsoft.Json.JsonConvert.DeserializeObject<WaterboxConfig>(
				"{\"video\":{\"rebuildOnStateLoad\":true}}");
			Assert.IsTrue(asked.Video.RebuildOnStateLoad);
		}
	}
}
