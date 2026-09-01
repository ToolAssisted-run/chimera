using System.Collections.Generic;

using Chimera.Client.Common;
using Chimera.Client.GUI;
using Chimera.Emulation.Common.Waterbox;

namespace Chimera.Tests.Client.GUI
{
	/// <summary>
	/// One slot that takes several kinds of file offers only the chosen machine's
	/// kind. QuickerNesHawk's rom slot declares .nes and .fds, but the wizard asks
	/// which machine first - and a Famicom Disk System project takes .fds and
	/// nothing else, rather than deriving what it is from whichever file happens
	/// to be picked. A slot the machine claims no format of (save data) is not
	/// the machine's to narrow.
	/// </summary>
	[TestClass]
	public class WizardMachineFormatTests
	{
		/// <summary>The shape neshawk's file_slots.json declares: one rom slot, both formats.</summary>
		private const string Declaration = """
			{
			  "slots": [
			    { "id": "rom", "title": "Cartridge or Famicom disk", "min": 1, "max": 1,
			      "formats": ["nes", "fds"] },
			    { "id": "savedata", "title": "Save data", "min": 0, "max": 1,
			      "formats": ["sav", "srm"] }
			  ]
			}
			""";

		private static WaterboxConfig TwoMachines() => new()
		{
			MachineSetting = "machine",
			Machines =
			[
				new()
				{
					Id = "NES",
					Label = "NES / Famicom (cartridge)",
					When = [ "nes" ],
					Extensions = new() { [".nes"] = "NES" },
				},
				new()
				{
					Id = "NES",
					Label = "Famicom Disk System",
					When = [ "fds" ],
					Extensions = new() { [".fds"] = "NES" },
				},
			],
			Settings =
			[
				new WaterboxConfig.SettingDecl
				{
					Name = "machine",
					Type = "enum",
					Options = [ "nes", "fds" ],
					Default = "nes",
				},
			],
		};

		private static NewProjectWizard MakeForm(string machine)
		{
			NewProjectWizard form = new([ ], static _ => [ ]);
			form.Show();
			form.UseSettingsFrom(TwoMachines());
			form.SetSettingValue("machine", machine);
			form.UseDeclaration(ProjectSlotDeclaration.Parse(Declaration));
			return form;
		}

		[TestMethod]
		public void ACartridgeMachineTakesOnlyCartridges()
		{
			using var form = MakeForm("nes");
			CollectionAssert.AreEqual(new[] { "nes" }, new List<string>(form.OfferedFormats("rom")),
				"an explicit cartridge machine has no business offering .fds");
		}

		[TestMethod]
		public void ADiskSystemTakesOnlyDisks()
		{
			using var form = MakeForm("fds");
			CollectionAssert.AreEqual(new[] { "fds" }, new List<string>(form.OfferedFormats("rom")),
				"an explicit Famicom Disk System takes .fds and nothing else");
		}

		[TestMethod]
		public void SaveDataIsNotTheMachinesToNarrow()
		{
			using var form = MakeForm("fds");
			CollectionAssert.AreEqual(new[] { "sav", "srm" }, new List<string>(form.OfferedFormats("savedata")),
				"the machine claims rom formats; a slot offering none of them keeps its own list");
		}

		[TestMethod]
		public void APackageThatIsOneMachineIsLeftAlone()
		{
			NewProjectWizard form = new([ ], static _ => [ ]);
			form.Show();
			using (form)
			{
				form.UseDeclaration(ProjectSlotDeclaration.Parse(Declaration));
				CollectionAssert.AreEqual(new[] { "nes", "fds" }, new List<string>(form.OfferedFormats("rom")),
					"no machines declared means nothing to narrow by");
			}
		}

		[TestMethod]
		public void AFileRoutesToTheMachinesSlot()
		{
			// the disk machine's rom slot claims .fds, so a dropped disk lands
			// there rather than in whatever is at the top of the form
			using var form = MakeForm("fds");
			Assert.AreEqual("rom", form.SlotForFile("/somewhere/Nazo no Murasamejou.fds"));
		}
	}
}
