using System.ComponentModel;
using System.Runtime.InteropServices;
using System.Threading;
using System.Windows.Forms;

using Chimera.Common;

using static Chimera.Common.Shell32Imports;

namespace Chimera.Client.GUI
{
	/// <summary>
	/// Picks a folder to scan, carrying the "walk sub-folders too" choice with it: the flag goes IN as the
	/// state to offer and comes back OUT as what the person settled on. A null return means no folder was
	/// chosen, and then the flag means nothing and must not be read.
	/// </summary>
	/// <remarks>
	/// One delegate for every Scan Folder button there is, because the choice belongs to the act of choosing
	/// a folder and not to the page holding the button - which is how it came to exist on one page out of
	/// three. On Windows the picker carries the tick box itself (<see cref="FolderBrowserEx.CheckBoxLabel"/>);
	/// where it cannot, the flag comes back exactly as it went in and the caller decides what to do about it.
	/// </remarks>
	public delegate string PickScanFolder(ref bool includeSubfolders);

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

		/// <summary>
		/// The one wording for the sub-folders choice, wherever it is offered - the picker's own tick box,
		/// and the page-level box that stands in for it on a toolkit that cannot carry one. Two spellings of
		/// one setting read as two settings.
		/// </summary>
		public const string ScanSubfoldersLabel = "Include sub-folders";

		public string Description = "Please select a folder below:";

		public string SelectedPath;

		/// <summary>
		/// What to write beside a tick box in the dialog itself, for a choice that belongs to choosing the
		/// folder (today: whether a scan walks below it). Null - the default - is no tick box, and then the
		/// dialog is in every respect the one that was here before.
		/// </summary>
		public string CheckBoxLabel;

		/// <summary>
		/// The tick box's state: in, what it opens on; out, what it was confirmed at. It is changed ONLY by a
		/// person ticking the box and pressing Select Folder. Cancel leaves it alone, and so does every path
		/// that has no tick box to offer - see <see cref="CheckBoxShown"/>.
		/// </summary>
		public bool CheckBoxChecked;

		/// <summary>
		/// Whether the dialog that just came up actually carried the tick box. False after any of the three
		/// pickers that cannot hold one - Mono's, the SHBrowseForFolder tree, and a Windows whose shell
		/// refused the customisation face - and then <see cref="CheckBoxChecked"/> is untouched, because
		/// nobody was asked. A window that needs the answer regardless must carry the choice itself.
		/// </summary>
		public bool CheckBoxShown { get; private set; }

		/// <summary>
		/// Whether this platform's picker can carry a tick box at all. Asked BEFORE any dialog opens, by a
		/// window deciding whether to put a check box of its own on the page. It answers for the platform,
		/// not for one dialog: a Windows that falls back to the tree has no tick box on that occasion, which
		/// only <see cref="CheckBoxShown"/> can say, and then the scan runs on the state passed in.
		/// </summary>
		public static bool CanShowCheckBox => !OSTailoredCode.IsUnixHost;

		/// <summary>Shows the folder picker with the specified owner window.</summary>
		public DialogResult ShowDialog(IWin32Window owner = null)
		{
			CheckBoxShown = false;
			if (OSTailoredCode.IsUnixHost)
			{
				// Mono's own dialog: none of the Windows shell is there, and no room for a control of
				// ours - so CheckBoxChecked comes back exactly as it went in, deliberately.
				using FolderBrowserDialog toolkit = new() { Description = Description, SelectedPath = SelectedPath ?? string.Empty };
				var chosen = toolkit.ShowDialog(owner);
				if (chosen is DialogResult.OK) SelectedPath = toolkit.SelectedPath;
				return chosen;
			}

			var hWndOwner = owner?.Handle ?? WmImports.GetActiveWindow();
			var check = CheckBoxLabel is null ? null : new Win32FolderPicker.CheckButton(CheckBoxLabel, CheckBoxChecked);
			var outcome = Win32FolderPicker.Show(hWndOwner, Description, SelectedPath, out var picked, check);
			CheckBoxShown = check is { Shown: true };
			switch (outcome)
			{
				case Win32FolderPicker.Outcome.Chosen:
					SelectedPath = picked;
					// only here: a box that was shown and a folder that was confirmed
					if (CheckBoxShown) CheckBoxChecked = check.Checked;
					return DialogResult.OK;
				case Win32FolderPicker.Outcome.Cancelled:
					return DialogResult.Cancel;
				default:
					// The tree has no custom control either; CheckBoxChecked stays as the caller set it,
					// so the scan behaves as it did before the option existed.
					CheckBoxShown = false;
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
