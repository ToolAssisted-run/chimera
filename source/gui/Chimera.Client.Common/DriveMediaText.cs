#nullable enable

using System.IO;

using Chimera.Emulation.Common;

namespace Chimera.Client.Common
{
	/// <summary>
	/// What the status bar says beside a drive that can be given something else: which image
	/// is in it, and - when that is not the same thing - which one the selector is on.
	///
	/// They differ more often than it sounds. DOSBox-X's changer has a selector that
	/// Previous/Next move and a Swap input that puts the selected image in, so after a Next the
	/// machine is still reading the old disc; nothing on screen said so, and somebody who
	/// cannot see the selector cannot tell one that moved from one that did not. A Sega CD's
	/// tray can also simply be empty. When the two agree there is one thing to say and it is
	/// said once.
	/// </summary>
	public static class DriveMediaText
	{
		/// <summary>The most of a file name the bar gives room to; the tooltip has the whole of it.</summary>
		public const int NameRoom = 28;

		/// <summary>Short: <c>2/4 Disk B.fdi</c>, <c>1/2 Disc 1.iso | selected 2/2 Disc 2.iso</c>, <c>empty | selected 1/2 ...</c>.</summary>
		public static string Short(DriveMedia media) => Compose(media, NameRoom);

		/// <summary>The same with nothing cut, led by what the drive is: for the tooltip.</summary>
		public static string Long(string driveName, DriveMedia media)
		{
			var inserted = media.Inserted >= 0 ? $"in the drive: {Entry(media, media.Inserted, int.MaxValue)}" : "the drive is empty";
			return media.Inserted == media.Selected
				? $"{driveName} - {inserted}"
				: $"{driveName} - {inserted}; selected, not inserted: {Entry(media, media.Selected, int.MaxValue)}";
		}

		private static string Compose(DriveMedia media, int room)
		{
			var inserted = media.Inserted >= 0 ? Entry(media, media.Inserted, room) : "empty";
			return media.Inserted == media.Selected ? inserted : $"{inserted} | selected {Entry(media, media.Selected, room)}";
		}

		private static string Entry(DriveMedia media, int index, int room)
		{
			if (index < 0 || index >= media.Names.Count) return "?";
			return $"{index + 1}/{media.Names.Count} {Fit(media.Names[index], room)}";
		}

		/// <summary>
		/// Cut in the MIDDLE, keeping the end: the images of one game share a long beginning and
		/// differ at the end - "... (Disk 1 of 4)(Disk A).fdi" - so cutting the tail off would
		/// leave four names that read the same.
		/// </summary>
		public static string Fit(string name, int room)
		{
			name = Path.GetFileName(name) is { Length: > 0 } file ? file : name;
			if (name.Length <= room || room < 8) return name;
			var tail = room * 2 / 3;
			return name.Substring(0, room - tail - 2) + ".." + name.Substring(name.Length - tail);
		}
	}
}
