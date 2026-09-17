using System.IO;
using System.Linq;

using Chimera.Client.Common;

namespace Chimera.Tests.Client.Common
{
	/// <summary>
	/// Issue #52: everything Chimera keeps per user can be sent somewhere other than the system
	/// drive. What is pinned is what could cost somebody their data: a move that fails leaves the
	/// old place whole, a folder with other things in it is not taken over, and a directory is
	/// never moved into or out of itself.
	/// </summary>
	[TestClass]
	public class DataDirectoryTests
	{
		private string _root = "";

		[TestInitialize]
		public void MakeRoot()
		{
			_root = Path.Combine(Path.GetTempPath(), "chimera-datadir-" + Path.GetRandomFileName());
			Directory.CreateDirectory(_root);
		}

		[TestCleanup]
		public void RemoveRoot()
		{
			if (Directory.Exists(_root)) Directory.Delete(_root, recursive: true);
		}

		private string Dir(string name) => Path.Combine(_root, name);

		private string MakeDataHome(string name)
		{
			var home = Dir(name);
			Directory.CreateDirectory(Path.Combine(home, "Projects", "ABCD"));
			File.WriteAllBytes(Path.Combine(home, "Projects", "ABCD", "history.bin"), new byte[5000]);
			Directory.CreateDirectory(Path.Combine(home, "Recovery", "ABCD"));
			File.WriteAllText(Path.Combine(home, "Recovery", "ABCD", "journal"), "unsaved work");
			File.WriteAllText(Path.Combine(home, "cache-locks.json"), "{}");
			return home;
		}

		private static string[] Listing(string dir)
			=> Directory.EnumerateFileSystemEntries(dir, "*", SearchOption.AllDirectories)
				.Select(p => p.Substring(dir.Length)).OrderBy(static p => p, StringComparer.Ordinal).ToArray();

		[TestMethod]
		public void AMoveWithinAVolumeCarriesEverythingAndLeavesNothingBehind()
		{
			var from = MakeDataHome("old");
			var before = Listing(from);
			Assert.IsNull(DataDirectory.Move(from, Dir("new")));
			CollectionAssert.AreEqual(before, Listing(Dir("new")));
			Assert.IsFalse(Directory.Exists(from), "an emptied data directory is removed");
			Assert.AreEqual("unsaved work", File.ReadAllText(Path.Combine(Dir("new"), "Recovery", "ABCD", "journal")));
		}

		[TestMethod]
		public void AMoveBetweenVolumesIsACopyThatIsCheckedBeforeTheOriginalGoes()
		{
			var from = MakeDataHome("old");
			var before = Listing(from);
			var stamp = File.GetLastWriteTimeUtc(Path.Combine(from, "Projects", "ABCD", "history.bin"));
			long last = 0, total = 0;
			// a rename that always refuses is what another volume looks like
			Assert.IsNull(DataDirectory.Move(from, Dir("new"), (done, all) => { last = done; total = all; },
				rename: static (_, _) => throw new IOException("not the same volume")));
			CollectionAssert.AreEqual(before, Listing(Dir("new")));
			Assert.IsFalse(Directory.Exists(from));
			Assert.AreEqual(total, last, "progress ends at the whole");
			Assert.IsTrue(total > 5000);
			// to the second: not every file system keeps more of a time than that
			var carried = File.GetLastWriteTimeUtc(Path.Combine(Dir("new"), "Projects", "ABCD", "history.bin"));
			Assert.IsTrue(Math.Abs((carried - stamp).TotalSeconds) < 2, "the cache manager reads ages from the files' times");
		}

		[TestMethod]
		public void AMoveThatFailsLeavesTheOldPlaceWholeAndTheNewOneEmpty()
		{
			var from = MakeDataHome("old");
			var before = Listing(from);
			var renames = 0;
			// the first entry goes over, the second refuses for good: a failure half way
			var why = DataDirectory.Move(from, Dir("new"), rename: (a, b) =>
			{
				if (a.StartsWith(Dir("new"), StringComparison.Ordinal)) { Directory.Move(a, b); return; } // the undo
				if (++renames is 1) { if (Directory.Exists(a)) Directory.Move(a, b); else File.Move(a, b); return; }
				throw new UnauthorizedAccessException("refused");
			});
			Assert.AreEqual("refused", why);
			CollectionAssert.AreEqual(before, Listing(from), "what was moved is put back");
			Assert.AreEqual(0, Directory.EnumerateFileSystemEntries(Dir("new")).Count(), "and nothing is left in the new place");
		}

		[TestMethod]
		public void AMoveThatIsCancelledIsUndone()
		{
			var from = MakeDataHome("old");
			var before = Listing(from);
			var asked = 0;
			Assert.AreEqual("cancelled", DataDirectory.Move(from, Dir("new"), cancelled: () => ++asked > 1));
			CollectionAssert.AreEqual(before, Listing(from));
		}

