#nullable enable

using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;

using Chimera.Emulation.Common.Engine;

namespace Chimera.Client.Common
{
	/// <summary>
	/// Resolves a project's unresolved files out of a folder the user points
	/// at: "my stuff is somewhere in here" instead of one Locate dialog per
	/// file. The SHA1 is the identity, so an exact-hash match resolves a file
	/// no matter what it is called on disk; a file that merely bears the
	/// recorded NAME is taken only when no exact match exists anywhere in the
	/// folder, and the mismatch verdict lands in the row for the user to
	/// judge - the same honesty the per-file Locate button has.
	/// </summary>
	public static class ProjectFolderScan
	{
		/// <summary>
		/// Stop enumerating after this many files: a scan pointed at a drive
		/// root must end, not index the drive.
		/// </summary>
		public const int MaxFiles = 10_000;

		/// <summary>
		/// Resolves what it can; already-resolved files are left alone.
		/// Returns how many files changed state. Hashing is dominated by file
		/// size, so candidates are tried smallest-first and the scan stops the
		/// moment every file is resolved.
		/// </summary>
		public static int Resolve(EngineProject project, string folder)
		{
			List<int> wanted = new();
			for (var i = 0; i < project.FileCount; i++)
			{
				if (project.FileStatus(i) is not 0) wanted.Add(i);
			}
			if (wanted.Count is 0 || !Directory.Exists(folder)) return 0;

			var candidates = Enumerate(folder).Take(MaxFiles)
				.Select(static path =>
				{
					try
					{
						return (Path: path, Length: new FileInfo(path).Length);
					}
					catch (IOException) { return (Path: path, Length: -1L); }
					catch (UnauthorizedAccessException) { return (Path: path, Length: -1L); }
				})
				.Where(static c => c.Length >= 0)
				.OrderBy(static c => c.Length)
				.ToList();

			var resolved = 0;

			// Pass 1: exact hashes. Each candidate is hashed once (streamed - a
			// disc image does not become a byte array) and claims every entry
			// whose recorded SHA1 it equals.
			var bySha = wanted
				.GroupBy(i => project.FileSha1(i), StringComparer.OrdinalIgnoreCase)
				.ToDictionary(static g => g.Key, static g => g.ToList(), StringComparer.OrdinalIgnoreCase);
			foreach (var (path, _) in candidates)
			{
				if (bySha.Count is 0) break;
				var sha = Sha1Of(path);
				if (sha is null || !bySha.TryGetValue(sha, out var entries)) continue;
				foreach (var index in entries)
				{
					try
					{
						project.FileResolve(index, path);
						resolved++;
					}
					catch (InvalidOperationException) { /* unreadable now; leave it */ }
				}
				bySha.Remove(sha);
			}

			// Pass 2: recorded names, for whatever no hash claimed. The verdict
			// (match or MISMATCH) is the engine's, and it shows in the row.
			foreach (var index in wanted)
			{
				if (project.FileStatus(index) is 0) continue;
				var name = Path.GetFileName(project.FileName(index));
				var named = candidates.FirstOrDefault(
					c => Path.GetFileName(c.Path).Equals(name, StringComparison.OrdinalIgnoreCase));
				if (named.Path is null) continue;
				try
				{
					project.FileResolve(index, named.Path);
					resolved++;
				}
				catch (InvalidOperationException) { }
			}

			return resolved;
		}

		/// <summary>every file under the folder, walking subfolders, skipping what cannot be listed</summary>
		public static IEnumerable<string> Enumerate(string folder)
		{
			Queue<string> dirs = new();
			dirs.Enqueue(folder);
			while (dirs.Count is not 0)
			{
				var dir = dirs.Dequeue();
				string[] files;
				try
				{
					files = Directory.GetFiles(dir);
					foreach (var sub in Directory.GetDirectories(dir)) dirs.Enqueue(sub);
				}
				catch (IOException) { continue; }
				catch (UnauthorizedAccessException) { continue; }
				foreach (var f in files) yield return f;
			}
		}

		/// <summary>uppercase hex, matching the engine's; null when unreadable</summary>
		public static string? Sha1Of(string path)
		{
			try
			{
				using var stream = File.OpenRead(path);
				using var sha1 = System.Security.Cryptography.SHA1.Create();
				var digest = sha1.ComputeHash(stream);
				return string.Concat(Array.ConvertAll(digest, static b => b.ToString("X2")));
			}
			catch (IOException) { return null; }
			catch (UnauthorizedAccessException) { return null; }
		}
	}
}
