#nullable enable

using System.Diagnostics;
using System.IO;
using System.Linq;
using System.Text;

using Newtonsoft.Json;

using Chimera.Emulation.Common.Engine;

namespace Chimera.Client.Common
{
	/// <summary>
	/// The work in progress, kept where a crash cannot take it (docs/project.md, "Recovery").
	///
	/// Not every crash can be caught. A GPU driver that fast-fails, a killed process and a power cut
	/// all end Chimera without running a line of it, so nothing here waits for a crash to happen.
	/// While a project is open, every change to its inputs is journaled by the engine the moment it is
	/// made (<see cref="EngineMovieLog.JournalTo"/>), and while there is unsaved work a snapshot of the
	/// whole project - markers, branches, settings - is rewritten every few seconds. Closing the project
	/// removes both. A project whose recovery files outlived the process that wrote them is rebuilt,
	/// on the next open, into a separate file: recovered work never overwrites a project unasked.
	/// </summary>
	public sealed class ProjectRecovery
	{
		public const string FolderName = "recovery";

		private const string JournalFile = "inputs.journal";

		private const string SnapshotFile = "snapshot.chimeraProject";

		private const string SessionFile = "session.json";

		/// <summary>The shortest time between two snapshots while there is unsaved work.</summary>
		public static readonly TimeSpan SnapshotEvery = TimeSpan.FromSeconds(5);

		/// <summary>Where a project's recovery files live: beside its cache, never beside the project.</summary>
		public static string DirectoryFor(string projectId) => Path.Combine(ProjectCache.DirectoryFor(projectId), FolderName);

		/// <summary>Who wrote a recovery folder - enough to tell a crash from another Chimera that has the project open.</summary>
		internal sealed class SessionRecord
		{
			public int ProcessId { get; set; }

			public long ProcessStartedUtcTicks { get; set; }

			public string ProjectPath { get; set; } = "";
		}

		private readonly TasMovie _movie;

		private readonly string _dir;

		private EngineMovieLog? _journaled;

		private string _fingerprint = "";

		private DateTime _lastSnapshotUtc = DateTime.MinValue;

		private DateTime _lastCheckUtc = DateTime.MinValue;

		private bool _ended;

		private ProjectRecovery(TasMovie movie, string dir)
		{
			_movie = movie;
			_dir = dir;
		}

		private string JournalPath => Path.Combine(_dir, JournalFile);

		private string SnapshotPath => Path.Combine(_dir, SnapshotFile);

		/// <summary>
		/// Starts keeping <paramref name="movie"/>'s work recoverable. Null when there is nowhere to keep
		/// it - said on stderr, never thrown: a session that cannot be protected still runs.
		/// </summary>
		public static ProjectRecovery? Begin(ITasMovie tasMovie, string projectId, string projectPath)
		{
			if (tasMovie is not TasMovie movie || string.IsNullOrEmpty(projectId)) return null;
			try
			{
				var dir = DirectoryFor(projectId);
				Directory.CreateDirectory(dir);
				using (var current = Process.GetCurrentProcess())
				{
					WriteSession(dir, new SessionRecord
					{
						ProcessId = current.Id,
						ProcessStartedUtcTicks = StartedTicks(current),
						ProjectPath = projectPath,
					});
				}
				ProjectRecovery recovery = new(movie, dir);
				movie.InputLogReplaced += recovery.FollowLog;
				// a base to rebuild on from the first moment, even for a project never saved
				recovery.Snapshot();
				recovery.AttachJournal();
				return recovery;
			}
			catch (Exception ex) when (ex is IOException or UnauthorizedAccessException or InvalidOperationException)
			{
				Console.Error.WriteLine($"[recovery] this project's work cannot be kept recoverable: {ex.Message}");
				return null;
			}
		}

		/// <summary>
		/// Called often (the main loop); does anything at most once a second. Keeps the journal on the
		/// movie's current log, and rewrites the snapshot when there is unsaved work it does not hold yet.
		/// Never throws: failing to protect the work must not be what ends it.
		/// </summary>
		public void Tick()
		{
			if (_ended) return;
			var now = DateTime.UtcNow;
			if (now - _lastCheckUtc < TimeSpan.FromSeconds(1)) return;
			_lastCheckUtc = now;
			try
			{
				if (!ReferenceEquals(CurrentLog(), _journaled) || _journaled?.Journaling is not true) AttachJournal();
				if (_movie.Changes && now - _lastSnapshotUtc >= SnapshotEvery && Fingerprint() != _fingerprint) Snapshot();
			}
			catch (Exception ex) when (ex is IOException or UnauthorizedAccessException or InvalidOperationException or ObjectDisposedException)
			{
				Console.Error.WriteLine($"[recovery] {ex.Message}");
			}
		}

