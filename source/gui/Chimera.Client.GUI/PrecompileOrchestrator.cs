using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Linq;
using System.Runtime.InteropServices;
using System.Text.RegularExpressions;
using System.Threading;

using Chimera.Client.Common;
using Chimera.Emulation.Common.Waterbox;

namespace Chimera.Client.GUI
{
	/// <summary>
	/// Compiles a game's code for a core that recompiles, before the game is
	/// ever run (docs/compile-cache.md). The objects are a pure function of the
	/// game, the package and the target CPU, so this is not a decision anybody
	/// has to make - only work that is better done once, in parallel, with
	/// something on screen, than a minute at a time at every boot.
	///
	/// The work happens in child processes: this same frontend, headless, each
	/// one a precompile session compiling its share. They report every object
	/// they store, and this collects them into the game's manifest.
	/// </summary>
	public static class PrecompileOrchestrator
	{
		/// <summary>"[cache] stored &lt;name&gt; &lt;sha1&gt;", as the engine's cache bridge prints it.</summary>
		private static readonly Regex CacheLine = new(@"^\[cache\] (stored|fetched) (\S+) ([0-9A-Fa-f]{40})\s*$", RegexOptions.Compiled);

		/// <summary>"Precompiled 3/21 modules", as a session prints it.</summary>
		private static readonly Regex ProgressLine = new(@"^Precompiled (\d+)/(\d+) modules", RegexOptions.Compiled);

		/// <summary>
		/// What a session said as it refused the job. A precompile session is a
		/// headless frontend, so the thing that stopped it is a modal it could not
		/// show: "[headless] text: ..." is that modal's words. Without this the
		/// wizard can only say the compile stopped - and on Windows the parent is a
		/// GUI-subsystem process whose Console.Error goes nowhere, so the reason
		/// was invisible in every log.
		/// </summary>
		private static readonly Regex RefusalLine = new(@"^\[headless\] text: (.+)$", RegexOptions.Compiled);

		/// <summary>Why the last Run returned null, in the words of whatever refused it; null when it succeeded.</summary>
		public static string LastFailure { get; private set; }

		/// <summary>
		/// How many sessions run side by side. A session compiling a big game
		/// peaks at several gigabytes - it holds a machine's address space and a
		/// compiler at once - so this is bounded by memory as well as by cores:
		/// one session per 8 GB the machine can spare, never more than half the
		/// cores, never more than eight. Fewer sessions is slower; too many is a
		/// machine that swaps, which is slower still.
		/// </summary>
		public static int Workers
		{
			get
			{
				var byCores = Math.Max(1, Environment.ProcessorCount / 2);
				var byMemory = Math.Max(1, (int)(AvailableGigabytes() / 8));
				return Math.Min(8, Math.Min(byCores, byMemory));
			}
		}

		/// <summary>What the machine can spare right now, in GB; 8 when it will not say.</summary>
		private static double AvailableGigabytes()
		{
			try
			{
				if (File.Exists("/proc/meminfo"))
				{
					foreach (var line in File.ReadLines("/proc/meminfo"))
					{
						if (!line.StartsWith("MemAvailable:", StringComparison.Ordinal)) continue;
						var kb = double.Parse(new string(line.Where(char.IsDigit).ToArray()));
						return kb / (1024 * 1024);
					}
					return 8;
				}
				MemoryStatusEx status = new() { Length = (uint)Marshal.SizeOf<MemoryStatusEx>() };
				return GlobalMemoryStatusEx(ref status) ? status.AvailPhys / (1024.0 * 1024 * 1024) : 8;
			}
			catch (Exception)
			{
				return 8; // an unknowable machine gets the middle of the road
			}
		}

		[StructLayout(LayoutKind.Sequential)]
		private struct MemoryStatusEx
		{
			public uint Length;
			public uint MemoryLoad;
			public ulong TotalPhys;
			public ulong AvailPhys;
			public ulong TotalPageFile;
			public ulong AvailPageFile;
			public ulong TotalVirtual;
			public ulong AvailVirtual;
			public ulong AvailExtendedVirtual;
		}

		[DllImport("kernel32.dll", SetLastError = true)]
		[return: MarshalAs(UnmanagedType.Bool)]
		private static extern bool GlobalMemoryStatusEx(ref MemoryStatusEx buffer);

		/// <summary>An object as the wizard shows it: named, hashed, and either there or not.</summary>
		public sealed class Entry
		{
			public string Name { get; set; }
			public string Sha1 { get; set; }
			public bool Present { get; set; }
		}

