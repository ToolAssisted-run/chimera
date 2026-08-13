using System;
using System.Collections.Generic;
using System.Linq;

using BizHawk.Client.Common;
using BizHawk.Emulation.Common;

namespace BizHawk.Tests.Client.Common.CorePackages
{
	/// <summary>
	/// Two packages may claim the same system - a fast core and an accurate one - and then the user
	/// has to be able to say which one a rom opens with. These cover what the Emulator > Core menu
	/// is built from and what picking an entry records as that system's default.
	/// </summary>
	[TestClass]
	public class CoreChoicesTests
	{
		private sealed class FakeFactory : ICoreFactory
		{
			public string CoreName { get; init; } = "";

			public IReadOnlyList<string> SystemIds => [ "NES" ];

			public Type CoreType => typeof(FakeFactory);

			public Type SettingsType => null;

			public Type SyncSettingsType => null;

			public IEmulator Create(CoreCreationContext ctx) => throw new NotSupportedException();
		}

		private static IReadOnlyList<ICoreFactory> Factories(params string[] names)
			=> names.Select(static n => (ICoreFactory) new FakeFactory { CoreName = n }).ToList();

		[TestMethod]
		public void EveryCoreForTheSystemIsOffered()
		{
			var choices = CoreChoices.For(Factories("quickerNES", "QuickerNesHawk"), "quickerNES");
			CollectionAssert.AreEqual(
				new[] { "quickerNES", "QuickerNesHawk" },
				choices.Select(static c => c.CoreName).ToList(),
				"the order must not depend on which package registered first");
			Assert.IsTrue(choices[0].IsCurrent);
			Assert.IsFalse(choices[1].IsCurrent);
		}

		[TestMethod]
		public void TheOnlyCoreIsStillOffered()
		{
			// one entry, checked: the menu doubles as "what is running"
			var choices = CoreChoices.For(Factories("quickerNES"), "quickerNES");
			Assert.AreEqual(1, choices.Count);
			Assert.IsTrue(choices[0].IsCurrent);
		}

		[TestMethod]
		public void NothingIsCheckedWhenTheRunningCoreIsNotInTheList()
		{
			// the loaded core came from a package that has since been replaced
			var choices = CoreChoices.For(Factories("quickerNES"), "someOtherCore");
			Assert.IsFalse(choices.Any(static c => c.IsCurrent));
		}

		[TestMethod]
		public void NoCoresMeansNoMenu()
			=> Assert.AreEqual(0, CoreChoices.For(Factories(), null).Count);

		[TestMethod]
		public void APackageRegisteringTheSameCoreTwiceShowsOnce()
			=> Assert.AreEqual(1, CoreChoices.For(Factories("quickerNES", "quickerNES"), null).Count);

		[TestMethod]
		public void TheEffectiveCoreIsTheDefaultOnlyWhileThatCoreExists()
		{
			// what the menu checkmarks, and what RomLoader would pick
			Config config = new();
			config.DefaultCores["NES"] = "somePackageTheUserRemoved";
			Assert.IsNull(CoreChoices.EffectiveCoreName(config, "NES"),
				"a preference naming a core no package provides must not be reported as the one that would run");
		}

		[TestMethod]
		public void ChoosingACoreRecordsItAgainstTheSystem()
		{
			Config config = new();
			Assert.IsTrue(CoreChoices.MakeDefault(config, "NES", "QuickerNesHawk"));
			Assert.AreEqual("QuickerNesHawk", config.DefaultCores["NES"]);
		}

		[TestMethod]
		public void ChoosingTheCoreAlreadyRunningChangesNothing()
		{
			// the caller uses this to skip a reload, which would otherwise throw away the session
			Config config = new();
			config.DefaultCores["NES"] = "quickerNES";
			Assert.IsFalse(CoreChoices.MakeDefault(config, "NES", "quickerNES"));
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
	}
}
