#nullable enable

using System;
using System.Collections.Generic;
using System.Linq;

namespace BizHawk.Client.Common
{
	/// <summary>
	/// What became of a discovered package. There are only two outcomes, because
	/// there is no way to ask for a third: everything readable in a search
	/// directory is loaded, and not loading one means taking it out of the folder.
	/// </summary>
	public enum CorePackageState
	{
		/// <summary>Loaded and available to open roms with.</summary>
		Loaded,

		/// <summary>Unreadable — listed so a broken package is visible instead of just absent.</summary>
		Failed,
	}

	/// <summary>One row of the core-package list: a package plus what the frontend did with it.</summary>
	public sealed class CorePackageListEntry
	{
		public DiscoveredCorePackage Package { get; init; } = new();

		public CorePackageState State { get; init; }

		/// <summary>Why it failed, for <see cref="CorePackageState.Failed"/>; else null.</summary>
		public string? Error { get; init; }

		public string Name => Package.Name;

		public string StatusText => State switch
		{
			CorePackageState.Loaded => "loaded",
			CorePackageState.Failed => $"error: {Error}",
			_ => "?",
		};
	}

	/// <summary>
	/// Builds the core-package list the UI shows. Kept out of the form so what the
	/// user is told - which packages exist, which are in use, which are broken and
	/// why - can be tested without one.
	/// </summary>
	public static class CorePackageList
	{
		/// <summary>
		/// Merges what the scan found with what the session actually loaded. Packages
		/// loaded from outside a search directory (File &gt; Open Core, or --core) are
		/// included too: they are part of the session, so hiding them would make the
		/// dialog disagree with the Core Settings menu.
		/// </summary>
		public static IReadOnlyList<CorePackageListEntry> Build(
			IEnumerable<DiscoveredCorePackage> discovered,
			IEnumerable<CoreRegistry.LoadedCorePackage> loaded,
			IEnumerable<(DiscoveredCorePackage Package, string Error)> failures)
		{
			Dictionary<string, string> errorByPath = new(StringComparer.OrdinalIgnoreCase);
			foreach (var (pkg, error) in failures) errorByPath[pkg.Path] = error;

			List<CorePackageListEntry> entries = new();
			HashSet<string> listedPaths = new(StringComparer.OrdinalIgnoreCase);
			foreach (var pkg in discovered)
			{
				if (!listedPaths.Add(pkg.Path)) continue;
				var error = pkg.Error ?? (errorByPath.TryGetValue(pkg.Path, out var e) ? e : null);
				entries.Add(new CorePackageListEntry
				{
					Package = pkg,
					Error = error,
					State = error is null ? CorePackageState.Loaded : CorePackageState.Failed,
				});
			}

			// loaded from outside any search directory: real, in use, and not rescannable
			foreach (var pkg in loaded)
			{
				if (!listedPaths.Add(pkg.Path)) continue;
				entries.Add(new CorePackageListEntry
				{
					State = CorePackageState.Loaded,
					Package = new DiscoveredCorePackage
					{
						Path = pkg.Path,
						Name = pkg.Name,
						Sha1 = pkg.Sha1,
					},
				});
			}

			return entries.OrderBy(static e => e.Name, StringComparer.OrdinalIgnoreCase).ToList();
		}
	}
}
