using System.Collections.Generic;
using System.IO;
using System.Linq;

namespace BizHawk.Emulation.Common.Waterbox
{
	/// <summary>Empty settings placeholder (per-core tunables come from waterbox.config's "settings", TBD).</summary>
	public sealed class WaterboxCoreSettings { }

	/// <summary>Empty sync-settings placeholder.</summary>
	public sealed class WaterboxCoreSyncSettings { }

	/// <summary>
	/// The built-in factory for waterbox packages (core.wbx + waterbox.config). One
	/// instance per loaded package: it parses that package's waterbox.config and
	/// produces the generic <see cref="WaterboxCore"/> for its declared system.
	/// Constructed by CorePackageLoader when it sees a waterbox package - there is no
	/// per-core managed assembly.
	/// </summary>
	public sealed class WaterboxCoreFactory : ICoreFactory
	{
		public const string WbxFileName = "core.wbx";
		public const string ConfigFileName = "waterbox.config";

		private readonly WaterboxConfig _cfg;
		private readonly string _wbxPath;

		public WaterboxCoreFactory(string packageDir)
		{
			_wbxPath = Path.Combine(packageDir, WbxFileName);
			_cfg = WaterboxConfig.FromJson(File.ReadAllText(Path.Combine(packageDir, ConfigFileName)));
			if (_cfg is null) throw new InvalidOperationException($"{ConfigFileName} is empty or invalid");
			if (string.IsNullOrEmpty(_cfg.SystemId)) throw new InvalidOperationException($"{ConfigFileName} is missing systemId");
		}

		/// <summary>True if the directory is a waterbox package (has both core.wbx and waterbox.config).</summary>
		public static bool IsWaterboxPackage(string packageDir)
			=> File.Exists(Path.Combine(packageDir, WbxFileName))
				&& File.Exists(Path.Combine(packageDir, ConfigFileName));

		public string CoreName => _cfg.CoreName ?? "Waterbox";

		/// <summary>Rom-extension -&gt; systemId map from waterbox.config (for the synthesized manifest).</summary>
		public Dictionary<string, string> Extensions => _cfg.Extensions ?? new Dictionary<string, string>();

		public IReadOnlyList<string> SystemIds => new[] { _cfg.SystemId };

		public Type CoreType => typeof(WaterboxCore);

		public Type SettingsType => typeof(WaterboxCoreSettings);

		public Type SyncSettingsType => typeof(WaterboxCoreSyncSettings);

		public IEmulator Create(CoreCreationContext ctx)
		{
			var rom = ctx.Roms.FirstOrDefault()
				?? throw new InvalidOperationException($"{CoreName} needs a rom to load");
			return new WaterboxCore(rom.FileData, _cfg, _wbxPath);
		}
	}
}
