using System.Collections.Generic;
using System.IO;
using System.IO.Compression;
using System.Linq;
using System.Windows.Forms;

using Chimera.Client.Common;
using Chimera.Client.GUI;

namespace Chimera.Tests.Client.GUI
{
	/// <summary>
	/// A core that takes no file at all - a game core whose game is all firmware,
	/// SDLPoP2's or SyndicatFX's - declares an empty slot list, and the wizard has
	/// no file step for it: Next on the core goes straight to the settings, and
	/// Back from the settings comes straight back (user, 2026-09-30). A core that
	/// takes files keeps the step.
	/// </summary>
	[TestClass]
	public class WizardNoFilesTests
	{
		private static string _dir = "";

		[ClassInitialize]
		public static void MakePlayground(TestContext _)
		{
			_dir = Path.Combine(Path.GetTempPath(), $"chimera-nofiles-{System.Diagnostics.Process.GetCurrentProcess().Id}");
			Directory.CreateDirectory(_dir);
		}

		[ClassCleanup(ClassCleanupBehavior.EndOfClass)]
		public static void RemovePlayground() => Directory.Delete(_dir, recursive: true);

		private const string Config = """
			{
			  "coreName": "gamebox",
			  "kind": "game",
			  "systemId": "GAME",
			  "video": { "width": 320, "height": 200 },
			  "audio": { "samplesPerFrame": 1024 },
			  "input": { "buttons": [] },
			  "settings": [ { "name": "cheats", "type": "bool", "default": false } ]
			}
			""";

		/// <summary>A package with nothing in it but the two declarations a wizard reads.</summary>
		private static string MakePackage(string name, string slots)
		{
			var path = Path.Combine(_dir, $"{name}.chimeraCore");
			if (File.Exists(path)) return path;
			using var zip = ZipFile.Open(path, ZipArchiveMode.Create);
			void Add(string entry, string text)
			{
				using var writer = new StreamWriter(zip.CreateEntry(entry).Open());
				writer.Write(text);
			}
			Add("waterbox.config", Config.Replace("gamebox", name));
			Add("file_slots.json", slots);
			Add("core.wbx", "not a real guest, and nothing here runs one");
			return path;
		}

		private static NewProjectWizard MakeForm(string name, string slots)
		{
			List<DiscoveredCorePackage> cores =
			[
				new() { Name = name, Path = MakePackage(name, slots), Systems = [ "GAME" ], Version = "", IsGameCore = true },
			];
			NewProjectWizard form = new(cores, static _ => [ ], kind: CoreKindFilter.Games);
			form.Show();
			return form;
		}

		private static void Press(Form form, string prefix)
			=> form.Controls.OfType<Button>().Single(b => b.Text.StartsWith(prefix)).PerformClick();

		[TestMethod]
		public void ACoreThatTakesNoFilesHasNoFileStep()
		{
			using var form = MakeForm("nofiles", """{ "slots": [] }""");
			Assert.AreEqual(0, form.PageForTest);

			Press(form, "Next");
			Assert.AreEqual("", form.StatusText, "nothing is asked of a core with no slots");
			Assert.AreEqual(2, form.PageForTest, "Next on the core goes straight to the settings");

			Press(form, "< Back");
			Assert.AreEqual(0, form.PageForTest, "and Back comes straight back to the core");
		}

		[TestMethod]
		public void ACoreThatTakesFilesKeepsItsFileStep()
		{
			using var form = MakeForm("withfiles", """{ "slots": [ { "id": "game", "title": "Game", "min": 0, "max": 1, "formats": ["dat"] } ] }""");
			Press(form, "Next");
			Assert.AreEqual(1, form.PageForTest, "a core that takes files is asked for them");
		}
	}
}
