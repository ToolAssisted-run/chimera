#nullable enable

using System.Collections.Generic;
using System.IO;
using System.Linq;

using Chimera.Emulation.Common;
using Chimera.Emulation.Common.Engine;

namespace Chimera.Client.Common
{
	/// <summary>
	/// Finds the files that satisfy a firmware requirement, so the user does
	/// not have to (docs/project.md). The SHA1 is the identity - a file's name
	/// is only a hint - and a file satisfies a requirement exactly when its
	/// hash matches one of the requirement's openly listed candidates.
	/// </summary>
	public static class FirmwareLocator
	{
		/// <summary>one hashed file from the search directories</summary>
		public sealed class IndexedFile
		{
			public string Path { get; init; } = "";
			public string Sha1 { get; init; } = "";
			public long Length { get; init; }
		}

		/// <summary>
		/// The file that satisfies the requirement, if the index holds one: hash
		/// equals the entry's - the name plays no part. The decisions upstream
		/// nailed the requirement to one exact file, so there is nothing to
		/// choose between; duplicate copies are the same bytes, first one wins.
		/// </summary>
		public static IndexedFile? FindFor(CoreFirmwareDecl decl, IReadOnlyList<IndexedFile> index)
			=> string.IsNullOrEmpty(decl.Sha1)
				? null
				: index.FirstOrDefault(f => f.Sha1.Equals(decl.Sha1, StringComparison.OrdinalIgnoreCase));

		/// <summary>
		/// Files this large or larger are never firmware; hashing them would only
		/// hurt. The bar was 64 MiB, which is smaller than a PlayStation 3 system
		/// software update (206 MB): no PS3 project could ever find its firmware
		/// in the Firmware folder, nor by the path it remembered, since that
		/// goes through the same index. A gigabyte is above every firmware a
		/// core declares and below any disc image.
		/// </summary>
		public const long MaxBytes = 1024L * 1024 * 1024;

		/// <summary>
		/// Hashes every plausible file in the given directories (and any extra
		/// paths), once - the same index then answers for every requirement.
		/// Unreadable files and directories are simply not in the index.
		/// </summary>
		public static IReadOnlyList<IndexedFile> BuildIndex(
			IEnumerable<string> searchDirs, IEnumerable<string>? extraPaths = null)
		{
			List<IndexedFile> index = new();
			HashSet<string> seen = new();
			IEnumerable<string> files = searchDirs
				.Where(Directory.Exists)
				.SelectMany(static dir => Directory.EnumerateFiles(dir, "*", SearchOption.TopDirectoryOnly));
			if (extraPaths is not null) files = files.Concat(extraPaths);
			foreach (var path in files)
			{
				string full;
				try
				{
					full = Path.GetFullPath(path);
				}
				catch (IOException)
				{
					continue;
				}
				catch (ArgumentException)
				{
					continue;
				}
				if (!seen.Add(full)) continue;
				try
				{
					var info = new FileInfo(full);
					if (!info.Exists || info.Length is 0 or >= MaxBytes) continue;
					var bytes = File.ReadAllBytes(full);
					index.Add(new()
					{
						Path = full,
						Sha1 = ChimeraEngine.Sha1Hex(bytes),
						Length = bytes.LongLength,
					});
				}
				catch (IOException)
				{
					// not in the index, then
				}
				catch (UnauthorizedAccessException)
				{
				}
			}
			return index;
		}

	}
}
