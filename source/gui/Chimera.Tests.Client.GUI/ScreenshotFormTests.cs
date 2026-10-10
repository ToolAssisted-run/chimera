using System.Drawing;

using Chimera.Client.GUI;

namespace Chimera.Tests.Client.GUI
{
	/// <summary>
	/// A branch's screenshot is shown no larger than a set size on its longer side, proportions
	/// kept: 320 pixels unless the user sets another (issue 237; it was 128 since issue 183, when
	/// a picture at the display's own size covered the game or fell off the screen).
	/// </summary>
	[TestClass]
	public class ScreenshotFormTests
	{
		[TestMethod]
		public void ABranchPreviewFitsInsideTheSizeByTheLongerSide()
		{
			Assert.AreEqual(320, ScreenshotForm.BranchPreviewSide);
			const int side = ScreenshotForm.BranchPreviewSide;
			Assert.AreEqual(new Size(320, 240), ScreenshotForm.FitWithin(1280, 960, side));   // Xbox at 2x
			Assert.AreEqual(new Size(320, 180), ScreenshotForm.FitWithin(1920, 1080, side));  // a widescreen window
			Assert.AreEqual(new Size(240, 320), ScreenshotForm.FitWithin(720, 960, side));    // taller than wide
			Assert.AreEqual(new Size(320, 1), ScreenshotForm.FitWithin(4000, 10, side));      // never vanishes
			Assert.AreEqual(new Size(128, 96), ScreenshotForm.FitWithin(1280, 960, 128));     // a smaller setting
		}

		[TestMethod]
		public void ASmallPictureIsNotEnlargedAndAnEmptyOneStaysEmpty()
		{
			Assert.AreEqual(new Size(100, 80), ScreenshotForm.FitWithin(100, 80, ScreenshotForm.BranchPreviewSide));
			Assert.AreEqual(new Size(240, 320), ScreenshotForm.FitWithin(240, 320, ScreenshotForm.BranchPreviewSide));  // an N-Gage, whole
			Assert.AreEqual(new Size(0, 0), ScreenshotForm.FitWithin(0, 240, ScreenshotForm.BranchPreviewSide));
		}

		/// <summary>
		/// The stored setting: a config file from before the setting existed holds 0 and gets the
		/// default, and a value outside the limits is brought inside them.
		/// </summary>
		[TestMethod]
		public void TheSettingHasADefaultAndLimits()
		{
			Assert.AreEqual(320, ScreenshotForm.BranchPreviewSideFor(0));
			Assert.AreEqual(512, ScreenshotForm.BranchPreviewSideFor(512));
			Assert.AreEqual(ScreenshotForm.SmallestBranchPreviewSide, ScreenshotForm.BranchPreviewSideFor(8));
			Assert.AreEqual(ScreenshotForm.LargestBranchPreviewSide, ScreenshotForm.BranchPreviewSideFor(100000));
		}

		/// <summary>The window shows the picture and nothing under it: no caption (issue 237).</summary>
		[TestMethod]
		public void ThePopupIsThePictureAlone()
		{
			using ScreenshotForm form = new();
			Chimera.Display.BitmapBuffer picture = new(64, 48, new int[64 * 48]);
			form.UpdateValues(picture, new Point(0, 0), width: 64, height: 48);
			Assert.AreEqual(0, form.Padding);
		}
	}
}
