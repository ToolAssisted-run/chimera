using Chimera.Client.Common;
using Chimera.Client.GUI;

namespace Chimera.Tests.Client.GUI
{
	/// <summary>
	/// Adaptation within the file step itself: a slot's exposedWhen is
	/// evaluated over the current picks, so providing a Famicom disk greys
	/// the cartridge slot until the disk is unloaded, and vice versa. An
	/// unavailable slot's minimum does not bind - two mutually exclusive
	/// min-1 slots together mean "exactly one of either".
	/// </summary>
	[TestClass]
	public class WizardSlotGateTests
	{
		private const string Declaration = """
			{
			  "slots": [
			    { "id": "cart", "title": "Cartridge", "min": 1, "max": 1, "formats": ["nes"],
			      "exposedWhen": { "not": { "slot": "fds" } } },
			    { "id": "fds", "title": "Famicom disk", "min": 1, "max": 1, "formats": ["fds"],
			      "exposedWhen": { "not": { "slot": "cart" } } }
			  ]
			}
			""";

		private static NewProjectWizard MakeForm()
		{
			NewProjectWizard form = new([ ], static _ => [ ]);
			form.Show();
			form.UseDeclaration(ProjectSlotDeclaration.Parse(Declaration));
			return form;
		}

		[TestMethod]
		public void FillingOneSlotRulesOutTheOther()
		{
			using var form = MakeForm();
			Assert.IsTrue(form.SlotAvailable("cart"), "empty form: everything on offer");
			Assert.IsTrue(form.SlotAvailable("fds"));

			form.AddFileToSlot("fds", "/games/zelda.fds");
			Assert.IsFalse(form.SlotAvailable("cart"), "a disk rules out a cartridge");
			Assert.IsTrue(form.SlotAvailable("fds"));

			form.AddFileToSlot("cart", "/games/smb.nes");
			CollectionAssert.AreEqual(new string[ ] { }, form.SlotFileNames("cart"), "an unavailable slot refuses files");
		}

		[TestMethod]
		public void UnloadingRestoresTheChoice()
		{
			using var form = MakeForm();
			form.AddFileToSlot("fds", "/games/zelda.fds");
			Assert.IsFalse(form.SlotAvailable("cart"));

			form.RemoveFileFromSlot("fds", "zelda.fds");
			Assert.IsTrue(form.SlotAvailable("cart"), "unloading the disk brings the cartridge back");

			form.AddFileToSlot("cart", "/games/smb.nes");
			Assert.IsFalse(form.SlotAvailable("fds"), "and vice versa");
		}

		[TestMethod]
		public void AnUnavailableSlotsMinimumDoesNotBind()
		{
			using var form = MakeForm();
			Assert.IsNotNull(form.FilesComplaint(), "nothing chosen: some minimum is owed");

			form.AddFileToSlot("fds", "/games/zelda.fds");
			Assert.IsNull(form.FilesComplaint(), "one of either satisfies both minimums - the greyed slot owes nothing");
		}
	}
}
