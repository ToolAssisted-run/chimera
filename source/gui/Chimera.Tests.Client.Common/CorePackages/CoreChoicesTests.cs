using Chimera.Client.Common;

namespace Chimera.Tests.Client.Common.CorePackages
{
	/// <summary>
	/// Two packages may claim the same system - a fast core and an accurate one - so the frontend
	/// remembers which one a system's roms open with. Opening a package is what says so; these cover
	/// what that records.
	/// </summary>
	[TestClass]
	public class CoreChoicesTests
	{
		[TestMethod]
		public void OpeningACoreRecordsItAgainstTheSystem()
		{
			Config config = new();
			Assert.IsTrue(CoreChoices.MakeDefault(config, "NES", "QuickerNesHawk"));
			Assert.AreEqual("QuickerNesHawk", config.DefaultCores["NES"]);
		}

		[TestMethod]
		public void OpeningTheCoreAlreadyChosenChangesNothing()
		{
			// the caller uses this to skip a reload, which would otherwise throw away the session
			Config config = new();
			config.DefaultCores["NES"] = "quickerNES";
			Assert.IsFalse(CoreChoices.MakeDefault(config, "NES", "quickerNES"));
		}

		[TestMethod]
		public void OpeningACoreReplacesTheSystemsPreviousOne()
		{
			// this is the whole selection story: open the other package and its cores take over
			Config config = new();
			CoreChoices.MakeDefault(config, "NES", "quickerNES");
			Assert.IsTrue(CoreChoices.MakeDefault(config, "NES", "QuickerNesHawk"));
			Assert.AreEqual("QuickerNesHawk", config.DefaultCores["NES"]);
		}

		[TestMethod]
		public void EachSystemRemembersItsOwnCore()
		{
			Config config = new();
			CoreChoices.MakeDefault(config, "NES", "QuickerNesHawk");
			CoreChoices.MakeDefault(config, "SNES", "someSnesCore");
			Assert.AreEqual("QuickerNesHawk", config.DefaultCores["NES"]);
			Assert.AreEqual("someSnesCore", config.DefaultCores["SNES"]);
		}

		// ---- several builds of one core (issue #63) --------------------------------

		private sealed class Build
		{
			public string Sha1 { get; init; } = "";
			public System.DateTime Installed { get; init; }
		}

		private static readonly Build Older = new() { Sha1 = new string('a', 40), Installed = new(2026, 9, 1) };
		private static readonly Build Newer = new() { Sha1 = new string('b', 40), Installed = new(2026, 9, 12) };
		private static readonly Build Local = new() { Sha1 = new string('c', 40), Installed = new(2026, 9, 5) };

		private static Build? Pick(string? pinned, string? chosen, params Build[] builds)
			=> CoreChoices.PickBuild(builds, static b => b.Sha1, static b => b.Installed, pinned, chosen);

		[TestMethod]
		public void AProjectRunsTheBuildItPinsWheneverItIsInstalled()
		{
			// the whole of #63: an older build, pinned, beside a newer one that was even chosen
			Assert.AreSame(Older, Pick(Older.Sha1.ToUpperInvariant(), Newer.Sha1, Newer, Older, Local));
		}

		[TestMethod]
		public void WithoutAPinTheChosenBuildRuns()
		{
			Assert.AreSame(Local, Pick(pinned: null, chosen: Local.Sha1, Older, Newer, Local));
		}

		[TestMethod]
		public void APinOrChoiceThatIsNotInstalledFallsToTheNewestInstalled()
		{
			Assert.AreSame(Newer, Pick(new string('d', 40), new string('e', 40), Older, Local, Newer));
			Assert.AreSame(Newer, Pick(pinned: null, chosen: null, Older, Newer, Local));
		}

		[TestMethod]
		public void NoBuildsIsNoBuild()
		{
			Assert.IsNull(Pick(Older.Sha1, Older.Sha1));
		}

		[TestMethod]
		public void ChoosingABuildIsRememberedPerCore()
		{
			Config config = new();
			Assert.IsTrue(CoreChoices.MakeDefaultBuild(config, "xemu", Older.Sha1));
			Assert.IsFalse(CoreChoices.MakeDefaultBuild(config, "xemu", Older.Sha1.ToUpperInvariant()), "the same build, however it is spelled");
			Assert.IsTrue(CoreChoices.MakeDefaultBuild(config, "xemu", Newer.Sha1));
			CoreChoices.MakeDefaultBuild(config, "ares", Local.Sha1);
			Assert.AreEqual(Newer.Sha1, config.DefaultCoreBuilds["xemu"]);
			Assert.AreEqual(Local.Sha1, config.DefaultCoreBuilds["ares"]);
		}
	}
}
