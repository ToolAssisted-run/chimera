using System;
using System.IO;
using System.Security.Cryptography;

using MiniHawk.Cores.SynthSharp;

// synth-run-sharp: Level A tester for the pure-C# flavor. Same CLI and output
// as native/synth-run.c so both flavors verify against the same goldens:
//   synth-run-sharp <rom.testrom> <movie.txt> [--rerecord]
// prints frames= ramSha1= videoSha1= audioSha1= status= (uppercase hex).
internal static class Program
{
	private static int Main(string[] args)
	{
		string romPath = null, moviePath = null;
		var rerecord = false;
		foreach (var arg in args)
		{
			if (arg == "--rerecord") rerecord = true;
			else if (romPath == null) romPath = arg;
			else if (moviePath == null) moviePath = arg;
			else { Console.Error.WriteLine($"unexpected arg {arg}"); return 2; }
		}
		if (romPath == null || moviePath == null)
		{
			Console.Error.WriteLine("usage: synth-run-sharp <rom.testrom> <movie.txt> [--rerecord]");
			return 2;
		}

		SynthMachine machine;
		try
		{
			machine = new SynthMachine(File.ReadAllBytes(romPath));
		}
		catch (InvalidOperationException e)
		{
			Console.Error.WriteLine($"rom rejected: {e.Message}");
			return 1;
		}
		machine.Reset();

		var state = new byte[SynthMachine.StateSize];
		var audioBytes = new byte[SynthMachine.SamplesPerFrame * 2];
		using var videoSha = SHA1.Create();
		using var audioSha = SHA1.Create();
		var frames = 0u;

		foreach (var rawLine in File.ReadAllLines(moviePath))
		{
			var line = rawLine.TrimEnd('\r');
			if (line.Length == 0 || line[0] != '|') continue;
			byte pad = 0;
			const string keys = "UDLRABST"; // bitmask order
			for (var i = 0; i < 8 && 1 + i < line.Length && line[1 + i] != '|'; i++)
			{
				if (line[1 + i] != '.' && line[1 + i] == keys[i]) pad |= (byte)(1u << i);
			}
			if (rerecord)
			{
				machine.Serialize(state);
				machine.Deserialize(state);
			}
			machine.FrameAdvance(pad);
			frames++;
			videoSha.TransformBlock(machine.Framebuffer, 0, SynthMachine.FbSize, null, 0);
			Buffer.BlockCopy(machine.AudioOut, 0, audioBytes, 0, audioBytes.Length);
			audioSha.TransformBlock(audioBytes, 0, audioBytes.Length, null, 0);
		}

		videoSha.TransformFinalBlock([], 0, 0);
		audioSha.TransformFinalBlock([], 0, 0);
		string Hex(byte[] h) => BitConverter.ToString(h).Replace("-", "");
		string ramHex;
		using (var ramSha = SHA1.Create()) ramHex = Hex(ramSha.ComputeHash(machine.Ram));

		Console.WriteLine($"frames={frames}");
		Console.WriteLine($"ramSha1={ramHex}");
		Console.WriteLine($"videoSha1={Hex(videoSha.Hash)}");
		Console.WriteLine($"audioSha1={Hex(audioSha.Hash)}");
		Console.WriteLine($"status={machine.Ram[0]}");
		return 0;
	}
}
