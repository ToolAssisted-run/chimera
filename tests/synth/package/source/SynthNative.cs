using System.IO;

using BizHawk.BizInvoke;
using BizHawk.Common;
using BizHawk.Emulation.Common;

namespace MiniHawk.Cores.Synth
{
	/// <summary>
	/// The native-flavor Synth core: adapter over libsynthcore, the reference
	/// implementation of tests/synth/SPEC.md. One of three planned flavor twins
	/// (native / pure C# / waterboxed) that must be indistinguishable through
	/// this interface.
	/// </summary>
	[PortedCore(
		name: "SynthNative",
		author: "Sergio Martin",
		portedVersion: "1.0.0",
		portedUrl: "https://github.com/SergioMartin86/miniHawk")]
	public sealed class SynthNative : IEmulator, IVideoProvider, ISoundProvider, IStatable, IInputPollable
	{
		private static readonly LibSynthCore Core;

		static SynthNative()
		{
			var resolver = new DynamicLibraryImportResolver(
				$"libsynthcore{(OSTailoredCode.IsUnixHost ? ".so" : ".dll")}", hasLimitedLifetime: false);
			Core = BizInvoker.GetInvoker<LibSynthCore>(resolver, CallingConventionAdapters.Native);
		}

		private IntPtr _ctx;
		private readonly byte[] _stateBuff;
		private readonly int[] _videoBuff = new int[128 * 120];
		private readonly short[] _stereoBuff = new short[735 * 2];

		public SynthNative(byte[] file)
		{
			ServiceProvider = new BasicServiceProvider(this);
			_ctx = Core.synth_create(file, (uint)file.Length);
			if (_ctx == IntPtr.Zero)
			{
				throw new InvalidOperationException("not a valid .testrom (see tests/synth/SPEC.md)");
			}

			Core.synth_reset(_ctx);
			_stateBuff = new byte[Core.synth_state_size()];

			var domains = new System.Collections.Generic.List<MemoryDomain>
			{
				new MemoryDomainIntPtr("RAM", MemoryDomain.Endian.Little, Core.synth_get_ram(_ctx), 4096, true, 1),
				new MemoryDomainIntPtr("VRAM", MemoryDomain.Endian.Little, Core.synth_get_framebuffer(_ctx), 128 * 120, false, 1),
			};
			((BasicServiceProvider)ServiceProvider).Register<IMemoryDomains>(new MemoryDomainList(domains));
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

			Core.synth_frame(_ctx, pad);
			Frame++;
			IsLagFrame = Core.synth_input_was_read(_ctx) == 0;
			if (IsLagFrame) LagCount++;

			if (render) Core.synth_get_video_bgra(_ctx, _videoBuff);
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
			if (_ctx != IntPtr.Zero)
			{
				Core.synth_destroy(_ctx);
				_ctx = IntPtr.Zero;
			}
		}

		private void CheckDisposed()
		{
			if (_ctx == IntPtr.Zero) throw new ObjectDisposedException(nameof(SynthNative));
		}

		// ---------------- IVideoProvider ----------------

		public int BufferWidth => 128;
		public int BufferHeight => 120;
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
			nsamp = 735;
		}

		public void DiscardSamples() { }

		public void SetSyncMode(SyncSoundMode mode)
		{
			if (mode == SyncSoundMode.Async) throw new NotSupportedException("Async mode is not supported.");
		}

		public void GetSamplesAsync(short[] samples) => throw new InvalidOperationException("Async mode is not supported.");

		private unsafe void DrainAudio()
		{
			var mono = (short*)Core.synth_get_audio(_ctx);
			fixed (short* dst0 = _stereoBuff)
			{
				short* dst = dst0;
				for (int i = 0; i < 735; i++)
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
			Core.synth_serialize(_ctx, _stateBuff);
			writer.Write(_stateBuff.Length);
			writer.Write(_stateBuff);
			writer.Write(IsLagFrame);
			writer.Write(LagCount);
			writer.Write(Frame);
		}

		public void LoadStateBinary(BinaryReader reader)
		{
			CheckDisposed();
			int len = reader.ReadInt32();
			if (len != _stateBuff.Length) throw new InvalidOperationException("Unexpected savestate buffer length!");
			reader.Read(_stateBuff, 0, len);
			Core.synth_deserialize(_ctx, _stateBuff);
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
