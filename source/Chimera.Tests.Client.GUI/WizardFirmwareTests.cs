using System.Collections.Generic;
using System.IO;

using Chimera.Client.Common;
using Chimera.Client.GUI;

namespace Chimera.Tests.Client.GUI
{
	/// <summary>
	/// The firmware page's rules: the hash is the identity (a file's name plays
	/// no part), the folder answers before the user, a required entry accepts
	/// only a listed candidate, an optional one also takes a custom dump, and
	/// nothing creates until every required entry is satisfied.
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
			NewProjectWizard form = new([ ], static () => null, static _ => [ ]);
			form.Show();
			return form;
		}

		[TestMethod]
		public void TheFolderSatisfiesByHashAndTheExactCandidateIsKnown()
		{
			using var form = MakeForm(out var dir);
			var (cfg, index) = ProjectFormsShots.MakeFirmwareFixture(dir);
			form.UseFirmwareNeeds(cfg, [ ("bios_cd", true) ], index);

			Assert.IsTrue(form.FirmwareSatisfied("bios_cd"), "the folder's dump satisfies by hash");
			var chosen = form.ChosenFirmware("bios_cd");
			Assert.AreEqual(0, chosen.Candidate, "and the US candidate is the one it matched");
			StringAssert.Contains(chosen.Path, "my_us_bios.bin", "the file name was only a hint - a different name satisfied");
		}

		[TestMethod]
		public void ARequiredEntryRefusesANonCandidateFile()
		{
			using var form = MakeForm(out var dir);
			var (cfg, _) = ProjectFormsShots.MakeFirmwareFixture(dir);
			form.UseFirmwareNeeds(cfg, [ ("disksys.rom", true) ], [ ]);

			Assert.IsFalse(form.FirmwareSatisfied("disksys.rom"));
			form.ProvideFirmware("disksys.rom", Path.Combine(dir, "random.bin"));
			Assert.IsFalse(form.FirmwareSatisfied("disksys.rom"), "a hash matching no candidate satisfies nothing");

			var good = Path.Combine(dir, "differently named.bios");
			File.WriteAllText(good, "FDS BIOS BYTES");
			form.ProvideFirmware("disksys.rom", good);
			Assert.IsTrue(form.FirmwareSatisfied("disksys.rom"), "the right bytes under any name satisfy");
		}

		[TestMethod]
		public void CreateIsDisabledUntilEveryRequiredEntryIsSatisfied()
		{
			using var form = MakeForm(out var dir);
			var (cfg, index) = ProjectFormsShots.MakeFirmwareFixture(dir);
			form.UseFirmwareNeeds(cfg, [ ("bios_cd", true), ("disksys.rom", true) ], index);

			Assert.IsFalse(form.CreateEnabled, "the FDS bios is required and unsatisfied");

			var good = Path.Combine(dir, "whatever.bin");
			File.WriteAllText(good, "FDS BIOS BYTES");
			form.ProvideFirmware("disksys.rom", good);
			Assert.IsTrue(form.CreateEnabled, "every required entry satisfied - Create returns");
		}

		[TestMethod]
		public void AnOptionalEntryTakesACustomDumpAndAFoundOneCanBeOverridden()
		{
			using var form = MakeForm(out var dir);
			var (cfg, index) = ProjectFormsShots.MakeFirmwareFixture(dir);
			form.UseFirmwareNeeds(cfg, [ ("bios_cd", true), ("ltn0.pgf", false) ], index);

			// optional: a custom (unrecognised) dump is allowed and recorded
			form.ProvideFirmware("ltn0.pgf", Path.Combine(dir, "random.bin"));
			var font = form.ChosenFirmware("ltn0.pgf");
			Assert.IsNotNull(font.Path);
			Assert.AreEqual(-1, font.Candidate, "custom: no candidate matched, used anyway");
			Assert.IsFalse(form.FirmwareSatisfied("ltn0.pgf"), "custom never counts as candidate-satisfied");

			// found firmware can still be replaced by another matching file
			var jp = Path.Combine(dir, "some_other_dump.bin");
			File.WriteAllText(jp, "JP CD BIOS BYTES");
			form.ProvideFirmware("bios_cd", jp);
			Assert.AreEqual(1, form.ChosenFirmware("bios_cd").Candidate, "now the JP candidate is the satisfied one");
		}
	}
}
