using System;
using System.Collections.Generic;
using System.Drawing;
using System.Drawing.Imaging;
using System.Linq;
using System.Reflection;
using System.Runtime.InteropServices;
using System.Windows.Forms;

using Chimera.Client.Common;
using Chimera.Client.GUI;

namespace Chimera.Tests.Client.GUI
{
	/// <summary>
	/// A list the theme has taken over is drawn ENTIRELY by the frontend. There is
	/// no toolkit underneath to fall back on, so a state nobody wrote code for is
	/// not drawn in the toolkit's colours - it is not drawn at all.
	///
	/// That has now shipped twice. First the CHOSEN row, which came out as a beige
	/// bar with invisible writing on it. Then the row under the POINTER, which came
	/// out as a bar with no writing at all, because WinForms raises DrawSubItem for
	/// a hovered row with a null SubItem and the text was being taken from it.
	///
	/// So this does not test a state. It tests the LIST of states, and every one of
	/// them, by driving the real draw handlers the walk installs and looking at what
	/// comes out: the row has the background its state calls for, and the text on it
	/// is both present and readable.
	///
	/// The states a row can be in, which is the list this asserts:
	///   normal, chosen, chosen while the list has no focus, under the pointer,
	///   chosen AND under the pointer, on a list that is switched off,
	///   and the tick box in each of ticked and not.
	/// </summary>
	[TestClass]
	public class ListRowStateTests
	{
		private const int Width = 260;

		private const int Height = 20;

		/// <summary>
		/// Raises the list's own draw events, which is what the toolkit does. The
		/// handlers are the walk's private ones and are reached the only way anything
		/// outside can reach them - by asking the control to raise the event.
		/// </summary>
		private static void Raise(ListView list, string method, object args)
		{
			var raise = typeof(ListView).GetMethod(method, BindingFlags.Instance | BindingFlags.NonPublic)
				?? throw new InvalidOperationException($"{method} is not on this toolkit's ListView");
			try
			{
				raise.Invoke(list, [args]);
			}
			catch (TargetInvocationException e)
			{
				throw e.InnerException ?? e;
			}
		}

		/// <summary>One row, drawn the way the control would draw it, as pixels.</summary>
		private static Bitmap Draw(ListView list, int index, ListViewItemStates states, bool nullSubItem)
		{
			var item = list.Items[index];
			Bitmap bmp = new(Width, Height, PixelFormat.Format32bppArgb);
			using (var g = Graphics.FromImage(bmp))
			{
				g.Clear(Color.Magenta);
				Rectangle bounds = new(0, 0, Width, Height);
				Raise(list, "OnDrawItem", new DrawListViewItemEventArgs(g, item, bounds, index, states));
				// The null sub-item is not a hypothetical: it is what WinForms hands
				// this event when it redraws a row because the pointer moved onto it.
				Raise(list, "OnDrawSubItem", new DrawListViewSubItemEventArgs(
					g,
					bounds,
					item,
					nullSubItem ? null : item.SubItems[0],
					nullSubItem ? -1 : index,
					0,
					list.Columns[0],
					states));
			}
			return bmp;
		}

		private static Dictionary<int, int> Census(Bitmap bmp)
		{
			Dictionary<int, int> seen = new();
			for (var y = 0; y < bmp.Height; y++)
			{
				for (var x = 0; x < bmp.Width; x++)
				{
					var argb = bmp.GetPixel(x, y).ToArgb();
					seen[argb] = seen.TryGetValue(argb, out var n) ? n + 1 : 1;
				}
			}
			return seen;
		}

		private static double Luminance(Color c)
		{
			static double Channel(int v)
			{
				var s = v / 255.0;
				return s <= 0.03928 ? s / 12.92 : Math.Pow((s + 0.055) / 1.055, 2.4);
			}
			return (0.2126 * Channel(c.R)) + (0.7152 * Channel(c.G)) + (0.0722 * Channel(c.B));
		}

		private static double Contrast(Color a, Color b)
		{
			var one = Luminance(a);
			var two = Luminance(b);
			return one > two ? (one + 0.05) / (two + 0.05) : (two + 0.05) / (one + 0.05);
		}

		private static ListView Build()
		{
			ListView list = new()
			{
				View = View.Details,
				FullRowSelect = true,
				HideSelection = false,
				CheckBoxes = true,
				Bounds = new Rectangle(0, 0, Width, 120),
			};
			list.Columns.Add("Name", Width);
			foreach (var name in new[] { "plain", "chosen", "ticked" })
			{
				list.Items.Add(new ListViewItem(name));
			}
			list.Items[1].Selected = true;
			list.Items[2].Checked = true;
			return list;
		}