		[TestMethod]
		public void WhatIsAlreadyInTheNewPlaceIsNeverWrittenOver()
		{
			var from = MakeDataHome("old");
			Directory.CreateDirectory(Path.Combine(Dir("new"), "Projects"));
			StringAssert.Contains(DataDirectory.Move(from, Dir("new"))!, "already there");
			Assert.IsTrue(File.Exists(Path.Combine(from, "cache-locks.json")));
		}

		[TestMethod]
		public void AFolderWithOtherThingsInItIsNotTakenOver()
		{
			Directory.CreateDirectory(Dir("documents"));
			File.WriteAllText(Path.Combine(Dir("documents"), "thesis.doc"), "");
			Assert.AreEqual(Path.Combine(Dir("documents"), "Chimera"), DataDirectory.TargetFor(Dir("documents")));
			Assert.AreEqual(Dir("fresh"), DataDirectory.TargetFor(Dir("fresh")), "an empty or new folder is used as it is");
			var existing = MakeDataHome("moved-earlier");
			Assert.AreEqual(existing, DataDirectory.TargetFor(existing), "and so is one that already holds Chimera's data");
		}

		[TestMethod]
		public void ADirectoryIsNotMovedIntoOrOutOfItself()
		{
			var home = MakeDataHome("home");
			Assert.AreEqual(DataDirectoryChoice.Same, DataDirectory.Inspect(home, home + Path.DirectorySeparatorChar));
			Assert.AreEqual(DataDirectoryChoice.Nested, DataDirectory.Inspect(home, Path.Combine(home, "Projects", "elsewhere")));
			Assert.AreEqual(DataDirectoryChoice.Nested, DataDirectory.Inspect(home, _root));
			Assert.AreEqual(DataDirectoryChoice.Empty, DataDirectory.Inspect(home, Dir("home2")), "a name that merely starts the same is another directory");
			Assert.AreEqual(DataDirectoryChoice.HoldsChimeraData, DataDirectory.Inspect(home, MakeDataHome("other")));
		}

		[TestMethod]
		public void APendingChangeIsCarriedOutOnceAndAFailedOneChangesNothing()
		{
			var from = MakeDataHome("old");
			Config config = new() { DataDirectory = from, DataDirectoryPending = Dir("new"), DataDirectoryPendingMove = true };
			Assert.IsNull(DataDirectory.ApplyPending(config, static (a, b) => DataDirectory.Move(a, b)));
			Assert.AreEqual(Dir("new"), config.DataDirectory);
			Assert.IsNull(config.DataDirectoryPending, "it does not happen again at the start after");
			Assert.IsTrue(File.Exists(Path.Combine(Dir("new"), "cache-locks.json")));

			config.DataDirectoryPending = Dir("third");
			config.DataDirectoryPendingMove = true;
			StringAssert.Contains(DataDirectory.ApplyPending(config, static (_, _) => "the disk is full")!, "the disk is full");
			Assert.AreEqual(Dir("new"), config.DataDirectory, "a move that did not happen does not repoint anything");
			Assert.IsNull(config.DataDirectoryPending);

			// without carrying: the new place is adopted and the old one is left alone
			config.DataDirectoryPending = Dir("fourth");
			config.DataDirectoryPendingMove = false;
			Assert.IsNull(DataDirectory.ApplyPending(config, static (_, _) => throw new InvalidOperationException("must not move")));
			Assert.AreEqual(Dir("fourth"), config.DataDirectory);
			Assert.IsTrue(File.Exists(Path.Combine(Dir("new"), "cache-locks.json")));
		}

		[TestMethod]
		[DoNotParallelize] // the environment and the setting are the process's, not the test's
		public void TheSettingMovesEverythingTheResolverIsAskedFor()
		{
			var env = Environment.GetEnvironmentVariable("CHIMERA_DATA_HOME");
			try
			{
				Environment.SetEnvironmentVariable("CHIMERA_DATA_HOME", null);
				ProjectCache.CustomDataHome = Dir("custom");
				Assert.AreEqual(Dir("custom"), ProjectCache.DataHome);
				Assert.AreEqual(Path.Combine(Dir("custom"), "Cores"), CoreStore.Path, "the core store follows, and is not remembered from before");
				StringAssert.StartsWith(CacheStore.PrecompiledCode, Dir("custom"));
				StringAssert.StartsWith(CrashCapture.Root, Dir("custom"));

				Environment.SetEnvironmentVariable("CHIMERA_DATA_HOME", Dir("portable"));
				Assert.AreEqual(Dir("portable"), ProjectCache.DataHome, "the environment still wins: whoever set it meant it");
				Assert.IsTrue(ProjectCache.DecidedByEnvironment);
			}
			finally
			{
				ProjectCache.CustomDataHome = "";
				Environment.SetEnvironmentVariable("CHIMERA_DATA_HOME", env);
			}
		}
	}
}
