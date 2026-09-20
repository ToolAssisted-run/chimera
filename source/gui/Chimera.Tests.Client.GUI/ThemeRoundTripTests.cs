using System;
using System.Collections.Generic;
using System.Drawing;
using System.Linq;
using System.Windows.Forms;

using Chimera.Client.Common;
using Chimera.Client.GUI;

namespace Chimera.Tests.Client.GUI
{
	/// <summary>
	/// Changing the theme has to land the window where a window started in that
	/// theme would be. Not "something repainted" - the same pixels.
	///
	/// This exists because a test that only checked that a repaint HAPPENED was
	/// green while switching from Dark back to Light changed nothing but the title
	/// bar. The desktop theme deliberately assigns nothing by control type, which
	/// is right for a window built under it and leaves a window wearing Dark
	/// exactly as dark as it was.
	///
	/// Both directions, because the desktop theme is the asymmetric one: it is the
	/// only theme that has to put things BACK rather than set them.
	/// </summary>
	[TestClass]
	public class ThemeRoundTripTests
	{
		/// <summary>
		/// A window with one of everything the walk treats differently, so that a
		/// property it changes and cannot change back shows up as pixels.
		/// </summary>
		private static Form Window()
		{
			Form form = new()
			{
				ClientSize = new(660, 330),
				StartPosition = FormStartPosition.Manual,
				Location = new(0, 0),
				FormBorderStyle = FormBorderStyle.None,
			};

			MenuStrip menu = new();
			ToolStripMenuItem file = new("&File");
			file.DropDownItems.Add(new ToolStripMenuItem("Open"));
			file.DropDownItems.Add(new ToolStripSeparator());
			file.DropDownItems.Add(new ToolStripMenuItem("Close"));
			menu.Items.Add(file);
			menu.Items.Add(new ToolStripMenuItem("&Edit"));

			ListView view = new()
			{
				Bounds = new(4, 30, 380, 120),
				View = View.Details,
				FullRowSelect = true,
				HideSelection = false,
				CheckBoxes = true,
			};
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

			ListBox box = new() { Bounds = new(392, 30, 180, 120) };
			box.Items.AddRange(["one", "two", "three"]);
			box.SelectedIndex = 1;

			DataGridView grid = new() { Bounds = new(4, 156, 380, 90), AllowUserToAddRows = false };
			grid.Columns.Add("a", "Frame");
			grid.Columns.Add("b", "Text");
			grid.Rows.Add("0", "power on");
			grid.Rows.Add("120", "first input");
			grid.Rows[1].Selected = true;

			TextBox text = new() { Bounds = new(392, 156, 180, 22), Text = "typed" };
			Button button = new() { Bounds = new(392, 184, 100, 26), Text = "Press" };
			CheckBox tick = new() { Bounds = new(392, 214, 120, 22), Text = "Ticked", Checked = true };
			ComboBox combo = new() { Bounds = new(392, 240, 180, 22) };
			combo.Items.AddRange(["first", "second"]);
			combo.SelectedIndex = 0;
			LinkLabel link = new() { Bounds = new(4, 252, 300, 20), Text = "toolAssisted.run" };
			GroupBox group = new() { Bounds = new(4, 276, 380, 48), Text = "A group" };
			group.Controls.Add(new Label { Text = "a label", AutoSize = true, Left = 12, Top = 20 });

			form.Controls.AddRange([ view, box, grid, text, button, tick, combo, link, group, menu ]);
			form.MainMenuStrip = menu;
			return form;
		}

		private static Bitmap Shoot(Form form)
		{
			// the same control focused in either picture: a list draws its chosen
			// row in the active highlight when it has the focus and a duller one
			// when it does not, and a list whose handle has been recreated (which
			// is what turning owner-drawing off does) does not keep it. Which
			// control has the focus is not what is under test.
			form.Activate();
			form.Controls.OfType<ListView>().FirstOrDefault()?.Focus();
			Application.DoEvents();
			form.Refresh();
			Application.DoEvents();
			Bitmap bmp = new(form.Width, form.Height);
			using (var g = Graphics.FromImage(bmp)) g.CopyFromScreen(form.Location, Point.Empty, form.Size);
			return bmp;
		}

		/// <summary>
		/// A picture of a window built and shown with this theme already on, and
		/// then closed. Closed because the next window has to be the only one on
		/// screen when IT is photographed: a list draws its chosen row in the
		/// active highlight or the inactive one depending on focus, and two windows
		/// up at once means one of them is not the focused one.
		/// </summary>
		private static Bitmap Fresh(string theme)
		{
			ThemeLibrary.Select(theme);
			using Form form = Window();
			form.Show();
			form.Activate();
			ThemeEngine.Apply(form, ThemeLibrary.Current);
			var shot = Shoot(form);
			form.Close();
			Application.DoEvents();
			return shot;
		}

