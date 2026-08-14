using System.Collections.Generic;
using System.Globalization;
using System.IO;
using System.Linq;
using System.Runtime.InteropServices;
using System.Text;

using BizHawk.BizInvoke;
using BizHawk.Common;

using Newtonsoft.Json;

namespace BizHawk.Emulation.Common.Waterbox
{
	/// <summary>
	/// The ONE built-in generic waterbox core adapter. Loads any <c>core.wbx</c>
	/// through the miniBox host (libminiboxhost, shipped with miniHawk) and presents
	/// it as an IEmulator, driven entirely by the package's <c>waterbox.config</c>
	/// (the static machine surface) plus the guest's runtime self-description of its
	/// memory domains. No per-core managed code: this is the sole adapter for every
	/// waterboxed core.
	///
	/// The host is kept ACTIVE for the core's lifetime (guest memory mapped and
	/// callable), briefly deactivated only to bracket save/load state. Guest export
	/// addresses and memory-domain pointers are fixed (non-PIE guest), so they stay
	/// valid across the whole run.
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

		[UnmanagedFunctionPointer(CallingConvention.Cdecl)] private delegate int InitFn();
		[UnmanagedFunctionPointer(CallingConvention.Cdecl)] private delegate void FrameFn(ulong input);
		[UnmanagedFunctionPointer(CallingConvention.Cdecl)] private delegate void SetAxisFn(int index, int value);
		[UnmanagedFunctionPointer(CallingConvention.Cdecl)] private delegate IntPtr GetPtrFn();
		[UnmanagedFunctionPointer(CallingConvention.Cdecl)] private delegate int IntFn();
		[UnmanagedFunctionPointer(CallingConvention.Cdecl)] private delegate IntPtr MdNameFn(int i);
		[UnmanagedFunctionPointer(CallingConvention.Cdecl)] private delegate IntPtr MdPtrFn(int i);
		[UnmanagedFunctionPointer(CallingConvention.Cdecl)] private delegate long MdSizeFn(int i);
		[UnmanagedFunctionPointer(CallingConvention.Cdecl)] private delegate int MdIntFn(int i);

		private readonly WaterboxConfig _cfg;
		private readonly LibMiniBoxHost _host;
		private readonly WaterboxAbiShim _abi;
		private IntPtr _obj;
		private bool _active;

		private readonly FrameFn _frameAdvance;
		private readonly GetPtrFn _getAudio;
		private readonly GetPtrFn _getVideoBgra;
		private readonly IntFn _inputWasRead; // may be null

		private readonly LibMiniBoxHost.ReadCallback _imageRead;
		private readonly LibMiniBoxHost.ReadCallback _romRead;
		private readonly LibMiniBoxHost.ReadCallback _settingsRead;
		private readonly LibMiniBoxHost.WriteCallback _stateWrite;
		private readonly LibMiniBoxHost.ReadCallback _stateRead;

		private readonly byte[] _wbxBytes;
		private int _wbxPos;
		private readonly byte[] _romBytes;
		private int _romPos;

		// The files the user provided for the package's firmware declarations, by id.
		// Mounted like the rom is - the guest opens them by name during Init - and, like
		// the rom, they are part of the machine rather than of its state.
		private readonly IReadOnlyDictionary<string, byte[]> _firmware;
		private readonly List<LibMiniBoxHost.ReadCallback> _firmwareReads = new();
		private readonly byte[] _settingsBytes;
		private int _settingsPos;
		private WaterboxCoreSyncSettings _syncSettings;
		private WaterboxCoreSettings _settings;

		// The optional "live settings" group: a guest that exports all three can be
		// told about a non-sync setting change without being rebooted. The buffer is
		// guest-owned (the host cannot hand a host pointer across the sandbox) and
		// belongs in memory excluded from savestates, since settings are not state.
		private IntFn _settingsCapacity;
		private GetPtrFn _settingsBuffer;
		private VoidIntFn _settingsApply;
		private MemoryStream _saveBuf;
		private byte[] _loadBuf;
		private int _loadPos;
		private int _loadLen;

		private readonly int _width, _height, _samplesPerFrame, _channels;
		private IntFn _getAudioSampleCount; // optional: this frame's real sample count
		private int _nsamp;                 // what the last frame actually produced
		private int _vsyncNum, _vsyncDen;
		private readonly int[] _videoBuff;
		private readonly short[] _stereoBuff;
		private readonly string[] _buttons;
		private readonly WaterboxConfig.AxisConfig[] _axes;
		private readonly SetAxisFn _setAxis; // null iff the package declares no axes

