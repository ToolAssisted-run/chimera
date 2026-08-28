using System.IO;
using System.Linq;

using Chimera.Client.Common;

namespace Chimera.Tests.Client.Common.Movie
{
	/// <summary>
	/// The JSON .chimeraProject as TasMovie's native format: everything that is
	/// work survives a save-load round trip through the project file alone,
	/// and the cache sibling carries only what may be lost (docs/project.md).
	/// </summary>
	[TestClass]
	public class TasMovieProjectFormatTests
	{
		private static string _dir = "";

		[ClassInitialize]
		public static void MakePlayground(TestContext _)
		{
			_dir = Path.Combine(Path.GetTempPath(), $"chimera-tasmovie-project-{System.Diagnostics.Process.GetCurrentProcess().Id}");
			Directory.CreateDirectory(_dir);
		}

		[ClassCleanup]
		public static void RemovePlayground() => Directory.Delete(_dir, recursive: true);

		private static TasMovie MakeWorkedMovie(string path)
		{
			FakeEmulator emu = new();
			FakeMovieSession session = new(emu);
			TasMovie movie = new(session, path);
			session.Movie = movie;
			movie.Attach(emu);
			movie.InsertEmptyFrame(0, 6);
			movie.SetBoolState(3, "A", true);

			movie.HeaderEntries[HeaderKeys.Author] = "sergio";
			movie.HeaderEntries[HeaderKeys.Core] = "quickernes";
			movie.HeaderEntries[HeaderKeys.CoreVersion] = "abc123+local";
			movie.HeaderEntries[HeaderKeys.CorePackageSha1] = new string('B', 40);
			movie.HeaderEntries[HeaderKeys.GameName] = "sprilo";
			movie.Rerecords = 77;
			movie.SettingsJson = """{"region":"ntsc"}""";
			movie.Comments.Add("first comment");
			movie.Comments.Add("second comment");
			movie.Subtitles.AddFromString("subtitle 2 10 10 120 FFFFFFFF hi there");

			movie.Markers.Add(new TasMovieMarker(2, "the jump") { WantsState = false }, skipHistory: true);

			var branch = new TasBranch
			{
				Frame = 4,
				UserText = "risky route",
				TimeStamp = new DateTime(2026, 8, 26, 21, 0, 0, DateTimeKind.Utc),
				CoreData = [1, 2, 3, 4],
				InputLog = StringLogUtil.MakeStringLog(),
				Markers = new TasMovieMarkerList(movie),
			};
			branch.InputLog.Add(movie.GetInputLogEntry(0));
			branch.InputLog.Add(movie.GetInputLogEntry(3));
			branch.Markers.Add(new TasMovieMarker(1, "setup"), skipHistory: true);
			movie.Branches.Add(branch);

			return movie;
		}

		private static TasMovie LoadFresh(string path)
		{
			FakeEmulator emu = new();
			FakeMovieSession session = new(emu);
			TasMovie movie = new(session, path);
			session.Movie = movie;
			Assert.IsTrue(movie.Load(), "the project should load");
			movie.Attach(emu);
			return movie;
		}

		[TestMethod]
		public void TheRunsOwnMarkersAreNotWrittenAndDoNotAccumulate()
		{
			// They are derived on load - writing them down would let them go stale,
			// and reading them back as ordinary markers would breed three more of
			// them on every save.
			var path = Path.Combine(_dir, "permanent-markers.chimeraProject");
			MakeWorkedMovie(path).Save();

			var reloaded = LoadFresh(path);
			Assert.AreEqual(3, reloaded.Markers.Count(static m => m.IsPermanent),
				"exactly the three the run derives");
			CollectionAssert.AreEqual(
				new[] { "the jump" },
				reloaded.Markers.Where(static m => !m.IsPermanent).Select(static m => m.Message).ToArray(),
				"and nothing of theirs left behind as somebody's own marker");

			// and again, because accumulation only shows on the second pass
			var twice = Path.Combine(_dir, "permanent-markers-twice.chimeraProject");
			reloaded.Filename = twice;
			reloaded.Save();
			var third = LoadFresh(twice);
			Assert.AreEqual(1, third.Markers.Count(static m => !m.IsPermanent),
				"a second round trip adds none either");
		}

