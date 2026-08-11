using System.Collections.Generic;
using System.IO;
using System.Reflection;
using System.Runtime.InteropServices;

using BizHawk.BizInvoke;
using BizHawk.Common;
using BizHawk.Emulation.Common;

namespace MiniHawk.Cores.SynthBox
{
	/// <summary>
	/// The waterboxed-flavor Synth core (flavor c): the SAME reference machine
	/// (native/synthcore.c) compiled into synth.wbx and driven through the miniBox
	/// waterbox host. The whole machine lives in guest memory, so savestates are
	/// whole-machine (wbx_save_state/wbx_load_state) - there is no explicit
	/// serialize. Must be indistinguishable from the native and C# flavors through
	/// this interface.
	///
	/// The host is kept ACTIVE for the core's lifetime (guest memory mapped and
	/// callable), briefly deactivated only to bracket save/load state. Guest export
	/// addresses and the RAM/VRAM pointers are fixed (non-PIE guest), so they stay
	/// valid across the whole run.
	/// </summary>
	[PortedCore(
		name: "SynthBox",
		author: "Sergio Martin",
		portedVersion: "1.0.0",
		portedUrl: "https://github.com/SergioMartin86/miniHawk")]
	public sealed class SynthBox : IEmulator, IVideoProvider, ISoundProvider, IStatable, IInputPollable
	{
		private const int FbWidth = 128;
		private const int FbHeight = 120;
		private const int FbSize = FbWidth * FbHeight;
		private const int RamSize = 4096;
		private const int SamplesPerFrame = 735;

		[UnmanagedFunctionPointer(CallingConvention.Cdecl)] private delegate int InitFn();
		[UnmanagedFunctionPointer(CallingConvention.Cdecl)] private delegate void FrameFn(uint pad);
		[UnmanagedFunctionPointer(CallingConvention.Cdecl)] private delegate IntPtr GetPtrFn();
		[UnmanagedFunctionPointer(CallingConvention.Cdecl)] private delegate int IntFn();

		private readonly LibMiniBoxHost _host;
		private IntPtr _obj;
		private bool _active;

		// Guest exports.
		private readonly FrameFn _frameAdvance;
		private readonly GetPtrFn _getAudio;
		private readonly GetPtrFn _getVideoBgra;
		private readonly IntFn _inputWasRead;

		// Host callbacks (kept alive as fields so they are not collected mid-call).
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

		private readonly int[] _videoBuff = new int[FbSize];
		private readonly short[] _stereoBuff = new short[SamplesPerFrame * 2];

