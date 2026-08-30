namespace Chimera.Emulation.Common
{
	/// <summary>
	/// A machine's drive lights: one per medium it actually has - a disc, a hard
	/// disk - each lit on any frame that drive was read or written.
	/// </summary>
	/// <remarks>
	/// The plural is the point. <see cref="IDriveLight"/> models one light, which
	/// is all a console with a CD needs; a DOS machine can have a disc AND a hard
	/// disk at once, and "the CD spun" and "the disk was touched" are different
	/// facts. A single light meaning either would say almost nothing while a game
	/// streams from CD.
	///
	/// A core reports only the drives the PROJECT put media in. A light for a
	/// drive nobody filled is one that never comes on, which is worse than no
	/// light at all: it reads as a drive that is broken rather than absent.
	/// </remarks>
	public interface IDriveLights : ISpecializedEmulatorService
	{
		/// <summary>How many lights this machine has. Zero for most cores.</summary>
		int DriveLightCount { get; }

		/// <summary>What the drive is, for the tooltip: "Hard Disk", "CD-ROM".</summary>
		string DriveLightName(int index);

		/// <summary>Whether that drive was read or written during the last frame.</summary>
		bool DriveLightOn(int index);
	}
}
