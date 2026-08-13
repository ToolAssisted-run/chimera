using System.Collections.Generic;
using System.Globalization;
using System.IO;
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
		private readonly byte[] _settingsBytes;
		private int _settingsPos;
		private WaterboxCoreSyncSettings _syncSettings;
		private MemoryStream _saveBuf;
		private byte[] _loadBuf;
		private int _loadPos;
		private int _loadLen;

		private readonly int _width, _height, _samplesPerFrame, _channels;
		private readonly int[] _videoBuff;
		private readonly short[] _stereoBuff;
		private readonly string[] _buttons;
		private readonly WaterboxConfig.AxisConfig[] _axes;
		private readonly SetAxisFn _setAxis; // null iff the package declares no axes

		public WaterboxCore(byte[] rom, WaterboxConfig cfg, string wbxPath, WaterboxCoreSyncSettings syncSettings)
		{
			_cfg = cfg;
			_romBytes = rom;
			_wbxBytes = File.ReadAllBytes(wbxPath);

			// Effective settings = the package's waterbox.config defaults, overlaid
			// with the user/movie sync-settings. Delivered to the guest as a mounted
			// "settings" file it reads during Init, so they can shape the machine.
			_syncSettings = syncSettings?.Clone() ?? new WaterboxCoreSyncSettings();
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
			_getVideoBgra = Proc<GetPtrFn>(cfg.Video.GetBgra);
			_getAudio = Proc<GetPtrFn>(cfg.Audio.Get);
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

		private Dictionary<string, object> EffectiveSettings()
		{
			var effective = new Dictionary<string, object>();
			if (_cfg.Settings != null) foreach (var kv in _cfg.Settings) effective[kv.Key] = kv.Value;
			if (_syncSettings.Values != null) foreach (var kv in _syncSettings.Values) effective[kv.Key] = kv.Value;
			return effective;
		}

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
		public int VsyncNumerator => _cfg.Video.VsyncNumerator;
		public int VsyncDenominator => _cfg.Video.VsyncDenominator;
		public int[] GetVideoBuffer() => _videoBuff;

		// ---------------- ISoundProvider ----------------

		public bool CanProvideAsync => false;
		public SyncSoundMode SyncMode => SyncSoundMode.Sync;

		public void GetSamplesSync(out short[] samples, out int nsamp)
		{
			samples = _stereoBuff;
			nsamp = _samplesPerFrame;
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
			if (_channels == 2)
			{
				for (int i = 0; i < _samplesPerFrame * 2; i++) _stereoBuff[i] = src[i];
			}
			else
			{
				for (int i = 0; i < _samplesPerFrame; i++)
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
		// Sync settings shape the machine at construction (they are mounted for Init),
		// so changing them requires a core reboot to take effect. There are no plain
		// (non-sync) settings. GetSyncSettings returns the user overrides (what movies
		// record); the package defaults are applied by the adapter, not stored here.

		public WaterboxCoreSettings GetSettings() => new();

		public WaterboxCoreSyncSettings GetSyncSettings() => _syncSettings.Clone();

		public PutSettingsDirtyBits PutSettings(WaterboxCoreSettings o) => PutSettingsDirtyBits.None;

		public PutSettingsDirtyBits PutSyncSettings(WaterboxCoreSyncSettings o)
		{
			var incoming = o?.Clone() ?? new WaterboxCoreSyncSettings();
			bool changed = !SettingsEqual(_syncSettings.Values, incoming.Values);
			_syncSettings = incoming;
			return changed ? PutSettingsDirtyBits.RebootCore : PutSettingsDirtyBits.None;
		}

		private static bool SettingsEqual(Dictionary<string, object> a, Dictionary<string, object> b)
		{
			a ??= new();
			b ??= new();
			if (a.Count != b.Count) return false;
			foreach (var kv in a)
			{
				if (!b.TryGetValue(kv.Key, out var v)) return false;
				if (!Equals(Convert.ToString(kv.Value, CultureInfo.InvariantCulture),
					Convert.ToString(v, CultureInfo.InvariantCulture))) return false;
			}
			return true;
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
