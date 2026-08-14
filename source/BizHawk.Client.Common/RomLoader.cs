using System.Collections.Generic;
using System.IO;
using System.IO.Compression;
using System.Linq;

using BizHawk.Common;
using BizHawk.Common.IOExtensions;
using BizHawk.Common.StringExtensions;
using BizHawk.Emulation.Common;
using BizHawk.Emulation.DiscSystem;

namespace BizHawk.Client.Common
{
	public class RomLoader
	{
		private class DiscAsset
		{
			public Disc DiscData { get; set; }
			public DiscType DiscType { get; set; }
			public string DiscName { get; set; }
		}
		private class RomAsset : IRomAsset
		{
			public byte[] RomData { get; set; }
			public byte[] FileData { get; set; }
			public string Extension { get; set; }
			public string RomPath { get; set; }
			public GameInfo Game { get; set; }
		}
		private class LoadParameters
		{
			public CoreComm Comm { get; set; }

			public GameInfo Game { get; set; }

			public List<IRomAsset> Roms { get; set; } = new List<IRomAsset>();

			public List<object> Discs { get; set; } = new List<object>();
		}
		private readonly Config _config;

		private readonly IDialogParent _dialogParent;

		public RomLoader(Config config, IDialogParent dialogParent)
		{
			_config = config;
			_dialogParent = dialogParent;
		}

		private bool? Question(string text)
			=> _dialogParent.ModalMessageBox3(icon: EMsgBoxIcon.Question, caption: "ROM loader", text: text);

		public enum LoadErrorType
		{
			Unknown,
			Xml,
			DiscError,
		}

		// helper methods for the settings events
		private TSetting GetCoreSettings<TCore, TSetting>()
			where TCore : IEmulator
		{
			return (TSetting)GetCoreSettings(typeof(TCore), typeof(TSetting));
		}

		private TSync GetCoreSyncSettings<TCore, TSync>()
			where TCore : IEmulator
		{
			return (TSync)GetCoreSyncSettings(typeof(TCore), typeof(TSync));
		}

		private object GetCoreSettings(Type t, Type settingsType)
		{
			var e = new SettingsLoadArgs(t, settingsType);
			if (OnLoadSettings == null)
				throw new InvalidOperationException("Frontend failed to provide a settings getter");
			OnLoadSettings(this, e);
			if (e.Settings != null && e.Settings.GetType() != settingsType)
				throw new InvalidOperationException($"Frontend did not provide the requested settings type: Expected {settingsType}, got {e.Settings.GetType()}");
			return e.Settings;
		}

		private object GetCoreSyncSettings(Type t, Type syncSettingsType)
		{
			var e = new SettingsLoadArgs(t, syncSettingsType);
			if (OnLoadSyncSettings == null)
				throw new InvalidOperationException("Frontend failed to provide a sync settings getter");
			OnLoadSyncSettings(this, e);
			if (e.Settings != null && e.Settings.GetType() != syncSettingsType)
				throw new InvalidOperationException($"Frontend did not provide the requested sync settings type: Expected {syncSettingsType}, got {e.Settings.GetType()}");
			return e.Settings;
		}

		// TODO: reconsider the need for exposing these;
		public IEmulator LoadedEmulator { get; private set; }
		public GameInfo Game { get; private set; }
		public RomGame Rom { get; private set; }
		public string CanonicalFullPath { get; private set; }
		public bool Deterministic { get; set; }

		public class RomErrorArgs : EventArgs
		{
			// TODO: think about naming here, what to pass, a lot of potential good information about what went wrong could go here!
			public RomErrorArgs(string message, string systemId, LoadErrorType type)
			{
				Message = message;
				AttemptedCoreLoad = systemId;
				Type = type;
			}

			public RomErrorArgs(string message, string systemId, string path, bool? det, LoadErrorType type)
				: this(message, systemId, type)
			{
				Deterministic = det;
				RomPath = path;
			}

