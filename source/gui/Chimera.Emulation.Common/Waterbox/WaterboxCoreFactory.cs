using System.Collections.Generic;
using System.IO;
using System.Linq;

namespace Chimera.Emulation.Common.Waterbox
{
	/// <summary>
	/// The built-in factory for waterbox packages (core.wbx + waterbox.config). One
	/// instance per loaded package: it parses that package's waterbox.config and
	/// produces the generic <see cref="WaterboxCore"/> for its declared system.
	/// Constructed by CorePackageLoader when it sees a waterbox package - there is no
	/// per-core managed assembly.
	/// </summary>
	public sealed class WaterboxCoreFactory : ICoreFactory, ICoreIdentity, ICoreFirmwareUser
	{
		public const string WbxFileName = "core.wbx";
		public const string ConfigFileName = "waterbox.config";

		private readonly WaterboxConfig _cfg;
		private readonly string _packageDir;

		public WaterboxCoreFactory(string packageDir)
		{
			_packageDir = packageDir;
			_cfg = WaterboxConfig.FromJson(File.ReadAllText(Path.Combine(packageDir, ConfigFileName)));
			if (_cfg is null) throw new InvalidOperationException($"{ConfigFileName} is empty or invalid");
			if (_cfg.HasMachines)
			{
				// a package of machines must say which setting picks one, and that
				// setting must be one it actually declares - otherwise every session
				// would silently be the first machine
				if (string.IsNullOrEmpty(_cfg.MachineSetting))
				{
					throw new InvalidOperationException($"{ConfigFileName} declares machines but no machineSetting to pick between them");
				}
				if (_cfg.Settings?.Exists(s => s.Name == _cfg.MachineSetting) is not true)
				{
					throw new InvalidOperationException($"{ConfigFileName}: machineSetting \"{_cfg.MachineSetting}\" is not one of this package's settings");
				}
				foreach (var machine in _cfg.Machines)
				{
					if (string.IsNullOrEmpty(machine.Id)) throw new InvalidOperationException($"{ConfigFileName}: a machine has no id");
					// a machine without its own controller uses the package's -
					// dolphin's GameCube and Wii share one pad - so only a machine
					// with NO controller anywhere is a broken declaration
					if (machine.Input?.Buttons is null && _cfg.Input?.Buttons is null)
					{
						throw new InvalidOperationException($"{ConfigFileName}: machine \"{machine.Id}\" has no controller, and the package declares none to fall back on");
					}
				}
			}
			else if (string.IsNullOrEmpty(_cfg.SystemId))
			{
				throw new InvalidOperationException($"{ConfigFileName} is missing systemId");
			}
		}

		/// <summary>True if the directory is a waterbox package (has both core.wbx and waterbox.config).</summary>
		public static bool IsWaterboxPackage(string packageDir)
			=> File.Exists(Path.Combine(packageDir, WbxFileName))
				&& File.Exists(Path.Combine(packageDir, ConfigFileName));

		public string CoreName => _cfg.CoreName ?? "Waterbox";

		/// <summary>The package's identity, so lists of available cores name the core rather than the adapter.</summary>
		public CoreAttribute CoreIdentity => WaterboxCore.IdentityOf(_cfg);

		public WaterboxConfig Config => _cfg;
		public string PackageDir => _packageDir;


		/// <summary>Rom-extension -&gt; systemId map from waterbox.config, over every machine.</summary>
		public Dictionary<string, string> Extensions => _cfg.AllExtensions;

		/// <summary>Every machine this package can be (one, for most packages).</summary>
		public IReadOnlyList<string> SystemIds => _cfg.SystemIds;

		/// <summary>The files this core expects the user to provide (see waterbox.config "firmware").</summary>
		public IReadOnlyList<CoreFirmwareDecl> Firmware => _cfg.Firmware ?? (IReadOnlyList<CoreFirmwareDecl>) [ ];

		/// <summary>Of those, the ones the decisions actually called for at the last load.</summary>
		public IReadOnlyList<CoreFirmwareDecl> FirmwareInUse { get; private set; } = [ ];

		public Type CoreType => typeof(WaterboxCore);

		public Type SettingsType => typeof(WaterboxCoreSettings);


