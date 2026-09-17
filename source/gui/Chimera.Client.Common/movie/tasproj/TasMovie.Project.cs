using System;
using System.Collections.Generic;
using System.Globalization;
using System.IO;
using System.Linq;
using System.Text;

using Newtonsoft.Json;

using Chimera.Display;
using Chimera.Common;
using Chimera.Common.IOExtensions;
using Chimera.Emulation.Common;
using Chimera.Emulation.Common.Engine;

namespace Chimera.Client.Common
{
	internal partial class TasMovie
	{
		// The JSON .chimeraProject (docs/project.md) is this movie's NATIVE
		// format: the project IS the movie. Everything that is work - input
		// log, markers, branches (with their text, times and markers),
		// subtitles, comments-as-description, the header metadata - lives in
		// the project file; everything regenerable (greenzone, lag log,
		// branch states and screenshots, session position, column layout)
		// lives in a cache sibling that may be lost at the price of
		// recomputation only. There is no other project format: legacy zip
		// tasprojs (and imported movies of any provenance) are not read.

		private EngineProject _project;

		/// <summary>
		/// The engine-side project backing this movie. Holds what the movie
		/// machinery does not model (the file manifest, firmware pins, the
		/// wizard's title and description) so a load-edit-save round trip
		/// preserves them untouched.
		/// </summary>
		public EngineProject Project => _project ??= EngineProject.New();

		/// <summary>
		/// The greenzone: everything regenerable, in the per-user cache and keyed
		/// by the project's id. Present = loaded; absent = a clean slate
		/// (docs/project.md).
		///
		/// It used to sit beside the project, and it is the reason that stopped
		/// being tenable. A .chimeraProject is the one file that exists as far as
		/// anyone else is concerned, and people keep their projects in synced
		/// folders - the author's are in Google Drive - where a multi-gigabyte
		/// sibling is uploaded again on every save, by a client that holds files
		/// open while it works. Nothing that can be recomputed belongs there. The
		/// remembered file paths moved first (<see cref="ProjectLocalPaths"/>);
		/// this is the large one.
		/// </summary>
		/// <summary>
		/// Where the branches' machine states are: one file each, beside the rest of this
		/// project's cache, written and read by the engine (see <see cref="TasBranch.StateFile"/>).
		/// They are cache like the greenzone is - a branch without one replays to its frame.
		/// </summary>
		public string BranchStateDirectory => Path.Combine(ProjectCache.DirectoryFor(Project.Id), "Branches");

		/// <summary>Where a branch's state file is, from the name the branch holds.</summary>
		public string BranchStatePath(string stateFile) => Path.Combine(BranchStateDirectory, stateFile);

		/// <summary>A name nothing else has, in a directory that exists: where a new branch's state goes.</summary>
		public string NewBranchStatePath(out string stateFile)
		{
			Directory.CreateDirectory(BranchStateDirectory);
			stateFile = $"{Guid.NewGuid():N}.state";
			return BranchStatePath(stateFile);
		}

		/// <summary>
		/// Removes the state files no branch names - plus <paramref name="alsoKeep"/>, for a branch
		/// that is not in the list but can come back (TAStudio's undo). A state is gigabytes on a
		/// PS3, and one whose branch is gone is never read again.
		/// </summary>
		public void RemoveUnusedBranchStates(params string[] alsoKeep)
		{
			try
			{
				if (!Directory.Exists(BranchStateDirectory)) return;
				var used = new HashSet<string>(
					Branches.Select(static b => b.StateFile).Concat(alsoKeep).Where(static n => n is not null),
					StringComparer.OrdinalIgnoreCase);
				foreach (var file in Directory.GetFiles(BranchStateDirectory))
				{
					if (!used.Contains(Path.GetFileName(file))) File.Delete(file);
				}
			}
			catch (Exception ex) when (ex is IOException or UnauthorizedAccessException)
			{
				// it is cache; what would not go now goes next time
			}
		}

		public string GreenZoneFilename
			=> Path.Combine(ProjectCache.DirectoryFor(Project.Id), "greenzone.chimeraGreenZone");

		/// <summary>
		/// The engine's state history, beside the greenzone in the same cache.
		/// Its own file because the engine streams it a stretch at a time and
		/// nothing about it is ever assembled in memory - which is the bug this
		/// whole design removed, and a zip lump would put straight back.
		/// </summary>
		public string StateHistoryFilename
			=> Path.Combine(ProjectCache.DirectoryFor(Project.Id), "history.bin");

		/// <summary>
		/// Where a project written before the cache existed left its greenzone:
		/// beside the project file. Only ever read, and only to move it here.
		/// </summary>
		public static string LegacyGreenZonePathFor(string projectPath)
			=> Path.ChangeExtension(projectPath, "chimeraGreenZone");

		public string DroppedCacheNote { get; private set; }