		public WaterboxCore(byte[] rom, WaterboxConfig cfg, string wbxPath, WaterboxCoreSyncSettings syncSettings, WaterboxCoreSettings settings = null, IReadOnlyDictionary<string, byte[]> firmware = null)
		{
			_cfg = cfg;
			_romBytes = rom;
			_firmware = firmware ?? new Dictionary<string, byte[]>();
			_wbxBytes = File.ReadAllBytes(wbxPath);

			// Effective settings = the package's waterbox.config defaults, overlaid
			// with the user/movie sync-settings. Delivered to the guest as a mounted
			// "settings" file it reads during Init, so they can shape the machine.
			_syncSettings = syncSettings?.Clone() ?? new WaterboxCoreSyncSettings();
			_settings = settings?.Clone() ?? new WaterboxCoreSettings();
			_settingsBytes = SerializeSettings(EffectiveSettings());
			_width = cfg.Video.Width;
			_height = cfg.Video.Height;
			_samplesPerFrame = cfg.Audio.SamplesPerFrame;
			_channels = cfg.Audio.Channels;
			_videoBuff = new int[_width * _height];
			_stereoBuff = new short[_samplesPerFrame * 2];
			_buttons = cfg.Input.Buttons.ToArray();
			_axes = cfg.Input.Axes?.ToArray() ?? [ ];

			ServiceProvider = new BasicServiceProvider(this);

			_imageRead = ImageRead;
			_romRead = RomRead;
			_settingsRead = SettingsRead;
			_stateWrite = StateWrite;
			_stateRead = StateRead;

			var resolver = new DynamicLibraryImportResolver(
				$"libminiboxhost{(OSTailoredCode.IsUnixHost ? ".so" : ".dll")}", hasLimitedLifetime: false);
			_host = BizInvoker.GetInvoker<LibMiniBoxHost>(resolver, CallingConventionAdapters.Native);
			// The host itself is an ordinary library and speaks the host convention;
			// the GUEST is always sysv64, so calls into it go through this.
			_abi = new WaterboxAbiShim(resolver);

			var mib = cfg.MemoryLayoutMiB;
			var layout = new LibMiniBoxHost.MemoryLayoutTemplate
			{
				SbrkSize = (UIntPtr)((ulong)mib[0] << 20),
				SealedSize = (UIntPtr)((ulong)mib[1] << 20),
				InvisSize = (UIntPtr)((ulong)mib[2] << 20),
				PlainSize = (UIntPtr)((ulong)mib[3] << 20),
				MmapSize = (UIntPtr)((ulong)mib[4] << 20),
			};

			LibMiniBoxHost.ReturnData r = default;
			_host.wbx_create_host(ref layout, "core.wbx", _imageRead, UIntPtr.Zero, ref r);
			_obj = r.DataOrThrow();

			_host.wbx_mount_file(_obj, cfg.RomFile, _romRead, UIntPtr.Zero, 0, ref r);
			r.ThrowIfError();

			// The settings channel: always mounted (empty when the core has no
			// settings), so the guest ABI is uniform. Read-only, stable across states.
			_host.wbx_mount_file(_obj, "settings", _settingsRead, UIntPtr.Zero, 0, ref r);
			r.ThrowIfError();

			// Whatever firmware the user provided, under the ids the package declared.
			// The delegates are kept alive in a field: the host holds them as raw
			// function pointers for the mount's lifetime, and a collected one is a crash.
			foreach (var (id, bytes) in _firmware)
			{
				var pos = 0;
				LibMiniBoxHost.ReadCallback read = (ud, data, size) => ReadInto(bytes, ref pos, data, size);
				_firmwareReads.Add(read);
				_host.wbx_mount_file(_obj, id, read, UIntPtr.Zero, 0, ref r);
				r.ThrowIfError();
			}

			Activate();
			var init = Proc<InitFn>("Init");
			if (init() != 1)
			{
				throw new InvalidOperationException($"{cfg.CoreName}: core.wbx Init failed (bad rom?)");
			}
			Deactivate();
			_host.wbx_seal(_obj, ref r); // freeze the post-init image as the savestate baseline
			r.ThrowIfError();
			Activate();

			_frameAdvance = Proc<FrameFn>("FrameAdvance");
			if (_axes.Length != 0)
			{
				_setAxis = TryProc<SetAxisFn>("SetAxis")
					?? throw new InvalidOperationException($"{cfg.CoreName}: waterbox.config declares axes but core.wbx exports no SetAxis");
			}
			// optional: a core that can take non-sync settings without being rebooted
			_settingsCapacity = TryProc<IntFn>("GetSettingsCapacity");
			_settingsBuffer = TryProc<GetPtrFn>("GetSettingsBuffer");
			_settingsApply = TryProc<VoidIntFn>("PutSettings");

			_getVideoBgra = Proc<GetPtrFn>(cfg.Video.GetBgra);
			_getAudio = Proc<GetPtrFn>(cfg.Audio.Get);

			// Optional: a core whose frame rate or sample count depends on something only it knows
			// (a region setting, say) answers for them after Init, and waterbox.config's numbers are
			// the fallback. The declared samplesPerFrame is then the buffer's capacity.
			_getAudioSampleCount = TryProc<IntFn>("GetAudioSampleCount");
			_vsyncNum = TryProc<IntFn>("GetVsyncNumerator")?.Invoke() ?? 0;
			_vsyncDen = TryProc<IntFn>("GetVsyncDenominator")?.Invoke() ?? 0;
			if (_vsyncNum <= 0 || _vsyncDen <= 0)
			{
				_vsyncNum = cfg.Video.VsyncNumerator;
				_vsyncDen = cfg.Video.VsyncDenominator;
			}

			if (!string.IsNullOrEmpty(cfg.Lag?.InputWasRead))
			{
				_inputWasRead = Proc<IntFn>(cfg.Lag.InputWasRead);
			}

			// Memory domains are self-described by the guest at runtime (size/count
			// can depend on settings). Query them post-Init and build the list.
			var mdCount = Proc<IntFn>("GetMemoryDomainCount");
			var mdName = Proc<MdNameFn>("GetMemoryDomainName");
			var mdPtr = Proc<MdPtrFn>("GetMemoryDomainPtr");
			var mdSize = Proc<MdSizeFn>("GetMemoryDomainSize");
			var mdWritable = Proc<MdIntFn>("GetMemoryDomainWritable");
			int n = mdCount();
			var domains = new List<MemoryDomain>(n);
			for (int i = 0; i < n; i++)
			{
				var name = Marshal.PtrToStringAnsi(mdName(i));
				domains.Add(new MemoryDomainIntPtr(name, MemoryDomain.Endian.Little, mdPtr(i), mdSize(i), mdWritable(i) != 0, 1));
			}

			// The optional tooling ABI (see WaterboxCore.Tooling.cs) - may append bus
			// domains to the list, so it runs before the domains are published.
			InitTooling((BasicServiceProvider)ServiceProvider, domains);
			InitSaveRam((BasicServiceProvider)ServiceProvider);
			((BasicServiceProvider)ServiceProvider).Register<IMemoryDomains>(new MemoryDomainList(domains));
		}

