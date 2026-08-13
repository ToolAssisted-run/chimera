#nullable enable

namespace BizHawk.Client.Common
{
	/// <summary>
	/// Which core a system's roms open with. Nothing stops two packages from claiming the same
	/// system - a faster core and a more accurate one, say - so the frontend has to remember which
	/// one to use, and <c>RomLoader</c> reads that on the next load.
	///
	/// The user says so by opening a package (File &gt; Open Core), and nowhere else: there is no
	/// menu of cores to pick from, because a core is not a setting - it is the machine. Startup
	/// discovery deliberately does not write here, so what happens to be sitting in <c>Cores/</c>
	/// cannot silently reassign what someone chose.
	/// </summary>
	public static class CoreChoices
	{
		/// <summary>
		/// Makes <paramref name="coreName"/> the core <paramref name="systemId"/>'s roms open with.
		/// Returns false if that was already the case, so a caller can skip a needless reload.
		/// </summary>
		public static bool MakeDefault(Config config, string systemId, string coreName)
		{
			if (config.DefaultCores.TryGetValue(systemId, out var existing) && existing == coreName) return false;
			config.DefaultCores[systemId] = coreName;
			return true;
		}
	}
}
