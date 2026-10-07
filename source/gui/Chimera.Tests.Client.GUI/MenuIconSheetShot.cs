using System.Drawing;
using System.Linq;
using System.Windows.Forms;

using Chimera.Client.GUI;

namespace Chimera.Tests.Client.GUI
{
	/// <summary>
	/// A sheet of every menu icon, on a real menu, once per theme.
	///
	/// A menu cannot be opened for a photograph without something to click it, and
	/// the menus themselves live on MainForm, which needs a running frontend. This
	/// shows the same thing more usefully anyway: every icon at once, at the size
	/// it is actually drawn, against the colour it will actually sit on - which is
	/// what a person needs in order to say "that one does not read".
	/// </summary>
	[TestClass]
	public class MenuIconSheetShot
	{
		[TestMethod]
		public void MenuIconSheet()
		{
			var icons = MenuIcons.All();
			var perColumn = (icons.Count + 2) / 3;

			Form form = new()
			{
				ClientSize = new(760, (perColumn * 24) + 24),
				StartPosition = FormStartPosition.Manual,
				Location = new(0, 0),
				FormBorderStyle = FormBorderStyle.None,
			};

			for (var column = 0; column < 3; column++)
			{
				MenuStrip strip = new()
				{
					Dock = DockStyle.None,
					Bounds = new(column * 253, 0, 253, form.ClientSize.Height),
					LayoutStyle = ToolStripLayoutStyle.VerticalStackWithOverflow,
					GripStyle = ToolStripGripStyle.Hidden,
				};
				foreach (var (name, image) in icons.Skip(column * perColumn).Take(perColumn))
				{
					strip.Items.Add(new ToolStripMenuItem(name, image));
				}
				form.Controls.Add(strip);
			}

			UiShots.Shoot(form, "menu-icons");
			form.Dispose();
		}
	}
}
