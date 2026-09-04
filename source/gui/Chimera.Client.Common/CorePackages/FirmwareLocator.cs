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
		/// The file that answers a requirement that pins no hash - a dump only
		/// the user can own, a disk image every console writes differently - by
		/// the name the core says it goes by. A name is a weak identity, which is
		/// why it is only ever asked for when there is no hash to ask by; what
		/// the file turns out to be is recorded from its bytes, not from this.
		/// </summary>
		public static IndexedFile? FindNamed(CoreFirmwareDecl decl, IReadOnlyList<IndexedFile> index)
			=> string.IsNullOrEmpty(decl.Name)
				? null
				: index.FirstOrDefault(f =>
					Path.GetFileName(f.Path).Equals(decl.Name, StringComparison.OrdinalIgnoreCase));

		/// <summary>
		/// Whichever of the two answers this requirement: the hash when it pins
		/// one, the declared name when it does not.
		/// </summary>
		public static IndexedFile? FindEither(CoreFirmwareDecl decl, IReadOnlyList<IndexedFile> index)
			=> string.IsNullOrEmpty(decl.Sha1) ? FindNamed(decl, index) : FindFor(decl, index);

		/// <summary>
		/// Files this large or larger are never firmware, because the frontend
		/// could not hand one to a core if it were: firmware crosses into the
		/// sandbox as bytes, and a byte array stops here.
		///
		/// The bar was a size, and every size chosen was wrong about some real
		/// machine. 64 MiB was smaller than a PlayStation 3 system software
		/// update (206 MB); a gigabyte, chosen to be "above every firmware and
		/// below any disc image", was smaller than the Xbox hard disk image xemu
		/// declares - so a project made with one could never find it again, and
		/// said so as a hash mismatch, which is not what was wrong (issue #38).
		/// The only honest bar is what can actually be mounted.
		/// </summary>
		public const long MaxBytes = int.MaxValue;

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
					// read through rather than held: a disk image is firmware too,
					// and the engine answers a repeat with the hash it already took
					if (ChimeraEngine.Sha1OfFile(full) is not { } hashed) continue;
					index.Add(new()
					{
						Path = full,
						Sha1 = hashed.Sha1,
						Length = hashed.Length,
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