			public string Message { get; }
			public string AttemptedCoreLoad { get; }
			public string RomPath { get; }
			public bool? Deterministic { get; set; }
			public bool Retry { get; set; }
			public LoadErrorType Type { get; }
		}

		public class SettingsLoadArgs : EventArgs
		{
			public object Settings { get; set; }
			public Type Core { get; }
			public Type SettingsType { get; }
			public SettingsLoadArgs(Type t, Type s)
			{
				Core = t;
				SettingsType = s;
				Settings = null;
			}
		}

		public delegate void SettingsLoadEventHandler(object sender, SettingsLoadArgs e);
		public event SettingsLoadEventHandler OnLoadSettings;
		public event SettingsLoadEventHandler OnLoadSyncSettings;

		public delegate void LoadErrorEventHandler(object sender, RomErrorArgs e);
		public event LoadErrorEventHandler OnLoadError;

		public Func<HawkFile, int?> ChooseArchive { get; set; }

		public Func<RomGame, string> ChoosePlatform { get; set; }

		// in case we get sent back through the picker more than once, use the same choice the second time
		private int? _previousChoice;
		private int? HandleArchive(HawkFile file)
		{
			if (_previousChoice.HasValue)
			{
				return _previousChoice;
			}

			if (ChooseArchive != null)
			{
				_previousChoice = ChooseArchive(file);
				return _previousChoice;
			}

			return null;
		}

		// May want to phase out this method in favor of the overload with more parameters
		private void DoLoadErrorCallback(string message, string systemId, LoadErrorType type = LoadErrorType.Unknown)
		{
			OnLoadError?.Invoke(this, new RomErrorArgs(message, systemId, type));
		}

		private void DoLoadErrorCallback(string message, string systemId, string path, bool det, LoadErrorType type = LoadErrorType.Unknown)
		{
			OnLoadError?.Invoke(this, new RomErrorArgs(message, systemId, path, det, type));
		}

		public IOpenAdvanced OpenAdvanced { get; set; }

		private bool HandleArchiveBinding(HawkFile file, bool showDialog = true)
		{
			// try binding normal rom extensions first
			if (!file.IsBound)
			{
				file.BindSoleItemOf(CoreRegistry.Instance.KnownRomExtensions.ToList());
			}
			// ...including unrecognised extensions that the user has set a platform for
			if (!file.IsBound)
			{
				var exts = _config.PreferredPlatformsForExtensions.Where(static kvp => !string.IsNullOrEmpty(kvp.Value))
					.Select(static kvp => kvp.Key)
					.ToList();
				if (exts.Count is not 0) file.BindSoleItemOf(exts);
			}

			// if we have an archive and need to bind something, then pop the dialog
			if (file.IsArchive && !file.IsBound)
			{
				var result = showDialog ? HandleArchive(file) : null;
				if (result.HasValue)
				{
					file.BindArchiveMember(result.Value);
				}
				else
				{
					return false;
				}
			}

			CanonicalFullPath = file.CanonicalFullPath;

			return true;
		}

