using System.Collections.Generic;
using System.IO;
using System.Runtime.InteropServices;

using BizHawk.BizInvoke;
using BizHawk.Common;

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
	[PortedCore(
		name: "Waterbox",
		author: "miniBox",
		portedVersion: "1.0.0",
		portedUrl: "https://github.com/SergioMartin86/miniBox")]
	public sealed class WaterboxCore : IEmulator, IVideoProvider, ISoundProvider, IStatable, IInputPollable
	{
		[UnmanagedFunctionPointer(CallingConvention.Cdecl)] private delegate int InitFn();
		[UnmanagedFunctionPointer(CallingConvention.Cdecl)] private delegate void FrameFn(uint input);
		[UnmanagedFunctionPointer(CallingConvention.Cdecl)] private delegate IntPtr GetPtrFn();
		[UnmanagedFunctionPointer(CallingConvention.Cdecl)] private delegate int IntFn();
		[UnmanagedFunctionPointer(CallingConvention.Cdecl)] private delegate IntPtr MdNameFn(int i);
		[UnmanagedFunctionPointer(CallingConvention.Cdecl)] private delegate IntPtr MdPtrFn(int i);
		[UnmanagedFunctionPointer(CallingConvention.Cdecl)] private delegate long MdSizeFn(int i);
		[UnmanagedFunctionPointer(CallingConvention.Cdecl)] private delegate int MdIntFn(int i);

		private readonly WaterboxConfig _cfg;
		private readonly LibMiniBoxHost _host;
		private IntPtr _obj;
		private bool _active;

		private readonly FrameFn _frameAdvance;
		private readonly GetPtrFn _getAudio;
		private readonly GetPtrFn _getVideoBgra;
		private readonly IntFn _inputWasRead; // may be null

		private readonly LibMiniBoxHost.ReadCallback _imageRead;
		private readonly LibMiniBoxHost.ReadCallback _romRead;
		private readonly LibMiniBoxHost.WriteCallback _stateWrite;
		private readonly LibMiniBoxHost.ReadCallback _stateRead;

		private readonly byte[] _wbxBytes;
		private int _wbxPos;
		private readonly byte[] _romBytes;
		private int _romPos;
		private MemoryStream _saveBuf;
		private byte[] _loadBuf;
		private int _loadPos;

		private readonly int _width, _height, _samplesPerFrame, _channels;
		private readonly int[] _videoBuff;
		private readonly short[] _stereoBuff;
		private readonly string[] _buttons;

		public WaterboxCore(byte[] rom, WaterboxConfig cfg, string wbxPath)
		{
			_cfg = cfg;
			_romBytes = rom;
			_wbxBytes = File.ReadAllBytes(wbxPath);
			_width = cfg.Video.Width;
			_height = cfg.Video.Height;
			_samplesPerFrame = cfg.Audio.SamplesPerFrame;
			_channels = cfg.Audio.Channels;
			_videoBuff = new int[_width * _height];
			_stereoBuff = new short[_samplesPerFrame * 2];
			_buttons = cfg.Input.Buttons.ToArray();

			ServiceProvider = new BasicServiceProvider(this);

			_imageRead = ImageRead;
			_romRead = RomRead;
			_stateWrite = StateWrite;
			_stateRead = StateRead;

			var resolver = new DynamicLibraryImportResolver(
				$"libminiboxhost{(OSTailoredCode.IsUnixHost ? ".so" : ".dll")}", hasLimitedLifetime: false);
			_host = BizInvoker.GetInvoker<LibMiniBoxHost>(resolver, CallingConventionAdapters.Native);

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
			((BasicServiceProvider)ServiceProvider).Register<IMemoryDomains>(new MemoryDomainList(domains));
		}

		private T Proc<T>(string name) where T : Delegate
		{
			LibMiniBoxHost.ReturnData r = default;
			_host.wbx_get_proc_addr(_obj, name, ref r);
			return Marshal.GetDelegateForFunctionPointer<T>(r.DataOrThrow());
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
		private IntPtr StateRead(UIntPtr ud, IntPtr data, UIntPtr size) => ReadInto(_loadBuf, ref _loadPos, data, size);

		private int StateWrite(UIntPtr ud, IntPtr data, UIntPtr size)
		{
			int n = (int)size;
			var tmp = new byte[n];
			Marshal.Copy(data, tmp, 0, n);
			_saveBuf.Write(tmp, 0, n);
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
			return def.MakeImmutable();
		}

		public bool FrameAdvance(IController controller, bool render, bool renderSound = true)
		{
			CheckDisposed();
			uint input = 0;
			for (int i = 0; i < _buttons.Length; i++)
			{
				if (controller.IsPressed(_buttons[i])) input |= 1u << i;
			}

			_frameAdvance(input);
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
			_saveBuf = new MemoryStream();
			LibMiniBoxHost.ReturnData r = default;
			Deactivate();
			_host.wbx_save_state(_obj, _stateWrite, UIntPtr.Zero, ref r);
			Activate();
			r.ThrowIfError();

			var bytes = _saveBuf.ToArray();
			_saveBuf = null;
			writer.Write(bytes.Length);
			writer.Write(bytes);
			writer.Write(IsLagFrame);
			writer.Write(LagCount);
			writer.Write(Frame);
		}

		public void LoadStateBinary(BinaryReader reader)
		{
			CheckDisposed();
			int len = reader.ReadInt32();
			_loadBuf = reader.ReadBytes(len);
			_loadPos = 0;

			LibMiniBoxHost.ReturnData r = default;
			Deactivate();
			_host.wbx_load_state(_obj, _stateRead, UIntPtr.Zero, ref r);
			Activate();
			r.ThrowIfError();
			_loadBuf = null;

			IsLagFrame = reader.ReadBoolean();
			LagCount = reader.ReadInt32();
			Frame = reader.ReadInt32();
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
