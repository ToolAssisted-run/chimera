#nullable enable

using System.Collections.Generic;
using System.Diagnostics;
using System.Globalization;
using System.IO;
using System.Linq;
using System.Runtime.CompilerServices;
using System.Text;

using Newtonsoft.Json;
using Newtonsoft.Json.Linq;

using Chimera.Emulation.Common.Engine;

namespace Chimera.Client.Common
{
	/// <summary>
	/// The work in progress, kept where a crash cannot take it (docs/project.md, "Recovery").
	///
	/// Not every crash can be caught. A GPU driver that fast-fails, a killed process and a power cut
	/// all end Chimera without running a line of it, so nothing here waits for a crash to happen.
	/// While a project is open, one journal holds its work as it changes: the engine appends every
	/// change to the inputs (<see cref="EngineMovieLog.JournalTo"/>), and this appends every change to
	/// the markers and the branches (<see cref="EngineMovieLog.JournalNote"/>), each flushed the moment
	/// it is made. A snapshot of the whole project - settings, headers - is rewritten every few seconds
	/// while there is unsaved work. Closing the project removes it all. A project whose recovery
	/// folder outlived the process that wrote it is rebuilt, on the next open, into a separate file:
	/// recovered work never overwrites a project unasked.
	///
	/// The folder is its own cache entry (<see cref="CacheKind.Recovery"/>), under its own root rather
	/// than inside the project's cache, and locked by default: removing a greenzone - by hand or by the
	/// auto-clean - must never take unsaved work with it.
	/// </summary>
	public sealed class ProjectRecovery
	{
		/// <summary>Where every project's recovery folder lives: beside the caches, never inside one.</summary>
		public static string Root => Path.Combine(ProjectCache.DataHome, "Recovery");

		private const string JournalFile = "work.journal";

		/// <summary>What the journal was called when it held only the inputs; still read.</summary>
		private const string LegacyJournalFile = "inputs.journal";

		private const string SnapshotFile = "snapshot.chimeraProject";

		private const string SessionFile = "session.json";

		// The frontend's records in the journal. The engine journals the inputs itself.
		//   M [[frame, keepsState, "message"], ...]                          every marker a person placed
		//   B {"id", "text", "frame", "time", "log", "markers"}              one branch, whole
		//   O ["id", ...]                                                    the branches, in order: the list IS these
		private const char MarkersRecord = 'M';

		private const char BranchRecord = 'B';

		private const char OrderRecord = 'O';

		/// <summary>The shortest time between two snapshots while there is unsaved work.</summary>
		public static readonly TimeSpan SnapshotEvery = TimeSpan.FromSeconds(5);

		/// <summary>Records are pure ASCII whatever a marker says, so nothing between here and the file can mangle them.</summary>
		private static readonly JsonSerializerSettings RecordJson = new()
		{
			Formatting = Formatting.None,
			StringEscapeHandling = StringEscapeHandling.EscapeNonAscii,
		};

		/// <summary>An id for a branch that has none of its own (every branch added through the collection has a Uuid).</summary>
		private static readonly ConditionalWeakTable<TasBranch, string> SessionIds = new();

		/// <summary>A project's recovery folder.</summary>
		public static string DirectoryFor(string projectId)
			=> Path.Combine(Root, Path.GetFileName(ProjectCache.DirectoryFor(projectId)));

		/// <summary>
		/// Did the last session for this project end cleanly?
		///
		/// <see cref="End"/> deletes the folder on a clean end, so "no folder" is the answer - but a
		/// missing <see cref="Leftover"/> is NOT: <see cref="FindUnfinishedIn"/> also returns none when
		/// another Chimera still has the project open, and when a folder holds neither snapshot nor
		/// journal. Neither of those is a session that finished, and a cached greenzone is only worth
		/// trusting when one did: the history that bricked nss102 was written by a session that went on
		/// to die of guest heap corruption, and restoring a state from it killed the process inside the
		/// GPU driver on every open (docs/design-principles.md, 2026-09-14).
		/// </summary>
		public static bool LastSessionEndedCleanly(string projectId)
		{
			MoveLegacy(projectId);
			var dir = DirectoryFor(projectId);
			if (!Directory.Exists(dir)) return true;   // End(clean: true) took it away
			// Somebody else is working in it. Not a crash, but not a finished
			// session either - and its history is being written as we look.
			if (ReadSession(dir) is { } session && IsRunning(session)) return false;
			// A folder with no work in it is the remains of a clean end whose
			// delete did not land; FindUnfinishedIn discards those.
			return FindUnfinishedIn(dir) is null;
		}

