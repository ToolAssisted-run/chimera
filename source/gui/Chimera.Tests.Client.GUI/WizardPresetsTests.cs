using System;
using System.Linq;

using Chimera.Client.Common;
using Chimera.Client.GUI;
using Chimera.Emulation.Common.Waterbox;

namespace Chimera.Tests.Client.GUI
{
	/// <summary>
	/// Configuration presets: a named bundle of setting values the core suggests,
	/// which Apply WRITES INTO the settings and is then finished with.
	///
	/// The thing being prevented here is what DOSBox-X shipped: a "preset" the
	/// core resolved for itself at boot, by appending a .conf file after
	/// everything the user had chosen, so the preset silently won. Settings then
	/// had to describe themselves as "Auto uses the configuration preset's
	/// default" - the grid no longer said what the machine would be, and the
	/// project pinned a preset NAME whose meaning the next core build could
	/// change. So the assertions below are about VALUES landing in the settings,
	/// not about a preset being remembered anywhere: after Apply there is nothing
	/// left to remember.
	///
	/// States covered: no presets at all (the selector must be absent, not empty),
	/// presets offered, Apply, a value that gates a further setting, a name the
	/// machine does not have, and a preset belonging to one machine of several.
	/// </summary>
	[TestClass]
	public class WizardPresetsTests
	{
		private const string Declaration = """
			{
			  "slots": [
			    { "id": "disk", "title": "Disk", "min": 0, "max": -1, "formats": ["img"] }
			  ]
			}
			""";

		/// <summary>A DOSBox-X-shaped package: real settings, and presets made of them.</summary>
		private static WaterboxConfig WithPresets() => TestPackages.Config("""
			{
			  "coreName": "DOS-shaped",
			  "systemId": "DOS",
			  "video": { "width": 640, "height": 400 },
			  "audio": { "samplesPerFrame": 1024 },
			  "input": { "buttons": [] },
			  "settings": [
			    { "name": "cputype", "type": "enum", "options": ["8086", "386", "486"], "default": "486" },
			    { "name": "cycles", "type": "int", "default": 3000 },
			    { "name": "memsize", "type": "int", "default": 16 },
			    { "name": "sbtype", "type": "enum", "options": ["none", "sb16"], "default": "sb16" },
			    { "name": "joystick", "type": "bool", "default": false },
			    { "name": "joystickType", "type": "enum", "options": ["2axis", "4axis"], "default": "2axis",
			      "exposedWhen": { "setting": "joystick", "is": true } }
			  ],
			  "presets": [
			    { "id": "xt_1981", "label": "1981 IBM PC/XT 5150",
			      "values": { "cputype": "8086", "cycles": 315, "memsize": 1, "sbtype": "none" } },
			    { "id": "ps2_1993", "label": "1993 IBM PS/2 53 SLC2",
			      "values": { "cputype": "486", "cycles": 9000, "joystick": true } }
			  ]
			}
			""");

		/// <summary>The same package with the presets taken out, and nothing else changed.</summary>
		private static WaterboxConfig WithoutPresets()
		{
			var cfg = WithPresets();
			cfg.Presets = null;
			return cfg;
		}

		private static NewProjectWizard FormWith(WaterboxConfig cfg)
		{
			NewProjectWizard form = new([ ], static _ => [ ]);
			form.Show();
			form.UseDeclaration(TestPackages.Slots(Declaration));
			form.UseSettingsFrom(cfg);
			return form;
		}

		[TestMethod]
		public void ACoreThatSuggestsNothingShowsNoSelector()
		{
			using var form = FormWith(WithoutPresets());
			Assert.IsFalse(
				form.PresetsAreOffered,
				"a core with no presets must show no evidence that presets exist - "
					+ "an empty dropdown is a question with no answers");
		}

		[TestMethod]
		public void PresetsAreOfferedInTheOrderTheCoreDeclaredThem()
		{
			using var form = FormWith(WithPresets());
			Assert.IsTrue(form.PresetsAreOffered);
			CollectionAssert.AreEqual(
				new[] { "1981 IBM PC/XT 5150", "1993 IBM PS/2 53 SLC2" },
				form.PresetNames,
				"the core's order is chronological and means something; the selector must not sort it");
		}

