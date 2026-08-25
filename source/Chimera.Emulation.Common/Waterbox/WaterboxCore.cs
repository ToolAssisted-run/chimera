using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Runtime.InteropServices;
using System.Text;

using Chimera.Common;
using Chimera.Emulation.Common.Engine;

using Newtonsoft.Json;

namespace Chimera.Emulation.Common.Waterbox
{
	/// <summary>
	/// The ONE built-in generic waterbox core adapter, and since the engine
	/// migration a THIN one: the machine itself is the engine's ce_session (see
	/// docs/engine-migration.md) - the same machine chimera-run drives headlessly
	/// and the witness gate's Level E verifies. What remains here is the
	/// frontend-facing half: IEmulator and friends, the settings objects, and the
	/// service surfaces the optional tooling groups back - the groups themselves
	/// are probed and driven by the session.
	/// </summary>
	// The attribute names the ADAPTER, which is all a class-level attribute can do
	// when one class serves every package. It is the fallback; the real identity
	// comes from the package, through ICoreIdentity below.
	[PortedCore(
		name: "Waterbox",
		author: "miniBox",
		portedVersion: "1.0.0",
		portedUrl: "https://github.com/SergioMartin86/miniBox")]
	public sealed partial class WaterboxCore : IEmulator, IVideoProvider, ISoundProvider, IStatable, IInputPollable,
		ICoreIdentity, ISettable<WaterboxCoreSettings, WaterboxCoreSyncSettings>
	{
		/// <summary>
		/// The identity of the PACKAGE this instance is running - what the status bar,
		/// the movie header and the about box should say. Falls back to the adapter's
		/// name only if a package declares no coreName at all.
		/// </summary>
		public CoreAttribute CoreIdentity => IdentityOf(_cfg);

		/// <summary>Builds a core's identity from its package declaration.</summary>
		internal static CoreAttribute IdentityOf(WaterboxConfig cfg)
			=> new PortedCoreAttribute(
				name: string.IsNullOrWhiteSpace(cfg.CoreName) ? "Waterbox" : cfg.CoreName,
				author: cfg.Author ?? "",
				portedVersion: cfg.Version ?? "",
				portedUrl: cfg.Url ?? "");

		private readonly WaterboxConfig _cfg;
		private readonly EngineSession _session;
		private WaterboxCoreSyncSettings _syncSettings;
		private WaterboxCoreSettings _settings;

		private readonly int _width, _height, _samplesPerFrame;
		private int _nsamp; // what the last frame actually produced
		private readonly int[] _videoBuff;
		private readonly short[] _stereoBuff;
		private readonly string[] _buttons;
		private readonly WaterboxConfig.AxisConfig[] _axes;
		private byte[] _stateScratch = [ ];

		/// <summary>
		/// What built the waterbox host this session is running on, as JSON - shown by
		/// the frontend and recorded by movies.
		/// </summary>
		public static string HostBuildInfo => EngineSession.HostBuildInfo;

		public WaterboxCore(byte[] rom, WaterboxConfig cfg, string packageDir, WaterboxCoreSyncSettings syncSettings, WaterboxCoreSettings settings = null, IReadOnlyDictionary<string, byte[]> firmware = null)
		{
			_cfg = cfg;
			_syncSettings = syncSettings?.Clone() ?? new WaterboxCoreSyncSettings();
			_settings = settings?.Clone() ?? new WaterboxCoreSettings();
			_width = cfg.Video.Width;
			_height = cfg.Video.Height;
			_samplesPerFrame = cfg.Audio.SamplesPerFrame;
			_videoBuff = new int[_width * _height];
			_stereoBuff = new short[_samplesPerFrame * 2];
			_buttons = cfg.Input.Buttons.ToArray();
			_axes = cfg.Input.Axes?.ToArray() ?? [ ];

			ServiceProvider = new BasicServiceProvider(this);

			// The engine builds and runs the machine. A refused rom surfaces as the
			// core's own words (GetLoadError), which the engine already collected.
			try
			{
				_session = EngineSession.Open(packageDir, rom, SerializeSettings(EffectiveSettings()), firmware);
			}
			catch (InvalidOperationException ex)
			{
				throw new CoreLoadException(ex.Message);
			}

			// Memory domains are self-described by the guest at runtime (size/count
			// can depend on settings); pointer-backed straight into guest memory.
			var domains = new List<MemoryDomain>(_session.DomainCount);
			for (int i = 0; i < _session.DomainCount; i++)
			{
				domains.Add(new MemoryDomainIntPtr(
					_session.DomainName(i), MemoryDomain.Endian.Little,
					_session.DomainPtr(i), _session.DomainSize(i), _session.DomainWritable(i), 1));
			}

			// The optional tooling ABI (see WaterboxCore.Tooling.cs) - may append bus
			// domains to the list, so it runs before the domains are published.
			InitTooling((BasicServiceProvider)ServiceProvider, domains);
			((BasicServiceProvider)ServiceProvider).Register<IMemoryDomains>(new MemoryDomainList(domains));
		}

		// ---- settings ----

		/// <summary>
		/// What the guest is told: every declared setting, at the package's default
		/// unless the user (or a movie) overrode it. Both kinds go in the same object -
		/// the guest reads the keys it knows and the sync/non-sync split is a question
		/// about movies and reboots, which is the frontend's business, not the core's.
		/// (The engine overlays this onto the declared defaults again, harmlessly:
		/// the merge is idempotent.)
		/// </summary>
		private Dictionary<string, object> EffectiveSettings()
		{
			var effective = new Dictionary<string, object>();
			foreach (var decl in Decls) effective[decl.Name] = decl.DefaultValue;
			foreach (var kv in _settings.Values ?? new()) if (effective.ContainsKey(kv.Key) || Decl(kv.Key) is null) effective[kv.Key] = kv.Value;
			foreach (var kv in _syncSettings.Values ?? new()) effective[kv.Key] = kv.Value;
			return effective;
		}

		private IReadOnlyList<WaterboxConfig.SettingDecl> Decls
			=> _cfg.Settings ?? [ ];

		private WaterboxConfig.SettingDecl Decl(string name)
			=> Decls.FirstOrDefault(d => d.Name == name);

		/// <summary>The declarations for one half of the split, handed to the settings object so the grid can draw them.</summary>
		private IReadOnlyList<WaterboxConfig.SettingDecl> DeclsFor(bool sync)
			=> Decls.Where(d => d.Sync == sync).ToList();

		// Delivered as a flat JSON object, e.g. {"initFillByte":171}. The guest
		// parses it with a small JSON reader (jsmn for C cores, nlohmann for C++).
		private static string SerializeSettings(Dictionary<string, object> settings)
			=> JsonConvert.SerializeObject(settings);

		public IEmulatorServiceProvider ServiceProvider { get; }

		public ControllerDefinition ControllerDefinition => _controllerDefinition ??= MakeControllerDefinition();
		private ControllerDefinition _controllerDefinition;

		private ControllerDefinition MakeControllerDefinition()
		{
			var def = new ControllerDefinition(_cfg.Input.Name ?? "Waterbox Controller");
			foreach (var button in _buttons)
			{
				def.BoolButtons.Add(button);
			}
			foreach (var axis in _axes)
			{
				def.Axes.Add(axis.Name, new AxisSpec(axis.Min.RangeTo(axis.Max), axis.Neutral));
			}
			return def.MakeImmutable();
		}

		public bool FrameAdvance(IController controller, bool render, bool renderSound = true)
		{
			CheckDisposed();
			ulong input = 0;
			if (_buttons.Length > 64)
			{
				// A wide controller (a DOS keyboard): every button rides the
				// engine's set_button channel; the engine delivers only changes
				// to the guest. The packed mask stays zero - one path, exact.
				for (int i = 0; i < _buttons.Length; i++)
				{
					_session.SetButton(i, controller.IsPressed(_buttons[i]));
				}
			}
			else
			{
				for (int i = 0; i < _buttons.Length; i++)
				{
					if (controller.IsPressed(_buttons[i])) input |= 1ul << i;
				}
			}

			// Analog values don't fit in the button mask, so they go over separately
			// just before the frame they belong to.
			for (int i = 0; i < _axes.Length; i++)
			{
				_session.SetAxis(i, controller.AxisValue(_axes[i].Name));
			}

			IsLagFrame = _session.FrameAdvance(input, render);
			DrainTrace();
			Frame++;
			if (IsLagFrame) LagCount++;

			if (render) Marshal.Copy(_session.VideoBuffer, _videoBuff, 0, _width * _height);
			var audio = _session.AudioBuffer(out _nsamp);
			Marshal.Copy(audio, _stereoBuff, 0, _nsamp * 2);
			return true;
		}

		public int Frame { get; private set; }

		public string SystemId => _cfg.SystemId;

		public bool DeterministicEmulation => _cfg.Deterministic;

		public void ResetCounters()
		{
			Frame = 0;
			LagCount = 0;
			IsLagFrame = false;
		}

		public void Dispose() => _session.Dispose();

		private void CheckDisposed()
		{
			if (_session.Disposed) throw new ObjectDisposedException(nameof(WaterboxCore));
		}

		// ---------------- IVideoProvider ----------------

		public int BufferWidth => _width;
		public int BufferHeight => _height;
		public int VirtualWidth => _cfg.Video.VirtualWidth;
		public int VirtualHeight => _cfg.Video.VirtualHeight;
		public int BackgroundColor => unchecked((int)0xFF000000);
		public int VsyncNumerator => _session.VsyncNumerator;
		public int VsyncDenominator => _session.VsyncDenominator;
		public int[] GetVideoBuffer() => _videoBuff;

		// ---------------- ISoundProvider ----------------

		public bool CanProvideAsync => false;
		public SyncSoundMode SyncMode => SyncSoundMode.Sync;

		public void GetSamplesSync(out short[] samples, out int nsamp)
		{
			samples = _stereoBuff;
			nsamp = _nsamp;
		}

		public void DiscardSamples() { }

		public void SetSyncMode(SyncSoundMode mode)
		{
			if (mode == SyncSoundMode.Async) throw new NotSupportedException("Async mode is not supported.");
		}

		public void GetSamplesAsync(short[] samples) => throw new InvalidOperationException("Async mode is not supported.");

		// ---------------- IStatable ----------------

		public bool AvoidRewind => false;

		public void SaveStateBinary(BinaryWriter writer)
		{
			CheckDisposed();
			var state = _session.SaveState(out var len);
			if (_stateScratch.Length < len) _stateScratch = new byte[len];
			Marshal.Copy(state, _stateScratch, 0, len);
			writer.Write(len);
			writer.Write(_stateScratch, 0, len);
			writer.Write(IsLagFrame);
			writer.Write(LagCount);
			writer.Write(Frame);
		}

		public void LoadStateBinary(BinaryReader reader)
		{
			CheckDisposed();
			int len = reader.ReadInt32();
			if (_stateScratch.Length < len) _stateScratch = new byte[len];
			int got = 0;
			while (got < len)
			{
				int n = reader.Read(_stateScratch, got, len - got);
				if (n <= 0) throw new EndOfStreamException("truncated waterbox savestate");
				got += n;
			}
			// (the engine re-asserts the tracing flag itself: a state load overwrites
			// the guest memory it lives in)
			_session.LoadState(_stateScratch, len);

			IsLagFrame = reader.ReadBoolean();
			LagCount = reader.ReadInt32();
			Frame = reader.ReadInt32();
		}

		// ---------------- ISettable ----------------
		// The package declares which of its settings are sync. Sync ones shape the
		// machine, are mounted for Init, are recorded in movies, and need a reboot to
		// change. Non-sync ones are pushed to the running guest if it exports the
		// live-settings entry points, and otherwise also need a reboot - a setting
		// that silently failed to apply would be worse than a reboot.

		public WaterboxCoreSettings GetSettings()
		{
			var s = _settings.Clone();
			s.Declarations = DeclsFor(sync: false);
			return s;
		}

		public WaterboxCoreSyncSettings GetSyncSettings()
		{
			var s = _syncSettings.Clone();
			s.Declarations = DeclsFor(sync: true);
			return s;
		}

		public PutSettingsDirtyBits PutSettings(WaterboxCoreSettings o)
		{
			var incoming = o?.Clone() ?? new WaterboxCoreSettings();
			var changed = !_settings.ValuesEqual(incoming);
			_settings = incoming;
			if (!changed) return PutSettingsDirtyBits.None;
			return _session.ApplySettings(SerializeSettings(EffectiveSettings()))
				? PutSettingsDirtyBits.None
				: PutSettingsDirtyBits.RebootCore;
		}

		public PutSettingsDirtyBits PutSyncSettings(WaterboxCoreSyncSettings o)
		{
			var incoming = o?.Clone() ?? new WaterboxCoreSyncSettings();
			var changed = !_syncSettings.ValuesEqual(incoming);
			_syncSettings = incoming;
			return changed ? PutSettingsDirtyBits.RebootCore : PutSettingsDirtyBits.None;
		}

		// ---------------- IInputPollable ----------------

		public int LagCount { get; set; }
		public bool IsLagFrame { get; set; }

		public IInputCallbackSystem InputCallbacks
		{
			[FeatureNotImplemented]
#pragma warning disable CA1065 // convention for [FeatureNotImplemented] is to throw NIE
			get => throw new NotImplementedException();
#pragma warning restore CA1065
		}
	}
}