		/// <summary>Where the folder was before it was a cache entry of its own: inside the project's cache.</summary>
		private static string LegacyDirectoryFor(string projectId) => Path.Combine(ProjectCache.DirectoryFor(projectId), "recovery");

		/// <summary>Who wrote a recovery folder - enough to tell a crash from another Chimera that has the project open.</summary>
		internal sealed class SessionRecord
		{
			public int ProcessId { get; set; }

			public long ProcessStartedUtcTicks { get; set; }

			public string ProjectPath { get; set; } = "";

			/// <summary>What the cache manager calls the entry.</summary>
			public string Title { get; set; } = "";
		}

		private readonly TasMovie _movie;

		private readonly string _dir;

		private EngineMovieLog? _journaled;

		private string _fingerprint = "";

		private string _markersSignature = "";

		private readonly Dictionary<string, string> _branchSignatures = new(StringComparer.Ordinal);

		private string _orderSignature = "";

		/// <summary>How long the journal was right after its last rewrite; it is rewritten again once it has grown well past that.</summary>
		private long _imageBytes;

		private DateTime _lastSnapshotUtc = DateTime.MinValue;

		private DateTime _lastCheckUtc = DateTime.MinValue;

		private DateTime _lastAttachUtc = DateTime.MinValue;

		private bool _ended;

		private ProjectRecovery(TasMovie movie, string dir)
		{
			_movie = movie;
			_dir = dir;
		}

		private string JournalPath => Path.Combine(_dir, JournalFile);

		private string SnapshotPath => Path.Combine(_dir, SnapshotFile);

