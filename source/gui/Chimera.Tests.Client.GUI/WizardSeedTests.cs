using System.Collections.Generic;
using System.IO;
using System.IO.Compression;
using System.Linq;

using Chimera.Client.Common;
using Chimera.Client.GUI;
using Chimera.Emulation.Common.Engine;

namespace Chimera.Tests.Client.GUI
{
	/// <summary>
	/// Reconfiguring a project: the wizard opens already answered.
	///
	/// Changing a sync setting changes the machine, so there is no editing a
	/// project in place that would not amount to building another one. What made
	/// that unbearable was answering every question again to change one of them,
	/// so the questions arrive answered and only the one that is wrong is touched.
	/// </summary>
	[TestClass]
	public class WizardSeedTests
	{
		private static string _dir = "";

		[ClassInitialize]
		public static void MakePlayground(TestContext _)
		{
			_dir = Path.Combine(Path.GetTempPath(), $"chimera-seed-{System.Diagnostics.Process.GetCurrentProcess().Id}");
			Directory.CreateDirectory(_dir);
		}

		[ClassCleanup(ClassCleanupBehavior.EndOfClass)]
		public static void RemovePlayground() => Directory.Delete(_dir, recursive: true);

		private const string Config = """
			{
			  "coreName": "seedbox",
			  "systemId": "NES",
			  "video": { "width": 256, "height": 240 },
			  "audio": { "samplesPerFrame": 1024 },
			  "input": { "buttons": [] },
			  "settings": [
			    { "name": "region", "type": "enum", "options": ["ntsc", "pal"], "default": "ntsc" },
			    { "name": "renderer", "type": "enum", "options": ["software", "opengl"], "default": "software" }
			  ]
			}
			""";

		private const string Slots = """
			{ "slots": [ { "id": "rom", "title": "ROM", "min": 1, "max": 1, "formats": ["nes"] } ] }
			""";

		/// <summary>A package with nothing in it but the two declarations a wizard reads.</summary>
		private static string MakePackage()
		{
			var path = Path.Combine(_dir, "seedbox.chimeraCore");
			if (File.Exists(path)) return path;
			using var zip = ZipFile.Open(path, ZipArchiveMode.Create);
			void Add(string name, string text)
			{
				using var writer = new StreamWriter(zip.CreateEntry(name).Open());
				writer.Write(text);
			}
			Add("waterbox.config", Config);
			Add("file_slots.json", Slots);
			Add("core.wbx", "not a real guest, and nothing here runs one");
			return path;
		}

		private static string MakeRom(string name)
		{
			var path = Path.Combine(_dir, name);
			File.WriteAllText(path, $"rom bytes for {name}");
			return path;
		}

		private static NewProjectWizard MakeForm(string packagePath)
		{
			List<DiscoveredCorePackage> cores =
			[
				new() { Name = "seedbox", Path = packagePath, Systems = [ "NES" ], Version = "" },
			];
			NewProjectWizard form = new(cores, static _ => [ ]);
			form.Show();
			return form;
		}

		[TestMethod]
		public void ItComesUpOnTheProjectsCoreSettingsAndFiles()
		{
			var package = MakePackage();
			var rom = MakeRom("seeded.nes");

			using var project = EngineProject.New();
			project.SetCore("seedbox", "", "");
			project.SetSettingsJson("""{"region":"pal","renderer":"opengl"}""");
			project.FileAdd("seeded.nes", "rom", rom);

			using var form = MakeForm(package);
			form.SeedFrom(ProjectAnswers.Of(project));

			Assert.AreEqual("pal", form.SettingValue("region"), "the setting the project was running on");
			Assert.AreEqual("opengl", form.ChosenRenderer, "and the renderer it was drawn with");
			CollectionAssert.Contains(form.SlotFileNames("rom"), "seeded.nes",
				"and the file it was running, from where this session found it");
		}

		[TestMethod]
		public void AProjectWhoseCoreIsNotInstalledLeavesItBlank()
		{
			var package = MakePackage();

			using var project = EngineProject.New();
			project.SetCore("a core nobody has", "", "");
			project.SetSettingsJson("""{"region":"pal"}""");

			using var form = MakeForm(package);
			form.SeedFrom(ProjectAnswers.Of(project));

			Assert.AreNotEqual("pal", form.SettingValue("region"),
				"nothing is filled in from a project this build cannot open");
		}

		[TestMethod]
		public void TheAnswersOutliveTheProjectTheyCameFrom()
		{
			// closing a project is not a decision to start the next one from
			// nothing, and the project itself is disposed when it is replaced - so
			// what the wizard opens on is a copy taken while it was still open
			var package = MakePackage();
			var rom = MakeRom("outlives.nes");

			ProjectAnswers answers;
			using (var project = EngineProject.New())
			{
				project.SetCore("seedbox", "", "");
				project.SetSettingsJson("""{"region":"pal"}""");
				project.FileAdd("outlives.nes", "rom", rom);
				answers = ProjectAnswers.Of(project);
			}

			using var form = MakeForm(package);
			form.SeedFrom(answers);

			Assert.AreEqual("pal", form.SettingValue("region"));
			CollectionAssert.Contains(form.SlotFileNames("rom"), "outlives.nes");
		}

		[TestMethod]
		public void AFileThatIsNoLongerThereIsNotOfferedAsIfItWere()
		{
			var package = MakePackage();
			var rom = MakeRom("vanishing.nes");

			using var project = EngineProject.New();
			project.SetCore("seedbox", "", "");
			project.FileAdd("vanishing.nes", "rom", rom);
			File.Delete(rom);

			using var form = MakeForm(package);
			form.SeedFrom(ProjectAnswers.Of(project));

			Assert.AreEqual(0, form.SlotFileNames("rom").Length,
				"a project carries names and hashes, not paths; a file this run cannot find is not guessed at");
		}
	}
}