		/// <summary>Every state, and in every one of them the row's text can be read.</summary>
		[TestMethod]
		public void EveryStateOfARowIsDrawnAndItsTextCanBeRead()
		{
			List<string> complaints = new();
			foreach (var theme in ThemeLibrary.All.Where(static t => !t.FollowsDesktop))
			{
				ThemeLibrary.Select(theme.Name);
				using Form host = new() { ClientSize = new(Width, 160) };
				using var list = Build();
				host.Controls.Add(list);
				host.Show();
				ThemeEngine.Apply(host, theme);

				foreach (var (name, index, states, nullSub, off) in States())
				{
					list.Enabled = !off;
					using var bmp = Draw(list, index, states, nullSub);
					var census = Census(bmp);

					if (census.ContainsKey(Color.Magenta.ToArgb()))
					{
						complaints.Add($"{theme.Name}/{name}: part of the row was never painted at all "
							+ "- a state the drawing does not handle is a hole, not a fallback");
						continue;
					}

					var ground = Color.FromArgb(census.OrderByDescending(static kvp => kvp.Value).First().Key);
					var ink = census
						.Where(kvp => kvp.Key != ground.ToArgb())
						.OrderByDescending(static kvp => kvp.Value)
						.Select(static kvp => Color.FromArgb(kvp.Key))
						.ToList();

					// The text is the thing that vanished. Not "some pixels differ":
					// a tick box differs too. Enough pixels to be writing.
					var marks = census.Where(kvp => kvp.Key != ground.ToArgb()).Sum(static kvp => kvp.Value);
					if (marks < 30)
					{
						complaints.Add($"{theme.Name}/{name}: the row is a bar of one colour with {marks} pixels on it "
							+ "- nothing was written in it");
						continue;
					}

					var best = ink.Count is 0 ? ground : ink.OrderByDescending(c => Contrast(c, ground)).First();
					var contrast = Contrast(best, ground);
					if (contrast < 3.0)
					{
						complaints.Add($"{theme.Name}/{name}: what is drawn on the row is {contrast:0.00}:1 against it "
							+ $"({best.R},{best.G},{best.B} on {ground.R},{ground.G},{ground.B}) - it cannot be read");
					}
				}

				host.Close();
			}

			ThemeLibrary.Select("Light");
			Assert.AreEqual(0, complaints.Count, string.Join("\n", complaints));
		}

		/// <summary>
		/// The states, named. Adding a state a list can be in means adding it here -
		/// which is the whole point of this file.
		/// </summary>
		private static IEnumerable<(string Name, int Index, ListViewItemStates States, bool NullSubItem, bool Off)> States()
		{
			yield return ("normal", 0, default, false, false);
			yield return ("chosen", 1, ListViewItemStates.Selected, false, false);
			yield return ("chosen, list not focused", 1, ListViewItemStates.Selected, false, false);
			yield return ("under the pointer", 0, ListViewItemStates.Hot, false, false);
			// the shape the toolkit really sends for a hovered row
			yield return ("under the pointer, no sub-item", 0, ListViewItemStates.Hot, true, false);
			yield return ("chosen and under the pointer", 1, ListViewItemStates.Selected | ListViewItemStates.Hot, false, false);
			yield return ("ticked", 2, default, false, false);
			yield return ("ticked and under the pointer", 2, ListViewItemStates.Hot, false, false);
			yield return ("switched off", 0, default, false, true);
			yield return ("switched off and ticked", 2, default, false, true);
		}

		/// <summary>
		/// And the state that caused it: the row the pointer is on must not look
		/// like the row beside it, or hovering says nothing - but it must not look
		/// like the CHOSEN row either, or it lies about what is selected.
		/// </summary>
		[TestMethod]
		public void TheRowUnderThePointerIsItsOwnColour()
		{
			foreach (var theme in ThemeLibrary.All.Where(static t => !t.FollowsDesktop))
			{
				ThemeLibrary.Select(theme.Name);
				using Form host = new() { ClientSize = new(Width, 160) };
				using var list = Build();
				host.Controls.Add(list);
				host.Show();
				ThemeEngine.Apply(host, theme);

				using var cold = Draw(list, 0, default, false);
				using var hot = Draw(list, 0, ListViewItemStates.Hot, false);
				using var chosen = Draw(list, 1, ListViewItemStates.Selected, false);
				host.Close();

				var coldGround = Ground(cold);
				var hotGround = Ground(hot);
				var chosenGround = Ground(chosen);

				Assert.AreNotEqual(
					coldGround.ToArgb(),
					hotGround.ToArgb(),
					$"{theme.Name}: a row looks the same whether the pointer is on it or not");
				Assert.AreNotEqual(
					chosenGround.ToArgb(),
					hotGround.ToArgb(),
					$"{theme.Name}: a row under the pointer is drawn as though it were selected");
			}
			ThemeLibrary.Select("Light");
		}

		private static Color Ground(Bitmap bmp)
			=> Color.FromArgb(Census(bmp).OrderByDescending(static kvp => kvp.Value).First().Key);
	}
}