		/// <summary>What a game needs and what of it is there, from the manifest a previous compile left.</summary>
		public static List<Entry> Survey(string cacheDir, string romSha1)
		{
			var manifest = CoreCacheManifest.Load(cacheDir, romSha1);
			if (manifest is null) return [ ];
			return manifest.Files
				.Select(f => new Entry
				{
					Name = f.Name,
					Sha1 = f.Sha1,
					Present = string.Equals(CoreCacheManifest.HashOf(cacheDir, f.Name), f.Sha1, StringComparison.OrdinalIgnoreCase),
				})
				.ToList();
		}

		/// <summary>Everything the game needs is on disk and unchanged.</summary>
		public static bool Satisfied(string cacheDir, string romSha1)
		{
			var entries = Survey(cacheDir, romSha1);
			return entries.Count is not 0 && entries.All(e => e.Present);
		}

		public static long CacheBytes(string cacheDir)
			=> cacheDir is null || !Directory.Exists(cacheDir)
				? 0
				: new DirectoryInfo(cacheDir).EnumerateFiles("*", SearchOption.AllDirectories).Sum(f => f.Length);

		public static void Clear(string cacheDir)
		{
			if (cacheDir is not null && Directory.Exists(cacheDir)) Directory.Delete(cacheDir, recursive: true);
		}

		/// <summary>
		/// Runs the sessions and collects what they compile. onEntry is called as
		/// each object lands, so a list can fill while the work happens; onProgress
		/// gets the modules finished across every session and the game's module
		/// count, which they all report the same. Returns the manifest that was
		/// written, or null when nothing usable came of it.
		/// </summary>
		public static CoreCacheManifest Run(
			string packagePath, string configPath, string romPath, string romSha1, string cacheDir,
			Action<Entry> onEntry, Action<uint, uint> onProgress, Func<bool> cancelled,
			IReadOnlyDictionary<string, string> firmware = null)
		{
			LastFailure = null;
			if (string.IsNullOrEmpty(romPath) || !File.Exists(romPath) || cacheDir is null)
			{
				LastFailure = "there is no game file to compile";
				return null;
			}

			var n = Workers;
			var done = new uint[n];
			var total = new uint[n];
			var found = new Dictionary<string, string>(StringComparer.Ordinal);
			var processes = new List<Process>();
			string failure = null;
			LastFailure = null;

			void Line(int index, string line)
			{
				var refusal = RefusalLine.Match(line);
				if (refusal.Success)
				{
					// first one wins: the others are the same modal in the other sessions
					lock (found) { failure ??= refusal.Groups[1].Value; }
					return;
				}
				var m = CacheLine.Match(line);
				if (m.Success)
				{
					lock (found)
					{
						// stored and fetched both count: a worker that found an
						// object another one had just written still needs it
						found[m.Groups[2].Value] = m.Groups[3].Value.ToUpperInvariant();
					}
					onEntry?.Invoke(new Entry { Name = m.Groups[2].Value, Sha1 = m.Groups[3].Value.ToUpperInvariant(), Present = true });
					return;
				}
				m = ProgressLine.Match(line);
				if (!m.Success) return;
				// Every session reports the game's module count, and finishes a
				// share of it: one total, and the dones add up.
				done[index] = uint.Parse(m.Groups[1].Value);
				total[index] = uint.Parse(m.Groups[2].Value);
				onProgress?.Invoke((uint)done.Sum(v => (long)v), total.Max());
			}

			for (var i = 0; i < n; i++)
			{
				var args = new List<string> { "--headless" };
				if (!string.IsNullOrEmpty(configPath)) args.Add($"--config={configPath}");
				args.Add($"--core={packagePath}");
				args.Add($"--precompile={i}/{n}");
				// Say the firmware outright. Going through the config means the
				// parent must have written it there first, and the wizard can hold
				// a path that never gets written - a core may call a firmware
				// optional that this GAME cannot boot without. A session refused at
				// boot compiles nothing.
				foreach (var (id, path) in firmware ?? new Dictionary<string, string>())
				{
					args.Add($"--firmware={id}={path}");
				}
				args.Add(romPath);
				var index = i;
				var p = SelfProcess.Start(args, line => Line(index, line));
				if (p is not null) processes.Add(p);
			}
			if (processes.Count is 0) return null;

			var stopped = false;
			while (processes.Any(p => !p.HasExited))
			{
				Thread.Sleep(150);
				if (stopped || cancelled?.Invoke() != true) continue;
				stopped = true;
				foreach (var p in processes.Where(p => !p.HasExited))
				{
					try { p.Kill(); } catch (Exception) { /* it finished on its own */ }
				}
			}
			foreach (var p in processes) p.WaitForExit();
			if (stopped) return null;

			// A session that died took its share with it, and a manifest written
			// from what the others managed would be a list of what this game
			// needs with holes in it - which is exactly what must not happen.
			var died = processes.Count(p => p.ExitCode != 0);
			if (died is not 0)
			{
				Console.Error.WriteLine($"precompile: {died} of {processes.Count} sessions failed");
				LastFailure = failure ?? $"{died} of {processes.Count} sessions failed without saying why";
				return null;
			}

			// What the sessions compiled is what this game needs. Written beside
			// the objects; the project records the same list.
			var manifest = new CoreCacheManifest
			{
				RomName = Path.GetFileName(romPath),
				RomSha1 = romSha1,
				Files = found.OrderBy(kv => kv.Key, StringComparer.Ordinal)
					.Select(kv => new CoreCacheFile { Name = kv.Key, Sha1 = kv.Value })
					.ToList(),
			};
			if (manifest.Files.Count is 0)
			{
				LastFailure = failure ?? "the sessions compiled nothing for this game";
				return null;
			}
			manifest.Save(cacheDir, romSha1);
			return manifest;
		}
	}

