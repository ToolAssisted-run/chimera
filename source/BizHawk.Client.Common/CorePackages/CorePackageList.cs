#nullable enable

using System;
using System.Collections.Generic;
using System.Linq;

namespace BizHawk.Client.Common
{
	/// <summary>What the user's core-package situation is, per package.</summary>
	public enum CorePackageState
	{
		/// <summary>Loaded and in use this session.</summary>
		Loaded,

		/// <summary>Enabled but not loaded yet — it will load next launch.</summary>
		PendingLoad,

		/// <summary>
		/// Loaded this session, but switched off for the next. Packages cannot be
		/// unloaded in-process (loading pins native modules, and adapter packages an
		/// assembly), so this state exists and the UI must say so rather than pretend
		/// the core went away.
		/// </summary>
		LoadedDisabled,

		/// <summary>Switched off and not loaded.</summary>
		Disabled,

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

		/// <summary>True if the user has it switched on (regardless of whether it has loaded yet).</summary>
		public bool Enabled => State is CorePackageState.Loaded or CorePackageState.PendingLoad;

		/// <summary>True if a restart is needed for the current enabled/disabled choice to take effect.</summary>
		public bool NeedsRestart => State is CorePackageState.PendingLoad or CorePackageState.LoadedDisabled;

		public string StatusText => State switch
		{
			CorePackageState.Loaded => "loaded",
			CorePackageState.PendingLoad => "loads on restart",
			CorePackageState.LoadedDisabled => "loaded (off after restart)",
			CorePackageState.Disabled => "disabled",
			CorePackageState.Failed => $"error: {Error}",
			_ => "?",
		};
	}

	/// <summary>
	/// Builds the core-package list the UI shows. Kept out of the form so the states
	/// - which are the whole substance of that dialog - can be tested without one.
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
			IEnumerable<(DiscoveredCorePackage Package, string Error)> failures,
			Config config)
		{
			HashSet<string> loadedPaths = new(loaded.Select(static p => p.Path), StringComparer.OrdinalIgnoreCase);
			Dictionary<string, string> errorByPath = new(StringComparer.OrdinalIgnoreCase);
			foreach (var (pkg, error) in failures) errorByPath[pkg.Path] = error;

			List<CorePackageListEntry> entries = new();
			HashSet<string> listedPaths = new(StringComparer.OrdinalIgnoreCase);
			foreach (var pkg in discovered)
			{
				if (!listedPaths.Add(pkg.Path)) continue;
				var error = pkg.Error ?? (errorByPath.TryGetValue(pkg.Path, out var e) ? e : null);
				var enabled = CorePackageDiscovery.IsEnabled(config, pkg);
				var isLoaded = loadedPaths.Contains(pkg.Path);
				entries.Add(new CorePackageListEntry
				{
					Package = pkg,
					Error = error,
					State = error is not null ? CorePackageState.Failed
						: isLoaded ? (enabled ? CorePackageState.Loaded : CorePackageState.LoadedDisabled)
						: (enabled ? CorePackageState.PendingLoad : CorePackageState.Disabled),
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
						IsDirectoryForm = pkg.Sha1 is null,
					},
				});
			}

			return entries.OrderBy(static e => e.Name, StringComparer.OrdinalIgnoreCase).ToList();
		}
	}
}
