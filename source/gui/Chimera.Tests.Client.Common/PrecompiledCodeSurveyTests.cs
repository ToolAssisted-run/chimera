using System;
using System.IO;
using System.Linq;

using Chimera.Client.Common;

namespace Chimera.Tests.Client.Common
{
	/// <summary>
	/// What the pre-compiled modules window lists, and the rules that make such
	/// a window safe to offer: every row can be removed, each row is one whole
	/// directory, and a row nothing can name is still a row.
	/// </summary>
	[TestClass]
	public class PrecompiledCodeSurveyTests
	{
		private string _dir = "";

		private string Root => Path.Combine(_dir, "PrecompiledCode");

		[TestInitialize]
		public void MakePlayground()
		{
			_dir = Path.Combine(Path.GetTempPath(), $"chimera-precompiled-{Guid.NewGuid():N}");
			Directory.CreateDirectory(_dir);
		}

		[TestCleanup]
		public void RemovePlayground()
		{
			if (Directory.Exists(_dir)) Directory.Delete(_dir, recursive: true);
		}

		/// <summary>A game's directory: the objects it lists, and the manifest that vouches for them.</summary>
		private string Game(string sha1, string romName, string core, string version, params string[] objects)
		{
			var dir = Path.Combine(Root, sha1);
			Directory.CreateDirectory(dir);
			CoreCacheManifest manifest = new()
			{
				RomName = romName,
				RomSha1 = sha1,
				CoreName = core,
				CoreVersion = version,
				Compiled = new DateTime(2026, 9, 17, 5, 0, 0, DateTimeKind.Utc),
				Files = objects.Select(o => new CoreCacheFile { Name = o, Sha1 = new string('A', 40) }).ToList(),
			};
			foreach (var o in objects)
			{
				var file = Path.Combine(dir, o.Replace('/', Path.DirectorySeparatorChar));
				Directory.CreateDirectory(Path.GetDirectoryName(file)!);
				File.WriteAllText(file, "compiled bytes");
			}
			manifest.Save(dir);
			return dir;
		}

		[TestMethod]
		public void AGameIsListedByWhatItIsAndWhatItWeighs()
		{
			Game("AA11", "Prince of Persia.iso", "RPCS3", "d2be7a387e86", "cache/BLUS30214/a.obj.gz", "cache/ppu-lib/b.obj.gz");

			var row = PrecompiledCodeSurvey.Take(Root).Single();

			Assert.AreEqual("AA11", row.GameSha1, "the directory's name is the game's hash");
			Assert.AreEqual("Prince of Persia.iso", row.Label, "and a person sees the game, not the hash");
			Assert.AreEqual(2, row.Modules);
			Assert.IsTrue(row.Bytes > 0, "what it weighs is what the window is for");
			Assert.IsTrue(row.Complete, "every object it lists is there");
			Assert.IsFalse(row.Unknown);
			StringAssert.Contains(row.By, "RPCS3", "and which core compiled it");
			Assert.IsNotNull(row.Compiled, "and when");
		}

		/// <summary>
		/// The case the old layout could not express: a compile that stopped
		/// half way leaves objects and no manifest. It must still be listed, or
		/// it is space nobody can find and nobody can reclaim.
		/// </summary>
		[TestMethod]
		public void LeftoversNothingCanNameAreStillListedAndStillRemovable()
		{
			var orphan = Path.Combine(Root, "BB22");
			Directory.CreateDirectory(orphan);
			File.WriteAllText(Path.Combine(orphan, "half-a-compile.obj.gz"), "bytes");

			var row = PrecompiledCodeSurvey.Take(Root).Single();
			Assert.IsTrue(row.Unknown, "nothing says what it is");
			Assert.AreEqual("BB22", row.Label, "so it is named by the only thing known about it");
			Assert.AreNotEqual("", row.Note, "and the window says why it looks like that");
			Assert.IsTrue(row.Bytes > 0);

			PrecompiledCodeSurvey.Remove(row);
			Assert.IsFalse(Directory.Exists(orphan), "an unnameable leftover is exactly what Remove is for");
		}