		private static int ArgCount<T>() where T : Delegate
			=> typeof(T).GetMethod("Invoke")!.GetParameters().Length;

		private T Proc<T>(string name) where T : Delegate
		{
			LibMiniBoxHost.ReturnData r = default;
			_host.wbx_get_proc_addr(_obj, name, ref r);
			return Marshal.GetDelegateForFunctionPointer<T>(_abi.Wrap(r.DataOrThrow(), ArgCount<T>()));
		}

		/// <summary>
		/// Like <see cref="Proc{T}"/> but for OPTIONAL exports: the host reports a
		/// missing symbol as address 0 rather than an error, which is how the adapter
		/// discovers which tooling groups a core implements.
		/// </summary>
		private T TryProc<T>(string name) where T : Delegate
		{
			LibMiniBoxHost.ReturnData r = default;
			_host.wbx_get_proc_addr(_obj, name, ref r);
			var addr = r.DataOrThrow();
			return addr == IntPtr.Zero ? null : Marshal.GetDelegateForFunctionPointer<T>(_abi.Wrap(addr, ArgCount<T>()));
		}

		private void Activate()
		{
			if (_active) return;
			LibMiniBoxHost.ReturnData r = default;
			_host.wbx_activate_host(_obj, ref r);
			r.ThrowIfError();
			_active = true;
		}

