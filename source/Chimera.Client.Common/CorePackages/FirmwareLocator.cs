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

		/// <summary>a file that satisfies a requirement, and which candidate it matched</summary>
		public sealed class Match
		{
			public string Path { get; init; } = "";
			public string Sha1 { get; init; } = "";
			public int CandidateIndex { get; init; }
		}

		/// <summary>files this large or larger are never firmware; hashing them would only hurt</summary>
		public const long MaxBytes = 64 * 1024 * 1024;

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

		/// <summary>
		/// Every indexed file that satisfies the requirement: hash equals one of
		/// the candidates' - the name plays no part.
		/// </summary>
		public static IReadOnlyList<Match> MatchesFor(CoreFirmwareDecl decl, IReadOnlyList<IndexedFile> index)
		{
			List<Match> matches = new();
			var candidates = decl.AllCandidates;
			foreach (var file in index)
			{
				for (var i = 0; i < candidates.Count; i++)
				{
					if (!file.Sha1.Equals(candidates[i].Sha1, StringComparison.OrdinalIgnoreCase)) continue;
					matches.Add(new() { Path = file.Path, Sha1 = file.Sha1, CandidateIndex = i });
					break;
				}
			}
			return matches;
		}
	}
}
