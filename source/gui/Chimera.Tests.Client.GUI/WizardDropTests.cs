using System.Collections.Generic;

using Chimera.Client.Common;
using Chimera.Client.GUI;

namespace Chimera.Tests.Client.GUI
{
	/// <summary>
	/// Files arriving by drag rather than through a dialog.
	///
	/// Two questions have to be answered without asking: which core a dropped
	/// file is for, and which of that core's slots it belongs in. Both are
	/// answered out of what the PACKAGES declare - the extensions they handle and
	/// the formats each slot takes - so a core installed tomorrow is understood
	/// without a table here changing.
	/// </summary>
	[TestClass]
	public class WizardDropTests
	{
		private const string Declaration = """
			{
			  "slots": [
			    { "id": "disc", "title": "Disc", "min": 1, "max": 1, "formats": ["cue", "chd", "iso"] },
			    { "id": "cards", "title": "Memory cards", "min": 0, "max": 2, "formats": ["mcd"] }
			  ]
			}
			""";

		private static DiscoveredCorePackage Package(string name, params string[] extensions)
		{
			Dictionary<string, string> map = new();
			foreach (var extension in extensions) map[extension] = name;
			return new DiscoveredCorePackage { Name = name, Path = $"/cores/{name}.chimeraCore", Extensions = map };
		}

		private static NewProjectWizard MakeForm(IReadOnlyList<DiscoveredCorePackage> cores = null)
		{
			NewProjectWizard form = new(cores ?? [ ], static _ => [ ]);
			form.Show();
			form.UseDeclaration(ProjectSlotDeclaration.Parse(Declaration));
			return form;
		}

		[TestMethod]
		public void EveryFileListTakesADrop()
		{
			using var form = MakeForm();
			Assert.IsTrue(form.SlotAcceptsDrops("disc"), "the list is the target, so it accepts what lands on it");
			Assert.IsTrue(form.SlotAcceptsDrops("cards"));
		}

		[TestMethod]
		public void AFileGoesToTheSlotThatDeclaresItsFormat()
		{
			using var form = MakeForm();
			Assert.AreEqual("disc", form.SlotForFile("/games/revolt.chd"));
			Assert.AreEqual("cards", form.SlotForFile("/saves/slot1.mcd"),
				"a memory card is not a disc, and the formats say so");
		}

		[TestMethod]
		public void AFileNobodyDeclaresGoesToTheFirstSlot()
		{
			using var form = MakeForm();
			Assert.AreEqual("disc", form.SlotForFile("/games/mystery.bin"),
				"a guess has to land somewhere, and the first slot is the one a person expects");
		}

		[TestMethod]
		public void TheCoreIsGuessedFromWhatThePackagesClaim()
		{
			using var form = MakeForm([ Package("Genesis Plus GX", ".gg", ".md"), Package("Flycast", ".chd", ".gdi") ]);
			Assert.AreEqual(0, form.GuessCoreIndexFor("/games/sonic.gg"));
			Assert.AreEqual(1, form.GuessCoreIndexFor("/games/revolt.chd"));
			Assert.AreEqual(1, form.GuessCoreIndexFor("/GAMES/REVOLT.CHD"), "an extension is not case");
		}

		[TestMethod]
		public void AnExtensionNobodyClaimsIsNotGuessedAt()
		{
			using var form = MakeForm([ Package("Genesis Plus GX", ".gg") ]);
			Assert.AreEqual(-1, form.GuessCoreIndexFor("/games/thing.xyz"));
			Assert.AreEqual(-1, form.GuessCoreIndexFor("/games/no-extension"));
		}

		[TestMethod]
		public void AnUnexpectedFormatIsSaidRatherThanRefused()
		{
			using var form = MakeForm();
			form.AddFileToSlot("disc", "/games/revolt.chd");
			Assert.AreEqual("", form.StatusText, "a format the slot declares passes without comment");

			form.AddFileToSlot("cards", "/games/revolt.iso");
			CollectionAssert.AreEqual(new[] { "revolt.iso" }, form.SlotFileNames("cards"),
				"the file is still added: the picker offers All files too, and a renamed file is the person's business");
			StringAssert.Contains(form.StatusText, ".mcd", "but the slot says what it expected");
		}
	}
}
