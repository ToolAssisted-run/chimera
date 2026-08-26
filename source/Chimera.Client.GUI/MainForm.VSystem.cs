using System.Collections.Generic;
using System.Windows.Forms;

using Chimera.Client.Common;
using Chimera.Emulation.Common;
using Chimera.WinForms.Controls;

namespace Chimera.Client.GUI
{
	public partial class MainForm
	{
		private enum VSystemCategory : int
		{
			Consoles = 0,
			Handhelds = 1,
			PCs = 2,
			Other = 3,
		}

		/// <summary>
		/// Rebuilds the "Emulator" menu for whatever core is running.
		///
		/// The menu is named "Emulator", always, and never after the system: a
		/// top-level menu whose title moves around as you load games is a menu the
		/// user has to re-find every time. It is absent until a project is running;
		/// its CONTENTS are what varies by core.
		/// </summary>
		private void HandlePlatformMenus()
		{
			DisplayDefaultCoreMenu();
			// everything in it comes from a core, and the only way to a core is a
			// project: with none there is nothing to show, so it does not appear
			GenericCoreSubMenu.Visible = GenericCoreSubMenu.DropDownItems.Count is not 0;

			// the same goes for "System": pause, reboot and the core's name are all
			// about a machine that is running
			SystemSubMenu.Visible = !Emulator.IsNull();
		}
	}
}
