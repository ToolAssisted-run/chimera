using System.ComponentModel;
using System.Runtime.InteropServices;
using System.Threading;
using System.Windows.Forms;

using Chimera.Common;

using static Chimera.Common.Shell32Imports;

namespace Chimera.Client.GUI
{
	/// <summary>
	/// The one folder picker. On Windows it is the Explorer-style dialog with an address bar and a box to
	/// type or paste a path into (<see cref="Win32FolderPicker"/>, issue #57); the old Browse For Folder tree
	/// below is only what is left if that dialog cannot be had. Elsewhere it is the toolkit's own dialog.
	/// Call ShowDialog() to bring it up.
	/// </summary>
	/// <remarks>
	/// I believe this code is from http://support.microsoft.com/kb/306285<br/>
	/// The license is assumed to be effectively public domain.<br/>
	/// I saw a version of it with at least one bug fixed at https://github.com/slavat/MailSystem.NET/blob/master/Queuing%20System/ActiveQLibrary/CustomControl/FolderBrowser.cs<br/>
	/// --zeromus
	/// </remarks>
	public sealed class FolderBrowserEx : Component
	{
		private const BROWSEINFOW.FLAGS BrowseOptions = BROWSEINFOW.FLAGS.RestrictToFilesystem | BROWSEINFOW.FLAGS.RestrictToDomain |
			BROWSEINFOW.FLAGS.NewDialogStyle | BROWSEINFOW.FLAGS.ShowTextBox;

		public string Description = "Please select a folder below:";

		public string SelectedPath;

		/// <summary>Shows the folder picker with the specified owner window.</summary>
		public DialogResult ShowDialog(IWin32Window owner = null)
		{
			if (OSTailoredCode.IsUnixHost)
			{
				// Mono's own dialog: none of the Windows shell is there
				using FolderBrowserDialog toolkit = new() { Description = Description, SelectedPath = SelectedPath ?? string.Empty };
				var chosen = toolkit.ShowDialog(owner);
				if (chosen is DialogResult.OK) SelectedPath = toolkit.SelectedPath;
				return chosen;
			}

			var hWndOwner = owner?.Handle ?? WmImports.GetActiveWindow();
			switch (Win32FolderPicker.Show(hWndOwner, Description, SelectedPath, out var picked))
			{
				case Win32FolderPicker.Outcome.Chosen:
					SelectedPath = picked;
					return DialogResult.OK;
				case Win32FolderPicker.Outcome.Cancelled:
					return DialogResult.Cancel;
				default:
					return ShowTreeDialog(hWndOwner);
			}
		}

		/// <summary>SHBrowseForFolder, for a Windows where the item dialog is not to be had.</summary>
		private DialogResult ShowTreeDialog(IntPtr hWndOwner)
		{
			const int startLocation = 0; // = Desktop CSIDL
			int Callback(IntPtr hwnd, uint uMsg, IntPtr lParam, IntPtr lpData)
			{
				if (uMsg == BFFM_INITIALIZED)
				{
					var str = Marshal.StringToHGlobalUni(SelectedPath);
					try
					{
						WmImports.SendMessageW(hwnd, BFFM_SETSELECTIONW, new(1), str);
					}
					finally
					{
						Marshal.FreeHGlobal(str);
					}
				}

				return 0;
			}

			_ = SHGetSpecialFolderLocation(hWndOwner, startLocation, out var pidlRoot);
			if (pidlRoot == IntPtr.Zero)
			{
				return DialogResult.Cancel;
			}

			var pidlRet = IntPtr.Zero;
			var pszDisplayName = IntPtr.Zero;
			try
			{
				var browseOptions = BrowseOptions;
				if (ApartmentState.MTA == Application.OleRequired())
				{
					browseOptions &= ~BROWSEINFOW.FLAGS.NewDialogStyle;
				}

				pszDisplayName = Marshal.AllocCoTaskMem(Win32Imports.MAX_PATH * sizeof(char));
				var bi = new BROWSEINFOW
				{
					hwndOwner = hWndOwner,
					pidlRoot = pidlRoot,
					pszDisplayName = pszDisplayName,
					lpszTitle = Description,
					ulFlags = browseOptions,
					lpfn = Callback,
				};

				pidlRet = SHBrowseForFolderW(ref bi);
				if (pidlRet == IntPtr.Zero)
				{
					return DialogResult.Cancel; // user clicked Cancel
				}

				var path = new char[Win32Imports.MAX_PATH];
				if (SHGetPathFromIDListW(pidlRet, path) == 0)
				{
					return DialogResult.Cancel;
				}

				SelectedPath = new string(path).TrimEnd('\0');
			}
			finally
			{
				Marshal.FreeCoTaskMem(pidlRoot);
				Marshal.FreeCoTaskMem(pidlRet);
				Marshal.FreeCoTaskMem(pszDisplayName);
			}

			return DialogResult.OK;
		}
	}
}