		public SynthBox(byte[] rom)
		{
			ServiceProvider = new BasicServiceProvider(this);
			_romBytes = rom;

			var dir = Path.GetDirectoryName(typeof(SynthBox).Assembly.Location);
			_wbxBytes = File.ReadAllBytes(Path.Combine(dir, "synth.wbx"));

			_imageRead = ImageRead;
			_romRead = RomRead;
			_stateWrite = StateWrite;
			_stateRead = StateRead;

			var resolver = new DynamicLibraryImportResolver(
				$"libminiboxhost{(OSTailoredCode.IsUnixHost ? ".so" : ".dll")}", hasLimitedLifetime: false);
			_host = BizInvoker.GetInvoker<LibMiniBoxHost>(resolver, CallingConventionAdapters.Native);

			var layout = new LibMiniBoxHost.MemoryLayoutTemplate
			{
				SbrkSize = (UIntPtr)(16u << 20),
				SealedSize = (UIntPtr)(16u << 20),
				InvisSize = (UIntPtr)(16u << 20),
				PlainSize = (UIntPtr)(16u << 20),
				MmapSize = (UIntPtr)(32u << 20),
			};

			LibMiniBoxHost.ReturnData r = default;
			_host.wbx_create_host(ref layout, "synth.wbx", _imageRead, UIntPtr.Zero, ref r);
			_obj = r.DataOrThrow();

			// The rom is delivered as a mounted, read-only file named "rom".
			_host.wbx_mount_file(_obj, "rom", _romRead, UIntPtr.Zero, 0, ref r);
			r.ThrowIfError();

			Activate();
			var init = Proc<InitFn>("Init");
			if (init() != 1)
			{
				throw new InvalidOperationException("synth.wbx Init failed (not a valid .testrom; see tests/synth/SPEC.md)");
			}
			Deactivate();
			_host.wbx_seal(_obj, ref r); // freeze the post-init image as the savestate baseline
			r.ThrowIfError();
			Activate();

			_frameAdvance = Proc<FrameFn>("FrameAdvance");
			_getAudio = Proc<GetPtrFn>("GetAudio");
			_getVideoBgra = Proc<GetPtrFn>("GetVideoBgra");
			_inputWasRead = Proc<IntFn>("InputWasRead");
			var getRam = Proc<GetPtrFn>("GetRam");
			var getFramebuffer = Proc<GetPtrFn>("GetFramebuffer");

			// RAM/VRAM live in guest memory at fixed addresses; valid while active
			// (which is the core's whole lifetime except during save/load).
			var domains = new List<MemoryDomain>
			{
				new MemoryDomainIntPtr("RAM", MemoryDomain.Endian.Little, getRam(), RamSize, true, 1),
				new MemoryDomainIntPtr("VRAM", MemoryDomain.Endian.Little, getFramebuffer(), FbSize, false, 1),
			};
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

		// ---- host callbacks ----

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

		public static readonly ControllerDefinition SynthController = MakeControllerDefinition();

		private static ControllerDefinition MakeControllerDefinition()
		{
			ControllerDefinition def = new("Synth Controller");
			foreach (var button in new[] { "P1 Up", "P1 Down", "P1 Left", "P1 Right", "P1 A", "P1 B", "P1 Select", "P1 Start" })
			{
				def.BoolButtons.Add(button);
			}
			return def.MakeImmutable();
		}

		public ControllerDefinition ControllerDefinition => SynthController;

		public bool FrameAdvance(IController controller, bool render, bool renderSound = true)
		{
			CheckDisposed();
			byte pad = 0;
			if (controller.IsPressed("P1 Up")) pad |= 0x01;
			if (controller.IsPressed("P1 Down")) pad |= 0x02;
			if (controller.IsPressed("P1 Left")) pad |= 0x04;
			if (controller.IsPressed("P1 Right")) pad |= 0x08;
			if (controller.IsPressed("P1 A")) pad |= 0x10;
			if (controller.IsPressed("P1 B")) pad |= 0x20;
			if (controller.IsPressed("P1 Select")) pad |= 0x40;
			if (controller.IsPressed("P1 Start")) pad |= 0x80;

			_frameAdvance(pad);
			Frame++;
			IsLagFrame = _inputWasRead() == 0;
			if (IsLagFrame) LagCount++;

			if (render) Marshal.Copy(_getVideoBgra(), _videoBuff, 0, FbSize);
			DrainAudio();
			return true;
		}

		public int Frame { get; private set; }

		public string SystemId => "Synth";

		public bool DeterministicEmulation => true;

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
			if (_obj == IntPtr.Zero) throw new ObjectDisposedException(nameof(SynthBox));
		}

		// ---------------- IVideoProvider ----------------

		public int BufferWidth => FbWidth;
		public int BufferHeight => FbHeight;
		public int VirtualWidth => 256;
		public int VirtualHeight => 240;
		public int BackgroundColor => unchecked((int)0xFF000000);
		public int VsyncNumerator => 60;
		public int VsyncDenominator => 1;
		public int[] GetVideoBuffer() => _videoBuff;

		// ---------------- ISoundProvider ----------------

		public bool CanProvideAsync => false;
		public SyncSoundMode SyncMode => SyncSoundMode.Sync;

		public void GetSamplesSync(out short[] samples, out int nsamp)
		{
			samples = _stereoBuff;
			nsamp = SamplesPerFrame;
		}

		public void DiscardSamples() { }

		public void SetSyncMode(SyncSoundMode mode)
		{
			if (mode == SyncSoundMode.Async) throw new NotSupportedException("Async mode is not supported.");
		}

		public void GetSamplesAsync(short[] samples) => throw new InvalidOperationException("Async mode is not supported.");

		private unsafe void DrainAudio()
		{
			var mono = (short*)_getAudio();
			fixed (short* dst0 = _stereoBuff)
			{
				short* dst = dst0;
				for (int i = 0; i < SamplesPerFrame; i++)
				{
					*dst++ = mono[i];
					*dst++ = mono[i];
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
