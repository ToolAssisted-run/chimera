#nullable enable

using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Reflection;
using System.Runtime.CompilerServices;

using Chimera.Common.CollectionExtensions;
using Chimera.Emulation.Common;
using Chimera.Emulation.Common.Waterbox;

namespace Chimera.Client.Common
{
	/// <summary>
	/// The set of core factories known to this frontend, all loaded from external
	/// core packages. Replaces the reflection-driven CoreInventory.
	/// </summary>
	public sealed class CoreRegistry
	{
		public static readonly CoreRegistry Instance = new();

		private readonly List<ICoreFactory> _all = new();

		private readonly Dictionary<string, List<ICoreFactory>> _bySystem = new();

		private readonly Dictionary<string, string> _systemByExtension = new(); // ".nes" (lowercase) -> "NES"

		private CoreRegistry()
		{
		}

		/// <summary>Rom file extensions (with leading dot, lowercase) declared by loaded packages.</summary>
		public IReadOnlyCollection<string> KnownRomExtensions => _systemByExtension.Keys;

		public bool TryGetSystemForExtension(string extWithLeadingDot, out string systemId)
			=> _systemByExtension.TryGetValue(extWithLeadingDot.ToLowerInvariant(), out systemId);

		public IReadOnlyList<ICoreFactory> AllFactories => _all;

		/// <summary>Distinct assemblies of all registered factories (used e.g. to discover virtual pad schemas).</summary>
		public IEnumerable<Assembly> FactoryAssemblies => _all.Select(static f => f.CoreType.Assembly).Distinct();

		public IReadOnlyList<ICoreFactory> GetFactories(string systemId)
			=> _bySystem.TryGetValue(systemId, out var list) ? list : [ ];

		/// <summary>
		/// Default input bindings provided by loaded packages (each package's optional
		/// <see cref="PackageKeybinds.FileName"/>), keyed by controller definition
		/// name. The frontend ships no bindings of its own; these fill in for
		/// controllers the user's config has never seen.
		/// </summary>
		public DefaultControls PackageControlDefaults { get; } = new();

		/// <summary>
		/// The package each registered factory came from. Per FACTORY, not per adapter assembly: every
		/// miniBox package is served by the same adapter type, so a map by assembly answered one hash for
		/// all of them - whichever package registered last.
		/// </summary>
		private readonly Dictionary<ICoreFactory, (string? Sha1, string Path)> _packageOf = new();

		/// <summary>Which factory made each live emulator, so "which build is running" has an exact answer.</summary>
		private readonly ConditionalWeakTable<IEmulator, ICoreFactory> _madeBy = new();

		/// <summary>
		/// The build the user chose for a core (<see cref="Config.DefaultCoreBuilds"/>), asked when several
		/// builds are registered and no project pins one. Set once by the frontend, which owns the config.
		/// </summary>
		public Func<string, string?>? ChosenBuildOf { get; set; }

		/// <summary>
		/// The SHA1 of the package file <paramref name="factory"/> came from - a package's ground-truth
		/// identity; name/version/platform are secondary. Null for directory-form (dev) packages, which
		/// have no file to hash, and for a factory registered by hand.
		/// </summary>
		public string? PackageSha1Of(ICoreFactory factory)
			=> _packageOf.TryGetValue(factory, out var package) ? package.Sha1 : null;

		/// <summary>The SHA1 of the package whose factory made <paramref name="emulator"/>, when known.</summary>
		public string? PackageSha1Of(IEmulator? emulator)
			=> emulator is not null && _madeBy.TryGetValue(emulator, out var factory) ? PackageSha1Of(factory) : null;

		/// <summary>Remembers that <paramref name="factory"/> made <paramref name="emulator"/> (see <see cref="PackageSha1Of(IEmulator)"/>).</summary>
		public void NoteCreated(IEmulator? emulator, ICoreFactory factory)
		{
			if (emulator is null) return;
			_madeBy.Remove(emulator);
			_madeBy.Add(emulator, factory);
		}

		/// <summary>
		/// The registered build of <paramref name="coreName"/> to run: the one <paramref name="pinnedSha1"/>
		/// names when it is registered, else the chosen build, else the most recently installed
		/// (<see cref="CoreChoices.PickBuild"/>). Null when no build of that core is registered.
		/// </summary>
		public ICoreFactory? FactoryFor(string coreName, string? pinnedSha1)
			=> CoreChoices.PickBuild(
				_all.Where(f => f.CoreName == coreName),
				PackageSha1Of,
				InstalledAt,
				pinnedSha1,
				ChosenBuildOf?.Invoke(coreName));

		private DateTime InstalledAt(ICoreFactory factory)
		{
			if (!_packageOf.TryGetValue(factory, out var package) || package.Path.Length is 0) return DateTime.MinValue;
			try
			{
				return File.Exists(package.Path) ? File.GetLastWriteTimeUtc(package.Path) : Directory.GetLastWriteTimeUtc(package.Path);
			}
			catch (Exception ex) when (ex is IOException or UnauthorizedAccessException or ArgumentException)
			{
				return DateTime.MinValue;
			}
		}

		public void Register(ICoreFactory factory) => Register(factory, sha1: null, path: "");