		/// <summary>
		/// What the cached states are states OF: the core build, the settings, the
		/// firmware pins and the game files, hashed into one line. A savestate is
		/// the memory of one exact machine, and the sandbox checks only that the
		/// core is the same binary - not what it was configured with or given to
		/// read - so a cache left beside a project whose machine has since changed
		/// (a hand-edited setting, a project saved over another's name, the
		/// settings a bug once wrote as "{}") loaded states into a machine they
		/// were not made by, and that machine fell over (issue #26). The cache
		/// carries this line, and is used only when it still says the same.
		/// </summary>
		internal static string MachineIdentityOf(EngineProject p)
		{
			StringBuilder sb = new();
			// The build that RUNS, when it is known. A project opened on another
			// build of its core - accepted at the "not the project's core build"
			// prompt, or simply the version that registered first when two are
			// installed - still pins the old hash until it is saved, and a cache
			// checked against the pin was taken for this machine's: its branch
			// states reached the sandbox, which refused them (issue #63).
			// Resolved exactly as the boot resolves it (CoreRegistry.FactoryFor), so with several builds
			// installed this names the one that will run.
			var runningFactory = CoreRegistry.Instance.FactoryFor(p.CoreName, p.CoreSha1);
			var running = runningFactory is null ? null : CoreRegistry.Instance.PackageSha1Of(runningFactory);
			sb.Append("core=").Append((running ?? p.CoreSha1).ToUpperInvariant()).Append('\n');
			sb.Append("settings=").Append(CanonicalJson(p.SettingsJson)).Append('\n');
			sb.Append("firmware=").Append(CanonicalJson(p.FirmwareJson)).Append('\n');
			for (var i = 0; i < p.FileCount; i++)
			{
				// the hash is the file's identity; its name on disk is not
				sb.Append("file=").Append(p.FileSlot(i)).Append(':').Append(p.FileSha1(i).ToUpperInvariant()).Append('\n');
			}
			return ChimeraEngine.Sha1Hex(Encoding.UTF8.GetBytes(sb.ToString()));
		}

		/// <summary>Object keys sorted, no whitespace: the same settings in any order are the same machine.</summary>
		private static string CanonicalJson(string json)
		{
			if (string.IsNullOrWhiteSpace(json)) return "";
			try
			{
				return Canonical(Newtonsoft.Json.Linq.JToken.Parse(json)).ToString(Formatting.None);
			}
			catch (JsonException)
			{
				return json.Trim();
			}
		}

		private static Newtonsoft.Json.Linq.JToken Canonical(Newtonsoft.Json.Linq.JToken token)
		{
			switch (token)
			{
				case Newtonsoft.Json.Linq.JObject obj:
					Newtonsoft.Json.Linq.JObject sorted = new();
					foreach (var prop in obj.Properties().OrderBy(static prop => prop.Name, StringComparer.Ordinal))
					{
						sorted[prop.Name] = Canonical(prop.Value);
					}
					return sorted;
				case Newtonsoft.Json.Linq.JArray arr:
					Newtonsoft.Json.Linq.JArray items = new();
					foreach (var item in arr) items.Add(Canonical(item));
					return items;
				default:
					return token;
			}
		}

		/// <summary>
		/// Adopts the frontend's RESOLVED instance (files located and hashed for
		/// this session) in place of the one this movie opened itself, so a save
		/// records the ACTUAL hashes - a knowing override included. The frontend
		/// keeps the same instance for mounts; nothing disposes it until the next
		/// project replaces it.
		/// </summary>
		public void UseResolvedProject(EngineProject project)
		{
			if (ReferenceEquals(_project, project)) return;
			_project?.Dispose();
			_project = project;
		}

		// header keys the project format first-classes; the rest ride the
		// project's ordered headers map verbatim
		private static bool IsFirstClassHeaderKey(string key)
			=> key is HeaderKeys.GameName or HeaderKeys.Rerecords
				or HeaderKeys.Core or HeaderKeys.CoreVersion or HeaderKeys.CorePackageSha1;

		/// <summary>the header's value when it has one, and what the project already says when it does not</summary>
		private static string Keep(string headerValue, string pinned)
			=> string.IsNullOrWhiteSpace(headerValue) ? pinned : headerValue;

		/// <summary>
		/// Who a save records as the project's core: the one that RAN, when it can
		/// be named exactly.
		///
		/// A project opened on another core build says so, drops its cached states
		/// and carries on - and used to be written back out still pinned to the
		/// build it was created with, so every later open asked the same question
		/// about a core nobody was using any more (user-reported, 2026-09-16). The
		/// pin is a package, so only a loaded package may replace it: the answer is
		/// the running core's name, the version it states, and the SHA1 of the
		/// package file, together or not at all - a name from one build beside a
		/// hash from another would describe a machine that never existed.
		/// Null when nothing identifies the running core (no emulator, or one that
		/// came from no package - a test's fake, a core loaded outside the store),
		/// and then the project keeps the pin it had.
		/// </summary>
		internal static (string Name, string Version, string Sha1)? RunningCoreIdentity(IEmulator emulator, string packageSha1)
		{
			if (emulator is null || string.IsNullOrWhiteSpace(packageSha1)) return null;
			var name = emulator.Attributes().CoreName;
			if (string.IsNullOrWhiteSpace(name)) return null;
			return (name, emulator.CoreVersion() ?? "", packageSha1);
		}

		/// <summary>
		/// The project as it stands, written to <paramref name="path"/> for <see cref="ProjectRecovery"/>:
		/// exactly what a backup writes, so no greenzone and no change to what counts as saved.
		/// </summary>
		internal FileWriteResult WriteRecoverySnapshot(string path) => Write(path, isBackup: true);

		/// <summary>Set for the duration of <see cref="SaveWithoutGreenzone"/>.</summary>
		private bool _savingWithoutGreenzone;

		/// <summary>
		/// The project as <see cref="MovieBase.Save"/> writes it - inputs, markers, branches, settings -
		/// but with no state history, and none left from an earlier save: the file the next open would
		/// have loaded is removed. For a greenzone nobody should trust any more, which is what a machine
		/// that died mid-frame leaves behind (the core-stopped dialog offers it). The history in memory
		/// is untouched; only what reaches the disk is.
		/// </summary>
		public FileWriteResult SaveWithoutGreenzone()
		{
			_savingWithoutGreenzone = true;
			try
			{
				return Save();
			}
			finally
			{
				_savingWithoutGreenzone = false;
			}
		}

