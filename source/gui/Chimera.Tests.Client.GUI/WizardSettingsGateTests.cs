using System.Collections.Generic;

using Chimera.Client.Common;
using Chimera.Client.GUI;
using Chimera.Emulation.Common.Waterbox;

namespace Chimera.Tests.Client.GUI
{
	/// <summary>
	/// The settings page's gate: the chosen game files decide which sync
	/// settings are exposed at all, and a setting can gate further settings,
	/// re-evaluated as values change - the GPGX shape (a Game Gear cart
	/// exposes its sound chip, a Genesis cart its own).
	/// </summary>
	[TestClass]
	public class WizardSettingsGateTests
	{
		private const string Declaration = """
			{
			  "slots": [
			    { "id": "cart", "title": "Cartridge", "min": 1, "max": 1, "formats": ["md", "gg"] }
			  ]
			}
			""";

		private static WaterboxConfig MakeCfg()
			=> WaterboxConfig.FromJson("""
				{
				  "coreName": "GPGX-shaped",
				  "systemId": "GEN",
				  "video": { "width": 320, "height": 224 },
				  "audio": { "samplesPerFrame": 1024 },
				  "input": { "buttons": [] },
				  "settings": [
				    { "name": "region", "type": "enum", "options": ["us", "jp", "eu"], "default": "us" },
				    { "name": "ggSoundChip", "type": "enum", "options": ["stock", "enhanced"], "default": "stock",
				      "exposedWhen": { "slot": "cart", "extension": "gg" } },
				    { "name": "mdSoundChip", "type": "enum", "options": ["ym2612", "ym3438"], "default": "ym2612",
				      "exposedWhen": { "slot": "cart", "extension": "md" } },
				    { "name": "expansion", "type": "bool", "default": false },
				    { "name": "expansionRam", "type": "int", "default": 64,
				      "exposedWhen": { "setting": "expansion", "is": true } }
				  ]
				}
				""");

		private static NewProjectWizard MakeForm(string cartName)
		{
			NewProjectWizard form = new([ ], static _ => [ ]);
			form.Show();
			form.UseDeclaration(ProjectSlotDeclaration.Parse(Declaration));
			form.AddFileToSlot("cart", $"/games/{cartName}");
			form.UseSettingsFrom(MakeCfg());
			return form;
		}

		[TestMethod]
		public void TheCartDecidesWhichSoundChipExists()
		{
			using var gg = MakeForm("sonic.gg");
			CollectionAssert.AreEqual(new[] { "region", "ggSoundChip", "expansion" }, gg.ExposedSettingNames);

			using var md = MakeForm("sonic.md");
			CollectionAssert.AreEqual(new[] { "region", "mdSoundChip", "expansion" }, md.ExposedSettingNames);
		}

		[TestMethod]
		public void ASettingCanGateAFurtherSettingLive()
		{
			using var form = MakeForm("sonic.md");
			CollectionAssert.DoesNotContain(form.ExposedSettingNames, "expansionRam");

			form.SetSettingValue("expansion", true);
			CollectionAssert.Contains(form.ExposedSettingNames, "expansionRam", "flipping the gate exposes the dependent knob");

			form.SetSettingValue("expansion", false);
			CollectionAssert.DoesNotContain(form.ExposedSettingNames, "expansionRam");
		}

		private static WaterboxConfig MakeCfgWithFirmware()
			=> WaterboxConfig.FromJson("""
				{
				  "coreName": "GPGX-shaped",
				  "systemId": "GEN",
				  "video": { "width": 320, "height": 224 },
				  "audio": { "samplesPerFrame": 1024 },
				  "input": { "buttons": [] },
				  "settings": [
				    { "name": "loadBios", "type": "bool", "default": false }
				  ],
				  "firmware": [
				    { "id": "mdBios", "display": "TMSS boot rom",
				      "requiredWhen": { "setting": "loadBios", "is": true } }
				  ]
				}
				""");

		[TestMethod]
		public void AMachineThatNeedsNoFirmwareIsNotAskedAboutFirmware()
		{
			// the settings step is the last one, so its button offers to create
			using var form = MakeForm("sonic.md");
			Assert.AreEqual("Create", form.NextButtonText,
				"nothing declares firmware: the settings step ends the wizard");
		}

		[TestMethod]
		public void TheFirmwareStepAppearsOnlyWhileSomethingNeedsIt()
		{
			NewProjectWizard form = new([ ], static _ => [ ]);
			using (form)
			{
				form.Show();
				form.UseDeclaration(ProjectSlotDeclaration.Parse(Declaration));
				form.AddFileToSlot("cart", "/games/sonic.md");
				form.UseSettingsFrom(MakeCfgWithFirmware());

				Assert.AreEqual("Create", form.NextButtonText,
					"the bios is not asked for while loadBios is off");

				form.SetSettingValue("loadBios", true);
				Assert.AreEqual("Next >", form.NextButtonText,
					"turning the bios on adds the step that asks for it");

				form.SetSettingValue("loadBios", false);
				Assert.AreEqual("Create", form.NextButtonText,
					"and turning it off takes the step away again");
			}
		}
	}
}
