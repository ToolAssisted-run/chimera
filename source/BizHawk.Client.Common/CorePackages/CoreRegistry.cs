#nullable enable

using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Reflection;

using BizHawk.Common.CollectionExtensions;
using BizHawk.Emulation.Common;

namespace BizHawk.Client.Common
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
		/// Default input bindings provided by loaded packages (a package's optional
		/// <c>defctrl.json</c>), keyed by controller definition name. The frontend
		/// ships no bindings of its own; these fill in for controllers the user's
		/// config has never seen.
		/// </summary>
		public DefaultControls PackageControlDefaults { get; } = new();

		private readonly Dictionary<Assembly, string> _packageSha1ByAssembly = new();

		/// <summary>
		/// The SHA1 of the package file that provides <paramref name="coreType"/> (any type
		/// from the package's adapter assembly). A package's ground-truth identity is this
		/// hash — name/version/platform are secondary. Null for directory-form (dev)
		/// packages, which have no file to hash.
		/// </summary>
		public string/*?*/ GetPackageSha1ForCore(Type coreType)
			=> _packageSha1ByAssembly.TryGetValue(coreType.Assembly, out var sha1) ? sha1 : null;

		public void Register(ICoreFactory factory)
		{
			if (_all.Exists(f => f.CoreName == factory.CoreName))
			{
				Console.WriteLine($"CoreRegistry: ignoring duplicate registration of core \"{factory.CoreName}\"");
				return;
			}
			_all.Add(factory);
			foreach (var sysID in factory.SystemIds) _bySystem.GetValueOrPutNew(sysID).Add(factory);
		}

		/// <summary>
		/// Loads a single core package (directory or zip) and registers its factories,
		/// extensions, and control defaults. Loading is an explicit act — there is no
		/// discovery. Throws with a user-showable message on any problem.
		/// </summary>
		/// <returns>the loaded package's manifest and the SHA1 of its file (null for directory-form packages)</returns>
		public (CorePackageManifest Manifest, string/*?*/ PackageSha1) LoadCorePackage(string path)
		{
			var (manifest, factories, packageDir, packageSha1) = CorePackageLoader.LoadPackage(path);
			foreach (var factory in factories)
			{
				Register(factory);
				if (packageSha1 is not null) _packageSha1ByAssembly[factory.CoreType.Assembly] = packageSha1;
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
			var defctrlPath = Path.Combine(packageDir, "defctrl.json");
			if (File.Exists(defctrlPath))
			{
				// first package to name a controller wins; later duplicates are ignored
				PackageControlDefaults.OverlayMissingFrom(ConfigService.Load<DefaultControls>(defctrlPath));
			}
			return (manifest, packageSha1);
		}

		public static CoreAttribute? AttributesFor(ICoreFactory factory)
			=> factory.CoreType.GetCustomAttributes(typeof(CoreAttribute), false).OfType<CoreAttribute>().FirstOrDefault();
	}
}