		/// <summary>Raised when the input log is replaced by another object (a branch load), so a journal can follow it.</summary>
		public event Action InputLogReplaced;

		/// <summary>The movie holds work the project file does not: it was opened from recovered work.</summary>
		public void MarkRecovered() => Changes = true;

		protected override FileWriteResult Write(string fn, bool isBackup = false)
		{
			if (StartsFromSavestate)
			{
				// an anchored movie's savestate is sync data with no project home
				// yet; refusing beats silently dropping it (docs/project.md)
				return new FileWriteResult(FileWriteEnum.FailedDuringWrite, new(fn, ""),
					new InvalidOperationException("a savestate-anchored movie cannot be saved as a project yet"));
			}

			SetCycleValues();
			if (!Header.ContainsKey(HeaderKeys.OriginalEmulatorVersion))
			{
				Header[HeaderKeys.OriginalEmulatorVersion] = Header[HeaderKeys.EmulatorVersion];
			}
			Header[HeaderKeys.EmulatorVersion] = VersionInfo.GetEmuVersion();

			var p = Project;

			// identity: the project's title IS the game name the movie shows
			if (!string.IsNullOrEmpty(Header[HeaderKeys.GameName]))
			{
				p.Title = Header[HeaderKeys.GameName];
			}
			// The core that RAN, when it can be named (RunningCoreIdentity); otherwise
			// the pin is the project's, and a movie header that does not carry it is
			// silent rather than empty: writing "" here would unpin the core and leave
			// a project nothing can run
			var ran = RunningCoreIdentity(Emulator, CoreRegistry.Instance.PackageSha1Of(Emulator) ?? "");
			if (ran is { } core)
			{
				// the movie's own headers say the same, so what it reports and what
				// the next open checks against are one answer
				Header[HeaderKeys.Core] = core.Name;
				Header[HeaderKeys.CoreVersion] = core.Version;
				Header[HeaderKeys.CorePackageSha1] = core.Sha1;
				p.SetCore(core.Name, core.Version, core.Sha1);
			}
			else
			{
				p.SetCore(
					Keep(Header[HeaderKeys.Core], p.CoreName),
					Keep(Header[HeaderKeys.CoreVersion], p.CoreVersion),
					Keep(Header[HeaderKeys.CorePackageSha1], p.CoreSha1));
			}
			p.Rerecords = Rerecords;
			// The settings are the movie's when it has them, and the project's
			// own when it is silent - the same rule as the core pin above. A
			// movie that starts from a wizard-made project has no settings text
			// of its own (the project boot fills headers, not settings), and
			// flattening that silence wrote "{}" over the wizard's answers: the
			// reopened machine ran on every default, and desynced (issue #29).
			if (!string.IsNullOrWhiteSpace(SettingsJson)) p.SetSettingsJson(FlattenSettings(SettingsJson));
			p.Description = string.Join("\n", Comments);

			p.SubtitlesClear();
			Subtitles.Sort();
			foreach (var subtitle in Subtitles)
			{
				p.SubtitleAdd(subtitle.ToString());
			}

			// Two facts about the run that only the running machine knows, written
			// at save rather than at record so that a project made before either
			// existed gains them the first time it is saved.
			//
			// Where the input stops, so a reader does not have to know what a
			// neutral entry looks like for this core's controller...
			Header[HeaderKeys.LastInputFrame] = LastNonEmptyInputFrame.ToString(CultureInfo.InvariantCulture);
			// ...and the rate the machine runs at. Chimera keeps no per-system rate
			// table - the exact rate is the core's - so without this a movie is a
			// frame count nothing outside that core can turn into a duration.
			// ...and whether a GPU drew. A run made on one carries no promise that
			// it replays anywhere - the GPU is outside the sandbox, outside the
			// savestate and different on every machine - so it is written down,
			// and a replay that desyncs elsewhere can be understood.
			if (Emulator is IGpuRendered gpu && !string.IsNullOrEmpty(gpu.GpuRenderer))
			{
				Header[HeaderKeys.GpuRenderer] = gpu.GpuRenderer;
				// ...and whether the states beside this project can be opened
				// again in another session, which is a question about the core
				// and has to be answered while there is a core to ask
				if (gpu.GpuStatesSurviveTheContext) Header[HeaderKeys.GpuStatesSurvive] = "1";
			}

			if (IsAttached())
			{
				Header[HeaderKeys.VsyncNumerator] =
					Emulator.VsyncNumerator().ToString(CultureInfo.InvariantCulture);
				Header[HeaderKeys.VsyncDenominator] =
					Emulator.VsyncDenominator().ToString(CultureInfo.InvariantCulture);
			}

			p.HeadersClear();
			foreach (var (key, value) in Header)
			{
				if (!IsFirstClassHeaderKey(key)) p.HeaderSet(key, value);
			}

			// the input lump, exactly the [Input] block a movie file carries
			var engineLog = ((EngineStringLog)Log).Engine;
			engineLog.Key = string.IsNullOrEmpty(LogKey)
				? LogEntryGenerator.GenerateLogKey(Session.MovieController.Definition)
				: LogKey;
			p.LogText = engineLog.Serialize(crlf: false);

			p.MarkersClear();
			foreach (var marker in Markers)
			{
				// the run's own three are derived from the movie and worked out
				// again on load: writing them would let them go stale, and reading
				// them back would leave three of somebody else's markers behind
				// every time the project was saved
				if (marker.IsPermanent) continue;
				p.MarkerAdd(marker.Frame, marker.Message ?? "", marker.WantsState);
			}

			p.BranchesClear();
			foreach (var branch in Branches)
			{
				p.BranchAdd(
					branch.UserText ?? "",
					branch.Frame,
					branch.TimeStamp.ToString("o", CultureInfo.InvariantCulture),
					JoinLogLines(branch.InputLog));
				var index = p.BranchCount - 1;
				if (branch.Markers is null) continue;
				foreach (var marker in branch.Markers)
				{
					p.BranchMarkerAdd(index, marker.Frame, marker.Message ?? "", marker.WantsState);
				}
			}

			// The piano roll as it is laid out - which columns, in what order and width, which way round,
			// how lag is shown - is part of the work somebody set up, so it is in the project and not in
			// the cache beside it, which a new core or a crash throws away (issue #83). Backups and the
			// recovery snapshot are this same write, so they carry it too.
			if (ClientSettingsForSave is not null)
			{
				try
				{
					p.SetTAStudioJson(ClientSettingsForSave());
				}
				catch (InvalidOperationException)
				{
					// a layout that would not serialize is not a reason to lose the save
				}
			}

			EngineProgress.Report("writing the project");
			try
			{
				// The folder first: a backup goes to the "Movie backups" path, which
				// a fresh install does not have, and the engine writes files, not
				// folders - so Save Backup failed until somebody made Movies/backup
				// by hand (issue #19). A Save As lands in a folder the dialog showed,
				// but the same line costs nothing there.
				var folder = Path.GetDirectoryName(Path.GetFullPath(fn));
				if (!string.IsNullOrEmpty(folder)) Directory.CreateDirectory(folder);
				p.Save(fn);
			}
			catch (Exception ex) when (ex is InvalidOperationException or IOException or UnauthorizedAccessException)
			{
				return new FileWriteResult(FileWriteEnum.FailedDuringWrite, new(fn, ""), ex);
			}

			if (!isBackup)
			{
				EngineProgress.Report("writing the greenzone");
				ProjectCache.Ensure(p.Id);
				WriteCacheFile(GreenZoneFilename);
				// A machine a GPU drew makes states good only in the session that
				// made them (docs/gpu-bridge.md), so it writes none - and removes
				// any an earlier session left, which would otherwise be loaded
				// into a machine that cannot draw.
				// Queued, not waited for. This is the longest thing a save does -
				// the history is written against budgets of four gigabytes in
				// memory and ten on disk, which measured ten seconds to a Linux
				// disk and over a minute to NTFS - and none of it needs the
				// machine, so the run carries on while it is written. What lands
				// in the file is the history as it stands right here; frames
				// captured afterwards belong to the next save. The barrier is at
				// Dispose, where the project is let go of.
				if (States is not null && !_savingWithoutGreenzone && GreenzoneMayOutliveSession)
				{
					States.SaveLater(StateHistoryFilename, MachineIdentityOf(p));
				}
				else
				{
					// a save an earlier call queued may still be writing that very file
					States?.SaveWait();
					TryDelete(StateHistoryFilename);
				}
				// and where this machine keeps the project's files, in a sibling of
				// its own: the project itself stays distributable, carrying names and
				// hashes and no paths at all (docs/project.md). Merged over whatever
				// is already there, so firmware locations recorded at load survive.
				ProjectLocalPaths.Read(p, fn).Save(p);
				// and who this cache belongs to, so the cache manager can name it
				// and say where the project it serves was last seen
				ProjectCache.Remember(p.Id, ProjectCache.FactsOf(p, fn));
				Changes = false;
			}
			return new FileWriteResult();
		}

