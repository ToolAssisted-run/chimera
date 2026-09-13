using System.IO;

using Chimera.Client.Common;
using Chimera.Emulation.Common.Engine;

namespace Chimera.Tests.Client.Common.Movie
{
	/// <summary>
	/// The work in progress, after a crash nothing could catch (docs/project.md, "Recovery"): the
	/// engine's input journal and the project snapshot a session leaves behind are rebuilt into a
	/// separate project file, and a session that is still running is never mistaken for a crash.
	/// </summary>
	[TestClass]
	public class ProjectRecoveryTests
	{
		private string _dir = "";

		[TestInitialize]
		public void Setup()
		{
			// Everything under a folder of its own, and no CHIMERA_DATA_HOME: that is process-wide, and
			// test classes run side by side - setting it here moved another class's cache mid-test.
			_dir = Path.Combine(Path.GetTempPath(), "chimera-recovery-" + Path.GetRandomFileName());
			Directory.CreateDirectory(_dir);
		}

		[TestCleanup]
		public void Teardown()
		{
			try { Directory.Delete(_dir, recursive: true); } catch { }
		}

		private const string SavedLog = "[Input]\nLogKey:#P1 A|P1 B|\n|..|\n|A.|\n[/Input]\n";

		/// <summary>A project as it was last saved: two frames of input and a title.</summary>
		private string SaveProject(string title, string log = SavedLog)
		{
			var path = Path.Combine(_dir, "run.chimeraProject");
			using var project = EngineProject.New();
			project.Title = title;
			project.LogText = log;
			project.Save(path);
			return path;
		}

		/// <summary>The recovery folder a session would keep for the project (the per-user cache's, in real use).</summary>
		private string RecoveryDirOf(string projectPath)
		{
			var dir = Path.Combine(_dir, "recovery");
			Directory.CreateDirectory(dir);
			return dir;
		}

		/// <summary>A process id no running process has, and a start time no process had.</summary>
		private static void CrashedSession(string dir, string projectPath)
			=> ProjectRecovery.WriteSession(dir, new ProjectRecovery.SessionRecord
			{
				ProcessId = int.MaxValue,
				ProcessStartedUtcTicks = 1,
				ProjectPath = projectPath,
			});

		/// <summary>The session's inputs, journaled as they were entered - and the process gone without a close.</summary>
		private static void JournalWorkThenDie(string dir, Action<EngineMovieLog> work)
		{
			var log = new EngineMovieLog();
			Assert.IsTrue(log.Parse(SavedLog, out _));
			Assert.IsTrue(log.JournalTo(Path.Combine(dir, "inputs.journal")));
			work(log);
			log.Dispose();   // freed, never closed: what a crash leaves
		}

		private static string LogOf(string projectPath)
		{
			using var project = EngineProject.Open(projectPath);
			return project.LogText;
		}

		[TestMethod]
		public void EveryInputEnteredBeforeTheCrashIsRecovered()
		{
			var projectPath = SaveProject("before the crash");
			var dir = RecoveryDirOf(projectPath);
			File.Copy(projectPath, Path.Combine(dir, "snapshot.chimeraProject"));
			CrashedSession(dir, projectPath);
			JournalWorkThenDie(dir, static log =>
			{
				log.Add("|.B|");
				log.Add("|AB|");
				log.Set(0, "|A.|");
				log.Insert(1, "|..|");
			});

			var leftover = ProjectRecovery.FindUnfinishedIn(RecoveryDirOf(projectPath));
			Assert.IsNotNull(leftover, "a session whose process is gone left work behind");
			Assert.AreEqual(projectPath, leftover.ProjectPath);

			var backups = Path.Combine(_dir, "Backups");
			var copy = ProjectRecovery.BuildRecoveredCopy(leftover, projectPath, backups);
			StringAssert.StartsWith(copy, backups);
			StringAssert.Contains(Path.GetFileName(copy), "run.recovered ");
			Assert.AreEqual("[Input]\nLogKey:#P1 A|P1 B|\n|A.|\n|..|\n|A.|\n|.B|\n|AB|\n[/Input]\n", LogOf(copy));
			Assert.AreEqual(SavedLog, LogOf(projectPath), "the project itself is never overwritten");

			ProjectRecovery.Discard(leftover);
			Assert.IsNull(ProjectRecovery.FindUnfinishedIn(dir), "once kept elsewhere, it is not offered again");
		}

		[TestMethod]
		public void AProjectRunningRightNowIsNotACrash()
		{
			var projectPath = SaveProject("open elsewhere");
			var dir = RecoveryDirOf(projectPath);
			File.Copy(projectPath, Path.Combine(dir, "snapshot.chimeraProject"));
			using (var me = System.Diagnostics.Process.GetCurrentProcess())
			{
				ProjectRecovery.WriteSession(dir, new ProjectRecovery.SessionRecord
				{
					ProcessId = me.Id,
					ProcessStartedUtcTicks = me.StartTime.ToUniversalTime().Ticks,
					ProjectPath = projectPath,
				});
			}
			JournalWorkThenDie(dir, static log => log.Add("|AB|"));
			Assert.IsNull(ProjectRecovery.FindUnfinishedIn(RecoveryDirOf(projectPath)));
			Assert.IsTrue(Directory.Exists(dir), "and its files are left alone");
		}