		[TestMethod]
		public void TheWorkRoundTripsThroughTheProjectAlone()
		{
			var path = Path.Combine(_dir, "roundtrip.chimeraProject");
			var movie = MakeWorkedMovie(path);
			Assert.IsFalse(movie.Save().IsError);

			// it IS the JSON project format, with the cache beside it
			using (var fs = File.OpenRead(path))
			{
				Assert.AreEqual('{', (char)fs.ReadByte());
			}
			Assert.IsTrue(File.Exists(Path.ChangeExtension(path, "chimeraGreenZone")),
				$"greenzone missing; dir has: {string.Join(",", Directory.GetFiles(_dir).Select(Path.GetFileName))}");

			var loaded = LoadFresh(path);
			Assert.AreEqual(6, loaded.InputLogLength);
			Assert.AreEqual(movie.GetInputLogEntry(3), loaded.GetInputLogEntry(3));
			Assert.AreEqual("sergio", loaded.HeaderEntries[HeaderKeys.Author]);
			Assert.AreEqual("quickernes", loaded.HeaderEntries[HeaderKeys.Core]);
			Assert.AreEqual("abc123+local", loaded.HeaderEntries[HeaderKeys.CoreVersion]);
			Assert.AreEqual(new string('B', 40), loaded.HeaderEntries[HeaderKeys.CorePackageSha1]);
			Assert.AreEqual("sprilo", loaded.HeaderEntries[HeaderKeys.GameName]);
			Assert.AreEqual(77UL, loaded.Rerecords);
			StringAssert.Contains(loaded.SettingsJson, "\"region\":\"ntsc\"");
			CollectionAssert.AreEqual(new[] { "first comment", "second comment" }, loaded.Comments.ToArray());
			Assert.AreEqual(1, loaded.Subtitles.Count);

			var marker = loaded.Markers.Single(m => m.Frame is 2);
			Assert.AreEqual("the jump", marker.Message);
			Assert.IsFalse(marker.WantsState);

			Assert.AreEqual(1, loaded.Branches.Count);
			var branch = loaded.Branches[0];
			Assert.AreEqual("risky route", branch.UserText);
			Assert.AreEqual(4, branch.Frame);
			Assert.AreEqual(new DateTime(2026, 8, 26, 21, 0, 0, DateTimeKind.Utc), branch.TimeStamp.ToUniversalTime());
			Assert.AreEqual(2, branch.InputLog.Count);
			Assert.AreEqual("setup", branch.Markers.Single().Message);
			CollectionAssert.AreEqual(new byte[] { 1, 2, 3, 4 }, branch.CoreData, "the state came from the cache");
		}

		[TestMethod]
		public void ALostCacheCostsRecomputationNeverWork()
		{
			var path = Path.Combine(_dir, "nocache.chimeraProject");
			var movie = MakeWorkedMovie(path);
			Assert.IsFalse(movie.Save().IsError);
			File.Delete(Path.ChangeExtension(path, "chimeraGreenZone"));

			var loaded = LoadFresh(path);
			Assert.AreEqual(6, loaded.InputLogLength, "the input log is work, not cache");
			Assert.AreEqual(1, loaded.Branches.Count, "the branch itself is work");
			Assert.AreEqual("risky route", loaded.Branches[0].UserText);
			Assert.IsNull(loaded.Branches[0].CoreData, "only its state was cache");
		}

		[TestMethod]
		public void TheWizardSideOfTheProjectSurvivesAMovieRoundTrip()
		{
			// a project born in the creation wizard carries a manifest and a
			// description the movie machinery knows nothing about; saving the
			// movie must preserve them
			var path = Path.Combine(_dir, "wizard.chimeraProject");
			var gamePath = Path.Combine(_dir, "game.nes");
			File.WriteAllText(gamePath, "cartridge bytes");
			using (var p = Chimera.Emulation.Common.Engine.EngineProject.New())
			{
				p.Title = "sprilo";
				p.Description = "the wizard's description";
				p.SetCore("quickernes", "abc123+local", new string('B', 40));
				p.FileAdd("game.nes", "rom", gamePath);
				p.Save(path);
			}

			var movie = LoadFresh(path);
			Assert.AreEqual("sprilo", movie.HeaderEntries[HeaderKeys.GameName]);
			movie.InsertEmptyFrame(0, 3);
			Assert.IsFalse(movie.Save().IsError);

			using var reloaded = Chimera.Emulation.Common.Engine.EngineProject.Open(path);
			Assert.AreEqual(1, reloaded.FileCount, "the manifest survived the movie save");
			Assert.AreEqual("game.nes", reloaded.FileName(0));
			Assert.AreEqual("the wizard's description", reloaded.Description);
			StringAssert.Contains(reloaded.LogText, "|", "and the input log is in the project now");
		}

		/// <summary>
		/// A project made by the wizard pins its core; the movie that starts from it
		/// has no headers at all yet. Saving must not read that silence as "no core"
		/// and unpin the project - a project nothing can run, which is what the
		/// frontend then says when you reopen it.
		/// </summary>
		[TestMethod]
		public void SavingAMovieThatIsSilentAboutTheCoreKeepsThePin()
		{
			var path = Path.Combine(_dir, "pinned.chimeraProject");
			using (var p = Chimera.Emulation.Common.Engine.EngineProject.New())
			{
				p.SetCore("quickernes", "abc123+local", new string('B', 40));
				p.Save(path);
			}

			FakeEmulator emu = new();
			FakeMovieSession session = new(emu);
			TasMovie movie = new(session, path);
			session.Movie = movie;
			Assert.IsTrue(movie.Load(), "the project should load");
			movie.Attach(emu);

			// the movie says nothing about the core, the way a brand new one does
			movie.HeaderEntries.Remove(HeaderKeys.Core);
			movie.HeaderEntries.Remove(HeaderKeys.CoreVersion);
			movie.HeaderEntries.Remove(HeaderKeys.CorePackageSha1);
			movie.InsertEmptyFrame(0, 2);
			Assert.IsFalse(movie.Save().IsError);

			using var reloaded = Chimera.Emulation.Common.Engine.EngineProject.Open(path);
			Assert.AreEqual("quickernes", reloaded.CoreName, "the core pin was lost on save");
			Assert.AreEqual("abc123+local", reloaded.CoreVersion);
			Assert.AreEqual(new string('B', 40), reloaded.CoreSha1);
		}
	}
}
