using System.Collections.Generic;
using System.Linq;

using Chimera.Client.Common;
using Chimera.Client.GUI;
using Chimera.Emulation.Common.Waterbox;

namespace Chimera.Tests.Client.GUI
{
	/// <summary>
	/// The import dialog is rendered from the core's movieImport declaration, and
	/// what it sends is that declaration's semantics: each file input's option
	/// carries the mounted names (a list's joined in the user's order, an empty list
	/// not at all), and an option only when ticked. A refusal keeps the dialog open
	/// with everything still picked.
	/// </summary>
	[TestClass]
	public class MovieImportFormTests
	{
		private static WaterboxConfig.MovieImportDecl Decl()
			=> WaterboxConfig.FromJson("""
				{
				  "coreName": "importbox",
				  "video": { "width": 320, "height": 200 },
				  "audio": { "samplesPerFrame": 1260 },
				  "input": { "buttons": [] },
				  "movieImport": {
				    "menu": "Import Demo (LMP)...",
				    "movie": { "label": "Demo", "extensions": [ "lmp" ] },
				    "files": [
				      { "option": "importIwad", "label": "IWAD", "firmware": true, "required": true, "extensions": [ "wad" ] },
				      { "option": "importPwads", "label": "PWADs", "slot": "pwad", "multiple": true, "separator": ";", "extensions": [ "wad", "deh" ] }
				    ],
				    "options": [
				      { "name": "importNoPwads", "display": "Load no PWADs", "type": "bool" },
				      { "name": "importLongtics", "display": "Longtics", "type": "bool" }
				    ]
				  }
				}
				""")!.MovieImport!;

		private sealed class Core
		{
			public List<MovieImportRequest> Asked { get; } = new();

			public string Answer { get; set; } = """{"format":"Doom 1.9","frames":3,"input":"[Input]\n[/Input]\n"}""";

			public (MovieImportAnswer?, string?) Import(MovieImportRequest request)
			{
				Asked.Add(request);
				return (MovieImportAnswer.Parse(Answer), null);
			}
		}

		private static MovieImportForm MakeForm(Core core)
		{
			MovieImportForm form = new(Decl(), static (_, _, _) => [ ], core.Import);
			form.Show();
			return form;
		}

		[TestMethod]
		public void TheTitleIsTheMenuItem()
		{
			using var form = MakeForm(new());
			Assert.AreEqual("Import Demo (LMP)", form.Text);
		}

		[TestMethod]
		public void TheMovieAndTheRequiredFileAreAskedFor()
		{
			Core core = new();
			using var form = MakeForm(core);
			form.Import();
			StringAssert.Contains(form.StatusText, "demo");
			form.SetMovie("/demos/run.lmp");
			form.Import();
			StringAssert.Contains(form.StatusText, "IWAD");
			Assert.AreEqual(0, core.Asked.Count, "nothing reaches the core until the dialog is complete");
		}

		[TestMethod]
		public void TheOptionsCarryTheNamesInTheUsersOrder()
		{
			Core core = new();
			using var form = MakeForm(core);
			form.SetMovie("/demos/run.lmp");
			form.AddFiles("importIwad", [ "/iwads/DOOM2.WAD" ]);
			form.AddFiles("importPwads", [ "/pwads/b.wad", "/pwads/a.wad", "/pwads/fix.deh" ]);
			form.SetOption("importLongtics", true);
			form.Import();

			var asked = core.Asked.Single();
			Assert.AreEqual("/demos/run.lmp", asked.Movie);
			Assert.AreEqual("DOOM2.WAD", asked.Settings!["importIwad"]);
			Assert.AreEqual("b.wad;a.wad;fix.deh", asked.Settings["importPwads"], "joined in the order given, not sorted");
			Assert.IsTrue((bool)asked.Settings["importLongtics"]);
			Assert.IsFalse(asked.Settings.ContainsKey("importNoPwads"), "an option is sent only when ticked");
			CollectionAssert.AreEqual(
				new[] { "DOOM2.WAD=/iwads/DOOM2.WAD", "b.wad=/pwads/b.wad", "a.wad=/pwads/a.wad", "fix.deh=/pwads/fix.deh" },
				asked.Files.Select(static f => $"{f.Name}={f.Path}").ToArray(),
				"every file is mounted under its own name");
			Assert.AreEqual(System.Windows.Forms.DialogResult.OK, form.DialogResult);
			Assert.IsNotNull(form.Answer);
		}

		[TestMethod]
		public void AnEmptyListIsNotSentAtAll()
		{
			// absent means "what the movie itself names"; present but empty would mean "none"
			Core core = new();
			using var form = MakeForm(core);
			form.SetMovie("/demos/run.lmp");
			form.AddFiles("importIwad", [ "/iwads/DOOM2.WAD" ]);
			form.Import();
			Assert.IsFalse(core.Asked.Single().Settings!.ContainsKey("importPwads"));
		}

		[TestMethod]
		public void TwoFilesOfOneNameCannotBothBeMounted()
		{
			using var form = MakeForm(new());
			form.AddFiles("importIwad", [ "/iwads/DOOM2.WAD" ]);
			form.AddFiles("importPwads", [ "/elsewhere/doom2.wad", "/pwads/map.wad" ]);
			StringAssert.Contains(form.StatusText, "doom2.wad");
			CollectionAssert.AreEqual(new[] { "map.wad" }, form.FilesFor("importPwads").ToArray());
		}

		[TestMethod]
		public void ARefusalIsShownAndEverythingStaysPicked()
		{
			Core core = new() { Answer = """{"error":"its footer names map.wad, which is not at hand"}""" };
			using var form = MakeForm(core);
			form.SetMovie("/demos/run.lmp");
			form.AddFiles("importIwad", [ "/iwads/DOOM2.WAD" ]);
			form.Import();
			Assert.AreEqual("its footer names map.wad, which is not at hand", form.StatusText);
			Assert.IsNull(form.Answer);
			Assert.AreNotEqual(System.Windows.Forms.DialogResult.OK, form.DialogResult);

			// the file it named is added, and the import tried again
			core.Answer = """{"format":"Doom 1.9","frames":3,"input":"[Input]\n[/Input]\n"}""";
			form.AddFiles("importPwads", [ "/pwads/map.wad" ]);
			form.Import();
			Assert.AreEqual("map.wad", core.Asked.Last().Settings!["importPwads"]);
			Assert.IsNotNull(form.Answer);
		}
	}
}
