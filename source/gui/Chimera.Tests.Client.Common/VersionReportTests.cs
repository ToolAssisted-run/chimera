using Chimera.Client.Common;

namespace Chimera.Tests.Client.Common
{
	/// <summary>
	/// Help &gt; Copy Version Info (issue #188): the two lines a bug report starts
	/// with, in the template's own words, so they are pasted and not retyped.
	/// </summary>
	[TestClass]
	public class VersionReportTests
	{
		private const string Build = "Commit 1ab07c06c (2026-10-06 17:05 UTC)";

		[TestMethod]
		public void ARunningCoreIsNamedWithItsVersionUnderTheBuild()
			=> Assert.AreEqual(
				"**Chimera build:** Commit 1ab07c06c (2026-10-06 17:05 UTC)\n**Core and version:** RPCS3 2026-10-07 06:35  (1924ad8b)",
				VersionReport.Text(Build, "RPCS3", "2026-10-07 06:35  (1924ad8b)"));

		[TestMethod]
		public void WithNoCoreRunningThereIsOnlyTheBuild()
		{
			Assert.AreEqual("**Chimera build:** " + Build, VersionReport.Text(Build, null, null));
			Assert.AreEqual("**Chimera build:** " + Build, VersionReport.Text(Build, " ", "2026-10-07"));
		}

		[TestMethod]
		public void ACoreThatSaysNoVersionIsStillNamed()
			=> Assert.AreEqual(
				"**Chimera build:** " + Build + "\n**Core and version:** Ruffle",
				VersionReport.Text(Build, "Ruffle", ""));
	}
}
