using System.Globalization;
using System.IO;
using System.Linq;

using Newtonsoft.Json;

using Chimera.Bizware.Graphics;
using Chimera.Common;
using Chimera.Common.IOExtensions;
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
		/// The greenzone sibling: everything regenerable, beside the project.
		/// Present = loaded; absent = a clean slate (docs/project.md).
		/// </summary>
		public string GreenZoneFilename => Path.ChangeExtension(Filename, "chimeraGreenZone");

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
			p.SetCore(Header[HeaderKeys.Core], Header[HeaderKeys.CoreVersion], Header[HeaderKeys.CorePackageSha1]);
			p.Rerecords = Rerecords;
			p.SetSettingsJson(FlattenSyncSettings(SyncSettingsJson));
			p.Description = string.Join("\n", Comments);

			p.SubtitlesClear();
			Subtitles.Sort();
			foreach (var subtitle in Subtitles)
			{
				p.SubtitleAdd(subtitle.ToString());
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

			try
			{
				p.Save(fn);
			}
			catch (InvalidOperationException ex)
			{
				return new FileWriteResult(FileWriteEnum.FailedDuringWrite, new(fn, ""), ex);
			}

			if (!isBackup)
			{
				WriteCacheFile(Path.ChangeExtension(fn, "chimeraGreenZone"));
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
		internal static string FlattenSyncSettings(string syncSettingsJson)
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

		internal static string WrapSyncSettings(string flatJson)
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
		/// The regenerable bulk, beside the project: greenzone, lag log, session
		/// position, column layout, verification log, branch states and
		/// screenshots (joined to the project's branches by order). A failed
		/// cache write never fails the save - the cache is the one file whose
		/// loss costs recomputation only.
		/// </summary>
		private void WriteCacheFile(string path)
		{
			var createResult = ZipStateSaver.Create(path, Session.Settings.MovieCompressionLevel);
			if (createResult.IsError) return;
			var bs = createResult.Value;
			try
			{
				IStateManagerSettings settingsToSave;
				try
				{
					settingsToSave = TasStateManager?.Settings ?? Session.Settings.DefaultTasStateManagerSettings;
				}
				catch
				{
					settingsToSave = Session.Settings.DefaultTasStateManagerSettings;
				}
				var settings = JsonConvert.SerializeObject(
					settingsToSave,
					new JsonSerializerSettings() { TypeNameHandling = TypeNameHandling.Objects });
				bs.PutLump(BinaryStateLump.StateHistorySettings, tw => tw.WriteLine(settings));
				bs.PutLump(BinaryStateLump.LagLog, tw => LagLog.Save(tw), zstdCompress: true);
				if (ClientSettingsForSave != null)
				{
					var clientSettingsJson = ClientSettingsForSave();
					bs.PutLump(BinaryStateLump.ClientSettings, (TextWriter tw) => tw.Write(clientSettingsJson));
				}
				if (VerificationLog.Count is not 0)
				{
					bs.PutLump(BinaryStateLump.VerificationLog, tw => tw.WriteLine(VerificationLog.ToInputLog()));
				}
				bs.PutLump(BinaryStateLump.Session, tw => tw.WriteLine(JsonConvert.SerializeObject(TasSession)));
				// ZipStateSaver surfaces a failing lump only at close, which would
				// poison the whole cache - so the state history is serialized into
				// memory FIRST, and a manager that cannot serialize costs a cold
				// greenzone on the next load, nothing more
				byte[] history = null;
				if (TasStateManager is not null)
				{
					try
					{
						using var ms = new MemoryStream();
						using var bw = new BinaryWriter(ms);
						TasStateManager.SaveStateHistory(bw);
						bw.Flush();
						history = ms.ToArray();
					}
					catch
					{
						history = null;
					}
				}
				if (history is not null)
				{
					bs.PutLump(BinaryStateLump.StateHistory, (Stream s) => s.Write(history, 0, history.Length));
				}

				var ncore = new IndexedStateLump(BinaryStateLump.BranchCoreData);
				var nframebuffer = new IndexedStateLump(BinaryStateLump.BranchFrameBuffer);
				var ncoreframebuffer = new IndexedStateLump(BinaryStateLump.BranchCoreFrameBuffer);
				foreach (var b in Branches)
				{
					if (b.CoreData is not null)
					{
						bs.PutLump(ncore, (Stream s) => s.Write(b.CoreData, 0, b.CoreData.Length));
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
			_project = EngineProject.Open(Filename); // structural errors surface with the engine's reason

			ClearBeforeLoad();
			var p = _project;

			// the ctor's defaults, then the stored header metadata over them
			Header[HeaderKeys.MovieVersion] = $"BizHawk v2.0 Tasproj v{CurrentVersion.ToString(CultureInfo.InvariantCulture)}";
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

			SyncSettingsJson = WrapSyncSettings(p.SettingsJson);

			var logText = p.LogText;
			if (logText.Length is not 0)
			{
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
			if (!Markers.Any(m => m.Frame is 0))
			{
				Markers.Add(new TasMovieMarker(0, "Power on"), skipHistory: true);
			}

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

			LoadCacheFile();

			ChangeLog.Clear();
			Changes = false;
			return true;
		}

		/// <summary>
		/// The cache sibling, when it is there: absent or unreadable means a
		/// clean slate (fresh greenzone, no session position), never a failed
		/// load - that is the deal that keeps it out of the project's identity.
		/// </summary>
		private void LoadCacheFile()
		{
			ZipStateLoader bl = null;
			try
			{
				if (File.Exists(GreenZoneFilename)) bl = ZipStateLoader.LoadAndDetect(GreenZoneFilename, true);
			}
			catch
			{
				bl = null;
			}

			if (bl is null)
			{
				TasStateManager?.Dispose();
				TasStateManager = Session.Settings.DefaultTasStateManagerSettings.CreateManager(IsReserved);
				return;
			}

			using (bl)
			{
				bl.GetLump(BinaryStateLump.LagLog, abort: false, tr => LagLog.Load(tr));

				bl.GetLump(BinaryStateLump.ClientSettings, abort: false, tr =>
				{
					var clientSettings = tr.ReadToEnd();
					if (!string.IsNullOrEmpty(clientSettings)) LoadedClientSettings = clientSettings;
				});

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
				var ncore = new IndexedStateLump(BinaryStateLump.BranchCoreData);
				var nframebuffer = new IndexedStateLump(BinaryStateLump.BranchFrameBuffer);
				var ncoreframebuffer = new IndexedStateLump(BinaryStateLump.BranchCoreFrameBuffer);
				foreach (var b in Branches)
				{
					bl.GetLump(ncore, abort: false, (Stream s, long _) => b.CoreData = s.ReadAllBytes());
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

				var settings = Session.Settings.DefaultTasStateManagerSettings;
				bl.GetLump(BinaryStateLump.StateHistorySettings, abort: false, tr =>
				{
					try
					{
						settings = JsonConvert.DeserializeObject<IStateManagerSettings>(tr.ReadToEnd()) ?? settings;
					}
					catch
					{
						// defaults instead
					}
				});

				TasStateManager?.Dispose();
				TasStateManager = null;
				var hasHistory = bl.GetLump(BinaryStateLump.StateHistory, abort: false, br =>
				{
					try
					{
						TasStateManager = settings.CreateManager(IsReserved);
						TasStateManager.LoadStateHistory(br);
					}
					catch
					{
						TasStateManager?.Dispose();
						TasStateManager = null;
					}
				});
				if (!hasHistory || TasStateManager is null)
				{
					try
					{
						TasStateManager = settings.CreateManager(IsReserved);
					}
					catch
					{
						TasStateManager = Session.Settings.DefaultTasStateManagerSettings.CreateManager(IsReserved);
					}
				}
			}
		}
	}
}
