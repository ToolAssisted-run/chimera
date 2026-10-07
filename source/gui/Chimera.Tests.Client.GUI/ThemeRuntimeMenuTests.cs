using System.Windows.Forms;

using Chimera.Client.Common;
using Chimera.Client.GUI;

namespace Chimera.Tests.Client.GUI
{
	/// <summary>
	/// A menu built while its window is open is themed too. TAStudio's Columns menu
	/// is rebuilt for every project, grouping its keys and players into sub-menus of
	/// its own; the walk that themed the window never saw them, so their text came
	/// out grey on the dark theme's background (#173).
	/// </summary>
	[TestClass]
	public class ThemeRuntimeMenuTests
	{
		[TestCleanup]
		public void Light() => ThemeLibrary.Select("Light");

		[TestMethod]
		public void ASubMenuAddedLaterIsThemedWhenItsParentOpens()
		{
			var dark = ThemeLibrary.Select("Dark");
			using Form form = new();
			MenuStrip menu = new();
			ToolStripMenuItem columns = new("&Columns");
			columns.DropDownItems.Add(new ToolStripMenuItem("placeholder"));
			menu.Items.Add(columns);
			form.Controls.Add(menu);
			form.MainMenuStrip = menu;
			form.Show();
			ThemeEngine.Apply(form, dark);

			// what TAStudio does on every project: clear and rebuild, with groups
			columns.DropDownItems.Clear();
			ToolStripMenuItem keys = new("Keys (1 - F9)");
			ToolStripMenuItem key = new("1 (Key 1)");
			keys.DropDownItems.Add(key);
			columns.DropDownItems.Add(keys);

			columns.ShowDropDown();
			Application.DoEvents();

			Assert.IsInstanceOfType(keys.DropDown.Renderer, typeof(ThemeToolStripRenderer), "the group's own drop-down paints with the theme");
			Assert.AreEqual(dark[ThemeColorRole.MenuText].ToArgb(), key.ForeColor.ToArgb(), "and its items' text is the theme's menu text");
			Assert.AreEqual(dark[ThemeColorRole.MenuBackground].ToArgb(), keys.DropDown.BackColor.ToArgb());
			columns.HideDropDown();
		}
	}
}