		/// <summary>
		/// The project stores the flat name-to-value map the engine's settings
		/// channel takes (and chimera-run passes straight through); the movie
		/// machinery stores the settings OBJECT ({"Values":{...}} for a waterbox
		/// core). These two translate at the boundary.
		/// </summary>
		internal static string FlattenSettings(string syncSettingsJson)
		{
			if (string.IsNullOrWhiteSpace(syncSettingsJson)) return "{}";
			try
			{
				var root = Newtonsoft.Json.Linq.JObject.Parse(syncSettingsJson);
				if (root.Count is 1 && root["Values"] is Newtonsoft.Json.Linq.JObject values)
				{
					return values.ToString(Formatting.None);
				}
				return root.ToString(Formatting.None);
			}
			catch (JsonException)
			{
				return "{}";
			}
		}

		internal static string WrapSettings(string flatJson)
		{
			try
			{
				var values = Newtonsoft.Json.Linq.JObject.Parse(
					string.IsNullOrWhiteSpace(flatJson) ? "{}" : flatJson);
				return new Newtonsoft.Json.Linq.JObject { ["Values"] = values }.ToString(Formatting.None);
			}
			catch (JsonException)
			{
				return "{\"Values\":{}}";
			}
		}

		private static string JoinLogLines(IStringLog log)
		{
			if (log is null || log.Count is 0) return "";
			var sb = new System.Text.StringBuilder();
			for (var i = 0; i < log.Count; i++)
			{
				sb.Append(log[i]).Append('\n');
			}
			return sb.ToString();
		}

