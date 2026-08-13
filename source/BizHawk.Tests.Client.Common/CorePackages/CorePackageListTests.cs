using System.Collections.Generic;
using System.Linq;

using BizHawk.Client.Common;

namespace BizHawk.Tests.Client.Common.CorePackages
{
	/// <summary>
	/// The Core Packages window is a renderer for this list, so what it tells the
	/// user is decided here: which packages exist, which are in use, which could
	/// not be read and why.
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
			IEnumerable<(DiscoveredCorePackage, string)> failures = null)
			=> CorePackageList.Build(discovered, loaded, failures ?? [ ]);

		[TestMethod]
		public void AReadablePackageIsLoaded()
		{
			var pkg = Pkg("quickerNES", "/cores/q.zip", sha1: "aa");
			var entry = Build([ pkg ], [ Loaded("quickerNES", "/cores/q.zip", "aa") ])[0];
			Assert.AreEqual(CorePackageState.Loaded, entry.State);
			Assert.AreEqual("loaded", entry.StatusText);
		}

		[TestMethod]
		public void UnreadablePackagesAreListedAsFailedRatherThanOmitted()
		{
			// silently missing would be indistinguishable from "you forgot to copy it"
			var entry = Build([ Pkg("Broken", "/cores/b.zip", sha1: "dd", error: "waterbox.config is empty or invalid") ], [ ])[0];
			Assert.AreEqual(CorePackageState.Failed, entry.State);
			StringAssert.Contains(entry.StatusText, "waterbox.config");
		}

		[TestMethod]
		public void APackageThatFailedWhileLoadingIsFailedToo()
		{
			// discovery read it fine; the load blew up afterwards (a missing native, say)
			var pkg = Pkg("Fine On Paper", "/cores/f.zip", sha1: "ee");
			var entry = Build([ pkg ], [ ], failures: [ (pkg, "declares native libfoo.so but it is missing") ])[0];
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
				[ Loaded("InCores", "/cores/a.zip", "aa"), Loaded("Elsewhere", "/home/user/dev/build.zip", "ff") ]);
			Assert.AreEqual(2, entries.Count);
			var stray = entries.Single(static e => e.Name == "Elsewhere");
			Assert.AreEqual(CorePackageState.Loaded, stray.State);
			Assert.AreEqual("/home/user/dev/build.zip", stray.Package.Path);
		}

		[TestMethod]
		public void ADirectoryFormPackageLoadedFromElsewhereIsMarkedAsOne()
		{
			var entry = Build([ ], [ Loaded("DevBuild", "/home/user/quickerNES/waterbox/bin") ])[0];
			Assert.IsTrue(entry.Package.IsDirectoryForm, "no hash means it was a directory");
			Assert.AreEqual("-", entry.Package.ShortSha1);
		}

		[TestMethod]
		public void EntriesAreNameSortedAndUnique()
		{
			var entries = Build(
				[ Pkg("Zulu", "/c/z.zip", sha1: "1"), Pkg("alpha", "/c/a.zip", sha1: "2") ],
				[ Loaded("Zulu", "/c/z.zip", "1") ]); // already listed by discovery; must not double up
			CollectionAssert.AreEqual(new[] { "alpha", "Zulu" }, entries.Select(static e => e.Name).ToList());
		}
	}
}