		public IEmulator Create(CoreCreationContext ctx)
		{
			var rom = ctx.Roms.FirstOrDefault()
				?? throw new InvalidOperationException($"{CoreName} needs a rom to load");
			var settings = MachinePinnedSettings(ctx);
			// one line per boot, greppable: a project open must produce exactly
			// one of these (the witness gate counts them)
			Console.WriteLine($"[waterbox] booting {CoreName}");
			return new WaterboxCore(
				// a file on disk is mounted from there; only a rom the frontend
				// made (or pulled out of an archive) arrives as bytes
				rom.RomPath is { Length: > 0 } path && File.Exists(path) ? null : rom.FileData,
				rom.RomPath,
				_cfg,
				_packageDir,
				settings,
				ResolveFirmware(ctx),
				ctx.ExtraFiles);
		}

		/// <summary>
		/// The settings this boot runs with, with the machine settled.
		///
		/// A project pins the machine like any other setting, so it is already here.
		/// A plain rom is not a project and pins nothing, but the frontend routed it
		/// to a system by its extension - and that system IS a machine, so it decides.
		/// Without this a .sms opened with no project would boot the package's first
		/// machine, which is a Mega Drive, and refuse the cartridge.
		/// </summary>
		private WaterboxCoreSettings MachinePinnedSettings(CoreCreationContext ctx)
		{
			var settings = ctx.Settings as WaterboxCoreSettings;
			if (!_cfg.HasMachines) return settings;

			// what the CALLER said, not what the package defaults to: every package
			// has a default machine, so asking the effective settings would mean the
			// rom never gets a say
			var pinned = settings?.Values is not null && settings.Values.TryGetValue(_cfg.MachineSetting, out var value)
				? value?.ToString() ?? ""
				: "";
			if (_cfg.Machines.Exists(m => m.Selects(pinned))) return settings;

			var routed = _cfg.MachineForSystem(ctx.Game?.System);
			if (routed is null) return settings;

			settings = settings?.Clone() ?? new WaterboxCoreSettings();
			settings.Values[_cfg.MachineSetting] = routed.When is { Count: > 0 } ? routed.When[0] : routed.Id;
			return settings;
		}

		/// <summary>
		/// Collects the files the user provided for this core's declarations,
		/// asking only for what the DECISIONS call for: the engine's firmware
		/// decision tree evaluates the declaration against the slot map and the
		/// effective settings (docs/project.md). A missing required file stops
		/// the load here, before the sandbox is built, so what the user sees
		/// names the file rather than "Init failed".
		/// </summary>
		private Dictionary<string, byte[]> ResolveFirmware(CoreCreationContext ctx)
		{
			var slotsJson = "{}";
			if (ctx.ExtraFiles is not null)
			{
				foreach (var extra in ctx.ExtraFiles)
				{
					// the slot map is one the frontend made, so it has bytes
					if (extra.Name is "slots" && extra.Data is not null)
					{
						slotsJson = System.Text.Encoding.UTF8.GetString(extra.Data);
						break;
					}
				}
			}
			var effective = WaterboxCore.EffectiveSettingsFor(
				_cfg, ctx.Settings as WaterboxCoreSettings);
			var applicable = Engine.EngineFirmware.Evaluate(
				_cfg.RawFirmwareJson, slotsJson, Newtonsoft.Json.JsonConvert.SerializeObject(effective));

			Dictionary<string, byte[]> resolved = new();
			List<CoreFirmwareDecl> used = new();
			FirmwareInUse = used;
			foreach (var (id, index) in applicable)
			{
				var decl = index >= 0 && index < Firmware.Count ? Firmware[index] : null;
				if (decl is null || decl.Id != id) decl = Firmware.FirstOrDefault(d => d.Id == id);
				if (decl is null) continue;
				used.Add(decl);
				var bytes = ctx.FirmwareProvider?.Invoke(decl);
				if (bytes is null)
				{
					// A declaration may say the core can start without it
					// (an optional identity file, an expansion rom); absent
					// simply means not mounted, and the core carries on.
					if (!decl.Required) continue;
					throw new MissingFirmwareException(
						$"{CoreName} needs firmware that has not been provided: {decl.DisplayName}."
							+ " Put the file in the Firmware folder and open the project again"
							+ " (a new project's wizard can also point at it).");
				}
				resolved[decl.Id] = bytes;
			}
			return resolved;
		}
	}
}
