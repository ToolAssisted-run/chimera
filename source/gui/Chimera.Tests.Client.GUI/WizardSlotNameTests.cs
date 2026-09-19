using Chimera.Client.Common;
using Chimera.Client.GUI;

namespace Chimera.Tests.Client.GUI
{
	/// <summary>
	/// A slot whose files the core reads BY NAME says which names (file_slots.json
	/// namePattern): the wizard refuses the wrong one when it is picked, with the
	/// core's own words, rather than taking it and having the core refuse the whole
	/// project at boot (issue #105: a memory card called anything but
	/// vmu_A1.bin..vmu_D1.bin).
	/// </summary>
	[TestClass]
	public class WizardSlotNameTests
	{
		private const string Declaration = """
			{
			  "slots": [
			    { "id": "disc", "title": "Disc", "min": 1, "max": 1, "formats": ["gdi", "cdi"] },
			    { "id": "savedata", "title": "Save data", "min": 0, "max": 4, "formats": ["bin"],
			      "help": "the cards",
			      "namePattern": "^vmu_[A-D][12]\\.bin$",
			      "nameHelp": "a memory card is read by its name: vmu_A1.bin through vmu_D1.bin" }
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
		public void ARightlyNamedFileIsTaken()
		{
			using var form = MakeForm();
			form.AddFileToSlot("savedata", "/saves/vmu_A1.bin");
			CollectionAssert.AreEqual(new[] { "/saves/vmu_A1.bin" }, (System.Collections.ICollection) form.FilesInSlot("savedata"));
		}

		[TestMethod]
		public void AWronglyNamedFileIsRefusedWithTheRule()
		{
			using var form = MakeForm();
			form.AddFileToSlot("savedata", "/saves/Sonic Adventure.bin");
			Assert.AreEqual(0, form.FilesInSlot("savedata").Count, "the card should not have been taken");
			StringAssert.Contains(form.StatusText, "vmu_A1.bin through vmu_D1.bin");
			StringAssert.StartsWith(form.StatusText, "Sonic Adventure.bin");
		}

		[TestMethod]
		public void ASlotWithoutAPatternTakesAnyName()
		{
			using var form = MakeForm();
			form.AddFileToSlot("disc", "/games/Anything Goes.gdi");
			Assert.AreEqual(1, form.FilesInSlot("disc").Count);
		}

		[TestMethod]
		public void TheParserReadsThePattern()
		{
			var decl = TestPackages.Slots(Declaration);
			var save = System.Linq.Enumerable.First(decl.Slots, static s => s.Id == "savedata");
			Assert.AreEqual("^vmu_[A-D][12]\\.bin$", save.NamePattern);
			Assert.IsNull(System.Linq.Enumerable.First(decl.Slots, static s => s.Id == "disc").NamePattern);
		}
	}
}
