using System.IO;

using Chimera.Client.Common;

namespace Chimera.Tests.Client.Common.Movie
{
	/// <summary>
	/// A core's import answer (engine.h, ce_import_movie), read as the frontend
	/// builds a project from it: a refusal is its sentence and nothing else, and an
	/// answer keeps the core's file order, which is the order the files load in.
	/// </summary>
	[TestClass]
	public class MovieImportAnswerTests
	{
		[TestMethod]
		public void ARefusalIsItsSentence()
		{
			var answer = MovieImportAnswer.Parse("""{"error":"players 5 are in the game; the core has four"}""");
			Assert.AreEqual("players 5 are in the game; the core has four", answer.Error);
		}

		[TestMethod]
		public void NoAnswerIsACoreWithoutAnImporter()
			=> Assert.IsNotNull(MovieImportAnswer.Parse("").Error);

		[TestMethod]
		public void AnAnswerWithoutInputIsRefused()
			=> Assert.IsNotNull(MovieImportAnswer.Parse("""{"format":"Doom 1.9","frames":0}""").Error);

		[TestMethod]
		public void TheAnswerIsReadWhole()
		{
			var answer = MovieImportAnswer.Parse("""
				{
				  "format": "MBF21", "tics": 2, "frames": 2,
				  "game": "doom2", "version": "doom2-1.9",
				  "settings": { "version": "doom2-1.9", "skill": "4", "soloNet": false, "map": 1 },
				  "firmware": [ { "id": "DOOM2.WAD", "sha1": "7ec7652fcfce8ddc6e801839291f0e28ef1d5ae7" } ],
				  "files": [ { "name": "z.wad", "sha1": "aa", "slot": "pwad" }, { "name": "a.deh", "sha1": "bb", "slot": "pwad" } ],
				  "notes": [ "a note", "" ],
				  "input": "[Input]\nLogKey:#Fire|\n|.|\n|F|\n[/Input]\n"
				}
				""");
			Assert.IsNull(answer.Error);
			Assert.AreEqual("MBF21", answer.Format);
			Assert.AreEqual(2, answer.Frames);
			Assert.AreEqual("doom2", answer.Game);
			Assert.AreEqual("4", answer.Settings["skill"]);
			Assert.IsFalse((bool)answer.Settings["soloNet"]);
			Assert.AreEqual(1L, answer.Settings["map"]);
			Assert.AreEqual(("DOOM2.WAD", "7ec7652fcfce8ddc6e801839291f0e28ef1d5ae7"), answer.Firmware[0]);
			CollectionAssert.AreEqual(new[] { "z.wad", "a.deh" }, answer.Files.ConvertAll(static f => f.Name), "the core's order, not sorted");
			CollectionAssert.AreEqual(new[] { "a note" }, answer.Notes);
			StringAssert.StartsWith(answer.Input, "[Input]");
		}

		[TestMethod]
		public void TheRequestRoundTripsThroughItsFile()
		{
			var path = Path.GetTempFileName();
			try
			{
				MovieImportRequest request = new() { Package = "/cores/dsda.chimeraCore", Movie = "/demos/run.lmp", Settings = new() { ["importIwad"] = "DOOM2.WAD", ["importLongtics"] = true } };
				request.Files.Add(new() { Name = "DOOM2.WAD", Path = "/iwads/DOOM2.WAD" });
				request.Write(path);
				var back = MovieImportRequest.Read(path);
				Assert.AreEqual(request.Package, back.Package);
				Assert.AreEqual(request.Movie, back.Movie);
				Assert.AreEqual("/iwads/DOOM2.WAD", back.Files[0].Path);
				Assert.AreEqual("""{"importIwad":"DOOM2.WAD","importLongtics":true}""", back.SettingsJson);
			}
			finally
			{
				File.Delete(path);
			}
		}
	}
}
