using System.Linq;

using Chimera.Emulation.Common.Waterbox;

namespace Chimera.Tests.Emulation.Common
{
	/// <summary>
	/// One core.wbx can be several machines: Genesis Plus GX is a Mega Drive, a
	/// Master System, a Game Gear and an SG-1000, and shipping that as four
	/// packages meant four copies of one binary whose only differences were a
	/// controller and a name (and, since a package registers its core by name,
	/// three of the four were silently dropped when all were installed).
	///
	/// Which machine a session is comes from a setting, so it is pinned in the
	/// project and cited by the movie like every other structural choice. These
	/// check what a declaration of machines resolves to.
	/// </summary>
	[TestClass]
	public class WaterboxMachinesTests
	{
		private const string TwoMachines = """
		{
			"coreName": "Genesis Plus GX",
			"romFile": "rom",
			"memoryLayoutMiB": [ 4, 4, 4, 4, 8 ],
			"video": { "width": 1024, "height": 512, "virtualWidth": 292, "virtualHeight": 224 },
			"audio": { "samplesPerFrame": 2048 },
			"machineSetting": "systemHardware",
			"machines": [
				{
					"id": "GEN", "label": "Mega Drive / Genesis", "when": [ "genesis" ],
					"input": { "name": "Genesis Controller", "buttons": [ "P1 Up", "P1 A", "P1 X" ] },
					"extensions": { ".md": "GEN" }
				},
				{
					"id": "SMS", "label": "Master System", "when": [ "sms" ],
					"input": { "name": "SMS Controller", "buttons": [ "P1 Up", "P1 Button 1" ] },
					"virtualWidth": 293, "virtualHeight": 192,
					"extensions": { ".sms": "SMS" },
					"settingOverrides": { "port1": { "options": [ "gamepad", "none" ], "default": "gamepad" } }
				}
			],
			"settings": [
				{ "name": "systemHardware", "type": "enum", "default": "genesis", "options": [ "genesis", "sms" ] },
				{ "name": "port1", "type": "enum", "default": "gamepad",
				  "options": [ "gamepad", "none", "mouse", "activator" ] }
			]
		}
		""";

		private static WaterboxConfig Cfg => WaterboxConfig.FromJson(TwoMachines);

		private static WaterboxCoreSettings Pin(string machine)
		{
			WaterboxCoreSettings s = new();
			s.Values["systemHardware"] = machine;
			return s;
		}

		[TestMethod]
		public void ThePackageIsEveryMachineItDeclares()
		{
			var cfg = Cfg;
			CollectionAssert.AreEqual(new[] { "GEN", "SMS" }, cfg.SystemIds.ToArray());
			CollectionAssert.AreEquivalent(new[] { ".md", ".sms" }, cfg.AllExtensions.Keys.ToArray());
			Assert.AreEqual("SMS", cfg.AllExtensions[".sms"]);
		}

		[TestMethod]
		public void TheSettingPicksTheMachine()
		{
			var cfg = Cfg;
			Assert.AreEqual("SMS", cfg.MachineFor(WaterboxCore.EffectiveSettingsFor(cfg, Pin("sms")))?.Id);
			Assert.AreEqual("GEN", cfg.MachineFor(WaterboxCore.EffectiveSettingsFor(cfg, Pin("genesis")))?.Id);
		}

		[TestMethod]
		public void WithNothingPinnedItIsThePackagesDefaultMachine()
		{
			var cfg = Cfg;
			// the machine setting's own default decides, so a session always has a
			// machine and never has none
			Assert.AreEqual("GEN", cfg.MachineFor(WaterboxCore.EffectiveSettingsFor(cfg, null))?.Id);
		}

		[TestMethod]
		public void AValueNamingNoMachineFallsBackRatherThanFailing()
		{
			var cfg = Cfg;
			Assert.AreEqual("GEN", cfg.MachineFor(WaterboxCore.EffectiveSettingsFor(cfg, Pin("nonesuch")))?.Id);
		}

		[TestMethod]
		public void AMachineNarrowsWhatItsSettingsMayBe()
		{
			var cfg = Cfg;
			var gen = cfg.SettingsFor(cfg.MachineForSystem("GEN"));
			var sms = cfg.SettingsFor(cfg.MachineForSystem("SMS"));

			CollectionAssert.AreEqual(
				new[] { "gamepad", "none", "mouse", "activator" },
				gen.Single(static d => d.Name == "port1").Options.ToArray(),
				"a Mega Drive port takes six devices");
			CollectionAssert.AreEqual(
				new[] { "gamepad", "none" },
				sms.Single(static d => d.Name == "port1").Options.ToArray(),
				"a Master System port takes a pad or nothing");

			// narrowing must not invent or drop settings, only change what they allow
			CollectionAssert.AreEqual(
				gen.Select(static d => d.Name).ToArray(),
				sms.Select(static d => d.Name).ToArray());
		}

		[TestMethod]
		public void NarrowedSettingsAreTheSameInstancesEachTime()
		{
			var cfg = Cfg;
			// the wizard compares declaration lists by reference to tell whether the
			// exposed set changed; fresh objects every call would redraw forever
			var machine = cfg.MachineForSystem("SMS");
			Assert.AreSame(cfg.SettingsFor(machine), cfg.SettingsFor(machine));
		}

		[TestMethod]
		public void ASingleMachinePackageIsUnaffected()
		{
			var cfg = WaterboxConfig.FromJson("""
			{
				"coreName": "quickerNES", "systemId": "NES",
				"video": { "width": 256, "height": 240 }, "audio": { "samplesPerFrame": 735 },
				"input": { "name": "NES Controller", "buttons": [ "P1 A" ] },
				"extensions": { ".nes": "NES" },
				"settings": [ { "name": "palette", "type": "enum", "default": "fceux", "options": [ "fceux", "nestopia" ] } ]
			}
			""");
			Assert.IsFalse(cfg.HasMachines);
			Assert.IsNull(cfg.MachineFor(WaterboxCore.EffectiveSettingsFor(cfg, null)));
			CollectionAssert.AreEqual(new[] { "NES" }, cfg.SystemIds.ToArray());
			Assert.AreSame(cfg.Settings, cfg.SettingsFor(null));
		}
	}
}
