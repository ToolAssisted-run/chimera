using System.Linq;
using System.Windows.Forms;

using Chimera.Client.Common;
using Chimera.Client.GUI;
using Chimera.Emulation.Common.Waterbox;

namespace Chimera.Tests.Client.GUI
{
	/// <summary>
	/// A slot the machine hides is not merely invisible - the wizard must be
	/// able to LEAVE the file page without it. Dolphin's shape found the hole:
	/// the memory card slot is the GameCube's, a Wii project never renders it,
	/// and the cardinality check walking every DECLARED slot indexed a row
	/// that was never built (KeyNotFoundException on Next).
	/// </summary>
	[TestClass]
	public class WizardHiddenSlotAdvanceTests
	{
		/// <summary>Dolphin's file_slots.json shape: the savedata slot is the GameCube's.</summary>
		private const string Declaration = """
			{
			  "slots": [
			    { "id": "game", "title": "Game", "min": 1, "max": 1,
			      "formats": ["iso", "gcm", "rvz", "dol", "elf"] },
			    { "id": "savedata", "title": "Save data", "min": 0, "max": 2, "formats": ["raw"],
			      "exposedWhen": { "setting": "machine", "is": "gamecube" } }
			  ]
			}
			""";

		private static WaterboxConfig GcOrWii() => new()
		{
			MachineSetting = "machine",
			Machines =
			[
				new() { Id = "GC", Label = "GameCube", When = [ "gamecube" ] },
				new() { Id = "Wii", Label = "Wii", When = [ "wii" ] },
			],
			Settings =
			[
				new WaterboxConfig.SettingDecl
				{
					Name = "machine",
					Type = "enum",
					Options = [ "gamecube", "wii" ],
					Default = "gamecube",
				},
			],
		};

		[TestMethod]
		public void TheFilePageAdvancesWithoutTheHiddenSlot()
		{
			NewProjectWizard form = new([ ], static _ => [ ]);
			form.Show();
			using (form)
			{
				form.UseSettingsFrom(GcOrWii());
				form.SetSettingValue("machine", "wii");
				form.UseDeclaration(ProjectSlotDeclaration.Parse(Declaration));
				Assert.IsFalse(form.SlotRendered("savedata"), "a Wii project has no memory card row");

				form.AddFileToSlot("game", "/somewhere/wii-game.iso");
				var next = form.Controls.OfType<Button>().Single(static b => b.Text.StartsWith("Next"));
				next.PerformClick();

				Assert.AreEqual("", form.StatusText,
					"the hidden slot demands nothing, so the page must advance without complaint");
			}
		}
	}
}
