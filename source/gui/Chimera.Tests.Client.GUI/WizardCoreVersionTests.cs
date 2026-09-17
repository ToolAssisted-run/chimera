using System.Collections.Generic;

using Chimera.Client.Common;
using Chimera.Client.GUI;

namespace Chimera.Tests.Client.GUI
{
	/// <summary>
	/// Issue #67: several versions of one core in the New Project picker said their commits and
	/// nothing about which was newer. They are dated now, the newest of a core is on top, and the
	/// top is what the picker opens on.
	/// </summary>
	[TestClass]
	public class WizardCoreVersionTests
	{
		private static DiscoveredCorePackage Build(string name, string version, string? date)
			=> new()
			{
				Name = name, Version = version, VersionDate = CoreVersionDates.Parse(date),
				Path = $"/cores/{name}-{version}.chimeraCore", Sha1 = version.PadRight(40, '0'), Systems = [ "DOS" ],
			};

		[TestMethod]
		public void VersionsAreDatedNewestOnTopAndTheTopIsChosen()
		{
			List<DiscoveredCorePackage> cores =
			[
				Build("dosbox-x", "8750be5a1c2d", "2026-09-01T12:00:00Z"),
				Build("dosbox-x", "e0c04b2c91c7", "2026-09-17T12:00:00Z"),
				Build("quickernes", "0eebbf6d4e5f", "2026-08-20T12:00:00Z"),
				Build("dosbox-x", "a179a04f0000", "2026-09-10T12:00:00Z"),
			];
			using NewProjectWizard form = new(cores, static _ => [ ]);
			var lines = form.CoreChoiceLines;

			Assert.AreEqual(4, lines.Count);
			StringAssert.Contains(lines[0], "2026-09-17  (e0c04b2c)");
			StringAssert.Contains(lines[1], "2026-09-10  (a179a04f)");
			StringAssert.Contains(lines[2], "2026-09-01  (8750be5a)");
			StringAssert.Contains(lines[3], "quickernes");
			StringAssert.Contains(lines[3], "2026-08-20  (0eebbf6d)");
			Assert.AreEqual(0, form.CoreChoiceIndex, "the picker opens on the latest");
		}

		[TestMethod]
		public void AVersionNobodyCanDateStillReadsAsItsCommit()
		{
			CoreVersionDates.Refresh();
			using NewProjectWizard form = new([ Build("dosbox-x", "12d65377b7d3-dirty+local", null) ], static _ => [ ]);
			StringAssert.Contains(form.CoreChoiceLines[0], "12d65377 local");
		}
	}
}
