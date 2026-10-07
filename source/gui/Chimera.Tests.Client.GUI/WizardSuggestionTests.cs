using System;
using System.Collections.Generic;

using Chimera.Client.GUI;
using Chimera.Emulation.Common.Waterbox;

namespace Chimera.Tests.Client.GUI
{
	/// <summary>
	/// Suggested settings: a core that can say what it would choose for a game
	/// (the RPCS3 core, from the RPCS3 wiki) is asked when the settings page is
	/// reached, and the answer lands in the settings as VALUES, with a note above
	/// the grid saying where they came from (user, 2026-09-23).
	///
	/// States covered: a core that suggests nothing (no lookup, no note), a game
	/// found (values applied, source shown), a game not found (nothing moves, the
	/// note says so, every setting still there), coming back to the page (edits
	/// survive), a different game (the previous game's values are taken back),
	/// a lookup that failed, and a game with settings of its own (an arcade
	/// game's dip switches): they join the grid, and leave it with the game.
	/// </summary>
	[TestClass]
	public class WizardSuggestionTests
	{
		private const string Declaration = """
			{
			  "slots": [
			    { "id": "game", "title": "Game", "min": 1, "max": 1, "formats": ["iso"] }
			  ]
			}
			""";

		private static WaterboxConfig Package(bool suggests) => TestPackages.Config($$"""
			{
			  "coreName": "PS3-shaped",
			  "systemId": "PS3",
			  "suggestSettings": {{(suggests ? "true" : "false")}},
			  "video": { "width": 1280, "height": 720 },
			  "audio": { "samplesPerFrame": 800 },
			  "input": { "buttons": [] },
			  "settings": [
			    { "name": "writeColorBuffers", "display": "Write color buffers", "type": "bool", "default": false },
			    { "name": "frameLimit", "display": "Framelimit", "type": "enum",
			      "options": ["Off", "60", "PS3 Native"], "default": "PS3 Native" },
			    { "name": "resolutionScaleThreshold", "type": "int", "default": 16 },
			    { "name": "spuBlockSize", "type": "enum", "options": ["Safe", "Mega"], "default": "Safe" }
			  ]
			}
			""");

		private const string Found = """
			{"title_id":"BLUS30000","values":{"writeColorBuffers":true,"frameLimit":"60","resolutionScaleThreshold":320},
			 "note":"RPCS3 compatibility list (2026-09-22): Game [BLUS30000] is Playable. Source: https://wiki.rpcs3.net/index.php?curid=1 (CC BY-SA 4.0)"}
			""";

		private const string Other = """
			{"title_id":"BLES00001","values":{"spuBlockSize":"Mega"},"note":"Other game, from the wiki."}
			""";

		private const string NotFound = """
			{"title_id":"NPUB99999","values":{},"note":"RPCS3 compatibility: NPUB99999 is not in the RPCS3 compatibility list or wiki (https://wiki.rpcs3.net/, snapshot 2026-09-22)."}
			""";

		/// <summary>
		/// A game with settings of its own (an arcade game's dip switches), one of
		/// which tries to redefine a package setting and must not.
		/// </summary>
		private const string WithDips = """
			{"values":{},"note":"This game's dip switches are below.",
			 "settings":[
			  {"name":"dip.Lives","display":"Lives","type":"enum","options":["3","5"],"default":"3"},
			  {"name":"dip.Difficulty","display":"Difficulty","type":"enum","options":["Easy","Hard"],"default":"Easy"},
			  {"name":"frameLimit","display":"Hijack","type":"enum","options":["x"],"default":"x"}
			 ]}
			""";

		private static NewProjectWizard FormWith(WaterboxConfig cfg, Func<string, (string?, string)> source, List<string>? asked = null)
		{
			NewProjectWizard form = new([ ], static _ => [ ]);
			form.Show();
			form.UseDeclaration(TestPackages.Slots(Declaration));
			form.UseSettingsFrom(cfg);
			form.SuggestionSourceForTest = game =>
			{
				asked?.Add(game);
				return source(game);
			};
			return form;
		}

