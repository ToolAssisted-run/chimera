using System.Collections.Generic;
using System.IO;
using System.Linq;

namespace BizHawk.Emulation.Common.Waterbox
{
	/// <summary>Non-sync settings - none yet (nothing about a waterbox core changes safely mid-run).</summary>
	public sealed class WaterboxCoreSettings { }

	/// <summary>
	/// The user-tunable sync settings: a bag of key -&gt; value that the adapter merges
	/// OVER the package's waterbox.config defaults and delivers to the guest before
	/// Init (so they can shape the machine). Sync (not plain) settings because they
	/// affect the constructed machine, hence movie determinism; recorded in movies.
	/// </summary>
	public sealed class WaterboxCoreSyncSettings
	{
		public Dictionary<string, object> Values { get; set; } = new();

		public WaterboxCoreSyncSettings Clone()
			=> new() { Values = Values is null ? new() : new Dictionary<string, object>(Values) };
	}

	/// <summary>
	/// The built-in factory for waterbox packages (core.wbx + waterbox.config). One
	/// instance per loaded package: it parses that package's waterbox.config and
	/// produces the generic <see cref="WaterboxCore"/> for its declared system.
	/// Constructed by CorePackageLoader when it sees a waterbox package - there is no
	/// per-core managed assembly.
	/// </summary>
	public sealed class WaterboxCoreFactory : ICoreFactory, ICoreIdentity
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

		/// <summary>The package's identity, so lists of available cores name the core rather than the adapter.</summary>
		public CoreAttribute CoreIdentity => WaterboxCore.IdentityOf(_cfg);

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
			return new WaterboxCore(rom.FileData, _cfg, _wbxPath, ctx.SyncSettings as WaterboxCoreSyncSettings);
		}
	}
}
