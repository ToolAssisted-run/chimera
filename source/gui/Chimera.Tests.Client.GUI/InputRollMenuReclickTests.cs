using System;
using System.Drawing;
using System.Windows.Forms;

using Chimera.Client.GUI;

namespace Chimera.Tests.Client.GUI
{
	/// <summary>
	/// A second click on a roll whose context menu is already open (issue #121).
	///
	/// The roll only learns where the pointer is from mouse moves. While a context
	/// menu is up, the menu has the pointer: the roll is told the pointer LEFT, and
	/// no move arrives until the menu is gone. A click that lands on the roll in
	/// that state - right-clicking a branch or marker row again while its menu is
	/// still open - is therefore a click on a cell the roll has not looked at. On
	/// Windows this threw NullReferenceException out of CalculatePointedCell on
	/// every core, with the reporter's trace showing OnMouseDown calling
	/// OnMouseMove by hand (the old workaround for exactly this) and the pointed
	/// cell being read with nothing there.
	///
	/// These drive the roll through the same protected handlers WinForms' WndProc
	/// calls, in the order the reporter's trace establishes: enter, move, right
	/// down, right up, the menu shown, LEAVE, and then a mouse-down with no move in
	/// between. The roll is a real control on a real window, so its geometry is
	/// real; the message order is the test's, not the toolkit's. Whether a toolkit
	/// sends the leave when a ContextMenuStrip takes the pointer, and a down with
	/// no move after it, is the toolkit's business and is not measured here - the
	/// trace in the issue is the evidence that .NET Framework's WinForms does. The
	/// legs themselves have been run under both toolkits (Mono under Xvfb, and the
	/// same assemblies natively on .NET Framework 4.8), red before the fix on both
	/// and green after on both.
	///
	/// A fix that only stopped the throw would leave the click doing nothing on
	/// the piano roll, which does not select on right-click and so never took the
	/// old workaround path; the piano-roll leg here is what tells the two fixes
	/// apart. The click must be handled as a fresh one: the roll points at the
	/// clicked cell, and a roll that selects on right-click selects it.
	/// </summary>
	[TestClass]
	public class InputRollMenuReclickTests
	{
		/// <summary>The roll, with the handlers the toolkit calls made reachable.</summary>
		private sealed class Roll : InputRoll
		{
			public void PointerIn() => OnMouseEnter(EventArgs.Empty);

			public void PointerOut() => OnMouseLeave(EventArgs.Empty);

			public void PointerTo(Point p) => OnMouseMove(new MouseEventArgs(MouseButtons.None, 0, p.X, p.Y, 0));

			public void Press(MouseButtons button, Point p) => OnMouseDown(new MouseEventArgs(button, 1, p.X, p.Y, 0));

			public void Release(MouseButtons button, Point p) => OnMouseUp(new MouseEventArgs(button, 1, p.X, p.Y, 0));
		}

		private sealed class Stage : IDisposable
		{
			public readonly Form Host;

			public readonly Roll Roll;

			public readonly ContextMenuStrip Menu = new();

			public int MenuOpenings;

			public Stage(bool branchesShaped)
			{
				Host = new()
				{
					ClientSize = new(320, 240),
					StartPosition = FormStartPosition.Manual,
					Location = new(0, 0),
					Text = branchesShaped ? "Branches" : "Piano roll",
				};
				Roll = new() { Dock = DockStyle.Fill, FullRowSelect = true };
				if (branchesShaped)
				{
					// the branch list: three columns, right-click selects the row the menu is for
					Roll.AllColumns.AddRange(
					[
						new RollColumn("BranchNumberColumn", 30, "#"),
						new RollColumn("FrameColumn", 64, "Frame"),
						new RollColumn("UserTextColumn", 90, "UserText"),
					]);
					Roll.RowCount = 6;
					Roll.ContextMenuStrip = Menu;
				}
				else
				{
					// the piano roll's own settings, from TAStudio.MakeInputRoll
					Roll.AllowRightClickSelection = false;
					Roll.InputPaintingMode = true;
					Roll.CellHeightPadding = 0;
					Roll.AllColumns.AddRange(
					[
						new RollColumn("Cursor", 18, ""),
						new RollColumn("Frame", 68, "Frame"),
						new RollColumn("P1 A", 24, "A"),
						new RollColumn("P1 B", 24, "B"),
					]);
					Roll.RowCount = 40;
				}
				Roll.QueryItemText += static (InputRoll sender, int index, RollColumn column, out string text, ref int x, ref int y)
					=> text = index.ToString();
				Menu.Items.Add("Load Branch");
				Menu.Items.Add("Remove Branch");
				Menu.Opening += (_, _) => MenuOpenings++;
				Host.Controls.Add(Roll);
				Host.Show();
				Application.DoEvents();
			}

