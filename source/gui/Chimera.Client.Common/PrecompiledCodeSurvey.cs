#nullable enable

using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;

namespace Chimera.Client.Common
{
	/// <summary>
	/// One game's precompiled code, as it sits on disk.
	///
	/// A row is a DIRECTORY, not a manifest: a compile that was interrupted
	/// leaves objects and no manifest, and that is exactly the case somebody
	/// needs to see and be able to remove. So a directory with nothing readable
	/// in it is still a row, named by the only thing known about it - the hash
	/// it is filed under.
	/// </summary>
	public sealed class PrecompiledGame
	{
		/// <summary>The game's SHA1, which is the directory's name and the whole key.</summary>
		public string GameSha1 { get; init; } = "";

		/// <summary>The game's file name as the compile recorded it; "" when no manifest says.</summary>
		public string GameName { get; init; } = "";

		/// <summary>Which core compiled it, and which build of its package; "" when not recorded.</summary>
		public string CoreName { get; init; } = "";

		public string CoreVersion { get; init; } = "";

		/// <summary>When the sessions produced it; null when no manifest says.</summary>
		public DateTime? Compiled { get; init; }

		/// <summary>How many objects the manifest lists; 0 when there is no manifest.</summary>
		public int Modules { get; init; }

		/// <summary>What the whole directory weighs, manifest included.</summary>
		public long Bytes { get; init; }

		public string Path { get; init; } = "";

		/// <summary>
		/// Every object the manifest lists is present.
		///
		/// PRESENT, not verified: this counts files, it does not hash them. A
		/// game is hundreds of megabytes and this list is opened to look at, so
		/// hashing every object to draw a window would make opening it cost what
		/// a compile costs. The hash check is the wizard's, where it decides
		/// whether a project may be created - the place the answer has to be
		/// right. Here it only separates "finished" from "stopped half way".
		/// </summary>
		public bool Complete { get; init; }

		/// <summary>
		/// Nothing readable says what this is: no manifest, or one that will not
		/// parse. Still listed, and still removable - unnameable leftovers are
		/// the ones most worth offering to take away.
		/// </summary>
		public bool Unknown { get; init; }

		/// <summary>
		/// Not a game: what an older Chimera left in the previous layout, kept
		/// as one row so that it can be seen and removed rather than sitting
		/// there forever unreadable by anything.
		/// </summary>
		public bool Legacy { get; init; }

		/// <summary>What a person would call this row.</summary>
		public string Label => Legacy
			? "Code compiled by an older Chimera"
			: GameName.Length is not 0 ? GameName : GameSha1;

		/// <summary>The core that compiled it, in one column's worth of words.</summary>
		public string By => Legacy
			? "a layout no longer used"
			: CoreName.Length is 0
				? "not recorded"
				: CoreVersion.Length is 0 ? CoreName : $"{CoreName} {Short(CoreVersion)}";

		/// <summary>What this row is, in a sentence, for a detail line.</summary>
		public string Note => Legacy
			? "The previous layout filed compiled code by core and package build. Nothing reads it now; removing it frees the space."
			: Unknown
				? "Nothing here says which game or core this belongs to - most likely a compile that was interrupted."
				: Complete
					? ""
					: "Some of the objects this game needs are missing; it will be compiled again when a project needs it.";

		private static string Short(string s) => s.Length <= 12 ? s : s.Substring(0, 12);
	}

	/// <summary>
	/// What is in the precompiled-code store (docs/compile-cache.md), for the
	/// window that lists it and for the tests that prove what it says.
	///
	/// A model rather than a form, for the same reason the cache manager has
	/// one: what is listed, what it weighs and what removing it costs are facts
	/// about the disk, and facts about the disk should be provable without
	/// opening a window.
	/// </summary>
	public static class PrecompiledCodeSurvey
	{
		/// <summary>
		/// Every game with compiled code, newest first, and - when there is one -
		/// a final row for what the previous layout left behind.
		/// </summary>
		/// <param name="root">the precompiled-code root; <see cref="CacheStore.PrecompiledCode"/> in the running frontend</param>
		/// <param name="legacyRoot">the old per-core/per-version root, or null to ignore it</param>
		public static IReadOnlyList<PrecompiledGame> Take(string? root, string? legacyRoot = null)
		{
			List<PrecompiledGame> games = new();

			foreach (var dir in Directories(root))
			{
				var manifest = CoreCacheManifest.Load(dir);
				var present = manifest?.Files.Count(f => File.Exists(ObjectPath(dir, f.Name))) ?? 0;
				games.Add(new PrecompiledGame
				{
					GameSha1 = System.IO.Path.GetFileName(dir),
					GameName = manifest?.RomName ?? "",
					CoreName = manifest?.CoreName ?? "",
					CoreVersion = manifest?.CoreVersion ?? "",
					Compiled = manifest?.Compiled,
					Modules = manifest?.Files.Count ?? 0,
					Bytes = SizeOf(dir),
					Path = dir,
					Complete = manifest is { Files.Count: > 0 } && present == manifest.Files.Count,
					Unknown = manifest is null,
				});
			}

			// newest first: what somebody is looking for is nearly always what
			// they did last, and a row with no date is one nothing vouches for
			games = games
				.OrderByDescending(static g => g.Compiled ?? DateTime.MinValue)
				.ThenBy(static g => g.Label, StringComparer.CurrentCultureIgnoreCase)
				.ToList();

			if (legacyRoot is { Length: > 0 } && Directory.Exists(legacyRoot) && SizeOf(legacyRoot) > 0)
			{
				games.Add(new PrecompiledGame
				{
					Path = legacyRoot,
					Bytes = SizeOf(legacyRoot),
					Legacy = true,
				});
			}

			return games;
		}

		/// <summary>
		/// Takes one row away, wholly: the objects and the manifest together,
		/// because they are one directory. Nothing else can depend on what was
		/// inside it - a game keeps its own copy of everything it needs - so
		/// there is no other game to check before deleting.
		/// </summary>
		public static void Remove(PrecompiledGame game)
		{
			if (game.Path is { Length: > 0 } path && Directory.Exists(path)) Directory.Delete(path, recursive: true);
		}

		/// <summary>What the whole store weighs.</summary>
		public static long TotalBytes(IEnumerable<PrecompiledGame> games) => games.Sum(static g => g.Bytes);

		/// <summary>An object's path from the name the core filed it under.</summary>
		private static string ObjectPath(string dir, string name)
			=> System.IO.Path.Combine(dir, name.Replace('/', System.IO.Path.DirectorySeparatorChar));

		private static IEnumerable<string> Directories(string? root)
		{
			if (root is not { Length: > 0 } || !Directory.Exists(root)) yield break;
			IEnumerable<string> dirs;
			try
			{
				dirs = Directory.EnumerateDirectories(root).ToList();
			}
			catch (Exception)
			{
				yield break; // a root that cannot be read is an empty list, not a crash
			}
			foreach (var dir in dirs) yield return dir;
		}

		private static long SizeOf(string dir)
		{
			try
			{
				return new DirectoryInfo(dir).EnumerateFiles("*", SearchOption.AllDirectories).Sum(static f => f.Length);
			}
			catch (Exception)
			{
				return 0;
			}
		}
	}
}