		private GameInfo MakeGameFromDisc(Disc disc, string ext, string name, bool fastFailUnsupportedSystems = true)
		{
			// TODO - use more sophisticated IDer
			var discType = new DiscIdentifier(disc).DetectDiscType();
			var discHasher = new DiscHasher(disc);
			var discHash = discHasher.CalculateBizHash(discType);

			var game = new GameInfo { Name = name, Hash = discHash };
			void NoCoreForSystem(string sysID)
			{
				// no supported emulator core for these (yet)
				game.System = sysID;
				if (fastFailUnsupportedSystems) throw new NoAvailableCoreException(sysID);
			}
			switch (discType)
			{
				case DiscType.DOS:
					game.System = VSystemID.Raw.DOS;
					break;

				case DiscType.SegaSaturn:
					game.System = VSystemID.Raw.SAT;
					break;
				case DiscType.MegaCD:
					game.System = VSystemID.Raw.GEN;
					break;
				case DiscType.Panasonic3DO:
					game.System = VSystemID.Raw.Panasonic3DO;
					break;
				case DiscType.PCFX:
					game.System = VSystemID.Raw.PCFX;
					break;

				case DiscType.TurboGECD:
				case DiscType.TurboCD:
					game.System = VSystemID.Raw.PCE;
					break;

				case DiscType.SonyPSP:
					game.System = VSystemID.Raw.PSP;
					break;

				case DiscType.JaguarCD:
					game.System = VSystemID.Raw.Jaguar;
					break;

				case DiscType.Amiga:
					NoCoreForSystem(VSystemID.Raw.Amiga);
					break;
				case DiscType.CDi:
					NoCoreForSystem(VSystemID.Raw.PhillipsCDi);
					break;
				case DiscType.Dreamcast:
					NoCoreForSystem(VSystemID.Raw.Dreamcast);
					break;
				case DiscType.GameCube:
					NoCoreForSystem(VSystemID.Raw.GameCube);
					break;
				case DiscType.NeoGeoCD:
					NoCoreForSystem(VSystemID.Raw.NeoGeoCD);
					break;
				case DiscType.Playdia:
					NoCoreForSystem(VSystemID.Raw.Playdia);
					break;
				case DiscType.SonyPS2:
					NoCoreForSystem(VSystemID.Raw.PS2);
					break;
				case DiscType.Wii:
					NoCoreForSystem(VSystemID.Raw.Wii);
					break;

				case DiscType.AudioDisc:
				case DiscType.UnknownCDFS:
				case DiscType.UnknownFormat:
					game.System = _config.TryGetChosenSystemForFileExt(ext, out var sysID) ? sysID : VSystemID.Raw.NULL;
					break;

				default: //"for an unknown disc, default to psx instead of pce-cd, since that is far more likely to be what they are attempting to open" [5e07ab3ec3b8b8de9eae71b489b55d23a3909f55, year 2015]
				case DiscType.SonyPSX:
					game.System = VSystemID.Raw.PSX;
					break;
			}
			return game;
		}

		private Disc/*?*/ InstantiateDiscFor(string path)
			=> DiscExtensions.CreateAnyType(
				path,
				str => DoLoadErrorCallback(message: str, systemId: "???"/*TODO we should NOT be doing this, even if it's just for error display*/, LoadErrorType.DiscError));

		private bool LoadDisc(string path, CoreComm nextComm, HawkFile file, string ext, string forcedCoreName, out IEmulator nextEmulator, out GameInfo game)
		{
			var disc = InstantiateDiscFor(path);
			if (disc == null)
			{
				game = null;
				nextEmulator = null;
				return false;
			}

			game = MakeGameFromDisc(disc, ext, Path.GetFileNameWithoutExtension(file.Name));

			var lp = new LoadParameters
			{
				Comm = nextComm,
				Game = game,
				Discs =
					{
						new DiscAsset
						{
							DiscData = disc,
							DiscType = new DiscIdentifier(disc).DetectDiscType(),
							DiscName = Path.GetFileNameWithoutExtension(path),
						},
					},
			};
			nextEmulator = MakeCoreFromRegistry(lp, forcedCoreName);
			return true;
		}

		/// <summary>
		/// The bundle the loaded game came from, or null when a bare rom was opened. What a
		/// core keeps (see <see cref="ICorePersistentData"/>) travels in here, and a movie
		/// recorded now cites it.
		/// </summary>
		public GameBundle LoadedBundle { get; private set; }

