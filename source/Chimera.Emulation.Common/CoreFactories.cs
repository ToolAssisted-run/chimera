#nullable enable

using System.Collections.Generic;

namespace Chimera.Emulation.Common
{
	/// <summary>
	/// One rom asset provided to a core at creation time.
	/// (Moved here from Chimera.Emulation.Cores as part of the published Chimera core contract.)
	/// </summary>
	public interface IRomAsset
	{
		byte[]? RomData { get; }

		byte[]? FileData { get; }

		string? Extension { get; }

		string? RomPath { get; }

		/// <summary>
		/// GameInfo for this individual asset.  Doesn't make sense a lot of the time;
		/// only use this if your individual rom assets are full proper games when considered alone.
		/// Not guaranteed to be set in any other situation.
		/// </summary>
		GameInfo? Game { get; }
	}

	/// <summary>
	/// Everything a core factory receives to construct an <see cref="IEmulator"/>.
	/// </summary>
	public sealed class CoreCreationContext
	{
		public CoreComm? Comm { get; set; }

		public GameInfo? Game { get; set; }

		/// <summary>
		/// All roms that should be loaded as part of this core load.
		/// Order may be significant. Does not include firmware or other general resources.
		/// </summary>
		public IReadOnlyList<IRomAsset> Roms { get; set; } = [ ];

		public bool DeterministicEmulationRequested { get; set; }

		/// <summary>Settings previously returned from the core, of the factory's SettingsType. May be null.</summary>
		public object? Settings { get; set; }

		/// <summary>Sync settings previously returned from the core, of the factory's SyncSettingsType. May be null.</summary>
		public object? SyncSettings { get; set; }

		/// <summary>
		/// Hands over the file the user provided for a firmware declaration, or null if
		/// there is none (or it no longer reads). Set by the frontend, which is the only
		/// part that knows where the user put it; a factory that needs firmware and finds
		/// this null should fail the load with a message naming what is missing.
		/// </summary>
		public Func<CoreFirmwareDecl, byte[]?>? FirmwareProvider { get; set; }
	}

	/// <summary>
	/// The Chimera core contract: a core package exposes one factory per core.
	/// The frontend discovers factories (built-in or from external core packages),
	/// indexes them by system, and calls <see cref="Create"/> at ROM load time.
	/// </summary>
	public interface ICoreFactory
	{
		/// <summary>Display/identity name; must match the <see cref="CoreAttribute.CoreName"/> of the produced core (movies record it).</summary>
		string CoreName { get; }

		/// <summary>System IDs (<see cref="VSystemID.Raw"/>) this core can emulate.</summary>
		IReadOnlyList<string> SystemIds { get; }

		/// <summary>
		/// The concrete <see cref="IEmulator"/> type produced. Used to key persisted
		/// per-core settings/sync-settings (via <c>Type.ToString()</c>), so it must be stable.
		/// </summary>
		Type CoreType { get; }

		Type SettingsType { get; }

		Type SyncSettingsType { get; }

		IEmulator Create(CoreCreationContext ctx);
	}
}
