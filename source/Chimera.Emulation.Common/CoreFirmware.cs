#nullable enable

using System.Collections.Generic;
using System.Linq;

namespace Chimera.Emulation.Common
{
	/// <summary>
	/// One file a core needs but cannot ship: a boot rom, a disk-system BIOS, a
	/// character set. The core package declares what it expects; the user supplies
	/// the file once and the frontend remembers where it is.
	///
	/// The declaration lives with the core and nowhere else. The frontend has no
	/// table of known firmware for consoles it has never heard of - it cannot have
	/// one, since it does not know what cores exist. All it does is ask for what a
	/// loaded package says it wants, check the file matches, and hand it over.
	/// </summary>
	public sealed class CoreFirmwareDecl
	{
		/// <summary>
		/// Identifies the file to both sides: the frontend keys the user's choice by
		/// it, and the core reads the file under this name.
		/// </summary>
		public string Id { get; set; } = "";

		/// <summary>What to call it in the firmware window. Falls back to <see cref="Id"/>.</summary>
		public string? Display { get; set; }

		/// <summary>The sentence under the row - what the file is, and what breaks without it.</summary>
		public string? Description { get; set; }

		/// <summary>Exact expected size in bytes, or 0 to accept any size.</summary>
		public int Size { get; set; }

		/// <summary>
		/// SHA1s of the dumps known to be right (uppercase hex, no separators). A file
		/// that matches none of them is still usable - it is reported as unrecognised
		/// rather than rejected, because a good dump this list has never seen is more
		/// likely than a frontend that should refuse to run.
		/// </summary>
		public string? Sha1 { get; set; }

		/// <summary>the file name this dump usually goes by - a hint, never a requirement</summary>
		public string? Name { get; set; }

		/// <summary>which release this is ("US v1.10", "6.61 official") - shown beside the name</summary>
		public string? Label { get; set; }

		/// <summary>
		/// False for a file the core can start without (an optional expansion rom, say).
		/// A required file that is missing fails the load with a message naming it.
		/// </summary>
		public bool Required { get; set; } = true;

		public string DisplayName => string.IsNullOrWhiteSpace(Display) ? Id : Display!;
	}

	/// <summary>
	/// Implemented by a core factory whose core expects firmware, so the frontend can
	/// ask what a package wants before it has loaded a rom - which is what the
	/// firmware window lists, and how it knows to gray itself out.
	/// </summary>
	public interface ICoreFirmwareUser
	{
		IReadOnlyList<CoreFirmwareDecl> Firmware { get; }
	}
}
