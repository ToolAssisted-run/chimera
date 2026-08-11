using System.IO;

using BizHawk.Emulation.Common;

namespace MiniHawk.Cores.SynthSharp
{
	/// <summary>
	/// The pure-C# flavor Synth core (flavor b): the whole machine lives in
	/// <see cref="SynthMachine"/>, managed code only - the package declares no
	/// natives and loads no libraries at runtime. Must be indistinguishable
	/// from the native flavor through this interface.
	/// </summary>
	[PortedCore(
		name: "SynthSharp",
		author: "Sergio Martin",
		portedVersion: "1.0.0",
		portedUrl: "https://github.com/SergioMartin86/miniHawk")]
	public sealed class SynthSharp : IEmulator, IVideoProvider, ISoundProvider, IStatable, IInputPollable
	{
		private readonly SynthMachine _machine;
		private readonly byte[] _stateBuff = new byte[SynthMachine.StateSize];
		private readonly int[] _videoBuff = new int[SynthMachine.FbSize];
		private readonly short[] _stereoBuff = new short[SynthMachine.SamplesPerFrame * 2];

		public SynthSharp(byte[] file)
		{
			ServiceProvider = new BasicServiceProvider(this);
			_machine = new SynthMachine(file); // throws on a bad rom
			_machine.Reset();

			var domains = new System.Collections.Generic.List<MemoryDomain>
			{
				new MemoryDomainByteArray("RAM", MemoryDomain.Endian.Little, _machine.Ram, true, 1),
				new MemoryDomainByteArray("VRAM", MemoryDomain.Endian.Little, _machine.Framebuffer, false, 1),
			};
			((BasicServiceProvider)ServiceProvider).Register<IMemoryDomains>(new MemoryDomainList(domains));
		}

		public IEmulatorServiceProvider ServiceProvider { get; }

		// The controller surface is part of the cross-flavor contract: names and
		// order must match the other flavors exactly, or movies would not be
		// portable between them.
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
			byte pad = 0;
			if (controller.IsPressed("P1 Up")) pad |= 0x01;
			if (controller.IsPressed("P1 Down")) pad |= 0x02;
			if (controller.IsPressed("P1 Left")) pad |= 0x04;
			if (controller.IsPressed("P1 Right")) pad |= 0x08;
			if (controller.IsPressed("P1 A")) pad |= 0x10;
			if (controller.IsPressed("P1 B")) pad |= 0x20;
			if (controller.IsPressed("P1 Select")) pad |= 0x40;
			if (controller.IsPressed("P1 Start")) pad |= 0x80;

			_machine.FrameAdvance(pad);
			Frame++;
			IsLagFrame = !_machine.InputWasRead;
			if (IsLagFrame) LagCount++;

			if (render) _machine.GetVideoBgra(_videoBuff);
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

		public void Dispose() { }

		// ---------------- IVideoProvider ----------------

		public int BufferWidth => SynthMachine.FbWidth;
		public int BufferHeight => SynthMachine.FbHeight;
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
			nsamp = SynthMachine.SamplesPerFrame;
		}

		public void DiscardSamples() { }

		public void SetSyncMode(SyncSoundMode mode)
		{
			if (mode == SyncSoundMode.Async) throw new NotSupportedException("Async mode is not supported.");
		}

		public void GetSamplesAsync(short[] samples) => throw new InvalidOperationException("Async mode is not supported.");

		private void DrainAudio()
		{
			var mono = _machine.AudioOut;
			for (int i = 0; i < SynthMachine.SamplesPerFrame; i++)
			{
				_stereoBuff[i * 2] = mono[i];
				_stereoBuff[i * 2 + 1] = mono[i];
			}
		}

		// ---------------- IStatable ----------------

		public bool AvoidRewind => false;

		public void SaveStateBinary(BinaryWriter writer)
		{
			_machine.Serialize(_stateBuff);
			writer.Write(_stateBuff.Length);
			writer.Write(_stateBuff);
			writer.Write(IsLagFrame);
			writer.Write(LagCount);
			writer.Write(Frame);
		}

		public void LoadStateBinary(BinaryReader reader)
		{
			int len = reader.ReadInt32();
			if (len != _stateBuff.Length) throw new InvalidOperationException("Unexpected savestate buffer length!");
			reader.Read(_stateBuff, 0, len);
			_machine.Deserialize(_stateBuff);
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