		/// <summary>The value the settings hold, or "(default)" when they hold none and the package's applies.</summary>
		private static string Value(NewProjectWizard form, string name)
			=> form.SettingValue(name) is { } value ? Convert.ToString(value)! : Default;

		private const string Default = "(default)";

		[TestMethod]
		public void ACoreThatSuggestsNothingIsNotAskedAndShowsNoNote()
		{
			var asked = new List<string>();
			using var form = FormWith(Package(suggests: false), _ => (Found, ""), asked);
			form.ArriveAtSettingsForTest("game.iso");
			Assert.AreEqual(0, asked.Count, "a core that declares no suggestions must not be loaded to ask");
			Assert.AreEqual("", form.SuggestionNoteText);
			Assert.AreEqual(Default, Value(form, "writeColorBuffers"));
		}

		[TestMethod]
		public void AFoundGameHasItsValuesAppliedAndItsSourceShown()
		{
			using var form = FormWith(Package(suggests: true), _ => (Found, ""));
			form.ArriveAtSettingsForTest("game.iso");
			Assert.AreEqual("True", Value(form, "writeColorBuffers"));
			Assert.AreEqual("60", Value(form, "frameLimit"));
			Assert.AreEqual("320", Value(form, "resolutionScaleThreshold"), "a number arrives as the declared int");
			Assert.AreEqual(Default, Value(form, "spuBlockSize"), "a setting the wiki does not name keeps its default");
			StringAssert.Contains(form.SuggestionNoteText, "https://wiki.rpcs3.net/index.php?curid=1");
			StringAssert.Contains(form.SuggestionNoteText, "Playable", "the compatibility level is shown");
			StringAssert.Contains(form.SuggestionNoteText, "Applied below: Write color buffers, Framelimit");
		}

		[TestMethod]
		public void AGameNotFoundMovesNothingAndSaysSo()
		{
			using var form = FormWith(Package(suggests: true), _ => (NotFound, ""));
			form.ArriveAtSettingsForTest("game.iso");
			Assert.AreEqual(Default, Value(form, "writeColorBuffers"));
			Assert.AreEqual(Default, Value(form, "frameLimit"));
			StringAssert.Contains(form.SuggestionNoteText, "not in the RPCS3 compatibility list or wiki");
			// the settings are all still there to experiment with
			form.SetSettingValue("frameLimit", "Off");
			Assert.AreEqual("Off", Value(form, "frameLimit"));
		}

		[TestMethod]
		public void ComingBackToThePageKeepsTheEdits()
		{
			var asked = new List<string>();
			using var form = FormWith(Package(suggests: true), _ => (Found, ""), asked);
			form.ArriveAtSettingsForTest("game.iso");
			form.SetSettingValue("frameLimit", "Off");
			form.ArriveAtSettingsForTest("game.iso");
			Assert.AreEqual(1, asked.Count, "the same game is looked up once");
			Assert.AreEqual("Off", Value(form, "frameLimit"), "an edit made after the suggestion must survive Back and Next");
		}

		[TestMethod]
		public void ADifferentGameTakesBackWhatThePreviousOneSet()
		{
			using var form = FormWith(Package(suggests: true), game => (game == "a.iso" ? Found : Other, ""));
			form.ArriveAtSettingsForTest("a.iso");
			Assert.AreEqual("True", Value(form, "writeColorBuffers"));
			form.ArriveAtSettingsForTest("b.iso");
			Assert.AreEqual(Default, Value(form, "writeColorBuffers"), "game A's recommendation is not game B's");
			Assert.AreEqual(Default, Value(form, "frameLimit"));
			Assert.AreEqual("Mega", Value(form, "spuBlockSize"));
			StringAssert.Contains(form.SuggestionNoteText, "Other game");
		}