	/// <summary>Launches this same frontend as a child process (headless), streaming its output lines.</summary>
	public static class SelfProcess
	{
		/// <summary>
		/// One argument, quoted the way Windows will parse it back.
		/// 
		/// A backslash is ONLY special before a quote: CommandLineToArgvW reads
		/// 2n of them plus a quote as n backslashes and a delimiter, 2n+1 as n
		/// backslashes and a literal quote, and leaves any other run alone. So
		/// escaping every backslash - which is what this did - doubles every
		/// separator in a path, and "C:\games\x.iso" reaches the child as
		/// "C:\\games\\x.iso", which does not exist. It only bit paths that
		/// needed quoting in the first place, i.e. ones containing a space,
		/// which is why it survived: the precompile sessions all failed at once
		/// on a game whose folder had one.
		/// </summary>
		private static string Quote(string a)
		{
			if (a.Length > 0 && a.IndexOfAny([ ' ', '"', '\t' ]) < 0) return a;
			var sb = new System.Text.StringBuilder(a.Length + 8);
			sb.Append('"');
			for (var i = 0; i < a.Length; i++)
			{
				var slashes = 0;
				while (i < a.Length && a[i] == '\\') { slashes++; i++; }
				if (i == a.Length)
				{
					// run at the end: doubled, so the closing quote stays a delimiter
					sb.Append('\\', slashes * 2);
					break;
				}
				if (a[i] == '"')
				{
					sb.Append('\\', slashes * 2 + 1).Append('"');
				}
				else
				{
					sb.Append('\\', slashes).Append(a[i]);
				}
			}
			sb.Append('"');
			return sb.ToString();
		}

		public static Process Start(IEnumerable<string> args, Action<string> onLine)
		{
			var psi = new ProcessStartInfo
			{
				UseShellExecute = false,
				RedirectStandardOutput = true,
				RedirectStandardError = true,
				CreateNoWindow = true,
			};
			var exe = Process.GetCurrentProcess().MainModule?.FileName ?? "";
			var entry = System.Reflection.Assembly.GetEntryAssembly()?.Location ?? "";
			var underMono = Type.GetType("Mono.Runtime") is not null;
			var all = new List<string>();
			psi.FileName = exe; // under mono: the runtime itself, then the assembly
			if (underMono) all.Add(entry);
			all.AddRange(args);
			psi.Arguments = string.Join(" ", all.Select(Quote));
			try
			{
				var p = Process.Start(psi);
				if (p is null) return null;
				p.OutputDataReceived += (_, e) => { if (e.Data is not null) onLine(e.Data); };
				p.ErrorDataReceived += (_, e) => { if (e.Data is not null) onLine(e.Data); };
				p.BeginOutputReadLine();
				p.BeginErrorReadLine();
				return p;
			}
			catch (Exception ex)
			{
				Console.Error.WriteLine($"precompile: could not start a worker: {ex.Message}");
				return null;
			}
		}
	}
}