		/// <summary>
		/// Starts keeping <paramref name="tasMovie"/>'s work recoverable. Null when there is nowhere to keep
		/// it - said on stderr, never thrown: a session that cannot be protected still runs.
		/// </summary>
		public static ProjectRecovery? Begin(ITasMovie tasMovie, string projectId, string projectPath)
		{
			if (tasMovie is not TasMovie movie || string.IsNullOrEmpty(projectId)) return null;
			try
			{
				MoveLegacy(projectId);
				var dir = DirectoryFor(projectId);
				Directory.CreateDirectory(dir);
				using (var current = Process.GetCurrentProcess())
				{
					var title = movie.Project.Title;
					WriteSession(dir, new SessionRecord
					{
						ProcessId = current.Id,
						ProcessStartedUtcTicks = StartedTicks(current),
						ProjectPath = projectPath,
						Title = title.Length is not 0 ? title : Path.GetFileNameWithoutExtension(projectPath),
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
		/// Called on every pass of the main loop. Every call journals whatever changed in the markers and
		/// the branches since the last (it costs a comparison when nothing did); once a second it keeps the
		/// journal on the movie's current log and short, and rewrites the snapshot when there is unsaved
		/// work it does not hold yet. Never throws: failing to protect the work must not be what ends it.
		/// </summary>
		public void Tick()
		{
			if (_ended) return;
			var now = DateTime.UtcNow;
			try
			{
				if (!ReferenceEquals(CurrentLog(), _journaled) || _journaled?.Journaling is not true)
				{
					// a log a branch load replaced, or a journal that could not be written: at most once a second
					if (now - _lastAttachUtc >= TimeSpan.FromSeconds(1)) AttachJournal();
				}
				else
				{
					JournalMarkersAndBranches();
				}

				if (now - _lastCheckUtc < TimeSpan.FromSeconds(1)) return;
				_lastCheckUtc = now;
				CompactIfLong();
				if (_movie.Changes && now - _lastSnapshotUtc >= SnapshotEvery && Fingerprint() != _fingerprint) Snapshot();
			}
			catch (Exception ex) when (ex is IOException or UnauthorizedAccessException or InvalidOperationException or ObjectDisposedException)
			{
				Console.Error.WriteLine($"[recovery] {ex.Message}");
			}
		}

		/// <summary>
		/// Something has gone wrong: journal what changed and write the snapshot now rather than at the next
		/// tick, so what is kept is the work as it stands at this moment. Never throws.
		/// </summary>
		public void SaveNow()
		{
			if (_ended) return;
			try
			{
				if (ReferenceEquals(CurrentLog(), _journaled) && _journaled?.Journaling is true) JournalMarkersAndBranches();
				Snapshot();
			}
			catch (Exception ex) when (ex is IOException or UnauthorizedAccessException or InvalidOperationException or ObjectDisposedException)
			{
				Console.Error.WriteLine($"[recovery] the work could not be kept in time: {ex.Message}");
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

		/// <summary>(Re)writes the journal: the inputs and, in the same fresh file, every marker and branch.</summary>
		private void AttachJournal()
		{
			_lastAttachUtc = DateTime.UtcNow;
			var log = CurrentLog();
			if (log is null) return;
			// the log a branch load let go of has already closed its journal by being freed; a live one is told
			if (_journaled is not null && !ReferenceEquals(_journaled, log)) _journaled.JournalClose(remove: false);
			if (!log.JournalTo(JournalPath, FrontendImage()))
			{
				Console.Error.WriteLine($"[recovery] the journal could not be written to {JournalPath}; trying again");
			}
			_journaled = log;
			_imageBytes = File.Exists(JournalPath) ? new FileInfo(JournalPath).Length : 0;
		}

		/// <summary>A journal that has grown to several times its last image is rewritten, which is all compaction is.</summary>
		private void CompactIfLong()
		{
			if (_journaled?.Journaling is not true || !File.Exists(JournalPath)) return;
			if (new FileInfo(JournalPath).Length > Math.Max(16L << 20, 3 * _imageBytes)) AttachJournal();
		}

		/// <summary>Every marker and branch as records, remembering them as what the journal now holds.</summary>
		private string FrontendImage()
		{
			StringBuilder image = new();
			image.Append(MarkersRecord).Append(' ').Append(MarkersJson(_movie.Markers)).Append('\n');
			_markersSignature = MarkersSignature(_movie.Markers);
			_branchSignatures.Clear();
			List<string> order = new();
			foreach (var branch in _movie.Branches)
			{
				var id = IdOf(branch);
				order.Add(id);
				image.Append(BranchRecord).Append(' ').Append(BranchJson(id, branch)).Append('\n');
				_branchSignatures[id] = BranchSignature(branch);
			}
			image.Append(OrderRecord).Append(' ').Append(JsonConvert.SerializeObject(order, RecordJson)).Append('\n');
			_orderSignature = string.Join("\n", order);
			return image.ToString();
		}

		/// <summary>
		/// Appends a record for whatever changed since the journal last heard: the markers whole, each
		/// branch that is new or different, and the branch order when it moved. Found by comparing, not by
		/// events, because a marker's message and a branch's text are plain properties anybody can set.
		/// </summary>
		private void JournalMarkersAndBranches()
		{
			var log = _journaled!;
			var markers = MarkersSignature(_movie.Markers);
			if (markers != _markersSignature)
			{
				log.JournalNote($"{MarkersRecord} {MarkersJson(_movie.Markers)}");
				_markersSignature = markers;
			}

			List<string> order = new(_movie.Branches.Count);
			foreach (var branch in _movie.Branches)
			{
				var id = IdOf(branch);
				order.Add(id);
				var signature = BranchSignature(branch);
				if (_branchSignatures.TryGetValue(id, out var was) && was == signature) continue;
				log.JournalNote($"{BranchRecord} {BranchJson(id, branch)}");
				_branchSignatures[id] = signature;
			}
			var orderSignature = string.Join("\n", order);
			if (orderSignature == _orderSignature) return;
			log.JournalNote($"{OrderRecord} {JsonConvert.SerializeObject(order, RecordJson)}");
			_orderSignature = orderSignature;
			foreach (var gone in _branchSignatures.Keys.Where(id => !order.Contains(id)).ToList()) _branchSignatures.Remove(gone);
		}

		private static string IdOf(TasBranch branch)
			=> branch.Uuid != Guid.Empty ? branch.Uuid.ToString("N") : SessionIds.GetValue(branch, static _ => Guid.NewGuid().ToString("N"));

		private static string MarkersSignature(IEnumerable<TasMovieMarker>? markers)
		{
			if (markers is null) return "";
			StringBuilder sb = new();
			foreach (var marker in markers)
			{
				if (marker.IsPermanent) continue;
				sb.Append(marker.Frame).Append(marker.WantsState ? '+' : '-').Append(marker.Message).Append('\u0001');
			}
			return sb.ToString();
		}

		/// <summary>The markers a person placed; the run's own are derived, and are not work.</summary>
		private static string MarkersJson(IEnumerable<TasMovieMarker>? markers)
		{
			JArray array = new();
			if (markers is not null)
			{
				foreach (var marker in markers)
				{
					if (marker.IsPermanent) continue;
					array.Add(new JArray(marker.Frame, marker.WantsState, marker.Message ?? ""));
				}
			}
			return JsonConvert.SerializeObject(array, RecordJson);
		}

		/// <summary>
		/// What makes a branch different: its text, frame and time, its markers, and its input log - by
		/// identity and length, because a branch's log is replaced rather than edited, and reading every
		/// branch's whole log on every pass of the main loop would be the cost of this.
		/// </summary>
		private static string BranchSignature(TasBranch branch)
			=> $"{branch.UserText}\u0001{branch.Frame}\u0001{branch.TimeStamp.Ticks}\u0001"
				+ $"{(branch.InputLog is null ? 0 : RuntimeHelpers.GetHashCode(branch.InputLog))}\u0001{branch.InputLog?.Count ?? 0}\u0001"
				+ MarkersSignature(branch.Markers);

		private static string BranchJson(string id, TasBranch branch)
		{
			StringBuilder log = new();
			if (branch.InputLog is not null)
			{
				for (var i = 0; i < branch.InputLog.Count; i++) log.Append(branch.InputLog[i]).Append('\n');
			}
			JObject record = new()
			{
				["id"] = id,
				["text"] = branch.UserText ?? "",
				["frame"] = branch.Frame,
				["time"] = branch.TimeStamp.ToString("o", CultureInfo.InvariantCulture),
				["log"] = log.ToString(),
				["markers"] = ParseArray(MarkersJson(branch.Markers)),
			};
			return JsonConvert.SerializeObject(record, RecordJson);
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
		}

		/// <summary>What only the snapshot holds changed: rerecords, the log's length, markers and branches (cheaply).</summary>
		private string Fingerprint()
		{
			StringBuilder sb = new();
			sb.Append(_movie.Rerecords).Append('|').Append(_movie.InputLogLength).Append('|');
			sb.Append(MarkersSignature(_movie.Markers)).Append('|');
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

			/// <summary>The process that wrote the folder, which is how a crash note is matched to it; 0 when unknown.</summary>
			public int ProcessId { get; init; }

			/// <summary>When that process started: a process id alone is reused.</summary>
			public DateTime? ProcessStartedUtc { get; init; }

			internal string? Snapshot { get; init; }

			internal string? Journal { get; init; }
		}

		/// <summary>
		/// The work a crashed session left for this project, or null: nothing was left, or the process that
		/// wrote it is still running (another Chimera has this project open right now).
		/// </summary>
		public static Leftover? FindUnfinished(string projectId)
		{
			MoveLegacy(projectId);
			return FindUnfinishedIn(DirectoryFor(projectId));
		}

		/// <summary>The same, for one recovery folder named outright (tests keep theirs out of the per-user cache).</summary>
		internal static Leftover? FindUnfinishedIn(string dir)
		{
			if (!Directory.Exists(dir)) return null;
			var session = ReadSession(dir);
			if (session is not null && IsRunning(session)) return null;
			var snapshot = Existing(Path.Combine(dir, SnapshotFile));
			string? journal = null;
			string? journalOnDisk = null;
			foreach (var name in new[] { JournalFile, LegacyJournalFile })
			{
				var candidate = Path.Combine(dir, name);
				journalOnDisk = Existing(candidate) ?? Existing(candidate + ".new");
				if (journalOnDisk is null) continue;
				journal = candidate;
				break;
			}
			if (snapshot is null && journal is null)
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
				ProcessId = session?.ProcessId ?? 0,
				ProcessStartedUtc = session is { ProcessStartedUtcTicks: > 0 } ? new DateTime(session.ProcessStartedUtcTicks, DateTimeKind.Utc) : null,
				Snapshot = snapshot,
				Journal = journal,
			};
		}

		/// <summary>
		/// Rebuilds the crashed session's work into a new project file in <paramref name="backupDirectory"/>
		/// and returns its path. The base is whichever is newer - the last snapshot, or
		/// <paramref name="projectPath"/> as last saved - and its inputs, markers and branches are the
		/// journal's, which is never older than either.
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
				using var journal = EngineMovieLog.FromJournal(leftover.Journal, out var error);
				if (journal is null)
				{
					Console.Error.WriteLine($"[recovery] the journal could not be replayed ({error}); the snapshot's work is used");
				}
				else
				{
					if (error.Length is not 0) Console.Error.WriteLine($"[recovery] the journal ended early: {error}");
					if (journal.Key is null)
					{
						// a journal that never saw a key keeps the one the project already has
						using EngineMovieLog saved = new();
						if (saved.Parse(project.LogText, out _)) journal.Key = saved.Key;
					}
					project.LogText = journal.Serialize(crlf: false);
					ApplyMarkersAndBranches(project, journal.JournalNotes);
				}
			}

			Directory.CreateDirectory(backupDirectory);
			var name = string.IsNullOrEmpty(projectPath) ? "project" : Path.GetFileNameWithoutExtension(projectPath);
			var copy = Path.Combine(backupDirectory,
				$"{name}.recovered {leftover.LastWorkUtc.ToLocalTime():yyyy-MM-dd HH.mm.ss}.chimeraProject");
			project.Save(copy);
			return copy;
		}

		/// <summary>
		/// The markers and branches a journal's records end on, written into <paramref name="project"/>. A
		/// journal with no marker record, or no order record, leaves the project's own; replay stops at a
		/// record that cannot be read, as the engine's does.
		/// </summary>
		internal static void ApplyMarkersAndBranches(EngineProject project, IReadOnlyList<string> records)
		{
			JArray? markers = null;
			JArray? order = null;
			Dictionary<string, JObject> branches = new(StringComparer.Ordinal);
			foreach (var record in records)
			{
				if (record.Length < 2 || record[1] != ' ') continue;
				try
				{
					switch (record[0])
					{
						case MarkersRecord:
							markers = ParseArray(record.Substring(2));
							break;
						case BranchRecord:
							var branch = ParseObject(record.Substring(2));
							if (branch["id"]?.Type is JTokenType.String) branches[(string)branch["id"]!] = branch;
							break;
						case OrderRecord:
							order = ParseArray(record.Substring(2));
							break;
					}
				}
				catch (JsonException ex)
				{
					Console.Error.WriteLine($"[recovery] a marker or branch record could not be read ({ex.Message}); what came before it is used");
					break;
				}
			}

			if (markers is not null)
			{
				project.MarkersClear();
				foreach (var marker in markers.OfType<JArray>())
				{
					project.MarkerAdd((long)marker[0], (string?)marker[2] ?? "", (bool)marker[1]);
				}
			}
			if (order is null) return;
			project.BranchesClear();
			foreach (var id in order)
			{
				if (!branches.TryGetValue((string?)id ?? "", out var branch)) continue;
				project.BranchAdd((string?)branch["text"] ?? "", (long?)branch["frame"] ?? 0, (string?)branch["time"] ?? "", (string?)branch["log"] ?? "");
				var index = project.BranchCount - 1;
				if (branch["markers"] is not JArray branchMarkers) continue;
				foreach (var marker in branchMarkers.OfType<JArray>())
				{
					project.BranchMarkerAdd(index, (long)marker[0], (string?)marker[2] ?? "", (bool)marker[1]);
				}
			}
		}

		/// <summary>Parsed as written: a branch's time stays the string it was, not a date re-rendered.</summary>
		private static JArray ParseArray(string json)
		{
			using JsonTextReader reader = new(new StringReader(json)) { DateParseHandling = DateParseHandling.None };
			return JArray.Load(reader);
		}

		private static JObject ParseObject(string json)
		{
			using JsonTextReader reader = new(new StringReader(json)) { DateParseHandling = DateParseHandling.None };
			return JObject.Load(reader);
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

		/// <summary>A folder from before recovery had a root of its own is moved there, once.</summary>
		private static void MoveLegacy(string projectId)
		{
			var legacy = LegacyDirectoryFor(projectId);
			var dir = DirectoryFor(projectId);
			try
			{
				if (!Directory.Exists(legacy) || Directory.Exists(dir)) return;
				Directory.CreateDirectory(Root);
				Directory.Move(legacy, dir);
			}
			catch (Exception ex) when (ex is IOException or UnauthorizedAccessException)
			{
				Console.Error.WriteLine($"[recovery] {ex.Message}");
			}
		}

		/// <summary>Every recovery folder, as the cache manager lists it: who it is for, and whether its session is running.</summary>
		internal static IReadOnlyList<(string Directory, string Label, string ProjectPath, bool Live)> Entries()
		{
			List<(string, string, string, bool)> entries = new();
			try
			{
				if (!Directory.Exists(Root)) return entries;
				foreach (var dir in Directory.EnumerateDirectories(Root))
				{
					var session = ReadSession(dir);
					var label = session is { Title.Length: > 0 } ? session.Title
						: session is { ProjectPath.Length: > 0 } ? Path.GetFileNameWithoutExtension(session.ProjectPath)
						: Path.GetFileName(dir);
					entries.Add((dir, label, session?.ProjectPath ?? "", session is not null && IsRunning(session)));
				}
			}
			catch (Exception ex) when (ex is IOException or UnauthorizedAccessException)
			{
				// a root that will not be listed lists nothing; the folders are still there
			}
			return entries;
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
