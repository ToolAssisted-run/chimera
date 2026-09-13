#nullable enable

using System;
using System.Collections.Generic;
using System.Linq;

namespace Chimera.Client.Common
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

		/// <summary>
		/// Makes the package <paramref name="sha1"/> the build of <paramref name="coreName"/> that bare
		/// roms and new projects use when several builds of that core are installed. Returns false if it
		/// already was.
		/// </summary>
		public static bool MakeDefaultBuild(Config config, string coreName, string sha1)
		{
			if (config.DefaultCoreBuilds.TryGetValue(coreName, out var existing)
				&& string.Equals(existing, sha1, StringComparison.OrdinalIgnoreCase))
			{
				return false;
			}
			config.DefaultCoreBuilds[coreName] = sha1;
			return true;
		}

		/// <summary>
		/// Which of several builds of one core to run (issue #63). The build a project pins, whenever it
		/// is among them - a project is its machine, and another build is another machine. Otherwise the
		/// build the user chose (File &gt; Open Core, or installing it in the core manager). Otherwise the
		/// most recently installed. Null only when there is no build at all.
		/// </summary>
		public static T? PickBuild<T>(IEnumerable<T> builds, Func<T, string?> sha1Of, Func<T, DateTime> installedAt,
			string? pinnedSha1, string? chosenSha1)
			where T : class
		{
			var list = builds.ToList();
			if (list.Count is 0) return null;
			bool Is(T build, string? sha1)
				=> !string.IsNullOrEmpty(sha1) && string.Equals(sha1Of(build), sha1, StringComparison.OrdinalIgnoreCase);
			return list.FirstOrDefault(b => Is(b, pinnedSha1))
				?? list.FirstOrDefault(b => Is(b, chosenSha1))
				?? list.OrderByDescending(installedAt).First();
		}
	}
}