		/// <summary>
		/// When the two do not match, the two pictures and a map of where they
		/// differ are written out, because "21072 pixels differ" is a number and
		/// the question is always WHICH ones.
		/// </summary>
		private static void Keep(Bitmap wanted, Bitmap got, string name)
		{
			var dir = UiShots.Dir ?? System.IO.Path.GetTempPath();
			System.IO.Directory.CreateDirectory(dir);
			wanted.Save(System.IO.Path.Combine(dir, name + ".wanted.png"), System.Drawing.Imaging.ImageFormat.Png);
			got.Save(System.IO.Path.Combine(dir, name + ".got.png"), System.Drawing.Imaging.ImageFormat.Png);
			using Bitmap map = new(wanted.Width, wanted.Height);
			for (var y = 0; y < wanted.Height; y++)
			{
				for (var x = 0; x < wanted.Width; x++)
				{
					map.SetPixel(x, y, wanted.GetPixel(x, y).ToArgb() == got.GetPixel(x, y).ToArgb() ? Color.White : Color.Red);
				}
			}
			map.Save(System.IO.Path.Combine(dir, name + ".where.png"), System.Drawing.Imaging.ImageFormat.Png);
		}

		private static int Differences(Bitmap a, Bitmap b)
		{
			if (a.Width != b.Width || a.Height != b.Height) return int.MaxValue;
			var one = UiShots.Pixels(a, new Rectangle(0, 0, a.Width, a.Height));
			var two = UiShots.Pixels(b, new Rectangle(0, 0, b.Width, b.Height));
			var n = 0;
			for (var i = 0; i < one.Length; i++)
			{
				if (one[i] != two[i]) n++;
			}
			return n;
		}

		private static void RoundTrip(string start, string via)
		{
			Form travelled = null!;
			try
			{
				using var wanted = Fresh(start);

				ThemeLibrary.Select(start);
				travelled = Window();
				travelled.Show();
				travelled.Activate();
				ThemeEngine.Apply(travelled, ThemeLibrary.Current);
				ThemeEngine.Apply(travelled, ThemeLibrary.Select(via));
				ThemeEngine.Apply(travelled, ThemeLibrary.Select(start));
				using var got = Shoot(travelled);

				var differences = Differences(wanted, got);
				if (differences >= 40) Keep(wanted, got, $"roundtrip-{start}-via-{via}".ToLowerInvariant());
				Assert.IsTrue(
					differences < 40,
					$"a window taken {start} -> {via} -> {start} does not look like one that started on {start}: "
						+ $"{differences} pixels differ. Switching back to a theme has to put every surface back, "
						+ "not only the ones that theme assigns.");
			}
			finally
			{
				travelled?.Close();
				travelled?.Dispose();
				ThemeLibrary.Select("Light");
			}
		}

		/// <summary>
		/// The one the user reported: on Dark, choose Light, and everything is
		/// still dark except the title bar.
		/// </summary>
		[TestMethod]
		public void GoingToDarkAndBackLeavesTheWindowLight() => RoundTrip("Light", "Dark");

		[TestMethod]
		public void GoingToLightAndBackLeavesTheWindowDark() => RoundTrip("Dark", "Light");

		/// <summary>
		/// And the plainest form of the same question: two windows, one that has
		/// only ever been Light and one that has been Dark and come back, must not
		/// differ in a single control's colours.
		/// </summary>
		[TestMethod]
		public void EveryControlComesBackToTheColourItStartedWith()
		{
			ThemeLibrary.Select("Light");
			using Form form = Window();
			form.Show();
			ThemeEngine.Apply(form, ThemeLibrary.Current);
			var before = Colours(form);

			ThemeEngine.Apply(form, ThemeLibrary.Select("Dark"));
			ThemeEngine.Apply(form, ThemeLibrary.Select("Light"));
			var after = Colours(form);

			List<string> wrong = new();
			foreach (var key in before.Keys)
			{
				if (before[key] != after[key]) wrong.Add($"{key}: was {before[key]}, came back as {after[key]}");
			}
			ThemeLibrary.Select("Light");
			Assert.AreEqual(0, wrong.Count, "controls that did not come back:\n" + string.Join("\n", wrong));
		}

		/// <summary>Every control's colours, by a name that says where it is.</summary>
		private static Dictionary<string, string> Colours(Control root, string path = "")
		{
			Dictionary<string, string> found = new(StringComparer.Ordinal);
			var name = $"{path}/{root.GetType().Name}";
			var i = 0;
			while (found.ContainsKey($"{name}#{i}")) i++;
			found[$"{name}#{i}"] = $"back {root.BackColor.ToArgb():X8} fore {root.ForeColor.ToArgb():X8}";
			foreach (Control child in root.Controls)
			{
				foreach (var kvp in Colours(child, name)) found[kvp.Key + "/" + found.Count] = kvp.Value;
			}
			return found;
		}
	}
}