		private void Register(ICoreFactory factory, string? sha1, string path)
		{
			foreach (var existing in _all.Where(f => f.CoreName == factory.CoreName))
			{
				// Two BUILDS of one core may be installed side by side and both run, as long as they
				// are not the same bytes (issue #63): a project pins one, and another build is another
				// machine. Only miniBox packages, though - an adapter package is a .NET assembly, and a
				// second assembly of the same identity would quietly be the first one's types.
				var other = PackageSha1Of(existing);
				var anotherBuild = factory is WaterboxCoreFactory && existing is WaterboxCoreFactory
					&& sha1 is not null && other is not null
					&& !string.Equals(sha1, other, StringComparison.OrdinalIgnoreCase);
				if (!anotherBuild)
				{
					Console.WriteLine($"CoreRegistry: ignoring duplicate registration of core \"{factory.CoreName}\"");
					return;
				}
			}
			_all.Add(factory);
			_packageOf[factory] = (sha1, path);
			// once per system whatever the factory reports: a core listed twice under
			// one system makes every lookup of it by name ambiguous
			foreach (var sysID in factory.SystemIds.Distinct()) _bySystem.GetValueOrPutNew(sysID).Add(factory);
		}

		/// <summary>
		/// Loads a single core package (directory or zip) and registers its factories,
		/// extensions, and control defaults. Loading is an explicit act; there is no
		/// discovery. Throws with a user-showable message on any problem.
		/// </summary>
		/// <returns>the package's manifest, the SHA1 of its file (null for directory-form packages), and the factories it registered</returns>
		public (CorePackageManifest Manifest, string? PackageSha1, IReadOnlyList<ICoreFactory> Factories) LoadCorePackage(string path)
		{
			var (manifest, factories, packageDir, packageSha1) = CorePackageLoader.LoadPackage(path);
			foreach (var factory in factories)
			{
				Register(factory, packageSha1, path);
			}
			// the same package can arrive twice (found in Cores/ AND named with --core);
			// registration itself is idempotent, but the session list must not double up
			if (!IsPackageLoaded(path))
			{
				_loadedPackages.Add(new LoadedCorePackage
				{
					Path = path,
					Name = manifest.Name ?? Path.GetFileNameWithoutExtension(path),
					Sha1 = packageSha1,
					CoreNames = factories.Select(static f => f.CoreName).ToList(),
				});
			}
			foreach (var (ext, sysID) in manifest.Extensions)
			{
				var extLower = ext.ToLowerInvariant();
				if (_systemByExtension.TryGetValue(extLower, out var existing) && existing != sysID)
				{
					Console.WriteLine($"CoreRegistry: extension {extLower} claimed for {existing} and {sysID}; keeping {existing}");
					continue;
				}
				_systemByExtension[extLower] = sysID;
			}
			var keybinds = PackageKeybinds.Read(packageDir);
			// first package to name a controller wins; later duplicates are ignored
			if (keybinds is not null) PackageControlDefaults.OverlayMissingFrom(keybinds);
			return (manifest, packageSha1, factories);
		}

		/// <summary>A package this session has loaded, in load order.</summary>
		public sealed class LoadedCorePackage
		{
			public string Path { get; init; } = "";

			public string Name { get; init; } = "";

			public string? Sha1 { get; init; }

			/// <summary>Names of the cores this package registered.</summary>
			public IReadOnlyList<string> CoreNames { get; init; } = [ ];
		}

		private readonly List<LoadedCorePackage> _loadedPackages = new();

		public IReadOnlyList<LoadedCorePackage> LoadedPackages => _loadedPackages;

		/// <summary>True if this exact path has already been loaded this session.</summary>
		public bool IsPackageLoaded(string path)
			=> _loadedPackages.Exists(p => string.Equals(p.Path, path, StringComparison.OrdinalIgnoreCase));

		/// <summary>
		/// Loads every loadable package in <paramref name="packages"/> that is not
		/// already loaded, and reports what went wrong for the ones that failed. A bad
		/// package must not stop the others: discovery is automatic, so one broken zip
		/// in Cores/ would otherwise take the whole frontend down with it.
		/// </summary>
		/// <returns>one entry per package that failed to load, with the reason</returns>
		public IReadOnlyList<(DiscoveredCorePackage Package, string Error)> LoadDiscovered(IEnumerable<DiscoveredCorePackage> packages)
		{
			List<(DiscoveredCorePackage, string)> failures = new();
			foreach (var pkg in packages)
			{
				if (pkg.Error is not null) { failures.Add((pkg, pkg.Error)); continue; }
				if (IsPackageLoaded(pkg.Path)) continue;
				try
				{
					_ = LoadCorePackage(pkg.Path);
				}
				catch (Exception ex)
				{
					failures.Add((pkg, ex.Message));
				}
			}
			return failures;
		}

		/// <summary>
		/// How a factory's core introduces itself. A factory that knows its own
		/// identity is asked first: one adapter type can back many packages, so its
		/// class attribute would name the adapter for all of them.
		/// </summary>
		public static CoreAttribute? AttributesFor(ICoreFactory factory)
			=> (factory as ICoreIdentity)?.CoreIdentity
				?? factory.CoreType.GetCustomAttributes(typeof(CoreAttribute), false).OfType<CoreAttribute>().FirstOrDefault();
	}
}