		public bool LoadRom(string path, CoreComm nextComm, string forcedCoreName = null, int recursiveCount = 0)
		{
			if (path == null) return false;

			// A bundle is a catalogue: it names the rom beside it, and what a core should
			// start with. Resolve it to that rom and carry the bundle for the caller, so
			// nothing downstream has to know bundles exist.
			LoadedBundle = null;
			if (GameBundle.IsBundlePath(path))
			{
				var bundle = GameBundle.Load(path);
				LoadedBundle = bundle;
				path = bundle.ResolveFile(bundle.Rom);
				if (!File.Exists(path))
				{
					DoLoadErrorCallback($"{Path.GetFileName(path)} is missing from this bundle's folder", "");
					return false;
				}
				_ = bundle.ReadFile(bundle.Rom); // hashes are checked before anything is loaded
			}

			if (recursiveCount > 1) // hack to stop recursive calls from endlessly rerunning if we can't load it
			{
				DoLoadErrorCallback("Failed multiple attempts to load ROM.", "");
				return false;
			}

			using HawkFile file = new(path, allowArchives: true);
			// make sure path is absolute
			path = CanonicalFullPath = file.CanonicalFullPath;

			if (!file.Exists) return false; // if the provided file doesn't even exist, give up!

			IEmulator nextEmulator;
			RomGame rom = null;
			GameInfo game = null;

			try
			{
				var cancel = false;

				{
					// do the archive binding we had to skip
					if (!HandleArchiveBinding(file))
					{
						return false;
					}

					// extension checking
					var ext = file.Extension;
					switch (ext)
					{
						default:
							if (Disc.IsValidExtension(ext))
							{
								if (file.IsArchive)
									throw new InvalidOperationException("Can't load CD files from archives!");
								if (!LoadDisc(path, nextComm, file, ext, forcedCoreName, out nextEmulator, out game))
									return false;
							}
							else
							{
								LoadOther(
									nextComm,
									file,
									ext: ext,
									forcedCoreName: forcedCoreName,
									out nextEmulator,
									out rom,
									out game,
									out cancel);
							}
							break;
					}
				}

				if (nextEmulator == null)
				{
					if (!cancel)
					{
						DoLoadErrorCallback("No core could load the rom.", null);
					}

					return false;
				}

				if (game is null) throw new Exception("RomLoader returned core but no GameInfo"); // shouldn't be null if `nextEmulator` isn't? just in case
			}
			catch (Exception ex)
			{
				var system = game?.System;

				DispatchErrorMessage(ex, system: system, path: path);
				return false;
			}

			Rom = rom;
			LoadedEmulator = nextEmulator;
			Game = game;
			return true;
		}

		private IEmulator MakeCoreFromRegistry(LoadParameters lp, string forcedCoreName = null)
		{
			IReadOnlyList<ICoreFactory> factories;
			if (forcedCoreName != null)
			{
				var singleFactory = CoreRegistry.Instance.GetFactories(lp.Game.System).SingleOrDefault(f => f.CoreName == forcedCoreName);
				factories = singleFactory != null ? [ singleFactory ] : [ ];
			}
			else
			{
				_ = _config.DefaultCores.TryGetValue(lp.Game.System, out var preferredCore);
				var dbForcedCoreName = lp.Game.ForcedCore;
				factories = CoreRegistry.Instance.GetFactories(lp.Game.System)
					.OrderBy(f =>
					{
						if (f.CoreName == preferredCore) return -2;
						if (f.CoreName.EqualsIgnoreCase(dbForcedCoreName)) return -1;
						return 0;
					})
					.ToList();
				if (factories.Count == 0)
				{
					throw new CoreLoadException($"No loaded core can run a {lp.Game.System} game. Open one with File > Open Core...");
				}
			}
			var exceptions = new List<Exception>();
			foreach (var factory in factories)
			{
				try
				{
					var ctx = new CoreCreationContext
					{
						Comm = lp.Comm,
						Game = lp.Game,
						Roms = lp.Roms,
						Discs = lp.Discs,
						DeterministicEmulationRequested = Deterministic,
						Settings = GetCoreSettings(factory.CoreType, factory.SettingsType),
						SyncSettings = GetCoreSyncSettings(factory.CoreType, factory.SyncSettingsType),
						FirmwareProvider = CoreFirmwareStore.ProviderFor(_config, factory.CoreName),
					};
					return factory.Create(ctx);
				}
				catch (Exception e) when (!_config.DontTryOtherCores && e is not InternalCoreException)
				{
					exceptions.Add(e);
				}
			}
			throw new AggregateException("No core could load the game", exceptions);
		}