		/// <summary>
		/// Something has gone wrong: write the snapshot now rather than at the next tick, so what is kept
		/// is the work as it stands at this moment. The inputs need no such help - they are journaled as
		/// they change. Never throws.
		/// </summary>
		public void SaveNow()
		{
			if (_ended) return;
			try
			{
				Snapshot();
			}
			catch (Exception ex) when (ex is IOException or UnauthorizedAccessException or InvalidOperationException or ObjectDisposedException)
			{
				Console.Error.WriteLine($"[recovery] the snapshot could not be written in time: {ex.Message}");
			}
		}

		/// <summary>
		/// The project is closing. <paramref name="clean"/>: nothing is left to recover - it was saved, or
		/// somebody chose to discard it - so the files go. Otherwise they stay for the next open to offer.
		/// </summary>
		public void End(bool clean)
		{
			if (_ended) return;
			_ended = true;
			_movie.InputLogReplaced -= FollowLog;
			try
			{
				_journaled?.JournalClose(remove: clean);
				_journaled = null;
				if (clean && Directory.Exists(_dir)) Directory.Delete(_dir, recursive: true);
			}
			catch (Exception ex) when (ex is IOException or UnauthorizedAccessException or ObjectDisposedException)
			{
				Console.Error.WriteLine($"[recovery] {ex.Message}");
			}
		}

		/// <summary>A branch load replaced the movie's input log: the journal moves to the new one at once.</summary>
		private void FollowLog()
		{
			if (_ended) return;
			try
			{
				AttachJournal();
			}
			catch (Exception ex) when (ex is IOException or UnauthorizedAccessException or ObjectDisposedException)
			{
				Console.Error.WriteLine($"[recovery] {ex.Message}");
			}
		}

		private EngineMovieLog? CurrentLog() => (_movie.GetLogEntries() as EngineStringLog)?.Engine;

		private void AttachJournal()
		{
			var log = CurrentLog();
			if (log is null) return;
			// the log a branch load let go of has already closed its journal by being freed; a live one is told
			if (_journaled is not null && !ReferenceEquals(_journaled, log)) _journaled.JournalClose(remove: false);
			if (!log.JournalTo(JournalPath))
			{
				Console.Error.WriteLine($"[recovery] the input journal could not be written to {JournalPath}; trying again");
			}
			_journaled = log;
		}

		private void Snapshot()
		{
			var fresh = SnapshotPath + ".new";
			var result = _movie.WriteRecoverySnapshot(fresh);
			if (result.IsError)
			{
				Console.Error.WriteLine($"[recovery] the snapshot was not written: {result.Exception?.Message}");
				return;
			}
			if (File.Exists(SnapshotPath)) File.Replace(fresh, SnapshotPath, destinationBackupFileName: null);
			else File.Move(fresh, SnapshotPath);
			_fingerprint = Fingerprint();
			_lastSnapshotUtc = DateTime.UtcNow;
			// the journal starts again from what the log holds now, which keeps it short
			if (_journaled is not null) AttachJournal();
		}

		/// <summary>What a snapshot holds that the journal does not: markers, branches, rerecords, the log's length.</summary>
		private string Fingerprint()
		{
			StringBuilder sb = new();
			sb.Append(_movie.Rerecords).Append('|').Append(_movie.InputLogLength).Append('|');
			foreach (var marker in _movie.Markers) sb.Append(marker.Frame).Append(':').Append(marker.Message).Append(';');
			sb.Append('|');
			foreach (var branch in _movie.Branches)
			{
				sb.Append(branch.TimeStamp.Ticks).Append(':').Append(branch.Frame).Append(':').Append(branch.UserText).Append(';');
			}
			return sb.ToString();
		}

		// ---- after a crash -----------------------------------------------------------------------

		/// <summary>Recovery files a session left behind without closing its project.</summary>
		public sealed class Leftover
		{
			public string Directory { get; init; } = "";

			/// <summary>Where the project was, as the session that crashed knew it.</summary>
			public string ProjectPath { get; init; } = "";

			/// <summary>The newest moment the files describe.</summary>
			public DateTime LastWorkUtc { get; init; }

			internal string? Snapshot { get; init; }

			internal string? Journal { get; init; }
		}

		/// <summary>
		/// The work a crashed session left for this project, or null: nothing was left, or the process that
		/// wrote it is still running (another Chimera has this project open right now).
		/// </summary>
		public static Leftover? FindUnfinished(string projectId) => FindUnfinishedIn(DirectoryFor(projectId));