		private void Deactivate()
		{
			if (!_active) return;
			LibMiniBoxHost.ReturnData r = default;
			_host.wbx_deactivate_host(_obj, ref r);
			r.ThrowIfError();
			_active = false;
		}

		private static IntPtr ReadInto(byte[] src, ref int pos, IntPtr dst, UIntPtr size)
		{
			int want = (int)size;
			int avail = src.Length - pos;
			int n = want < avail ? want : avail;
			if (n > 0)
			{
				Marshal.Copy(src, pos, dst, n);
				pos += n;
			}
			return (IntPtr)n;
		}

		private IntPtr ImageRead(UIntPtr ud, IntPtr data, UIntPtr size) => ReadInto(_wbxBytes, ref _wbxPos, data, size);
		private IntPtr RomRead(UIntPtr ud, IntPtr data, UIntPtr size) => ReadInto(_romBytes, ref _romPos, data, size);
		private IntPtr SettingsRead(UIntPtr ud, IntPtr data, UIntPtr size) => ReadInto(_settingsBytes, ref _settingsPos, data, size);
		// _loadBuf is reused and so may be larger than the state in it; _loadLen is
		// the part that counts.
		private IntPtr StateRead(UIntPtr ud, IntPtr data, UIntPtr size)
		{
			int want = (int)size;
			int avail = _loadLen - _loadPos;
			int n = want < avail ? want : avail;
			if (n > 0)
			{
				Marshal.Copy(_loadBuf, _loadPos, data, n);
				_loadPos += n;
			}
			return (IntPtr)n;
		}

		// ---- settings ----

		/// <summary>
		/// What the guest is told: every declared setting, at the package's default
		/// unless the user (or a movie) overrode it. Both kinds go in the same object -
		/// the guest reads the keys it knows and the sync/non-sync split is a question
		/// about movies and reboots, which is the frontend's business, not the core's.
		/// </summary>
		private Dictionary<string, object> EffectiveSettings()
		{
			var effective = new Dictionary<string, object>();
			foreach (var decl in Decls) effective[decl.Name] = decl.DefaultValue;
			foreach (var kv in _settings.Values ?? new()) if (effective.ContainsKey(kv.Key) || Decl(kv.Key) is null) effective[kv.Key] = kv.Value;
			foreach (var kv in _syncSettings.Values ?? new()) effective[kv.Key] = kv.Value;
			return effective;
		}