		private void LoadOther(
			CoreComm nextComm,
			HawkFile file,
			string ext,
			string forcedCoreName,
			out IEmulator nextEmulator,
			out RomGame rom,
			out GameInfo game,
			out bool cancel)
		{
			cancel = false;
			rom = new RomGame(file);

			if (string.IsNullOrEmpty(rom.GameInfo.System))
			{
				// Has the user picked a preference for this extension?
				if (_config.TryGetChosenSystemForFileExt(rom.Extension.ToLowerInvariant(), out var systemID))
				{
					rom.GameInfo.System = systemID;
				}
				else if (CoreRegistry.Instance.TryGetSystemForExtension(rom.Extension, out var declaredSysID))
				{
					// a loaded core package declared this extension
					rom.GameInfo.System = declaredSysID;
				}
				else if (ChoosePlatform != null)
				{
					var result = ChoosePlatform(rom);
					if (!string.IsNullOrEmpty(result))
					{
						rom.GameInfo.System = result;
					}
					else
					{
						cancel = true;
					}
				}
			}

			game = rom.GameInfo;

			nextEmulator = null;
			if (game.System == null)
				return; // The user picked nothing in the Core picker

			var lp = new LoadParameters
			{
				Comm = nextComm,
				Game = game,
				Roms =
				{
					new RomAsset
					{
						RomData = rom.RomData,
						FileData = rom.FileData,
						Extension = rom.Extension,
						RomPath = file.CanonicalFullPath,
						Game = game,
					},
				},
			};
			nextEmulator = MakeCoreFromRegistry(lp, forcedCoreName);
		}

		private static bool IsDiscForXML(string path)
		{
			if (HawkFile.PathContainsPipe(path))
			{
				return false;
			}

			return Disc.IsValidExtension(Path.GetExtension(path));
		}

		private void DispatchErrorMessage(Exception ex, string system, string path)
		{
			if (ex is AggregateException agg)
			{
				// all cores failed to load a game, so tell the user everything that went wrong and maybe they can fix it
				if (agg.InnerExceptions.Count > 1)
				{
					DoLoadErrorCallback("Multiple cores failed to load the rom:", system);
				}
				foreach (Exception e in agg.InnerExceptions)
				{
					DispatchErrorMessage(e, system: system, path: path);
				}

				return;
			}

			// all of the specific exceptions we're trying to catch here aren't expected to have inner exceptions,
			// so drill down in case we got a TargetInvocationException or something like that
			while (ex.InnerException != null)
				ex = ex.InnerException;

			if (ex is CoreLoadException)
			{
				// the user can fix this one, and what to do is in the message
				DoLoadErrorCallback(ex.Message, system);
			}
			else if (ex is NoAvailableCoreException)
			{
				// handle exceptions thrown by the new detected systems that BizHawk does not have cores for
				DoLoadErrorCallback($"{ex.Message}\n\n{ex}", system);
			}
			else
			{
				DoLoadErrorCallback($"A core accepted the rom, but threw an exception while loading it:\n\n{ex}", system);
			}
		}
		/// <remarks>rom extensions (with leading dot, UPPERCASE) declared by loaded core packages; this is what's expected at call-site</remarks>
		public static IReadOnlyCollection<string> KnownRomExtensions
			=> CoreRegistry.Instance.KnownRomExtensions.Select(static e => e.ToUpperInvariant()).ToList();

		/// <remarks>built from the extensions declared by loaded core packages</remarks>
		public static FilesystemFilterSet RomFilter
			=> new(
				combinedEntryDesc: "Everything",
				FilesystemFilter.Archives,
				new FilesystemFilter("Disc Images", FilesystemFilter.DiscExtensions),
				new FilesystemFilter("Game Bundles", new[] { GameBundle.Extension.TrimStart('.') }),
				new FilesystemFilter("ROMs", CoreRegistry.Instance.KnownRomExtensions.Select(static e => e.TrimStart('.')).ToList(), addArchiveExts: true),
				FilesystemFilter.EmuHawkSaveStates);
	}
}
