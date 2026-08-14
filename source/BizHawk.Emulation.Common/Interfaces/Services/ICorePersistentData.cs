#nullable enable

namespace BizHawk.Emulation.Common
{
	/// <summary>
	/// A machine that keeps something when it is switched off: a cartridge's
	/// battery-backed memory, a memory card, a disk the game wrote on.
	///
	/// The frontend moves these bytes and never interprets them. It does not know what
	/// a cartridge is, let alone what an SRAM is - so even the NAME of the thing comes
	/// from the core (<see cref="PersistentDataName"/>), which is what the menu shows.
	///
	/// A core implements this only when the LOADED game has something to keep: the same
	/// core runs a battery cartridge, a plain one and a disk, and only two of those give
	/// the user anything to carry away.
	/// </summary>
	public interface ICorePersistentData : IEmulatorService
	{
		/// <summary>What the core calls it, in the core's own words: "Cartridge SRAM", "Disk Contents".</summary>
		string PersistentDataName { get; }

		/// <summary>
		/// Stable id for this data, in the core's vocabulary ("sram", "disk"). A bundle
		/// files an attachment under it, and a file written on its own is named with it.
		/// </summary>
		string PersistentDataId { get; }

		/// <summary>
		/// A copy of what the machine would keep right now. Null when it keeps nothing,
		/// which the frontend treats as "there is no such file" rather than as an empty one.
		/// </summary>
		/// <param name="clearDirty">whether this counts as saving it, for <see cref="PersistentDataModified"/></param>
		byte[]? ClonePersistentData(bool clearDirty = true);

		/// <summary>
		/// Hands the core data to start from. The core is the judge of whether it fits this
		/// machine and throws if it does not - loading half of someone else's save silently
		/// would be worse than not loading it.
		/// </summary>
		void StorePersistentData(byte[] data);

		/// <summary>
		/// Whether the machine may have written something since it was last saved or loaded.
		/// A hint: cores are allowed to be pessimistic, and this one is.
		/// </summary>
		bool PersistentDataModified { get; }
	}
}