		[TestMethod]
		public void AGamesOwnSettingsJoinTheGrid()
		{
			using var form = FormWith(Package(suggests: true), _ => (WithDips, ""));
			form.ArriveAtSettingsForTest("game.iso");
			var names = form.ExposedSettingNames;
			CollectionAssert.Contains(names, "dip.Lives");
			CollectionAssert.Contains(names, "dip.Difficulty");
			Assert.AreEqual(1, Array.FindAll(names, static n => n == "frameLimit").Length,
				"a game's setting cannot redefine the package's");
			Assert.AreEqual("3", Convert.ToString(form.SettingDefault("dip.Lives")), "the core's default is the row's default");
			form.SetSettingValue("dip.Lives", "5");
			Assert.AreEqual("5", Value(form, "dip.Lives"));
			Assert.AreEqual(Default, Value(form, "frameLimit"));
		}

		[TestMethod]
		public void AnotherGameTakesTheLastGamesSettingsAway()
		{
			using var form = FormWith(Package(suggests: true), game => (game == "a.iso" ? WithDips : Other, ""));
			form.ArriveAtSettingsForTest("a.iso");
			form.SetSettingValue("dip.Lives", "5");
			form.ArriveAtSettingsForTest("b.iso");
			CollectionAssert.DoesNotContain(form.ExposedSettingNames, "dip.Lives", "game A's switches are not game B's");
			Assert.AreEqual(Default, Value(form, "dip.Lives"), "and what was set on them goes too");
		}

		[TestMethod]
		public void UnreadableGameSettingsAreIgnored()
		{
			CollectionAssert.AreEqual(new List<WaterboxConfig.SettingDecl>(),
				WaterboxConfig.ParseGameSettings("not json", [ ]));
			Assert.AreEqual(1, WaterboxConfig.ParseGameSettings(
				"""[{"name":"dip.A","options":["x"]},{"display":"no name"},{"name":"dip.A"}]""", [ ]).Count,
				"nameless and repeated entries are dropped");
		}

		/// <summary>
		/// Issue #214: the wizard opens on the last project's answers. Those hold
		/// what the core suggested for THAT project's game, and a game the core
		/// knows nothing about kept them.
		/// </summary>
		[TestMethod]
		public void ASeededWizardTakesBackTheSeededGamesSuggestionForAnotherGame()
		{
			using var form = FormWith(Package(suggests: true), game => (game == "last.iso" ? Found : NotFound, ""));
			// the last project: made for last.iso with what was suggested for it,
			// one suggested value changed by hand, and one setting of the user's own
			form.SeedSettingsForTest(
				"""{"writeColorBuffers":true,"frameLimit":"Off","resolutionScaleThreshold":320,"spuBlockSize":"Mega"}""",
				"last.iso");

			form.ArriveAtSettingsForTest("new.iso");

			Assert.AreEqual(Default, Value(form, "writeColorBuffers"), "suggested for the last game, not for this one");
			Assert.AreEqual(Default, Value(form, "resolutionScaleThreshold"), "likewise, compared as the declared int");
			Assert.AreEqual("Off", Value(form, "frameLimit"), "the user had moved this away from the suggestion: it is theirs");
			Assert.AreEqual("Mega", Value(form, "spuBlockSize"), "never suggested: the last project's answer carries over");
		}

		/// <summary>The seeded game itself keeps the project's settings, as it did.</summary>
		[TestMethod]
		public void ASeededWizardKeepsTheProjectsSettingsForItsOwnGame()
		{
			using var form = FormWith(Package(suggests: true), _ => (Found, ""));
			form.SeedSettingsForTest("""{"writeColorBuffers":true,"frameLimit":"Off"}""", "last.iso");
			form.ArriveAtSettingsForTest("last.iso");
			Assert.AreEqual("True", Value(form, "writeColorBuffers"));
			Assert.AreEqual("Off", Value(form, "frameLimit"), "not put back to the suggested 60");
			StringAssert.Contains(form.SuggestionNoteText, "This project's own settings are kept.");
		}

		[TestMethod]
		public void AFailedLookupSaysWhyAndMovesNothing()
		{
			using var form = FormWith(Package(suggests: true), _ => (null, "the core took too long to answer"));
			form.ArriveAtSettingsForTest("game.iso");
			Assert.AreEqual(Default, Value(form, "writeColorBuffers"));
			StringAssert.Contains(form.SuggestionNoteText, "the core took too long to answer");
		}
	}
}
