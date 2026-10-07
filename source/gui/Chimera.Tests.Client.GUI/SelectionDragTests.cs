using System.Collections.Generic;

using Chimera.Client.GUI;

namespace Chimera.Tests.Client.GUI
{
	/// <summary>
	/// Dragging over the frame numbers selects rows; these pin what each pointer
	/// move does to them, and the move that used to throw (issue #195).
	///
	/// With the piano roll's context menu open, a left click on a frame number
	/// closes the menu and begins a selection drag. The roll had been told the
	/// pointer left - the menu had it - so the next move arrives from a cell with
	/// no row, and the drag read that row: "Nullable object must have a value".
	/// </summary>
	[TestClass]
	public class SelectionDragTests
	{
		private static List<(int From, int To, bool Selected)> Steps(int? oldRow, int newRow, int dragStart, bool dragState = true)
			=> TAStudio.SelectionDragSteps(oldRow, newRow, dragStart, dragState);

		[TestMethod]
		public void AMoveFromNowhereComesFromWhereTheDragBegan()
		{
			// the reporter's move: the drag began on row 10, the cell before has no row
			CollectionAssert.AreEqual(new[] { (11, 13, true) }, Steps(null, 13, dragStart: 10));
			CollectionAssert.AreEqual(new[] { (9, 7, true) }, Steps(null, 7, dragStart: 10));
			// still on the row it began on: nothing to do, and nothing thrown
			Assert.AreEqual(0, Steps(null, 10, dragStart: 10).Count);
		}

		[TestMethod]
		public void MovingAwaySpreadsTheStateAndMovingBackTakesItOff()
		{
			CollectionAssert.AreEqual(new[] { (13, 15, true) }, Steps(12, 15, dragStart: 10));
			CollectionAssert.AreEqual(new[] { (15, 13, false) }, Steps(15, 12, dragStart: 10));
			// a drag that began on a selected row deselects as it spreads
			CollectionAssert.AreEqual(new[] { (13, 15, false) }, Steps(12, 15, dragStart: 10, dragState: false));
		}

		[TestMethod]
		public void CrossingTheStartUndoesOneSideAndSpreadsOnTheOther()
		{
			CollectionAssert.AreEqual(new[] { (12, 11, false), (9, 8, true) }, Steps(12, 8, dragStart: 10));
		}

		[TestMethod]
		public void AMoveAlongOneRowChangesNothing()
		{
			Assert.AreEqual(0, Steps(12, 12, dragStart: 10).Count);
		}
	}
}
