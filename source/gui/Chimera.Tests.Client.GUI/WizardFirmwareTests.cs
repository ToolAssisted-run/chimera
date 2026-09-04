using System.Collections.Generic;
using System.IO;

using Chimera.Client.Common;
using Chimera.Client.GUI;

namespace Chimera.Tests.Client.GUI
{
	/// <summary>
	/// The firmware page's rules: each requirement is ONE exact file (the hash
	/// is the identity, a file's name plays no part), the folder answers before
	/// the user, only that exact file satisfies, and nothing creates until
	/// every requirement is satisfied. Optional firmware does not exist.
	/// </summary>
	[TestClass]
	public class WizardFirmwareTests
	{
		private static string _dir = "";

		[ClassInitialize]
		public static void MakePlayground(TestContext _)
		{
			_dir = Path.Combine(Path.GetTempPath(), $"chimera-wizard-fw-{System.Diagnostics.Process.GetCurrentProcess().Id}");
			Directory.CreateDirectory(_dir);
		}

		[ClassCleanup]
		public static void RemovePlayground() => Directory.Delete(_dir, recursive: true);

		private static NewProjectWizard MakeForm(out string dir)
		{
			dir = Path.Combine(_dir, Path.GetRandomFileName());
			Directory.CreateDirectory(dir);
			NewProjectWizard form = new([ ], static _ => [ ]);
			form.Show();
			return form;
		}

		[TestMethod]
		public void TheFolderSatisfiesByHashUnderAnyName()
		{
			using var form = MakeForm(out var dir);
			var (cfg, index) = ProjectFormsShots.MakeFirmwareFixture(dir);
			form.UseFirmwareNeeds(cfg, [ ("bios_cd", 0) ], index);

			Assert.IsTrue(form.FirmwareSatisfied("bios_cd"), "the folder's dump satisfies by hash");
			StringAssert.Contains(form.ChosenFirmwarePath("bios_cd"),
				"my_us_bios.bin", "the file name was only a hint - a foreign name satisfied");
		}

		[TestMethod]
		public void OnlyTheExactFileSatisfies()
		{
			using var form = MakeForm(out var dir);
			var (cfg, _) = ProjectFormsShots.MakeFirmwareFixture(dir);
			form.UseFirmwareNeeds(cfg, [ ("bios", 2) ], [ ]);

			Assert.IsFalse(form.FirmwareSatisfied("bios"));
			form.ProvideFirmware("bios", Path.Combine(dir, "random.bin"));
			Assert.IsFalse(form.FirmwareSatisfied("bios"), "the wrong bytes satisfy nothing, whatever they are named");

			var good = Path.Combine(dir, "differently named.bios");
			File.WriteAllText(good, "FDS BIOS BYTES");
			form.ProvideFirmware("bios", good);
			Assert.IsTrue(form.FirmwareSatisfied("bios"), "the exact bytes under any name satisfy");
		}

		[TestMethod]
		public void TheVariantIsPickedUpstreamAndEachNailsOneFile()
		{
			// the same id, two entries - the sync setting decided which one
			// applies before this page ever rendered; only THAT dump satisfies
			using var form = MakeForm(out var dir);
			var (cfg, index) = ProjectFormsShots.MakeFirmwareFixture(dir);

			form.UseFirmwareNeeds(cfg, [ ("bios_cd", 1) ], index); // the JP entry
			Assert.IsFalse(form.FirmwareSatisfied("bios_cd"),
				"the folder's US dump does not satisfy the JP requirement");

			var jp = Path.Combine(dir, "whatever.bin");
			File.WriteAllText(jp, "JP CD BIOS BYTES");
			form.ProvideFirmware("bios_cd", jp);
			Assert.IsTrue(form.FirmwareSatisfied("bios_cd"));
		}

		[TestMethod]
		public void CreateIsDisabledUntilEveryRequirementIsSatisfied()
		{
			using var form = MakeForm(out var dir);
			var (cfg, index) = ProjectFormsShots.MakeFirmwareFixture(dir);
			form.UseFirmwareNeeds(cfg, [ ("bios_cd", 0), ("bios", 2) ], index);

			Assert.IsFalse(form.CreateEnabled, "the FDS bios is not on hand");

			var good = Path.Combine(dir, "whatever.bin");
			File.WriteAllText(good, "FDS BIOS BYTES");
			form.ProvideFirmware("bios", good);
			Assert.IsTrue(form.CreateEnabled, "every requirement satisfied - Create returns");
		}
	
		[TestMethod]
		public void OptionalFirmwareDoesNotExistSoADiscMustAskForThePup()
		{
			// RPCS3 declared the PUP "required": false, meaning "homebrew boots
			// without it". The engine has no such idea - firmware.cpp: "every
			// applying entry is REQUIRED ... and optional firmware does not
			// exist" - so the key was ignored, while the WIZARD honoured it and
			// let a disc project through with no PUP chosen. The config then got
			// no entry and every precompile session was refused at boot, silently.
			// "only discs need this" is spelt requiredWhen, as every other core
			// spells it.
			const string decl = """
				[ { "id": "PS3UPDAT.PUP", "name": "PS3UPDAT.PUP",
				    "requiredWhen": { "slot": "game", "extension": "iso" } } ]
				""";
			string Slots(string file) =>
				Newtonsoft.Json.JsonConvert.SerializeObject(
					new Dictionary<string, string[]> { [ "game" ] = [ file ] });

			var forDisc = Chimera.Emulation.Common.Engine.EngineFirmware.Evaluate(
				decl, Slots("gta.iso"), "{}");
			Assert.AreEqual(1, forDisc.Count, "a disc needs the system software");
			Assert.AreEqual("PS3UPDAT.PUP", forDisc[0].Id);

			var forHomebrew = Chimera.Emulation.Common.Engine.EngineFirmware.Evaluate(
				decl, Slots("hello.elf"), "{}");
			Assert.AreEqual(0, forHomebrew.Count, "homebrew boots without it");
		}
}
}