		/// <summary>The same, for one recovery folder named outright (tests keep theirs out of the per-user cache).</summary>
		internal static Leftover? FindUnfinishedIn(string dir)
		{
			if (!Directory.Exists(dir)) return null;
			var session = ReadSession(dir);
			if (session is not null && IsRunning(session)) return null;
			var snapshot = Existing(Path.Combine(dir, SnapshotFile));
			var journalPath = Path.Combine(dir, JournalFile);
			var journalOnDisk = Existing(journalPath) ?? Existing(journalPath + ".new");
			if (snapshot is null && journalOnDisk is null)
			{
				Discard(dir);
				return null;
			}
			var last = new[] { snapshot, journalOnDisk }.Where(static p => p is not null).Max(static p => File.GetLastWriteTimeUtc(p!));
			return new Leftover
			{
				Directory = dir,
				ProjectPath = session?.ProjectPath ?? "",
				LastWorkUtc = last,
				Snapshot = snapshot,
				Journal = journalOnDisk is null ? null : journalPath,
			};
		}

		/// <summary>
		/// Rebuilds the crashed session's work into a new project file in <paramref name="backupDirectory"/>
		/// and returns its path. The base is whichever is newer - the last snapshot, or
		/// <paramref name="projectPath"/> as last saved - and its inputs are the journal's, which is never
		/// older than either.
		/// </summary>
		/// <exception cref="InvalidOperationException">there is nothing to rebuild on, or the project cannot be written</exception>
		public static string BuildRecoveredCopy(Leftover leftover, string projectPath, string backupDirectory)
		{
			var basePath = leftover.Snapshot;
			if (File.Exists(projectPath)
				&& (basePath is null || File.GetLastWriteTimeUtc(projectPath) > File.GetLastWriteTimeUtc(basePath)))
			{
				basePath = projectPath;
			}
			if (basePath is null) throw new InvalidOperationException("there is no copy of the project to rebuild the work on");

			using var project = EngineProject.Open(basePath);
			if (leftover.Journal is not null)
			{
				using var inputs = EngineMovieLog.FromJournal(leftover.Journal, out var error);
				if (inputs is null)
				{
					Console.Error.WriteLine($"[recovery] the input journal could not be replayed ({error}); the snapshot's inputs are used");
				}
				else
				{
					if (error.Length is not 0) Console.Error.WriteLine($"[recovery] the input journal ended early: {error}");
					if (inputs.Key is null)
					{
						// a journal that never saw a key keeps the one the project already has
						using EngineMovieLog saved = new();
						if (saved.Parse(project.LogText, out _)) inputs.Key = saved.Key;
					}
					project.LogText = inputs.Serialize(crlf: false);
				}
			}

			Directory.CreateDirectory(backupDirectory);
			var name = string.IsNullOrEmpty(projectPath) ? "project" : Path.GetFileNameWithoutExtension(projectPath);
			var copy = Path.Combine(backupDirectory,
				$"{name}.recovered {leftover.LastWorkUtc.ToLocalTime():yyyy-MM-dd HH.mm.ss}.chimeraProject");
			project.Save(copy);
			return copy;
		}

		/// <summary>Removes a leftover once its work is safely somewhere else (<see cref="BuildRecoveredCopy"/>).</summary>
		public static void Discard(Leftover leftover) => Discard(leftover.Directory);

		private static void Discard(string dir)
		{
			try
			{
				if (Directory.Exists(dir)) Directory.Delete(dir, recursive: true);
			}
			catch (Exception ex) when (ex is IOException or UnauthorizedAccessException)
			{
				Console.Error.WriteLine($"[recovery] {ex.Message}");
			}
		}

		private static string? Existing(string path) => File.Exists(path) ? path : null;

		internal static void WriteSession(string dir, SessionRecord record)
			=> File.WriteAllText(Path.Combine(dir, SessionFile), JsonConvert.SerializeObject(record));

		private static SessionRecord? ReadSession(string dir)
		{
			try
			{
				var path = Path.Combine(dir, SessionFile);
				return File.Exists(path) ? JsonConvert.DeserializeObject<SessionRecord>(File.ReadAllText(path)) : null;
			}
			catch (Exception ex) when (ex is IOException or UnauthorizedAccessException or JsonException)
			{
				return null;
			}
		}

		private static bool IsRunning(SessionRecord session)
		{
			try
			{
				using var process = Process.GetProcessById(session.ProcessId);
				return StartedTicks(process) == session.ProcessStartedUtcTicks;
			}
			catch (Exception ex) when (ex is ArgumentException or InvalidOperationException
				or System.ComponentModel.Win32Exception or NotSupportedException)
			{
				return false;
			}
		}

		private static long StartedTicks(Process process) => process.StartTime.ToUniversalTime().Ticks;
	}
}
