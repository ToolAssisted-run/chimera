using System.IO;
using System.Runtime.InteropServices;

using Chimera.Common;

namespace Chimera.Client.Common
{
	/// <summary>
	/// How much memory a state history may reasonably take on THIS machine, for
	/// THIS system's states. The state managers hold caches, not requirements -
	/// a smaller budget only makes the history sparser - so the budget can be
	/// generous where the machine is, and must only never be a promise the
	/// machine cannot keep. A PlayStation 2 state is three thousand NES states;
	/// a fixed number in a dialog cannot serve both, and the user cannot be
	/// asked to know their own free RAM.
	/// </summary>
	public static class StateBudget
	{
		/// <summary>a state this big is a "big system" state: compress it, and keep the colder tiers on disk</summary>
		public const int BigStateBytes = 4 * 1024 * 1024;

		private const long MB = 1024 * 1024;

		/// <summary>
		/// The memory budget for state history: a quarter of what is AVAILABLE
		/// right now (not installed - the browser next door is real), clamped
		/// between the old fixed default and a ceiling. Available is measured
		/// when asked, so the budget respects the machine as it is today.
		/// </summary>
		public static int BudgetMB(int floorMB = 1024, int ceilingMB = 8192)
		{
			var quarter = AvailableRamBytes() / 4 / MB;
			if (quarter < floorMB) return floorMB;
			if (quarter > ceilingMB) return ceilingMB;
			return (int)quarter;
		}

		/// <summary>
		/// A pool size scaled to what the states actually cost: enough for
		/// <paramref name="targetStates"/> states of this size, but never more
		/// than the machine's budget and never less than <paramref name="floorMB"/>.
		/// Small systems stop reserving space they cannot use; big systems get
		/// what the machine can spare.
		/// </summary>
		public static int PoolMB(long stateSizeBytes, int targetStates, int floorMB, int ceilingMB)
		{
			var byNeed = stateSizeBytes * targetStates / MB;
			long size = Math.Min(byNeed, BudgetMB(floorMB, ceilingMB));
			if (size < floorMB) return floorMB;
			return (int)size;
		}

		/// <summary>
		/// Physical memory not currently in use, in bytes. Never throws: a
		/// machine that will not say gets a conservative 4GB assumed.
		/// </summary>
		public static long AvailableRamBytes()
		{
			try
			{
				if (OSTailoredCode.IsUnixHost)
				{
					foreach (var line in File.ReadLines("/proc/meminfo"))
					{
						// "MemAvailable:   12345678 kB" - the kernel's own answer to
						// "how much can be allocated without swapping", cache included
						if (!line.StartsWith("MemAvailable:", StringComparison.Ordinal)) continue;
						var parts = line.Split((char[])null, StringSplitOptions.RemoveEmptyEntries);
						if (parts.Length >= 2 && long.TryParse(parts[1], out var kb)) return kb * 1024;
						break;
					}
				}
				else
				{
					MEMORYSTATUSEX status = new();
					status.dwLength = (uint)Marshal.SizeOf<MEMORYSTATUSEX>();
					if (GlobalMemoryStatusEx(ref status)) return (long)status.ullAvailPhys;
				}
			}
			catch
			{
				// fall through to the assumption below
			}
			return 4L * 1024 * MB;
		}

		[StructLayout(LayoutKind.Sequential)]
		private struct MEMORYSTATUSEX
		{
			public uint dwLength;
			public uint dwMemoryLoad;
			public ulong ullTotalPhys;
			public ulong ullAvailPhys;
			public ulong ullTotalPageFile;
			public ulong ullAvailPageFile;
			public ulong ullTotalVirtual;
			public ulong ullAvailVirtual;
			public ulong ullAvailExtendedVirtual;
		}

		[DllImport("kernel32.dll", SetLastError = true)]
		private static extern bool GlobalMemoryStatusEx(ref MEMORYSTATUSEX lpBuffer);
	}
}