		/// <summary>
		/// True when a GPU drew this machine, which decides whether its states
		/// can outlive the session.
		///
		/// A bridged core's renderer keeps its OpenGL objects - textures,
		/// programs, vertex arrays - by the NAMES a driver gave it, and those
		/// names live in guest memory, so a savestate carries them faithfully
		/// into a session where they mean nothing: the context that owned them
		/// is gone, every call naming one is refused by the driver, and the
		/// core is never told (a GL error raised out at the bridge is invisible
		/// to the guest). The machine runs on perfectly - threads alive, memory
		/// changing - and draws NOTHING, which is what a PS3 project reopened
		/// after a few hundred frames did: a black screen, and then a crash
		/// (issue: "saving a project and then reloading it").
		///
		/// So a state a GPU drew is good for the session that made it and no
		/// other. Rewind and branches work as they always did; what does not
		/// cross a restart is written down here and recomputed by replay, which
		/// is what an empty greenzone has always meant.
		///
		/// A core may declare that its states DO survive a new context
		/// (<c>video.gpuStatesSurviveTheContext</c>), and that is still not taken on
		/// trust for a state that TRAVELS: a branch's machine rides inside the project
		/// file, which is handed to other people and opened on other PCs, and a state a
		/// GPU drew is only good where that GPU is.
		///
		/// The greenzone is a different case and is decided elsewhere (see
		/// <see cref="ProjectRecovery.LastSessionEndedCleanly"/>): it is a per-machine
		/// cache beside the project, and what bricked nss102 in 2026-09-14 was not the
		/// renderer but the SESSION - its history came from one already dying of guest
		/// heap corruption. A clean GPU session's history reloads and lands correctly,
		/// measured 2026-09-16 on the real 8916-frame project: a 626 MB greenzone
		/// written by one process and reloaded by another drew frame 8915 pixel for
		/// pixel, with no guest death in any run.
		/// </summary>
		private bool DrawnByGpu
			=> Emulator is IGpuRendered { GpuRenderer: { Length: > 0 } };

		/// <summary>
		/// The GPU-drawn cores whose greenzone is known to survive a reload, by EVIDENCE:
		/// a history written by one process, reloaded by another, drawing the same frame
		/// pixel for pixel, with no guest death.
		///
		/// Ruffle: the real 8916-frame nss102 project, 626 MB greenzone, frame 8915
		/// identical (2026-09-16), with a control at 8815 differing so the comparison
		/// could tell frames apart.
		///
		/// Dolphin: Pro Rally 2002, 1000 frames of no input, 192 MB greenzone written by
		/// one process and reloaded by another in 969 ms; frame 999 identical to the
		/// straight run, 0 of 286,720 pixels different, control at frame 899 differing
		/// by 98.9% (2026-09-16). No guest died.
		///
		/// xemu: Prince of Persia, 1000 frames of no input, a 241 MB greenzone ending at
		/// frame 300 - BEFORE the title's static tail, so the reload had to replay 700
		/// changing frames - loaded by another process in 966 ms; frame 999 identical,
		/// 0 of 307,200 pixels different, control at frame 300 differing by 99.0%
		/// (2026-09-16). An earlier attempt whose control sat inside the static tail
		/// matched trivially and proved nothing; the frame a control photographs has to
		/// be one the picture actually changes on.
		///
		/// PCSX2 (Gran Turismo 4), flycast (Re-Volt) and PPSSPP (Ridge Racer), all
		/// 2026-09-16, all 1000 frames of no input with the greenzone written at frame
		/// 900 and reloaded in a fresh process: frame 999 identical in every case - 0 of
		/// 286,720, 0 of 307,200 and 0 of 130,560 pixels - and no guest died.
		///
		/// A control frame is half the measurement, and choosing one badly is how this
		/// nearly shipped a false claim. flycast first "passed" against a control that
		/// was 100% PURE BLACK, which made its 6.9% difference meaningless: it was
		/// black against a frame 93% black. Re-Volt's picture also CYCLES on a period
		/// of about 400 frames, so frames 400 apart are byte-identical and any control
		/// at that spacing proves nothing whichever way it falls. Re-measured on the
		/// current core against frame 850, in the other phase of that cycle, it differs
		/// by 98.52% - and the target carries 900 colours. Greenzone 21.7 MB.
		///
		/// PCSX2's control (frame 200) is 88% near-black, which looked like the same
		/// trap and is not: its target at frame 999 carries 1,066 colours and is 0.6%
		/// near-black, so the 99.51% difference is real content, not an absence of it.
		/// Greenzone 158.7 MB.
		///
		/// RPCS3 (a PopCap title, 2026-09-16) reloaded a 100 MB greenzone - the largest
		/// of the seven - in a fresh process and replayed to frame 999 identically: 0 of
		/// 921,600 pixels. Its control differs by 99.82%, the widest here. Read that
		/// with its shape in mind: frame 999 is a dark screen, 72.6% pure black over 273
		/// colours, so the match rests on the 252,811 pixels (27.4%) that carry
		/// something. Solid evidence, but a darker target than PCSX2's.
		///
		/// The PS3 could not be photographed at all until the same day. It died during
		/// RSX bring-up because the core embedded only Icons/ui/*.png and never the 36
		/// icons under home/32 and home/256: the home-menu overlay asked for one, got an
		/// image_info with null data and zero width, and the renderer built a GL texture
		/// out of it - rpcs3's own "Invalid OpenGL texture definition" killed the RSX
		/// thread, and all 26 guest threads then waited forever on a thread that was
		/// gone. Fixed in the core (gen-assets.py walks the tree).
		///
		/// Every other GPU core keeps the old behaviour - greenzone cleared on save and
		/// load - until its own evidence exists. A core's own declaration is NOT enough:
		/// Ruffle declared <c>gpuStatesSurviveTheContext</c> while being the core that
		/// bricked a project (docs/design-principles.md, 2026-09-14).
		/// </summary>
		private static readonly HashSet<string> GreenzoneSurvivesReload =
			new(new[] { "Ruffle", "Dolphin", "xemu", "PCSX2", "flycast", "PPSSPP", "RPCS3" },
				StringComparer.OrdinalIgnoreCase);

