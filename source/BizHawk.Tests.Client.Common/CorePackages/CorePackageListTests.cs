using System.Collections.Generic;
using System.Linq;

using BizHawk.Client.Common;

namespace BizHawk.Tests.Client.Common.CorePackages
{
	/// <summary>
	/// The Core Packages window is a renderer for this list, so what it tells the
	/// user is decided here. In particular: a package cannot be unloaded from a
	/// running session, and the states must admit that rather than pretend a
	/// switched-off core went away.
	/// </summary>
	[TestClass]
	public class CorePackageListTests
	{
		private static DiscoveredCorePackage Pkg(string name, string path, string sha1 = null, string error = null)
			=> new() { Name = name, Path = path, Sha1 = sha1, Error = error, Systems = [ "NES" ] };

		private static CoreRegistry.LoadedCorePackage Loaded(string name, string path, string sha1 = null)
			=> new() { Name = name, Path = path, Sha1 = sha1, CoreNames = [ name ] };

		private static IReadOnlyList<CorePackageListEntry> Build(
			IEnumerable<DiscoveredCorePackage> discovered,
			IEnumerable<CoreRegistry.LoadedCorePackage> loaded,
			Config config,
			IEnumerable<(DiscoveredCorePackage, string)> failures = null)
			=> CorePackageList.Build(discovered, loaded, failures ?? [ ], config);

		[TestMethod]
		public void DiscoveredAndLoadedIsSimplyLoaded()
		{
			var pkg = Pkg("quickerNES", "/cores/q.zip", sha1: "aa");
			var entry = Build([ pkg ], [ Loaded("quickerNES", "/cores/q.zip", "aa") ], new Config())[0];
			Assert.AreEqual(CorePackageState.Loaded, entry.State);
			Assert.IsTrue(entry.Enabled);
			Assert.IsFalse(entry.NeedsRestart);
		}

		[TestMethod]
		public void EnabledButNotYetLoadedLoadsOnRestart()
		{
			// the user re-enabled a package this session, or dropped one in and rescanned
			// without loading it
			var entry = Build([ Pkg("New", "/cores/n.zip", sha1: "bb") ], [ ], new Config())[0];
			Assert.AreEqual(CorePackageState.PendingLoad, entry.State);
			Assert.IsTrue(entry.Enabled);
			Assert.IsTrue(entry.NeedsRestart);
		}

		[TestMethod]
		public void DisablingSomethingAlreadyLoadedSaysSoInsteadOfLying()
		{
			var pkg = Pkg("quickerNES", "/cores/q.zip", sha1: "aa");
			Config config = new();
			CorePackageDiscovery.SetEnabled(config, pkg, false);
			var entry = Build([ pkg ], [ Loaded("quickerNES", "/cores/q.zip", "aa") ], config)[0];
			Assert.AreEqual(CorePackageState.LoadedDisabled, entry.State);
			Assert.IsFalse(entry.Enabled);
			Assert.IsTrue(entry.NeedsRestart, "the core is still in this session; only the next one is affected");
			StringAssert.Contains(entry.StatusText, "restart");
		}

		[TestMethod]
		public void DisabledAndNotLoadedIsQuietlyDisabled()
		{
			var pkg = Pkg("Old", "/cores/o.zip", sha1: "cc");
			Config config = new();
			CorePackageDiscovery.SetEnabled(config, pkg, false);
			var entry = Build([ pkg ], [ ], config)[0];
			Assert.AreEqual(CorePackageState.Disabled, entry.State);
			Assert.IsFalse(entry.NeedsRestart, "nothing to change: it is off and it is not loaded");
		}

		[TestMethod]
		public void UnreadablePackagesAreListedAsFailed()
		{
			var entry = Build([ Pkg("Broken", "/cores/b.zip", sha1: "dd", error: "waterbox.config is empty or invalid") ], [ ], new Config())[0];
			Assert.AreEqual(CorePackageState.Failed, entry.State);
			Assert.IsFalse(entry.Enabled);
			StringAssert.Contains(entry.StatusText, "waterbox.config");
		}

		[TestMethod]
		public void APackageThatFailedWhileLoadingIsFailedNotPending()
		{
			// discovery read it fine; the load blew up afterwards (a missing native, say)
			var pkg = Pkg("Fine On Paper", "/cores/f.zip", sha1: "ee");
			var entry = Build([ pkg ], [ ], new Config(), failures: [ (pkg, "declares native libfoo.so but it is missing") ])[0];
			Assert.AreEqual(CorePackageState.Failed, entry.State);
			StringAssert.Contains(entry.StatusText, "libfoo.so");
		}

		[TestMethod]
		public void PackagesLoadedFromOutsideTheSearchPathsStillAppear()
		{
			// File > Open Core, or --core on the commandline: not in Cores/, but very
			// much part of this session
			var entries = Build(
				[ Pkg("InCores", "/cores/a.zip", sha1: "aa") ],
				[ Loaded("InCores", "/cores/a.zip", "aa"), Loaded("Elsewhere", "/home/user/dev/build.zip", "ff") ],
				new Config());
			Assert.AreEqual(2, entries.Count);
			var stray = entries.Single(static e => e.Name == "Elsewhere");
			Assert.AreEqual(CorePackageState.Loaded, stray.State);
			Assert.AreEqual("/home/user/dev/build.zip", stray.Package.Path);
		}

		[TestMethod]
		public void EntriesAreNameSortedAndUnique()
		{
			var entries = Build(
				[ Pkg("Zulu", "/c/z.zip", sha1: "1"), Pkg("alpha", "/c/a.zip", sha1: "2") ],
				[ Loaded("Zulu", "/c/z.zip", "1") ], // already listed by discovery; must not double up
				new Config());
			CollectionAssert.AreEqual(new[] { "alpha", "Zulu" }, entries.Select(static e => e.Name).ToList());
		}

		[TestMethod]
		public void DirectoryPackagesAreKeyedByPathSoDisablingOneStillWorks()
		{
			var pkg = Pkg("DevBuild", "/cores/devbuild");
			Config config = new();
			CorePackageDiscovery.SetEnabled(config, pkg, false);
			CollectionAssert.Contains(config.DisabledCorePackages, "/cores/devbuild");
			Assert.AreEqual(CorePackageState.Disabled, Build([ pkg ], [ ], config)[0].State);
		}
	}
}
