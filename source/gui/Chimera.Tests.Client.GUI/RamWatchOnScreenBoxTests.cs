using System.Linq;
using System.Reflection;
using System.Windows.Forms;

using Chimera.Client.Common;
using Chimera.Client.GUI;
using Chimera.Emulation.Common;

namespace Chimera.Tests.Client.GUI
{
	/// <summary>
	/// RAM Watch's On Screen box: the first column, a click on a watch's box ticks it or clears
	/// it, and a click anywhere else, or on a separator, leaves it alone.
	///
	/// The click goes through the roll's own OnMouseDown, as the toolkit calls it, with the roll
	/// pointed at the cell; where on the window that cell is drawn is the roll's geometry and is
	/// not measured here. The window is not shown: painting a watch asks the main form about
	/// freezes, and there is none.
	/// </summary>
	[TestClass]
	public class RamWatchOnScreenBoxTests
	{
		private const BindingFlags Private = BindingFlags.NonPublic | BindingFlags.Instance;

		private static InputRoll Roll(RamWatch ramWatch)
			=> (InputRoll)typeof(RamWatch).GetField("WatchListView", Private)!.GetValue(ramWatch)!;

		private static (RamWatch RamWatch, WatchList Watches) Stage()
		{
			var block = new MemoryDomainByteArray("Game State", MemoryDomain.Endian.Little, new byte[16], writable: true, wordSize: 1);
			var watches = new WatchList(new MemoryDomainList(new[] { (MemoryDomain)block }), "GAME")
			{
				Watch.GenerateWatch(block, 0, WatchSize.Byte, WatchDisplayType.Unsigned, bigEndian: false, "Kid"),
				SeparatorWatch.Instance,
				Watch.GenerateWatch(block, 1, WatchSize.Byte, WatchDisplayType.Unsigned, bigEndian: false, "Guard"),
			};
			// the watches are not on the screen, which has no display manager here
			var ramWatch = new RamWatch { Config = new Config { DisplayWatchesOnScreen = false } };
			typeof(RamWatch).GetField("_watches", Private)!.SetValue(ramWatch, watches);
			Roll(ramWatch).RowCount = watches.Count;
			return (ramWatch, watches);
		}

		private static void Click(InputRoll roll, string column, int row)
		{
			var cell = new Cell();
			typeof(Cell).GetProperty(nameof(Cell.Column))!.SetValue(cell, roll.AllColumns.Single(c => c.Name == column));
			typeof(Cell).GetProperty(nameof(Cell.RowIndex))!.SetValue(cell, row);
			roll.CurrentCell = cell;
			typeof(Control).GetMethod("OnMouseDown", Private)!.Invoke(roll, new object[] { new MouseEventArgs(MouseButtons.Left, 1, 0, 0, 0) });
		}

		[TestMethod]
		public void AClickOnTheBoxTicksItAndAnotherClearsIt()
		{
			var (ramWatch, watches) = Stage();
			using var _ = ramWatch;
			var roll = Roll(ramWatch);
			Assert.AreEqual(WatchList.OnScreen, roll.AllColumns[0].Name, "the box is the first column");

			Click(roll, WatchList.OnScreen, 2);
			CollectionAssert.AreEqual(new[] { false, false, true }, watches.Select(static w => w.OnScreen).ToArray());
			Assert.IsTrue(watches.Changes, "a tick is a change to the list");

			Click(roll, WatchList.OnScreen, 2);
			Assert.IsFalse(watches[2].OnScreen);
		}

		[TestMethod]
		public void AClickElsewhereOrOnASeparatorLeavesItAlone()
		{
			var (ramWatch, watches) = Stage();
			using var _ = ramWatch;
			var roll = Roll(ramWatch);
			Click(roll, WatchList.Address, 0);
			Click(roll, WatchList.Notes, 2);
			Click(roll, WatchList.OnScreen, 1);
			Assert.IsFalse(watches.Any(static w => w.OnScreen));
		}

		[TestMethod]
		public void ColumnsSavedBeforeTheBoxGainIt()
		{
			using var ramWatch = new RamWatch();
			ramWatch.Settings = new RamWatch.RamWatchSettings();
			ramWatch.Settings.Columns.RemoveAll(static c => c.Name == WatchList.OnScreen);
			typeof(RamWatch).GetMethod("LoadConfigSettings", Private)!.Invoke(ramWatch, null);
			var roll = Roll(ramWatch);
			Assert.AreEqual(WatchList.OnScreen, roll.AllColumns[0].Name);
			Assert.AreEqual(1, roll.AllColumns.Count(static c => c.Name == WatchList.OnScreen));
		}
	}
}
