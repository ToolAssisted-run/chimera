using Chimera.Client.Common;

namespace Chimera.Tests.Client.Common
{
	/// <summary>
	/// The auto-size policy: state history budgets scale to the machine and to
	/// the system's state size, inside hard bounds. The machine's free RAM is
	/// whatever it is when the test runs, so these assert the INVARIANTS - the
	/// clamps and the scaling directions - never exact numbers.
	/// </summary>
	[TestClass]
	public class StateBudgetTests
	{
		[TestMethod]
		public void TheBudgetStaysInsideItsBounds()
		{
			var budget = StateBudget.BudgetMB(floorMB: 1024, ceilingMB: 8192);
			Assert.IsTrue(budget is >= 1024 and <= 8192, $"budget {budget}MB escaped [1024, 8192]");
		}

		[TestMethod]
		public void SmallStatesStopAtWhatTheyCanUse()
		{
			// 10KB NES states: 4096 of them are 40MB, so the pool takes the floor
			var pool = StateBudget.PoolMB(10 * 1024, targetStates: 4096, floorMB: 256, ceilingMB: 8192);
			Assert.AreEqual(256, pool, "a small system should get the floor, not a gigabyte it cannot fill");
		}

		[TestMethod]
		public void BigStatesGetTheMachineBudget()
		{
			// 27MB PS2 states: 4096 of them dwarf any budget, so the machine decides
			var pool = StateBudget.PoolMB(27L * 1024 * 1024, targetStates: 4096, floorMB: 256, ceilingMB: 8192);
			var budget = StateBudget.BudgetMB(floorMB: 256, ceilingMB: 8192);
			Assert.IsTrue(pool >= 256 && pool <= budget, $"pool {pool}MB escaped [256, budget {budget}MB]");
			Assert.IsTrue(pool >= 1024 || budget < 1024, "a big system should get at least the old default when the machine has it");
		}

		[TestMethod]
		public void AvailableRamAnswersSomething()
		{
			Assert.IsTrue(StateBudget.AvailableRamBytes() > 0, "even a machine that will not say gets an assumption");
		}

		[TestMethod]
		public void ZwinderAutoLeavesSmallSystemsAlone()
		{
			var settings = new ZwinderStateManagerSettings();
			Assert.AreSame(settings, settings.ResolveAuto(10 * 1024), "small states resolve to the settings as written");
		}

		[TestMethod]
		public void ZwinderAutoMovesColdTiersToDiskAndCompresses()
		{
			var settings = new ZwinderStateManagerSettings();
			var resolved = settings.ResolveAuto(27L * 1024 * 1024);
			Assert.AreNotSame(settings, resolved);
			Assert.IsTrue(resolved.CurrentUseCompression && resolved.RecentUseCompression && resolved.GapsUseCompression,
				"big states compress everywhere");
			Assert.AreEqual(IRewindSettings.BackingStoreType.Memory, resolved.CurrentStoreType, "the hot buffer stays in memory");
			Assert.AreEqual(IRewindSettings.BackingStoreType.TempFile, resolved.RecentStoreType, "the recent tier goes to disk");
			Assert.AreEqual(IRewindSettings.BackingStoreType.TempFile, resolved.GapsStoreType, "the gap tier goes to disk");
			Assert.AreEqual(IRewindSettings.BackingStoreType.TempFile, resolved.AncientStoreType, "the ancient tier goes to disk");
			Assert.IsTrue(resolved.CurrentBufferSize >= 256, "the hot buffer never shrinks below the old default");
		}

		[TestMethod]
		public void ZwinderAutoRespectsTheOffSwitch()
		{
			var settings = new ZwinderStateManagerSettings { AutoSize = false };
			Assert.AreSame(settings, settings.ResolveAuto(27L * 1024 * 1024), "auto off means the settings as written, whatever the system");
		}
	}
}
