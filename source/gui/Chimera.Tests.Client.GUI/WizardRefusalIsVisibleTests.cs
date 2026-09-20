using Chimera.Client.Common;
using Chimera.Client.GUI;

namespace Chimera.Tests.Client.GUI
{
	/// <summary>
	/// A wizard that refuses to create the project has to SAY SO.
	///
	/// ShowPage clears the status line, which is right when a person is moving
	/// to a fresh step: the last step's complaint does not belong on the next
	/// one. But every create-time refusal set the text and THEN called
	/// ShowPage, so each one erased its own reason a line later. What reached
	/// the user was a progress dialog that flickered and a wizard that jumped
	/// backwards saying nothing - which is exactly how it was reported, on a
	/// PCem project whose file was perfectly good.
	///
	/// The bug was invisible to every other wizard test because they assert on
	/// what the form DID, and the form did the right thing; it was the
	/// explanation that went missing. So this asks the one question those do
	/// not: after a refusal, is there anything on screen to read?
	/// </summary>
	[TestClass]
	public class WizardRefusalIsVisibleTests
	{
		/// <summary>a cartridge slot that takes exactly one .nes and nothing else</summary>
		private const string Declaration = """
			{
			  "slots": [
			    { "id": "cart", "title": "Cartridge", "min": 1, "max": 1, "formats": ["nes"] }
			  ]
			}
			""";

		private static NewProjectWizard MakeForm()
		{
			NewProjectWizard form = new([ ], static _ => [ ]);
			form.Show();
			form.UseDeclaration(TestPackages.Slots(Declaration));
			return form;
		}

		[TestMethod]
		public void MovingToAFreshStepStillClearsTheLastOnesComplaint()
		{
			using var form = MakeForm();
			form.ShowPageForTest(1);
			Assert.AreEqual(string.Empty, form.StatusText, "a step arrived at cleanly says nothing");
		}

		[TestMethod]
		public void AStepReturnedToBecauseOfAProblemKeepsTheProblemOnScreen()
		{
			using var form = MakeForm();
			form.ShowPageBecauseForTest(1, "slot 'cart' needs at least 1 file");
			Assert.AreEqual(
				"slot 'cart' needs at least 1 file",
				form.StatusText,
				"a refusal that sends somebody back must survive the page change - "
					+ "otherwise the wizard goes backwards for no stated reason");
		}
	}
}
