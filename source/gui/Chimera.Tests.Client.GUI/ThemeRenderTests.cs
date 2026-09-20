using System.Collections.Generic;
using System.Drawing;
using System.Linq;
using System.Windows.Forms;

using Chimera.Client.Common;
using Chimera.Client.GUI;

namespace Chimera.Tests.Client.GUI
{
	/// <summary>
	/// What a themed window actually LOOKS like, in pixels.
	///
	/// ThemingContractTests reads colours off controls, and a list shipped with a
	/// row nobody could read while every one of those assertions passed: a
	/// ListView paints its selected row, its column headers and the strip past its
	/// last column out of the desktop's colours and tells no property about it.
	/// The only way to see that is to look at the picture.
	///
	/// These hold a window this file built, every pixel of which is something
	/// these tests put there, so it can be held to a stricter list than the real
	/// windows (which carry pictures of their own, and are checked as they are
	/// photographed - see <see cref="UiShots"/>). And they put every list into the
	/// state that hid the bug in the first place: a row CHOSEN.
	/// </summary>
	[TestClass]
	public class ThemeRenderTests
	{
		/// <summary>
		/// None of the toolkit's own colours may appear ANYWHERE in this window
		/// under a dark theme - not behind a chosen row, not in a corner of a
		/// header, not anywhere. The real windows are only checked band by band,
		/// because they have scroll bars and tick boxes the toolkit draws and no
		/// theme can reach; this one has neither, so it can be held to the whole
		/// picture.
		/// </summary>
		private static IReadOnlyList<(string Name, int Argb)> DesktopLight => UiShots.DesktopBeige;

		/// <summary>Draws the window as it really is on screen, which is the only rendering that tells the truth on Mono.</summary>
		private static Bitmap Shoot(Form form)
		{
			form.Refresh();
			Application.DoEvents();
			Bitmap bmp = new(form.Width, form.Height);
			using (var g = Graphics.FromImage(bmp)) g.CopyFromScreen(form.Location, Point.Empty, form.Size);
			return bmp;
		}

		/// <summary>
		/// A window holding one of every list the frontend uses, each with a row
		/// chosen, each with room to spare so that the parts holding no content are
		/// on screen too.
		/// </summary>
		private static Form ListsWithSomethingChosen()
		{
			Form form = new()
			{
				ClientSize = new(640, 300),
				StartPosition = FormStartPosition.Manual,
				Location = new(0, 0),
				FormBorderStyle = FormBorderStyle.None,
			};

			ListView view = new()
			{
				Bounds = new(4, 4, 400, 140),
				View = View.Details,
				FullRowSelect = true,
				HideSelection = false,
				CheckBoxes = true,
			};
			// narrow columns on purpose: the strip past the last one is exactly
			// where the toolkit used to leave the desktop's colour behind
			view.Columns.Add("Core", 90);
			view.Columns.Add("State", 90);
			foreach (var name in new[] { "alpha", "beta", "gamma" })
			{
				ListViewItem item = new(name);
				item.SubItems.Add("ready");
				view.Items.Add(item);
			}
			view.Items[1].Checked = true;
			view.Items[1].Selected = true;

			ListBox box = new() { Bounds = new(412, 4, 220, 140) };
			box.Items.AddRange(["one", "two", "three"]);
			box.SelectedIndex = 1;

			DataGridView grid = new()
			{
				Bounds = new(4, 150, 400, 140),
				AllowUserToAddRows = false,
				RowHeadersVisible = true,
			};
			grid.Columns.Add("a", "Frame");
			grid.Columns.Add("b", "Text");
			grid.Rows.Add("0", "power on");
			grid.Rows.Add("120", "first input");
			grid.Rows[1].Selected = true;

			form.Controls.AddRange([ view, box, grid ]);
			return form;
		}

		/// <summary>
		/// The test that should have caught the unreadable row: every pixel of a
		/// window full of chosen rows, under the dark theme, against the colours a
		/// light desktop hands out.
		/// </summary>
		[TestMethod]
		public void NoPartOfAListIsStillTheDesktopsColourUnderDark()
		{
			ThemeLibrary.Select("Dark");
			try
			{
				using var form = ListsWithSomethingChosen();
				form.Show();
				ThemeEngine.Apply(form, ThemeLibrary.Current);
				using var bmp = Shoot(form);

				var blocks = UiShots
					.CountOf(UiShots.Pixels(bmp, new Rectangle(0, 0, bmp.Width, bmp.Height)), DesktopLight)
					.Where(static kvp => kvp.Value > 40)
					.ToList();
				Assert.AreEqual(
					0,
					blocks.Count,
					"a light desktop colour is still being painted under the dark theme: "
						+ string.Join(", ", blocks.Select(static kvp => $"{kvp.Key} on {kvp.Value} pixels")));
			}
			finally
			{
				ThemeLibrary.Select("Light");
			}
		}

		/// <summary>
		/// And the other half of it: a row that is chosen has to be READABLE. A
		/// background and a foreground that are both themed can still be the same
		/// colour, and that is what a row of invisible text is.
		/// </summary>
		[TestMethod]
		public void AChosenRowCanStillBeRead()
		{
			ThemeLibrary.Select("Dark");
			try
			{
				using var form = ListsWithSomethingChosen();
				form.Show();
				ThemeEngine.Apply(form, ThemeLibrary.Current);
				using var bmp = Shoot(form);

				var view = form.Controls.OfType<ListView>().Single();
				var chosen = view.Items[1];
				// the SECOND cell, not the whole row: the first one carries a tick
				// box and an icon, and their edges would supply the contrast this is
				// asking about even when every letter is invisible
				var cell = view.RectangleToScreen(chosen.SubItems[1].Bounds);
				cell.Offset(-form.Left, -form.Top);
				cell = Rectangle.Intersect(cell, new Rectangle(0, 0, bmp.Width, bmp.Height));
				Assert.IsTrue(cell.Width > 20 && cell.Height > 4, "the chosen row is not on screen, so this is testing nothing");

				double darkest = 1, brightest = 0;
				foreach (var argb in UiShots.Pixels(bmp, cell))
				{
					var l = UiShots.Luma(Color.FromArgb(argb));
					if (l < darkest) darkest = l;
					if (l > brightest) brightest = l;
				}
				Assert.IsTrue(
					brightest - darkest > 0.25,
					$"the chosen row is {brightest - darkest:0.00} of contrast from end to end, which is text nobody can read "
						+ "- its background and its letters have ended up the same colour");
			}
			finally
			{
				ThemeLibrary.Select("Light");
			}
		}
	}
}
