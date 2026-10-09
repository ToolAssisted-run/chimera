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
			static string When(string iso) => CoreVersionDates.Format(CoreVersionDates.Parse(iso)!.Value);
			StringAssert.Contains(lines[0], $"{When("2026-09-17T12:00:00Z")}  (e0c04b2c)");
			StringAssert.Contains(lines[1], $"{When("2026-09-10T12:00:00Z")}  (a179a04f)");
			StringAssert.Contains(lines[2], $"{When("2026-09-01T12:00:00Z")}  (8750be5a)");
			StringAssert.Contains(lines[3], "quickernes");
			StringAssert.Contains(lines[3], $"{When("2026-08-20T12:00:00Z")}  (0eebbf6d)");
			Assert.AreEqual(0, form.CoreChoiceIndex, "the picker opens on the latest");
		}

		/// <summary>
		/// Issue #231: a core's line said its systems first and its version last, and a core
		/// with twenty-nine systems has a line longer than the box - so the version, which is
		/// what tells two entries of one core apart, was the part that fell off the end.
		/// </summary>
		[TestMethod]
		public void TheVersionComesBeforeTheSystems()
		{
			DiscoveredCorePackage many = new()
			{
				Name = "ares", Version = "bc8bb5b01234", VersionDate = CoreVersionDates.Parse("2026-10-08T12:00:00Z"),
				Path = "/cores/ares.chimeraCore", Sha1 = "bc8bb5b0".PadRight(40, '0'), Systems = [ "N64", "NES", "ZXS" ],
			};
			using NewProjectWizard form = new([ many ], static _ => [ ]);
			var line = form.CoreChoiceLines[0];

			var name = line.IndexOf("ares", System.StringComparison.Ordinal);
			var version = line.IndexOf("(bc8bb5b0)", System.StringComparison.Ordinal);
			var systems = line.IndexOf(many.SystemsSpelled, System.StringComparison.Ordinal);
			Assert.IsTrue(name >= 0 && version > name, $"the version follows the name: {line}");
			Assert.IsTrue(systems > version, $"and the systems follow the version: {line}");
		}

		private static DiscoveredCorePackage Game(string name)
			=> new() { Name = name, Version = "1a2b3c4d5e6f", Path = $"/cores/{name}.chimeraCore", Sha1 = "1".PadRight(40, '0'), Systems = [ "PoP" ], IsGameCore = true };

		[TestMethod]
		public void TheKindComesFirstAndThePickerListsOnlyThatKind()
		{
			// docs/game-cores.md: Kind: Emulator / Game above the core, where the picker used to
			// list both with a divider line (user-decided, 2026-09-29)
			List<DiscoveredCorePackage> cores = [ Game("SDLPoP"), Build("quickernes", "0eebbf6d4e5f", "2026-08-20T12:00:00Z") ];
			using NewProjectWizard form = new(cores, static _ => [ ]);
			Assert.AreEqual(CoreKindFilter.Emulators, form.Kind, "an emulator unless asked otherwise");
			Assert.AreEqual(1, form.CoreChoiceLines.Count);
			StringAssert.Contains(form.CoreChoiceLines[0], "quickernes");
			Assert.AreEqual(0, form.CoreChoiceIndex);

			form.ChooseKindForTest(CoreKindFilter.Games);
			Assert.AreEqual(1, form.CoreChoiceLines.Count);
			StringAssert.Contains(form.CoreChoiceLines[0], "SDLPoP");
			Assert.AreEqual(0, form.CoreChoiceIndex, "the other kind's first core is chosen");
			Assert.AreEqual("SDLPoP", form.ChosenCoreName);
		}

		[TestMethod]
		public void TheWizardOpensOnTheKindAskedForWhenThereIsOne()
		{
			List<DiscoveredCorePackage> both = [ Game("SDLPoP"), Build("quickernes", "0eebbf6d4e5f", "2026-08-20T12:00:00Z") ];
			using (NewProjectWizard form = new(both, static _ => [ ], kind: CoreKindFilter.Games))
			{
				Assert.AreEqual(CoreKindFilter.Games, form.Kind);
				Assert.AreEqual("SDLPoP", form.ChosenCoreName);
			}
			// no game core installed: the emulators, whatever was asked
			using (NewProjectWizard form = new([ Build("quickernes", "0eebbf6d4e5f", "2026-08-20T12:00:00Z") ], static _ => [ ], kind: CoreKindFilter.Games))
			{
				Assert.AreEqual(CoreKindFilter.Emulators, form.Kind);
				Assert.AreEqual("quickernes", form.ChosenCoreName);
			}
		}

		[TestMethod]
		public void AVersionNobodyCanDateStillReadsAsItsCommit()
		{
			using NewProjectWizard form = new([ Build("dosbox-x", "12d65377b7d3-dirty+local", null) ], static _ => [ ]);
			StringAssert.Contains(form.CoreChoiceLines[0], "12d65377 local");
		}
	}
}
