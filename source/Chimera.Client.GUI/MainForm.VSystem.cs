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
			// Everything in either menu is about a machine: the Emulator menu holds
			// what a core brought, and System pauses and reboots it. With none
			// running - before the first project, and after one is closed - neither
			// has anything to say, so neither appears. (A loaded package still
			// declaring firmware is not a reason to show a menu: there is nothing
			// for that firmware to be part of yet.)
			var running = !Emulator.IsNull();
			GenericCoreSubMenu.Visible = running && GenericCoreSubMenu.DropDownItems.Count is not 0;
			SystemSubMenu.Visible = running;

			// every icon down there reports on the running machine: what is playing,
			// whether it is paused, what is frozen, which core it is
			MainStatusBar.Visible = running;
		}
	}
}
