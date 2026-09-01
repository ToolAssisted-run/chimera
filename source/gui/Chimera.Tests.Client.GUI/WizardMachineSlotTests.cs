using System.Collections.Generic;

using Chimera.Client.Common;
using Chimera.Client.GUI;
using Chimera.Emulation.Common.Waterbox;

namespace Chimera.Tests.Client.GUI
{
	/// <summary>
	/// One package that is several machines offers each machine only its own kind
	/// of file. Genesis Plus GX is a Mega Drive, a Sega CD, a Master System, a Game
	/// Gear and an SG-1000, and choosing Game Gear should ask for a Game Gear
	/// cartridge and nothing else - not every extension the package can read, and
	/// not a CD drive the machine does not have.
	/// </summary>
	[TestClass]
	public class WizardMachineSlotTests
	{
		/// <summary>The shape gpgx's file_slots.json declares, one slot per machine.</summary>
		private const string Declaration = """
			{
			  "slots": [
			    { "id": "cart_md", "title": "Cartridge", "min": 1, "max": 1,
			      "formats": ["md", "gen", "bin", "smd", "mdx"],
			      "exposedWhen": { "setting": "systemHardware", "is": "genesis" } },
			    { "id": "cart_sms", "title": "Cartridge", "min": 1, "max": 1, "formats": ["sms"],
			      "exposedWhen": { "setting": "systemHardware", "is": "sms" } },
			    { "id": "cart_gg", "title": "Cartridge", "min": 1, "max": 1, "formats": ["gg"],
			      "exposedWhen": { "setting": "systemHardware", "is": "gg" } },
			    { "id": "cart_sg", "title": "Cartridge", "min": 1, "max": 1, "formats": ["sg"],
			      "exposedWhen": { "setting": "systemHardware", "is": "sg" } },
			    { "id": "cd", "title": "Sega CD discs", "min": 1, "max": -1, "formats": ["cue", "iso", "bin"],
			      "exposedWhen": { "setting": "systemHardware", "is": "segacd" } }
			  ]
			}
			""";

		private static readonly string[] EverySlot = [ "cart_md", "cart_sms", "cart_gg", "cart_sg", "cd" ];

		private static NewProjectWizard MakeForm(string machine)
		{
			NewProjectWizard form = new([ ], static _ => [ ]);
			form.Show();
			form.UseSettingsDecls(
			[
				new WaterboxConfig.SettingDecl
				{
					Name = "systemHardware",
					Type = "enum",
					Options = [ "genesis", "segacd", "sms", "gg", "sg" ],
					Default = "genesis",
				},
			]);
			form.SetSettingValue("systemHardware", machine);
			form.UseDeclaration(ProjectSlotDeclaration.Parse(Declaration));
			return form;
		}

		private static void OnlyOneOnOffer(string machine, string expected)
		{
			using var form = MakeForm(machine);
			foreach (var slot in EverySlot)
			{
				Assert.AreEqual(slot == expected, form.SlotAvailable(slot),
					$"{machine} should offer {expected} and nothing else, but {slot} disagreed");
			}
		}

		[TestMethod]
		public void EachMachineAsksForItsOwnCartridge()
		{
			OnlyOneOnOffer("genesis", "cart_md");
			OnlyOneOnOffer("sms", "cart_sms");
			OnlyOneOnOffer("gg", "cart_gg");
			OnlyOneOnOffer("sg", "cart_sg");
		}

		[TestMethod]
		public void SegaCDIsTheMachineThatTakesDiscs()
			=> OnlyOneOnOffer("segacd", "cd");

		[TestMethod]
		public void AMachineIsNotOfferedTheOthersFormats()
		{
			using var form = MakeForm("gg");
			var offered = new List<string>();
			foreach (var slot in EverySlot)
			{
				if (form.SlotAvailable(slot)) offered.Add(slot);
			}

			CollectionAssert.AreEqual(new[] { "cart_gg" }, offered,
				"a Game Gear takes .gg, which is the whole of what its slot declares");
		}

		[TestMethod]
		public void AnotherMachinesSlotIsNotEvenShown()
		{
			// not greyed: absent. A Game Gear form that lists a Mega Drive
			// cartridge row explains nothing a person can act on.
			using var form = MakeForm("gg");
			foreach (var slot in EverySlot)
			{
				Assert.AreEqual(slot == "cart_gg", form.SlotRendered(slot),
					$"a gg machine should render cart_gg alone, but {slot} disagreed");
			}
		}

		[TestMethod]
		public void ASlotGatedByFilesStaysVisible()
		{
			// the famicom shape: a disk rules out a cartridge and vice versa.
			// Both are exposed while nothing is picked - and both must stay ON
			// THE FORM afterwards, because unloading brings the other back.
			const string declaration = """
				{
				  "slots": [
				    { "id": "cart", "title": "Cartridge", "min": 0, "max": 1, "formats": ["nes"],
				      "exposedWhen": { "not": { "slot": "disk" } } },
				    { "id": "disk", "title": "Disk", "min": 0, "max": 1, "formats": ["fds"],
				      "exposedWhen": { "not": { "slot": "cart" } } }
				  ]
				}
				""";
			NewProjectWizard form = new([ ], static _ => [ ]);
			using var _ = form;
			form.Show();
			form.UseSettingsDecls([ ]);
			form.UseDeclaration(ProjectSlotDeclaration.Parse(declaration));
			Assert.IsTrue(form.SlotRendered("cart"), "the cartridge row belongs on the form");
			Assert.IsTrue(form.SlotRendered("disk"), "the disk row belongs on the form");
		}
	}
}