		/// <summary>
		/// Whether this project's greenzone may outlive its session: a machine no GPU
		/// drew always may; one a GPU drew only if its core is on the evidence list
		/// above. The core is named by the project's own header, because a project is
		/// read before its core is booted.
		/// </summary>
		/// <summary>
		/// The core's machine died at some point in this session (the core-stopped dialog was raised).
		/// Sticky until the project is let go of: a machine that died may have been wrong for some time
		/// before it faulted, so every state captured in this session is suspect, not just the last one.
		/// </summary>
		/// <remarks>
		/// "Save inputs and close" already wrote without a greenzone, but that is one of four choices.
		/// Going back to a safe point or restarting from frame zero leaves the session running, and the
		/// ordinary save at the end of it would write the greenzone as if nothing had happened - which
		/// is how a 430 MB history came to be written by a session whose Ruffle machine had died in
		/// naga's shader namer (2026-09-16).
		/// </remarks>
		public bool CoreDiedThisSession { get; private set; }

		/// <summary>Remembers that the machine died, so nothing captured in this session outlives it.</summary>
		public void NoteCoreDied() => CoreDiedThisSession = true;

		private bool GreenzoneMayOutliveSession
		{
			get
			{
				// A session that lost its machine cannot vouch for what it stored.
				if (CoreDiedThisSession) return false;
				if (!StatesMadeByGpu) return true;   // nothing outside the savestate drew it
				// The project's own record first: a project READ from disk may carry no
				// Core header at all (nss102 does not), while the project object always
				// knows the core it was made with.
				var core = Project.CoreName;
				if (core.Length is 0) HeaderEntries.TryGetValue(HeaderKeys.Core, out core);
				return core is { Length: > 0 } && GreenzoneSurvivesReload.Contains(core);
			}
		}

		/// <summary>
		/// The same question asked of the project file rather than of the machine,
		/// and it has to be: a project is READ before its core is booted, so there
		/// is no emulator to ask. What there is, is what the last save wrote down -
		/// the driver that drew it.
		///
		/// This decides the BRANCH states only, which travel inside the project
		/// file. The greenzone beside it is decided by the shutdown instead (see
		/// <see cref="ProjectRecovery.LastSessionEndedCleanly"/>).
		/// </summary>
		private bool StatesMadeByGpu
			=> HeaderEntries.TryGetValue(HeaderKeys.GpuRenderer, out var driver)
				&& !string.IsNullOrWhiteSpace(driver);

		/// <summary>
		/// The regenerable bulk, beside the project: greenzone, lag log, session
		/// position, column layout, verification log, branch states and
		/// screenshots (joined to the project's branches by order). A failed
		/// cache write never fails the save - the cache is the one file whose
		/// loss costs recomputation only.
		///
		/// The BRANCH states are left out for a machine a GPU drew (see
		/// <see cref="DrawnByGpu"/>): they ride inside the project file, which
		/// travels, and for a PlayStation 3 each one is the
		/// better part of a gigabyte.
		/// </summary>
		private static void TryDelete(string path)
		{
			try
			{
				if (File.Exists(path)) File.Delete(path);
			}
			catch (Exception ex) when (ex is IOException or UnauthorizedAccessException)
			{
				// it stays; it is a cache, and the machine check refuses it anyway
			}
		}

		private void WriteCacheFile(string path)
		{
			var createResult = ZipStateSaver.Create(path, Session.Settings.MovieCompressionLevel);
			if (createResult.IsError) return;
			var bs = createResult.Value;
			try
			{
				// which machine these states belong to - checked before any is loaded
				bs.PutLump(BinaryStateLump.Machine, tw => tw.WriteLine(MachineIdentityOf(Project)));
				bs.PutLump(BinaryStateLump.LagLog, tw => LagLog.Save(tw), zstdCompress: true);
				if (VerificationLog.Count is not 0)
				{
					bs.PutLump(BinaryStateLump.VerificationLog, tw => tw.WriteLine(VerificationLog.ToInputLog()));
				}
				bs.PutLump(BinaryStateLump.Session, tw => tw.WriteLine(JsonConvert.SerializeObject(TasSession)));

				var ncore = new IndexedStateLump(BinaryStateLump.BranchStateFile);
				var nframebuffer = new IndexedStateLump(BinaryStateLump.BranchFrameBuffer);
				var ncoreframebuffer = new IndexedStateLump(BinaryStateLump.BranchCoreFrameBuffer);
				foreach (var b in Branches)
				{
					// A branch's picture and metadata are always worth keeping. The
					// machine behind it rides INSIDE the project file, which people
					// hand to each other and open on other PCs - and a state a GPU
					// drew is only good where that GPU is. So it is still left out
					// for a GPU-drawn machine (see DrawnByGpu); the greenzone, which
					// is a per-machine cache beside the project, is not. Skipped for
					// ALL branches or none: these lumps are joined to them by order.
					// The state itself is already a file beside this one (the engine wrote it
					// when the branch was made); what is recorded is WHICH file is this
					// branch's. The rule above decides whether the next session may use it.
					if (b.StateFile is not null && !DrawnByGpu)
					{
						bs.PutLump(ncore, tw => tw.WriteLine(b.StateFile));
					}
					if (b.OSDFrameBuffer is not null)
					{
						bs.PutLump(nframebuffer, s =>
						{
							var vp = new BitmapBufferVideoProvider(b.OSDFrameBuffer);
							QuickBmpFile.Save(vp, s, b.OSDFrameBuffer.Width, b.OSDFrameBuffer.Height);
						}, zstdCompress: false);
					}
					if (b.CoreFrameBuffer is not null)
					{
						bs.PutLump(ncoreframebuffer, s =>
						{
							var vp = new BitmapBufferVideoProvider(b.CoreFrameBuffer);
							QuickBmpFile.Save(vp, s, b.CoreFrameBuffer.Width, b.CoreFrameBuffer.Height);
						}, zstdCompress: false);
					}
					ncore.Increment();
					nframebuffer.Increment();
					ncoreframebuffer.Increment();
				}
			}
			catch
			{
				bs.Abort();
				return;
			}
			bs.CloseAndDispose();
		}

