#nullable enable

using System;
using System.IO;
using System.Linq;

using Chimera.Common.PathExtensions;

namespace Chimera.Client.Common
{
	/// <summary>
	/// Where the caches that are not a project's live: unpacked core packages,
	/// and the code cores compiled for a game (docs/compile-cache.md).
	///
	/// Under the user's data directory, beside the core store and the project
	/// caches, and never beside the executable. A Chimera bundle is a zip
	/// somebody unpacks, and updating it means unpacking a newer one - anything
	/// that grows inside it is lost on every update and, worse, makes the install
	/// directory a thing whose size depends on what has been played. A PS3 game's
	/// compiled code alone runs to gigabytes. The bundle should be exactly what
	/// was downloaded; what a session works out goes in the user's own data.
	///
	/// The two kinds are two directories rather than one. They used to share
	/// <c>&lt;exe&gt;/CoreCache</c>, which meant a survey that walked it for
	/// packages also found the compiled-code directories and listed each core's
	/// name as an unpacked package - one directory holding two layouts cannot be
	/// read back without guessing.
	/// </summary>
	public static class CacheStore
	{
		/// <summary>Core packages unzipped so they can be loaded: <c>&lt;name&gt;-&lt;sha1&gt;</c>.</summary>
		public static string UnpackedCores => Path.Combine(ProjectCache.DataHome, "UnpackedCores");

		/// <summary>What cores compiled for a game: <c>&lt;core&gt;/&lt;package version&gt;</c>.</summary>
		public static string CompiledCode => Path.Combine(ProjectCache.DataHome, "CompiledCode");

		/// <summary>
		/// Where a game's precompiled code lives now: one directory per game,
		/// named by the game's own SHA1 and nothing else (user-decided,
		/// 2026-09-17).
		///
		/// The game's hash is the whole key. Neither the core's name nor the
		/// package version is in the path, so a game has ONE directory however
		/// many times the core is rebuilt - the old layout kept one per package
		/// version and quietly accumulated them, five of them and 231 MB on the
		/// machine this was decided on. What that layout bought - never reading
		/// objects an older build compiled - is bought instead by recording the
		/// core and version INSIDE the manifest and recompiling when they do
		/// not match, which costs a rebuild rather than a directory forever.
		///
		/// Objects are not shared between games: each game keeps its own copy of
		/// everything it needs, firmware libraries included. They are 12 MB
		/// against a game's own 47 MB, and the price of that duplication is one
		/// directory that can be deleted whole, with nothing else depending on
		/// what is inside it.
		/// </summary>
		public static string PrecompiledCode
			=> Path.Combine(ProjectCache.DataHome, "Cache", "PrecompiledCode");

		/// <summary>This game's precompiled code, by its SHA1. Null when there is no hash to file it under.</summary>
		public static string? PrecompiledCodeFor(string? gameSha1)
			=> gameSha1 is { Length: > 0 } sha1 ? Path.Combine(PrecompiledCode, sha1.ToUpperInvariant()) : null;

		/// <summary>Where both of them used to be, together, inside the install.</summary>
		public static string Legacy => Path.Combine(PathUtils.ExeDirectoryPath, "CoreCache");

		/// <summary>
		/// Moves what an older Chimera left in the install directory to where it
		/// belongs now, and takes the old directory away.
		///
		/// MOVED rather than deleted: it is all regenerable, but a PS3 game's
		/// compiled code is an hour of somebody's evening, and a rename is free.
		/// The two layouts are told apart the way they are named - a package is
		/// <c>&lt;name&gt;-&lt;40 hex&gt;</c>, and nothing else at that level is -
		/// so each half goes to its own new root. Whatever will not move is left
		/// where it is and the directory stays: this is housekeeping, and
		/// housekeeping that throws is worse than housekeeping that waits.
		/// </summary>
		public static void AdoptLegacy() => AdoptLegacy(Legacy, UnpackedCores, CompiledCode);

		/// <summary>
		/// The same, told where to move from and to. Every root is an argument so
		/// that what this does to a directory can be checked against a temporary
		/// one - a routine that moves somebody's install about is not one to test
		/// only against the real thing.
		/// </summary>
		public static void AdoptLegacy(string from, string toUnpackedCores, string toCompiledCode)
		{
			try
			{
				if (!Directory.Exists(from)) return;
				foreach (var dir in Directory.EnumerateDirectories(from).ToList())
				{
					var name = Path.GetFileName(dir);
					var root = LooksLikeAPackage(name) ? toUnpackedCores : toCompiledCode;
					Directory.CreateDirectory(root);
					var to = Path.Combine(root, name);
					// something already there is the newer answer by definition:
					// nothing writes to the old place any more
					if (Directory.Exists(to)) Directory.Delete(dir, recursive: true);
					else Directory.Move(dir, to);
				}
				// only when it is empty, so a file somebody put in it by hand is
				// not swept up with the caches
				if (Directory.EnumerateFileSystemEntries(from).Any()) return;
				Directory.Delete(from);
				Console.WriteLine($"[cache] moved what was in {from} out of the install directory");
			}
			catch (Exception ex) when (ex is IOException or UnauthorizedAccessException)
			{
				Console.WriteLine($"[cache] could not clear {from}: {ex.Message}");
			}
		}

		private static bool LooksLikeAPackage(string name)
		{
			var cut = name.LastIndexOf('-');
			if (cut <= 0) return false;
			var suffix = name.Substring(cut + 1);
			return suffix.Length is 40
				&& suffix.All(static c => c is (>= '0' and <= '9') or (>= 'a' and <= 'f') or (>= 'A' and <= 'F'));
		}
	}
}