			/// <summary>
			/// A point the roll itself says is on the given row of its second column,
			/// found by asking the roll rather than by guessing at its geometry.
			/// </summary>
			public Point PointOnRow(int row)
			{
				Roll.PointerIn();
				int x = Roll.AllColumns[1].Left + 4;
				for (int y = 0; y < Roll.ClientSize.Height; y += 2)
				{
					Point p = new(x, y);
					Roll.PointerTo(p);
					if (Roll.CurrentCell is { RowIndex: int r, Column: not null } && r == row) return p;
				}
				throw new InvalidOperationException($"row {row} is not on screen");
			}

			/// <summary>
			/// The first right-click, the way it reaches the roll: down, up, the menu
			/// shown where the click was (which is WinForms' job on WM_CONTEXTMENU, and
			/// TAStudio's own on the piano roll), and then the pointer is the menu's -
			/// the roll sees a leave, and no move until the menu is gone.
			/// </summary>
			public Point OpenMenuOn(int row)
			{
				var p = PointOnRow(row);
				Roll.Press(MouseButtons.Right, p);
				Roll.Release(MouseButtons.Right, p);
				Menu.Show(Roll, p);
				Application.DoEvents();
				Roll.PointerOut();
				Application.DoEvents();
				Assert.IsNull(Roll.CurrentCell, "with the menu holding the pointer the roll has no pointed cell");
				return p;
			}

			public void Dispose()
			{
				Menu.Close();
				Menu.Dispose();
				Host.Dispose();
			}
		}

		[TestMethod]
		public void RightClickingTheSameBranchRowWhileItsMenuIsOpenIsAFreshClick()
		{
			using Stage s = new(branchesShaped: true);
			var p = s.OpenMenuOn(2);
			Assert.AreEqual(1, s.MenuOpenings);

			s.Roll.Press(MouseButtons.Right, p);

			Assert.IsNotNull(s.Roll.CurrentCell, "the click landed on a cell");
			Assert.AreEqual(2, s.Roll.CurrentCell.RowIndex);
			Assert.IsNotNull(s.Roll.CurrentCell.Column);
			Assert.IsTrue(s.Roll.IsRowSelected(2), "a right-click selects the row the menu will be for");
			s.Roll.Release(MouseButtons.Right, p);
		}

		[TestMethod]
		public void RightClickingAnotherBranchRowWhileTheMenuIsOpenMovesTheSelection()
		{
			using Stage s = new(branchesShaped: true);
			s.OpenMenuOn(1);
			Assert.IsTrue(s.Roll.IsRowSelected(1));
			var other = s.PointOnRow(4);
			s.Roll.PointerOut();

			s.Roll.Press(MouseButtons.Right, other);

			Assert.IsNotNull(s.Roll.CurrentCell, "the click landed on a cell");
			Assert.AreEqual(4, s.Roll.CurrentCell.RowIndex);
			Assert.IsTrue(s.Roll.IsRowSelected(4), "the row under the second click is the selected one");
			Assert.IsFalse(s.Roll.IsRowSelected(1), "and the first is no longer");
			s.Roll.Release(MouseButtons.Right, other);
		}

		[TestMethod]
		public void LeftClickingABranchRowWhileTheMenuIsOpenSelectsIt()
		{
			using Stage s = new(branchesShaped: true);
			s.OpenMenuOn(1);
			var other = s.PointOnRow(3);
			s.Roll.PointerOut();

			s.Roll.Press(MouseButtons.Left, other);

			Assert.IsNotNull(s.Roll.CurrentCell, "the click landed on a cell");
			Assert.AreEqual(3, s.Roll.CurrentCell.RowIndex);
			Assert.IsTrue(s.Roll.IsRowSelected(3));
			Assert.IsFalse(s.Roll.IsRowSelected(1));
			s.Roll.Release(MouseButtons.Left, other);
		}

		/// <summary>
		/// The piano roll does not select on right-click, so it never took the old
		/// workaround and never threw - it silently lost the click instead:
		/// TAStudio's own mouse-down handler asks the roll which cell was hit and
		/// got nothing. A fresh click points at the cell.
		/// </summary>
		[TestMethod]
		public void RightClickingThePianoRollWhileItsMenuIsOpenPointsAtTheClickedCell()
		{
			using Stage s = new(branchesShaped: false);
			var p = s.OpenMenuOn(7);

			s.Roll.Press(MouseButtons.Right, p);

			Assert.IsNotNull(s.Roll.CurrentCell, "the click landed on a cell");
			Assert.AreEqual(7, s.Roll.CurrentCell.RowIndex);
			Assert.IsNotNull(s.Roll.CurrentCell.Column);
			Assert.IsTrue(s.Roll.RightButtonHeld);
			Assert.IsFalse(s.Roll.IsRowSelected(7), "the piano roll does not select on right-click");
			s.Roll.Release(MouseButtons.Right, p);
			Assert.IsFalse(s.Roll.RightButtonHeld);
		}
	}
}