		[TestMethod]
		public void ApplyWritesThePresetsValuesIntoTheSettings()
		{
			using var form = FormWith(WithPresets());
			form.SelectPreset("xt_1981");
			form.ApplyPreset();

			Assert.AreEqual("8086", Convert.ToString(form.SettingValue("cputype")));
			Assert.AreEqual("315", Convert.ToString(form.SettingValue("cycles")));
			Assert.AreEqual("1", Convert.ToString(form.SettingValue("memsize")));
			Assert.AreEqual("none", Convert.ToString(form.SettingValue("sbtype")));
		}

		[TestMethod]
		public void ASettingThePresetDoesNotNameIsLeftAlone()
		{
			using var form = FormWith(WithPresets());
			form.SetSettingValue("memsize", 64);
			form.SelectPreset("ps2_1993"); // names cputype, cycles and joystick - not memsize
			form.ApplyPreset();

			// both halves, or this passes on a build where Apply does nothing at all
			Assert.AreEqual(
				"9000",
				Convert.ToString(form.SettingValue("cycles")),
				"the preset's own values must land");
			Assert.AreEqual(
				"64",
				Convert.ToString(form.SettingValue("memsize")),
				"a preset transfers the values it names; it does not reset the rest of the machine");
		}

		[TestMethod]
		public void AValueIsCoercedToTheTypeTheCoreDeclared()
		{
			var cfg = WithPresets();
			// written by hand, as a generator or a person may well write it
			cfg.Presets![0].Values!["cycles"] = "315";

			using var form = FormWith(cfg);
			form.SelectPreset("xt_1981");
			form.ApplyPreset();

			Assert.IsInstanceOfType(
				form.SettingValue("cycles"),
				typeof(int),
				"the core declared cycles an int; a preset must not be able to put a string in the project");
		}

		[TestMethod]
		public void AValueThatGatesAFurtherSettingExposesItImmediately()
		{
			using var form = FormWith(WithPresets());
			CollectionAssert.DoesNotContain(form.ExposedSettingNames, "joystickType");

			form.SelectPreset("ps2_1993"); // turns the joystick on
			form.ApplyPreset();

			CollectionAssert.Contains(
				form.ExposedSettingNames,
				"joystickType",
				"the settings a preset's values gate must appear with it, not on the next unrelated edit");
		}

		[TestMethod]
		public void ANameThisMachineDoesNotHaveIsIgnoredAndSaidSo()
		{
			var cfg = WithPresets();
			cfg.Presets![0].Values!["cpuTypo"] = "8086";

			using var form = FormWith(cfg);
			form.SelectPreset("xt_1981");
			form.ApplyPreset();

			Assert.AreEqual("8086", Convert.ToString(form.SettingValue("cputype")), "the rest still applies");
			StringAssert.Contains(
				form.StatusText,
				"cpuTypo",
				"a preset naming a setting that does not exist is a core bug, and silence is how it survives");
		}

		[TestMethod]
		public void APresetBelongingToOneMachineIsNotOfferedByAnother()
		{
			var cfg = WithPresets();
			cfg.MachineSetting = "machine";
			cfg.Machines =
			[
				new() { Id = "DOS", Label = "IBM PC", When = [ "ibm" ] },
				new() { Id = "PC98", Label = "PC-98", When = [ "pc98" ] },
			];
			cfg.Settings!.Insert(0, new WaterboxConfig.SettingDecl
			{
				Name = "machine",
				Type = "enum",
				Options = [ "ibm", "pc98" ],
				Default = "ibm",
			});
			cfg.Presets![0].When = [ "ibm" ];
			cfg.Presets![1].When = [ "pc98" ];

			using var form = FormWith(cfg);
			CollectionAssert.AreEqual(
				new[] { "1981 IBM PC/XT 5150" },
				form.PresetNames,
				"an IBM PC must not be offered a PC-98's machine");

			form.SetSettingValue("machine", "pc98");
			CollectionAssert.AreEqual(
				new[] { "1993 IBM PS/2 53 SLC2" },
				form.PresetNames,
				"and changing the machine must change what is on offer, not leave the last machine's list up");
		}
	}
}