		protected override bool LoadProjectFormat()
		{
			_project?.Dispose();
			EngineProgress.Report("reading the project");
			_project = EngineProject.Open(Filename); // structural errors surface with the engine's reason

			ClearBeforeLoad();
			var p = _project;

			// the ctor's defaults, then the stored header metadata over them
			Header[HeaderKeys.MovieVersion] = $"Chimera Project File v{CurrentVersion.ToString(CultureInfo.InvariantCulture)}";
			for (var i = 0; i < p.HeaderCount; i++)
			{
				Header[p.HeaderKeyAt(i)] = p.HeaderValueAt(i);
			}
			if (StartsFromSavestate)
			{
				throw new InvalidOperationException("savestate-anchored projects are not supported (docs/project.md)");
			}
			if (p.CoreName.Length is not 0) Header[HeaderKeys.Core] = p.CoreName;
			if (p.CoreVersion.Length is not 0) Header[HeaderKeys.CoreVersion] = p.CoreVersion;
			if (p.CoreSha1.Length is not 0) Header[HeaderKeys.CorePackageSha1] = p.CoreSha1;
			if (p.Title.Length is not 0) Header[HeaderKeys.GameName] = p.Title;
			Header[HeaderKeys.Rerecords] = p.Rerecords.ToString();

			foreach (var line in p.Description.Split('\n'))
			{
				if (!string.IsNullOrWhiteSpace(line)) Comments.Add(line);
			}
			for (var i = 0; i < p.SubtitleCount; i++)
			{
				Subtitles.AddFromString(p.SubtitleAt(i));
			}
			Subtitles.Sort();

			SettingsJson = WrapSettings(p.SettingsJson);

			// TAStudio's layout, from the project itself (issue #83); a project saved before it lived there
			// still has it in the cache, read below when this is empty
			var layout = p.TAStudioJson;
			if (layout.Length is not 0) LoadedClientSettings = layout;

			var logText = p.LogText;
			if (logText.Length is not 0)
			{
				EngineProgress.Report("reading the input log");
				IsCountingRerecords = false;
				MakeBackup = false;
				ExtractInputLog(new StringReader(logText), out _);
				IsCountingRerecords = true;
			}

			for (var i = 0; i < p.MarkerCount; i++)
			{
				Markers.Add(new TasMovieMarker(checked((int)p.MarkerFrame(i)), p.MarkerText(i))
				{
					WantsState = p.MarkerKeepState(i),
				}, skipHistory: true);
			}
			// the run's own markers are never written to the file, so they are
			// worked out again here - which is the only way they can be trusted
			RefreshLastNonEmptyInput(0);
			Markers.RefreshPermanent();

			Branches.Clear();
			for (var i = 0; i < p.BranchCount; i++)
			{
				var b = new TasBranch
				{
					Frame = checked((int)p.BranchFrame(i)),
					UserText = p.BranchName(i),
					ChangeLog = new TasMovieChangeLog(this) { MaxSteps = ChangeLog.MaxSteps },
					InputLog = StringLogUtil.MakeStringLog(),
					Markers = new TasMovieMarkerList(this),
				};
				b.TimeStamp = DateTime.TryParse(
					p.BranchTime(i), CultureInfo.InvariantCulture, DateTimeStyles.RoundtripKind, out var stamp)
						? stamp
						: DateTime.Now;
				foreach (var line in p.BranchLogText(i).Split('\n'))
				{
					if (line.StartsWith('|')) b.InputLog.Add(line);
				}
				for (var m = 0; m < p.BranchMarkerCount(i); m++)
				{
					b.Markers.Add(new TasMovieMarker(checked((int)p.BranchMarkerFrame(i, m)), p.BranchMarkerText(i, m))
					{
						WantsState = p.BranchMarkerKeepState(i, m),
					}, skipHistory: true);
				}
				Branches.Add(b);
			}

			EngineProgress.Report("reading the greenzone");
			LoadCacheFile();
			// a state file no branch names - its branch was never saved, or the cache above was
			// another machine's and was not used - is never read again, and may be gigabytes
			RemoveUnusedBranchStates();

			ChangeLog.Clear();
			Changes = false;
			return true;
		}

		/// <summary>
		/// The cache sibling, when it is there: absent or unreadable means a
		/// clean slate (fresh greenzone, no session position), never a failed
		/// load - that is the deal that keeps it out of the project's identity.
		/// </summary>
		/// <summary>
		/// The greenzone to read, moving one left beside the project by an older
		/// Chimera into the cache on the way. Moved rather than copied - it is
		/// the file whose size is the whole problem - and if the move will not
		/// happen it is read where it lies, which costs nothing but trying again
		/// next time. Returns where to read from.
		/// </summary>
		private string TakeOverLegacyGreenZone()
		{
			var cached = GreenZoneFilename;
			if (File.Exists(cached)) return cached;
			var legacy = LegacyGreenZonePathFor(Filename);
			if (!File.Exists(legacy)) return cached;
			try
			{
				ProjectCache.Ensure(Project.Id);
				File.Move(legacy, cached);
				return cached;
			}
			catch (Exception ex) when (ex is IOException or UnauthorizedAccessException or NotSupportedException)
			{
				return legacy;
			}
		}

