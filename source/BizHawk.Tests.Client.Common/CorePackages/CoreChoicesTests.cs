using BizHawk.Client.Common;

namespace BizHawk.Tests.Client.Common.CorePackages
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
	}
}
