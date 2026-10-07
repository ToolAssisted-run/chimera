using System.Drawing;

using Chimera.Client.GUI;

namespace Chimera.Tests.Client.GUI
{
	/// <summary>
	/// A branch's screenshot is shown no larger than 128x128, proportions kept (issue #183):
	/// at the display's own size it covered the game, or fell off the screen.
	/// </summary>
	[TestClass]
	public class ScreenshotFormTests
	{
		[TestMethod]
		public void ABranchPreviewFitsInside128ByTheLongerSide()
		{
			const int side = ScreenshotForm.BranchPreviewSide;
			Assert.AreEqual(new Size(128, 96), ScreenshotForm.FitWithin(1280, 960, side));   // Xbox at 2x
			Assert.AreEqual(new Size(128, 72), ScreenshotForm.FitWithin(1920, 1080, side));  // a widescreen window
			Assert.AreEqual(new Size(96, 128), ScreenshotForm.FitWithin(720, 960, side));    // taller than wide
			Assert.AreEqual(new Size(128, 1), ScreenshotForm.FitWithin(4000, 10, side));     // never vanishes
		}

		[TestMethod]
		public void ASmallPictureIsNotEnlargedAndAnEmptyOneStaysEmpty()
		{
			Assert.AreEqual(new Size(100, 80), ScreenshotForm.FitWithin(100, 80, ScreenshotForm.BranchPreviewSide));
			Assert.AreEqual(new Size(0, 0), ScreenshotForm.FitWithin(0, 240, ScreenshotForm.BranchPreviewSide));
		}
	}
}
