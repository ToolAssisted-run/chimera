using System.Collections.Generic;
using System.Windows.Forms;

using BizHawk.Client.Common;
using BizHawk.Emulation.Common;
using BizHawk.WinForms.Controls;

namespace BizHawk.Client.EmuHawk
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

		private IReadOnlyCollection<ToolStripItem> CreateCoreSettingsSubmenus(bool includeDupes = false)
		{
			List<ToolStripItem> items = new();
			foreach (var factory in CoreRegistry.Instance.AllFactories)
			{
				var coreName = factory.CoreName;
				var settingsItem = new ToolStripMenuItemEx { Text = "Settings..." };
				settingsItem.Click += (_, _) => GenericCoreConfig.DoDialogFor(
					this,
					GetSettingsAdapterFor(factory),
					$"{coreName} Settings",
					isMovieActive: MovieSession.Movie.IsActive());
				var submenu = new ToolStripMenuItemEx { Tag = VSystemCategory.Consoles, Text = coreName };
				submenu.DropDownItems.Add(settingsItem);
				items.Add(submenu);
			}
			return items;
		}

		private void HandlePlatformMenus()
		{
			if (GenericCoreSubMenu.Visible)
			{
				var i = GenericCoreSubMenu.Text.IndexOf('&');
				if (i != -1) AvailableAccelerators.Add(GenericCoreSubMenu.Text[i + 1]);
			}
			NullHawkVSysSubmenu.Visible = false;
			GenericCoreSubMenu.Visible = false;

			switch (Emulator.SystemId)
			{
				case VSystemID.Raw.NULL:
					NullHawkVSysSubmenu.Visible = true;
					break;
				default:
					DisplayDefaultCoreMenu();
					break;
			}
		}
	}
}
