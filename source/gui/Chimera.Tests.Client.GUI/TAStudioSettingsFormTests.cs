using System.Linq;
using System.Windows.Forms;

using Chimera.Client.GUI;
using Chimera.Emulation.Common;

namespace Chimera.Tests.Client.GUI
{
	/// <summary>
	/// TAStudio's settings window has a "Branch screenshot size" box on its Misc page (issue 237),
	/// and the box accepts the sizes the setting allows.
	/// </summary>
	/// <remarks>
	/// The window is only built here, not shown: shown without a running TAStudio it closes
	/// itself. So what the box is loaded with and what the Apply button stores are not covered
	/// by this test; ScreenshotFormTests covers the default and the limits the value goes through.
	/// </remarks>
	[TestClass]
	public class TAStudioSettingsFormTests
	{
		[TestMethod]
		public void TheBranchScreenshotSizeBoxIsOnTheMiscPage()
		{
			TAStudio.AllSettings settings = new()
			{
				GeneralClientSettings = new TAStudio.TAStudioSettings(),
				// the patterns are what a running TAStudio fills in for the controller; none are needed here
				MovieSettings = new TAStudio.MovieClientSettings { BoolPatterns = [ ], AxisPatterns = [ ] },
			};
			Assert.AreEqual(320, settings.GeneralClientSettings.BranchScreenshotSide, "the default for new settings");
			var pad = new ControllerDefinition("pad") { BoolButtons = { "A" } }.MakeImmutable();
			using TAStudioSettingsForm form = new(settings, pad, static _ => { });
			var box = (NumericUpDown) form.Controls.Find("BranchScreenshotNum", searchAllChildren: true).Single();
			Assert.AreEqual("Misc", box.Parent.Text);
			Assert.AreEqual((decimal) ScreenshotForm.SmallestBranchPreviewSide, box.Minimum);
			Assert.AreEqual((decimal) ScreenshotForm.LargestBranchPreviewSide, box.Maximum);
		}
	}
}
