using System.Windows.Forms;

using Chimera.Client.GUI;
using Chimera.Emulation.Common.Engine;

namespace Chimera.Tests.Client.GUI
{
	/// <summary>
	/// The progress window: what the engine and the frontend report is what it
	/// shows - a length when the stage has one, a clock when it does not - and
	/// its owner cannot be operated while it is up.
	/// </summary>
	[TestClass]
	public class ProgressDialogTests
	{
		[TestMethod]
		public void ItShowsWhatIsReported()
		{
			using Form owner = new();
			owner.Show();
			using (var dialog = ProgressDialog.Begin(owner, "Opening project"))
			{
				Assert.IsFalse(owner.Enabled, "the owner waits with it");
				Assert.IsFalse(dialog.Determinate, "nothing is known yet");

				dialog.Step("reading the project");
				Assert.AreEqual("Reading the project", dialog.StageText);
				Assert.IsFalse(dialog.Determinate);

				EngineProgress.Report("hashing game.iso", 512UL << 20, 2048UL << 20);
				Assert.AreEqual("Hashing game.iso", dialog.StageText);
				Assert.IsTrue(dialog.Determinate, "a length makes a bar");
				Assert.AreEqual(0.25, dialog.Fraction, 0.001);
				StringAssert.Contains(dialog.DetailText, "512 MB of 2.0 GB");

				EngineProgress.Report("loading compiled code", 37, 0);
				Assert.IsFalse(dialog.Determinate, "a count with no end is a clock");
				StringAssert.StartsWith(dialog.DetailText, "37");
			}
			Assert.IsTrue(owner.Enabled, "and the owner is given back");
		}

		[TestMethod]
		public void AReportAfterItIsGoneIsNothing()
		{
			using Form owner = new();
			owner.Show();
			var dialog = ProgressDialog.Begin(owner, "Saving project");
			dialog.Dispose();
			EngineProgress.Report("compressing GreenZone", 1, 2); // must not throw into a disposed window
			Assert.IsTrue(owner.Enabled);
		}

		/// <summary>
		/// Saving a branch is milliseconds on an NES and half a minute on a PS3, from the same
		/// button. Work that is over before a window could be read must not flash one, and work
		/// that lasts must not be left looking like a hang.
		/// </summary>
		[TestMethod]
		public void WorkThatIsOverAtOnceNeverShowsAWindow()
		{
			ProgressDialog dialog;
			using (dialog = ProgressDialog.Begin(null, "Saving the branch", showAfterMs: 400))
			{
				System.Threading.Thread.Sleep(20);
			}
			Assert.IsFalse(dialog.WindowWasShown, "it was over long before it was worth a window");
		}

		[TestMethod]
		public void WorkThatLastsGetsItsWindowAndItGoesAwayAgain()
		{
			ProgressDialog dialog;
			using (dialog = ProgressDialog.Begin(null, "Saving the branch", showAfterMs: 100))
			{
				System.Threading.Thread.Sleep(600);
				Assert.IsTrue(dialog.WindowWasShown, "past the wait, the window is up");
				EngineProgress.Report("saving the machine state", 1UL << 30, 4UL << 30);
				Assert.IsTrue(dialog.Determinate);
				Assert.AreEqual(0.25, dialog.Fraction, 0.001);
			}
		}
	}
}
