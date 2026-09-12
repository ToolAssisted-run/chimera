using System.IO;

using Chimera.Client.Common;

namespace Chimera.Tests.Client.Common.Movie
{
	/// <summary>
	/// Saving a project writes the state history, and that is the longest thing
	/// a save does: the budgets it is written against are four gigabytes in
	/// memory and ten on disk, which measured ten seconds to a Linux disk and
	/// over a minute to NTFS - on the thread that runs the machine, fired every
	/// thirty minutes by TAStudio's autosave without anybody asking for it.
	///
	/// So it is QUEUED now (IStateHistory.SaveLater) and waited for where the
	/// session is closed. What is worth pinning here is the half a unit test can
	/// see: that a save asks for the queued one rather than the blocking one,
	/// that it names the machine, and that the movie does not reach into a
	/// history it has already let go of - which is how the first version of this
	/// crashed a four thousand frame soak on the way out.
	/// </summary>
	[TestClass]
	public class TasMovieHistorySaveTests
	{
		private static string TempDir()
		{
			var dir = Path.Combine(Path.GetTempPath(), "chimera-tasproj-save-" + Path.GetRandomFileName());
			Directory.CreateDirectory(dir);
			return dir;
		}

		[TestMethod]
		public void ASaveQueuesTheHistoryRatherThanWaitingForIt()
		{
			var dir = TempDir();
			try
			{
				FakeEmulator emu = new();
				FakeMovieSession session = new(emu);
				TasMovie movie = new(session, Path.Combine(dir, "run.chimeraProject"));
				session.Movie = movie;
				movie.Attach(emu);
				movie.InsertEmptyFrame(0, 8);

				var result = movie.Save();

				Assert.IsFalse(result.IsError, result.Exception?.Message ?? "the save failed");
				Assert.AreEqual(1, emu.SavesQueued, "the history was not asked for a queued save");
				Assert.IsNotNull(emu.LastSaveMachineId, "the history was saved without being told which machine it belongs to");
			}
			finally
			{
				Directory.Delete(dir, recursive: true);
			}
		}

		/// <summary>
		/// A movie is disposed whenever a project closes, and by then the
		/// emulator it borrowed may already be gone - so this must not call into
		/// it. The barrier for a queued save belongs to the session, which is the
		/// thing that knows it is still alive. Pinned because the crash it caused
		/// was on the way out of a run that had otherwise survived everything.
		/// </summary>
		[TestMethod]
		public void DisposingTheMovieDoesNotReachIntoTheHistory()
		{
			var dir = TempDir();
			try
			{
				FakeEmulator emu = new();
				FakeMovieSession session = new(emu);
				TasMovie movie = new(session, Path.Combine(dir, "run.chimeraProject"));
				session.Movie = movie;
				movie.Attach(emu);
				movie.InsertEmptyFrame(0, 4);

				var queuedBefore = emu.SavesQueued;
				var inLineBefore = emu.SavesInLine;
				movie.Dispose();

				Assert.AreEqual(queuedBefore, emu.SavesQueued);
				Assert.AreEqual(inLineBefore, emu.SavesInLine);
			}
			finally
			{
				Directory.Delete(dir, recursive: true);
			}
		}
	}
}
