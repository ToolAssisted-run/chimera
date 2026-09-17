using System.IO;
using System.Linq;

using Chimera.Client.Common;

namespace Chimera.Tests.Client.Common.CorePackages
{
	/// <summary>
	/// Issue #67: a commit says which version a package is and nothing about which of two is
	/// newer. Pinned here: where the date comes from, how it is written, and the order versions
	/// are offered in.
	/// </summary>
	[TestClass]
	public class CoreVersionDatesTests
	{
		private static DiscoveredCorePackage Package(string name, string version, string? date = null, string path = "")
			=> new() { Name = name, Version = version, VersionDate = CoreVersionDates.Parse(date), Path = path, Sha1 = version };

		private static string FeedCache(params (string Tag, string Published, string Asset)[] releases)
		{
			var dir = Path.Combine(Path.GetTempPath(), "chimera-dates-" + Path.GetRandomFileName());
			Directory.CreateDirectory(dir);
			var body = "[" + string.Join(",", releases.Select(static r =>
				$"{{\"tag_name\":\"{r.Tag}\",\"published_at\":\"{r.Published}\",\"assets\":[{{\"name\":\"{r.Asset}\"}}]}}")) + "]";
			File.WriteAllText(Path.Combine(dir, "Org_core.json"), Newtonsoft.Json.JsonConvert.SerializeObject(new { etag = "x", body }));
			return dir;
		}

		[TestMethod]
		public void ThePackagesOwnDateIsTheDate()
		{
			var package = Package("RPCS3", "1c4a0c476ea8", "2026-09-17T08:30:00Z");
			Assert.AreEqual(new DateTimeOffset(2026, 9, 17, 8, 30, 0, TimeSpan.Zero), CoreVersionDates.Of(package));
			StringAssert.EndsWith(package.DatedVersion, "  (1c4a0c47)");
			StringAssert.StartsWith(package.DatedVersion, "2026-09-1"); // the day, in local time
		}

		[TestMethod]
		public void APackageFromBeforeTheStampIsLookedUpInWhatTheCoreManagerLastHeard()
		{
			var cache = FeedCache(
				("nightly-2026-09-17", "2026-09-17T08:36:00Z", "rpcs3-1c4a0c476ea81218c6c09d612c1f1fe1b5f71466.chimeraCore"),
				("dev", "2026-09-17T08:34:31Z", "rpcs3-1c4a0c476ea81218c6c09d612c1f1fe1b5f71466.chimeraCore"),
				("nightly-2026-09-16", "2026-09-16T09:35:28Z", "rpcs3-da12f66b5f502e879cb8576c2b02b879a8ffca2b.chimeraCore"),
				("nightly-2026-09-15", "2026-09-15T09:00:00Z", "notes.txt"));
			try
			{
				CoreVersionDates.Refresh();
				// published twice: the first time is when it appeared
				Assert.AreEqual(new DateTimeOffset(2026, 9, 17, 8, 34, 31, TimeSpan.Zero), CoreVersionDates.Published("1c4a0c476ea8", cache));
				Assert.AreEqual(new DateTimeOffset(2026, 9, 16, 9, 35, 28, TimeSpan.Zero), CoreVersionDates.Published("da12f66b5f50-dirty+local", cache));
				Assert.IsNull(CoreVersionDates.Published("0123456789ab", cache), "a commit nobody published has no date");
				Assert.IsNull(CoreVersionDates.Published("unversioned+local", cache));
			}
			finally
			{
				CoreVersionDates.Refresh();
				Directory.Delete(cache, recursive: true);
			}
		}

		[TestMethod]
		public void AVersionNobodyCanDateIsListedByItsCommitAlone()
		{
			CoreVersionDates.Refresh();
			var missing = Path.Combine(Path.GetTempPath(), "chimera-dates-none-" + Path.GetRandomFileName());
			Assert.IsNull(CoreVersionDates.Published("12d65377b7d3", missing));
			Assert.IsNull(CoreVersionDates.Parse(null));
			Assert.IsNull(CoreVersionDates.Parse("last tuesday-ish"));
			CoreVersionDates.Refresh();
		}

		[TestMethod]
		public void VersionsOfOneCoreAreOfferedNewestFirstAndCoresStayWhereTheyWere()
		{
			var packages = new[]
			{
				Package("DOSBox-X", "aaaaaaaa", "2026-09-01T00:00:00Z"),
				Package("RPCS3", "bbbbbbbb", "2026-09-10T00:00:00Z"),
				Package("DOSBox-X", "cccccccc"),                          // undated: after the dated ones
				Package("RPCS3", "dddddddd", "2026-09-17T00:00:00Z"),
				Package("DOSBox-X", "eeeeeeee", "2026-09-12T00:00:00Z"),
				Package("DOSBox-X", "ffffffff"),                          // undated: keeps its place after cccccccc
			};
			var ordered = CoreVersionDates.NewestFirst(packages, static p => p.VersionDate).Select(static p => p.Version).ToArray();
			CollectionAssert.AreEqual(
				new[] { "eeeeeeee", "aaaaaaaa", "cccccccc", "ffffffff", "dddddddd", "bbbbbbbb" },
				ordered);
		}
	}
}
