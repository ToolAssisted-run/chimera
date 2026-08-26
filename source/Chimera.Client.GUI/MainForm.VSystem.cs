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
		/// user has to re-find every time. It stays in place and greys out when there
		/// is nothing loaded; its CONTENTS are what varies by core.
		/// </summary>
		private void HandlePlatformMenus()
		{
			DisplayDefaultCoreMenu();
			// the menu is exactly as useful as its contents: everything in it comes from a
			// core, so it is dead until one is opened
			GenericCoreSubMenu.Enabled = GenericCoreSubMenu.DropDownItems.Count is not 0;
		}
	}
}