		/// <summary>
		/// Removing one game cannot touch another. Games do not share objects -
		/// each keeps its own copy, firmware included (user-decided,
		/// 2026-09-17) - which is what makes a whole-directory delete safe.
		/// </summary>
		[TestMethod]
		public void RemovingOneGameLeavesTheOthersWhole()
		{
			Game("AA11", "one.iso", "RPCS3", "v1", "cache/ppu-shared/lib.obj.gz", "cache/AAA/own.obj.gz");
			Game("BB22", "two.iso", "RPCS3", "v1", "cache/ppu-shared/lib.obj.gz", "cache/BBB/own.obj.gz");

			var rows = PrecompiledCodeSurvey.Take(Root);
			Assert.AreEqual(2, rows.Count);

			PrecompiledCodeSurvey.Remove(rows.Single(r => r.GameSha1 is "AA11"));

			var left = PrecompiledCodeSurvey.Take(Root).Single();
			Assert.AreEqual("BB22", left.GameSha1);
			Assert.IsTrue(left.Complete, "the other game still has everything it needs, shared names and all");
		}

		[TestMethod]
		public void AGameMissingObjectsIsNotComplete()
		{
			var dir = Game("CC33", "three.iso", "RPCS3", "v1", "cache/a.obj.gz", "cache/b.obj.gz");
			File.Delete(Path.Combine(dir, "cache", "b.obj.gz"));

			var row = PrecompiledCodeSurvey.Take(Root).Single();
			Assert.IsFalse(row.Complete, "an object that is gone is one the game still needs");
			Assert.AreNotEqual("", row.Note, "and the window says so rather than showing a silent gap");
		}

		/// <summary>
		/// What the previous layout left is listed as one row, so that 231 MB of
		/// unreadable leftovers is something a person can see and take away
		/// rather than something that simply stays on the disk forever.
		/// </summary>
		[TestMethod]
		public void WhatTheOldLayoutLeftIsOfferedAsOneRow()
		{
			Game("AA11", "one.iso", "RPCS3", "v1", "cache/a.obj.gz");
			var legacy = Path.Combine(_dir, "CompiledCode");
			Directory.CreateDirectory(Path.Combine(legacy, "RPCS3", "da12f66"));
			File.WriteAllText(Path.Combine(legacy, "RPCS3", "da12f66", "old.obj.gz"), "bytes from the old layout");

			var rows = PrecompiledCodeSurvey.Take(Root, legacy);
			Assert.AreEqual(2, rows.Count);

			var old = rows.Single(static r => r.Legacy);
			Assert.AreEqual(rows[^1], old, "it sits at the end: it is not one of the games");
			Assert.IsTrue(old.Bytes > 0);
			StringAssert.Contains(old.Note, "Nothing reads it now", "the row says why it is safe to remove");
			StringAssert.Contains(old.By, "no longer used", "and the column says what it is");

			PrecompiledCodeSurvey.Remove(old);
			Assert.IsFalse(Directory.Exists(legacy));
		}

		[TestMethod]
		public void AnAbsentOrUnreadableRootIsNoRowsRatherThanAnError()
		{
			Assert.AreEqual(0, PrecompiledCodeSurvey.Take(Path.Combine(_dir, "never-existed")).Count);
			Assert.AreEqual(0, PrecompiledCodeSurvey.Take(null).Count);
			Assert.AreEqual(0, PrecompiledCodeSurvey.Take(Root, legacyRoot: Path.Combine(_dir, "nor-this")).Count);
		}

		/// <summary>Newest first, because what somebody wants is nearly always what they did last.</summary>
		[TestMethod]
		public void TheMostRecentlyCompiledGameComesFirst()
		{
			var older = Game("AA11", "older.iso", "RPCS3", "v1", "cache/a.obj.gz");
			Game("BB22", "newer.iso", "RPCS3", "v1", "cache/b.obj.gz");

			var manifest = CoreCacheManifest.Load(older)!;
			manifest.Compiled = new DateTime(2026, 1, 1, 0, 0, 0, DateTimeKind.Utc);
			manifest.Save(older);

			var rows = PrecompiledCodeSurvey.Take(Root);
			Assert.AreEqual("newer.iso", rows[0].Label);
			Assert.AreEqual("older.iso", rows[1].Label);
		}
	}
}