		private void LoadCacheFile()
		{
			DroppedCacheNote = null;
			ZipStateLoader bl = null;
			try
			{
				var path = TakeOverLegacyGreenZone();
				if (File.Exists(path)) bl = ZipStateLoader.LoadAndDetect(path, true);
			}
			catch
			{
				bl = null;
			}

			// The states in it are states of ONE machine; the sandbox will load
			// them into any machine running the same binary and only complain on
			// stderr, so the check has to be here, before a single one is used.
			// A cache from another machine is a clean slate, like a lost one:
			// recomputation, never work (docs/project.md).
			if (bl is not null)
			{
				string recorded = null;
				bl.GetLump(BinaryStateLump.Machine, abort: false, tr => recorded = tr.ReadLine()?.Trim());
				var current = MachineIdentityOf(Project);
				if (recorded is null || !string.Equals(recorded, current, StringComparison.OrdinalIgnoreCase))
				{
					DroppedCacheNote = recorded is null
						? "The cached states beside this project do not say which machine made them, so they were not used: the greenzone starts empty."
						: "The cached states beside this project were made by a machine with other settings, files or core, so they were not used: the greenzone starts empty.";
					bl.Dispose();
					bl = null;
				}
			}

			// No cache, or one of another machine: the lag log and the session
			// position stay as they were, which for a fresh load is empty. The
			// states are the engine's and are not read here at all.
			if (bl is null) return;

			using (bl)
			{
				bl.GetLump(BinaryStateLump.LagLog, abort: false, tr => LagLog.Load(tr));

				// only for a project saved before the layout moved into it (issue #83); its next save
				// puts the layout where a new core cannot take it away
				if (LoadedClientSettings is null)
				{
					bl.GetLump(BinaryStateLump.ClientSettings, abort: false, tr =>
					{
						var clientSettings = tr.ReadToEnd();
						if (!string.IsNullOrEmpty(clientSettings)) LoadedClientSettings = clientSettings;
					});
				}

				bl.GetLump(BinaryStateLump.VerificationLog, abort: false, tr =>
				{
					VerificationLog.Clear();
					while (tr.ReadLine() is string line)
					{
						if (line.StartsWith('|')) VerificationLog.Add(line);
					}
				});

				bl.GetLump(BinaryStateLump.Session, abort: false, tr =>
				{
					try
					{
						TasSession = JsonConvert.DeserializeObject<TasSession>(tr.ReadToEnd());
						Branches.Current = TasSession.CurrentBranch;
					}
					catch
					{
						// a fresh session position instead
					}
				});

				// branch states and screenshots, joined by order
				var ncore = new IndexedStateLump(BinaryStateLump.BranchStateFile);
				var nframebuffer = new IndexedStateLump(BinaryStateLump.BranchFrameBuffer);
				var ncoreframebuffer = new IndexedStateLump(BinaryStateLump.BranchCoreFrameBuffer);
				foreach (var b in Branches)
				{
					// a state a GPU drew belongs where that GPU is, and a project
					// travels; an older file may still hold one, and loading it
					// would put a machine that cannot draw on the screen. A session
					// that did not finish is not trusted either, for the reason the
					// greenzone is not (see TasMovie.Attach).
					if (!StatesMadeByGpu && ProjectRecovery.LastSessionEndedCleanly(Project.Id))
					{
						bl.GetLump(ncore, abort: false, tr =>
						{
							// a name and nothing else: a cache somebody edited must not be able to
							// point a branch at a path
							var name = Path.GetFileName(tr.ReadLine()?.Trim() ?? "");
							if (name.Length is not 0 && File.Exists(BranchStatePath(name))) b.StateFile = name;
						});
					}
					bl.GetLump(nframebuffer, abort: false, (Stream s, long _) =>
					{
						QuickBmpFile.LoadAuto(s, out var vp);
						b.OSDFrameBuffer = new BitmapBuffer(vp.BufferWidth, vp.BufferHeight, vp.GetVideoBuffer());
					});
					bl.GetLump(ncoreframebuffer, abort: false, (Stream s, long _) =>
					{
						QuickBmpFile.LoadAuto(s, out var vp);
						b.CoreFrameBuffer = new BitmapBuffer(vp.BufferWidth, vp.BufferHeight, vp.GetVideoBuffer());
					});
					ncore.Increment();
					nframebuffer.Increment();
					ncoreframebuffer.Increment();
				}

				// The states themselves are not in here any more: the engine keeps
				// the history and writes its own file beside this one, streamed
				// rather than assembled (docs/state-manager.md). It is read when
				// the emulator arrives, since there is nowhere to put it until
				// then - see Attach.
				//
				// Said when there is work to say it about: a project with frames in
				// it opens with an empty greenzone on a machine a GPU draws, and a
				// person who is not told simply sees their cached states gone.
				if (DroppedCacheNote is null && InputLogLength > 0 && !GreenzoneMayOutliveSession)
				{
					DroppedCacheNote =
						"This machine is drawn by a GPU, and its core is not yet known to reload a greenzone"
						+ " correctly, so the states are not kept between sessions. The greenzone starts empty"
						+ " and fills again as the movie plays.";
				}
				else if (DroppedCacheNote is null && InputLogLength > 0
					&& !ProjectRecovery.LastSessionEndedCleanly(Project.Id))
				{
					DroppedCacheNote =
						"Chimera did not close normally the last time this project was open, so its saved"
						+ " greenzone is not trusted: a history written by a session that was already going"
						+ " wrong can carry a broken machine back in. It starts empty and fills as the movie plays.";
				}
			}
		}
	}
}