		[TestMethod]
		public void AProjectSavedAfterTheSnapshotIsTheBase()
		{
			// markers, branches and settings come from the newer of the two; inputs from the journal
			var projectPath = SaveProject("old title");
			var dir = RecoveryDirOf(projectPath);
			var snapshot = Path.Combine(dir, "snapshot.chimeraProject");
			File.Copy(projectPath, snapshot);
			File.SetLastWriteTimeUtc(snapshot, DateTime.UtcNow.AddMinutes(-10));
			using (var project = EngineProject.Open(projectPath))
			{
				project.Title = "saved since";
				project.Save(projectPath);
			}
			CrashedSession(dir, projectPath);
			JournalWorkThenDie(dir, static log => log.Add("|.B|"));

			var copy = ProjectRecovery.BuildRecoveredCopy(ProjectRecovery.FindUnfinishedIn(RecoveryDirOf(projectPath))!, projectPath, Path.Combine(_dir, "Backups"));
			using var recovered = EngineProject.Open(copy);
			Assert.AreEqual("saved since", recovered.Title);
			StringAssert.Contains(recovered.LogText, "|.B|");
		}

		[TestMethod]
		public void WithoutAJournalTheSnapshotIsTheWork()
		{
			var projectPath = SaveProject("saved");
			var dir = RecoveryDirOf(projectPath);
			var snapshot = Path.Combine(dir, "snapshot.chimeraProject");
			using (var project = EngineProject.Open(projectPath))
			{
				project.Title = "unsaved title";
				project.LogText = "[Input]\nLogKey:#P1 A|P1 B|\n|AB|\n[/Input]\n";
				project.Save(snapshot);
			}
			File.SetLastWriteTimeUtc(projectPath, DateTime.UtcNow.AddMinutes(-10));
			CrashedSession(dir, projectPath);

			var copy = ProjectRecovery.BuildRecoveredCopy(ProjectRecovery.FindUnfinishedIn(RecoveryDirOf(projectPath))!, projectPath, Path.Combine(_dir, "Backups"));
			using var recovered = EngineProject.Open(copy);
			Assert.AreEqual("unsaved title", recovered.Title);
			StringAssert.Contains(recovered.LogText, "|AB|");
		}

		[TestMethod]
		public void MarkersAndBranchesComeBackFromTheJournal()
		{
			// the project as last saved has none; everything below is work the journal alone holds
			var projectPath = SaveProject("markers and branches");
			var dir = RecoveryDirOf(projectPath);
			CrashedSession(dir, projectPath);
			var log = new EngineMovieLog();
			Assert.IsTrue(log.Parse(SavedLog, out _));
			Assert.IsTrue(log.JournalTo(Path.Combine(dir, "work.journal"), "M []\nO []\n"));
			log.JournalNote("""M [[1,false,"the jump"],[5,true,"caf\u00e9"]]""");
			log.JournalNote("""B {"id":"a","text":"route A","frame":1,"time":"2026-09-13T20:00:00.0000000Z","log":"|..|\n|A.|\n","markers":[[0,true,"start"]]}""");
			log.JournalNote("""B {"id":"b","text":"route B","frame":2,"time":"2026-09-13T20:01:00.0000000Z","log":"|.B|\n","markers":[]}""");
			log.JournalNote("""O ["b","a"]""");
			// and an edit afterwards, which is the one that counts
			log.JournalNote("""B {"id":"a","text":"route A, renamed","frame":1,"time":"2026-09-13T20:00:00.0000000Z","log":"|..|\n|A.|\n","markers":[[0,true,"start"]]}""");
			log.Dispose();

			var copy = ProjectRecovery.BuildRecoveredCopy(ProjectRecovery.FindUnfinishedIn(dir)!, projectPath, Path.Combine(_dir, "Backups"));
			using var recovered = EngineProject.Open(copy);
			Assert.AreEqual(2, recovered.MarkerCount);
			Assert.AreEqual(1, recovered.MarkerFrame(0));
			Assert.AreEqual(5, recovered.MarkerFrame(1));
			Assert.AreEqual(2, recovered.BranchCount, "the order record says which branches exist");
			Assert.AreEqual("route B", recovered.BranchName(0), "and in which order");
			Assert.AreEqual("route A, renamed", recovered.BranchName(1), "the last record of a branch is the branch");
			Assert.AreEqual("|..|\n|A.|\n", recovered.BranchLogText(1));
			Assert.AreEqual(1, recovered.BranchMarkerCount(1));
			StringAssert.Contains(recovered.LogText, "|..|", "and the inputs came back with them");
		}

		[TestMethod]
		public void AFolderWithNothingInItOffersNothing()
		{
			var projectPath = SaveProject("nothing to recover");
			var dir = RecoveryDirOf(projectPath);
			CrashedSession(dir, projectPath);
			Assert.IsNull(ProjectRecovery.FindUnfinishedIn(dir));
			Assert.IsFalse(Directory.Exists(dir), "and the empty folder is tidied away");
		}
	}
}