		/// <summary>
		/// Hands the running guest the current settings. Returns false if the core
		/// does not export the live-settings group, in which case the caller must
		/// reboot instead - the alternative is a settings dialog whose changes quietly
		/// do nothing.
		/// </summary>
		private bool PushSettingsToGuest()
		{
			if (_settingsBuffer is null || _settingsApply is null || _settingsCapacity is null) return false;
			var json = SerializeSettings(EffectiveSettings());
			Activate();
			var capacity = _settingsCapacity();
			if (json.Length > capacity)
			{
				throw new InvalidOperationException(
					$"{_cfg.CoreName}: settings JSON is {json.Length} bytes but the core's buffer holds {capacity}");
			}
			var dest = _settingsBuffer();
			if (dest == IntPtr.Zero) return false;
			Marshal.Copy(json, 0, dest, json.Length);
			_settingsApply(json.Length);
			return true;
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
		private static byte[] SerializeSettings(Dictionary<string, object> settings)
			=> Encoding.UTF8.GetBytes(JsonConvert.SerializeObject(settings));

		// The host calls this once per flag array and once per dirty page, so a
		// fresh array per call meant hundreds of allocations per state - and rewind
		// takes a state every frame. One scratch buffer, grown on demand, instead.
		private byte[] _stateScratch = [ ];

		private int StateWrite(UIntPtr ud, IntPtr data, UIntPtr size)
		{
			int n = (int)size;
			if (_stateScratch.Length < n) _stateScratch = new byte[n];
			Marshal.Copy(data, _stateScratch, 0, n);
			_saveBuf.Write(_stateScratch, 0, n);
			return 0;
		}

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
			_saveRamMaybeDirty = true; // see SaveRamModified: any frame may have written to the save
			ulong input = 0;
			for (int i = 0; i < _buttons.Length; i++)
			{
				if (controller.IsPressed(_buttons[i])) input |= 1ul << i;
			}

			// Analog values don't fit in the button mask, so they go over separately
			// just before the frame they belong to.
			for (int i = 0; i < _axes.Length; i++)
			{
				_setAxis(i, controller.AxisValue(_axes[i].Name));
			}

			_frameAdvance(input);
			DrainTrace();
			Frame++;
			IsLagFrame = _inputWasRead != null && _inputWasRead() == 0;
			if (IsLagFrame) LagCount++;

			if (render) Marshal.Copy(_getVideoBgra(), _videoBuff, 0, _width * _height);
			DrainAudio();
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

		public void Dispose()
		{
			if (_obj == IntPtr.Zero) return;
			try { Deactivate(); }
			catch { /* tearing down anyway */ }
			LibMiniBoxHost.ReturnData r = default;
			_host.wbx_destroy_host(_obj, ref r);
			_obj = IntPtr.Zero;
			_abi?.Dispose();
		}

		private void CheckDisposed()
		{
			if (_obj == IntPtr.Zero) throw new ObjectDisposedException(nameof(WaterboxCore));
		}

		// ---------------- IVideoProvider ----------------

		public int BufferWidth => _width;
		public int BufferHeight => _height;
		public int VirtualWidth => _cfg.Video.VirtualWidth;
		public int VirtualHeight => _cfg.Video.VirtualHeight;
		public int BackgroundColor => unchecked((int)0xFF000000);
		public int VsyncNumerator => _vsyncNum;
		public int VsyncDenominator => _vsyncDen;
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

		private unsafe void DrainAudio()
		{
			var src = (short*)_getAudio();
			// A core that reports its own count may produce a different number every frame (blip
			// resamplers do); the declared samplesPerFrame is then the buffer we must not overrun.
			_nsamp = _getAudioSampleCount?.Invoke() ?? _samplesPerFrame;
			if (_nsamp < 0) _nsamp = 0;
			if (_nsamp > _samplesPerFrame) _nsamp = _samplesPerFrame;
			if (_channels == 2)
			{
				for (int i = 0; i < _nsamp * 2; i++) _stereoBuff[i] = src[i];
			}
			else
			{
				for (int i = 0; i < _nsamp; i++)
				{
					_stereoBuff[i * 2] = src[i];
					_stereoBuff[i * 2 + 1] = src[i];
				}
			}
		}

		// ---------------- IStatable ----------------

		public bool AvoidRewind => false;

		public void SaveStateBinary(BinaryWriter writer)
		{
			CheckDisposed();
			_saveBuf ??= new MemoryStream();
			_saveBuf.SetLength(0);
			LibMiniBoxHost.ReturnData r = default;
			// No deactivate/activate bracket: the host activates itself for the
			// duration and restores whatever state it found. Bracketing it costs FOUR
			// full guest-address-space remaps per state, which at rewind's one state
			// per frame was a 10x slowdown of the whole emulator.
			_host.wbx_save_state(_obj, _stateWrite, UIntPtr.Zero, ref r);
			r.ThrowIfError();

			// GetBuffer, not ToArray: ToArray copies the whole state into a fresh
			// array every frame purely to hand it to the writer.
			int len = (int)_saveBuf.Length;
			writer.Write(len);
			writer.Write(_saveBuf.GetBuffer(), 0, len);
			writer.Write(IsLagFrame);
			writer.Write(LagCount);
			writer.Write(Frame);
		}

		public void LoadStateBinary(BinaryReader reader)
		{
			CheckDisposed();
			int len = reader.ReadInt32();
			if (_loadBuf == null || _loadBuf.Length < len) _loadBuf = new byte[len];
			int got = 0;
			while (got < len)
			{
				int n = reader.Read(_loadBuf, got, len - got);
				if (n <= 0) throw new EndOfStreamException("truncated waterbox savestate");
				got += n;
			}
			_loadLen = len;
			_loadPos = 0;

			LibMiniBoxHost.ReturnData r = default;
			_host.wbx_load_state(_obj, _stateRead, UIntPtr.Zero, ref r); // see SaveStateBinary re: no bracket
			r.ThrowIfError();
			RestoreTraceState(); // the guest's tracing flag lives in guest memory, which the state just overwrote

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
			return PushSettingsToGuest() ? PutSettingsDirtyBits.None : PutSettingsDirtyBits.RebootCore;
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
