using System.IO;
using System.Runtime.InteropServices;

namespace Chimera.Common
{
	/// <summary>
	/// The folder picker Windows has had since Vista: the Explorer-style common item dialog in pick-folders mode,
	/// with an address bar and a box to type or paste a path into. SHBrowseForFolder offers only the tree, so
	/// reaching a folder meant opening every folder above it by hand (issue #57).
	/// </summary>
	/// <remarks>
	/// Raw vtables, the way <see cref="Win32ShellContextMenu"/> talks to the shell. Only the members that are
	/// called have a typed slot; the rest are placeholders that keep the offsets right.
	/// </remarks>
	public static unsafe class Win32FolderPicker
	{
		private static readonly Guid CLSID_FileOpenDialog = new("DC1C5A9C-E88A-4dde-A5A1-60F82A20AEF7");
		private static readonly Guid IID_IFileOpenDialog = new("d57c7288-d4ad-4768-be02-9d969532d960");
		private static readonly Guid IID_IFileDialogCustomize = new("e6fdd21a-163f-4975-9c8c-a69f1ba37034");
		private static readonly Guid IID_IShellItem = new("43826d1e-e718-42ee-bc55-a1e261c37bfe");

		/// <remarks>the id of our one added control; any non-zero number the dialog does not use itself will do</remarks>
		private const uint CHECK_BUTTON_ID = 0x1000;

		private const uint FOS_NOCHANGEDIR = 0x8;
		private const uint FOS_PICKFOLDERS = 0x20;
		private const uint FOS_FORCEFILESYSTEM = 0x40;
		private const uint FOS_PATHMUSTEXIST = 0x800;
		private const uint SIGDN_FILESYSPATH = 0x80058000;
		/// <remarks>HRESULT_FROM_WIN32(ERROR_CANCELLED): what Show answers when the person closes the dialog</remarks>
		private const int HR_CANCELLED = unchecked((int)0x800704C7);

		[StructLayout(LayoutKind.Sequential)]
		private struct IShellItem
		{
			[StructLayout(LayoutKind.Sequential)]
			public struct Vtbl
			{
				// IUnknown
				public IntPtr QueryInterface;
				public delegate* unmanaged[Stdcall]<IShellItem*, uint> AddRef;
				public delegate* unmanaged[Stdcall]<IShellItem*, uint> Release;
				// IShellItem
				public IntPtr BindToHandler;
				public IntPtr GetParent;
				public delegate* unmanaged[Stdcall]<IShellItem*, uint, out IntPtr, int> GetDisplayName;
			}

			public Vtbl* lpVtbl;
		}

		[StructLayout(LayoutKind.Sequential)]
		private struct IFileOpenDialog
		{
			[StructLayout(LayoutKind.Sequential)]
			public struct Vtbl
			{
				// IUnknown
				public delegate* unmanaged[Stdcall]<IFileOpenDialog*, Guid*, out IntPtr, int> QueryInterface;
				public delegate* unmanaged[Stdcall]<IFileOpenDialog*, uint> AddRef;
				public delegate* unmanaged[Stdcall]<IFileOpenDialog*, uint> Release;
				// IModalWindow
				public delegate* unmanaged[Stdcall]<IFileOpenDialog*, IntPtr, int> Show;
				// IFileDialog, in order
				public IntPtr SetFileTypes;
				public IntPtr SetFileTypeIndex;
				public IntPtr GetFileTypeIndex;
				public IntPtr Advise;
				public IntPtr Unadvise;
				public delegate* unmanaged[Stdcall]<IFileOpenDialog*, uint, int> SetOptions;
				public delegate* unmanaged[Stdcall]<IFileOpenDialog*, out uint, int> GetOptions;
				public IntPtr SetDefaultFolder;
				public delegate* unmanaged[Stdcall]<IFileOpenDialog*, IShellItem*, int> SetFolder;
				public IntPtr GetFolder;
				public IntPtr GetCurrentSelection;
				public IntPtr SetFileName;
				public IntPtr GetFileName;
				public delegate* unmanaged[Stdcall]<IFileOpenDialog*, char*, int> SetTitle;
				public IntPtr SetOkButtonLabel;
				public IntPtr SetFileNameLabel;
				public delegate* unmanaged[Stdcall]<IFileOpenDialog*, out IShellItem*, int> GetResult;
				// AddPlace ... GetSelectedItems follow, and are never called
			}

			public Vtbl* lpVtbl;
		}

		/// <summary>
		/// The same dialog object asked for its customisation face. QueryInterface on the IFileOpenDialog
		/// returns it; the controls added through it are laid out by the dialog itself, along the bottom.
		/// </summary>
		[StructLayout(LayoutKind.Sequential)]
		private struct IFileDialogCustomize
		{
			[StructLayout(LayoutKind.Sequential)]
			public struct Vtbl
			{
				// IUnknown
				public IntPtr QueryInterface;
				public IntPtr AddRef;
				public delegate* unmanaged[Stdcall]<IFileDialogCustomize*, uint> Release;
				// IFileDialogCustomize, in declaration order; only the two that are called have a typed slot
				public IntPtr EnableOpenDropDown;
				public IntPtr AddMenu;
				public IntPtr AddPushButton;
				public IntPtr AddComboBox;
				public IntPtr AddRadioButtonList;
				public delegate* unmanaged[Stdcall]<IFileDialogCustomize*, uint, char*, int, int> AddCheckButton;
				public IntPtr AddEditBox;
				public IntPtr AddSeparator;
				public IntPtr AddText;
				public IntPtr SetControlLabel;
				public IntPtr GetControlState;
				public IntPtr SetControlState;
				public IntPtr GetEditBoxText;
				public IntPtr SetEditBoxText;
				public delegate* unmanaged[Stdcall]<IFileDialogCustomize*, uint, out int, int> GetCheckButtonState;
				// SetCheckButtonState ... SetControlItemText follow, and are never called
			}

			public Vtbl* lpVtbl;
		}

		/// <summary>
		/// A tick box for the dialog to carry, for a choice that belongs to the act of choosing a folder
		/// rather than to whichever window happens to have the button. <see cref="Checked"/> goes IN as the
		/// state to open on and comes back OUT as what the person left it at.
		/// </summary>
		/// <remarks>
		/// <see cref="Checked"/> is left exactly as the caller set it in every case but one: the person
		/// ticked or unticked the box and then confirmed. A cancelled dialog does not change it, and
		/// neither does a Windows that could not be asked for the control - which <see cref="Shown"/>
		/// reports, so a caller can put the choice somewhere else instead.
		/// </remarks>
		public sealed class CheckButton
		{
			public CheckButton(string label, bool isChecked)
			{
				Label = label;
				Checked = isChecked;
			}

			/// <summary>what is written beside the box</summary>
			public string Label { get; }

			/// <summary>in: the state to open on. out: the state it was confirmed in.</summary>
			public bool Checked { get; set; }

			/// <summary>whether the dialog that came up actually carried the box. False means nobody was asked.</summary>
			public bool Shown { get; internal set; }
		}

		/// <summary>What <see cref="Show"/> came to.</summary>
		public enum Outcome
		{
			Chosen,
			Cancelled,
			/// <summary>This dialog cannot be had here (not Windows, or COM refused it); offer another.</summary>
			Unavailable,
		}

		/// <summary>
		/// Shows the dialog, modal to <paramref name="owner"/>, titled <paramref name="title"/> and opened on
		/// <paramref name="initialFolder"/> when that exists. Must be called on an STA thread, as every WinForms
		/// dialog is.
		/// </summary>
		/// <param name="checkButton">
		/// a tick box for the dialog to carry, read back when the person confirms; null (the default, and what
		/// every caller passed before this existed) is the dialog exactly as it was, with no added control.
		/// </param>
		public static Outcome Show(IntPtr owner, string? title, string? initialFolder, out string? path, CheckButton? checkButton = null)
		{
			path = null;
			if (checkButton is not null) checkButton.Shown = false;
			if (OSTailoredCode.IsUnixHost) return Outcome.Unavailable;
			int hr;
			IntPtr pdlg;
			try
			{
				hr = Ole32Imports.CoCreateInstance(CLSID_FileOpenDialog, IntPtr.Zero, Ole32Imports.CLSCTX.INPROC_SERVER, IID_IFileOpenDialog, out pdlg);
			}
			catch (Exception e) when (e is DllNotFoundException or EntryPointNotFoundException)
			{
				return Outcome.Unavailable;
			}
			if (hr < 0 || pdlg == IntPtr.Zero) return Outcome.Unavailable;

			var dlg = (IFileOpenDialog*)pdlg;
			IFileDialogCustomize* custom = null;
			try
			{
				if (dlg->lpVtbl->GetOptions(dlg, out var options) < 0
					|| dlg->lpVtbl->SetOptions(dlg, options | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST | FOS_NOCHANGEDIR) < 0)
				{
					return Outcome.Unavailable;
				}
				if (!string.IsNullOrEmpty(title))
				{
					fixed (char* t = title) _ = dlg->lpVtbl->SetTitle(dlg, t);
				}
				if (!string.IsNullOrEmpty(initialFolder) && Directory.Exists(initialFolder)
					&& Shell32Imports.SHCreateItemFromParsingName(initialFolder!, IntPtr.Zero, IID_IShellItem, out var pfolder) >= 0
					&& pfolder != IntPtr.Zero)
				{
					var folder = (IShellItem*)pfolder;
					_ = dlg->lpVtbl->SetFolder(dlg, folder);
					folder->lpVtbl->Release(folder);
				}
				// The control has to be added before the dialog is shown, and the same object answers
				// for it: IFileDialogCustomize is another face of this dialog, not another dialog.
				if (checkButton is not null)
				{
					var iid = IID_IFileDialogCustomize;
					if (dlg->lpVtbl->QueryInterface(dlg, &iid, out var pcustom) >= 0 && pcustom != IntPtr.Zero)
					{
						custom = (IFileDialogCustomize*)pcustom;
						fixed (char* label = checkButton.Label)
						{
							checkButton.Shown = custom->lpVtbl->AddCheckButton(custom, CHECK_BUTTON_ID, label, checkButton.Checked ? 1 : 0) >= 0;
						}
						if (!checkButton.Shown)
						{
							custom->lpVtbl->Release(custom);
							custom = null;
						}
					}
				}

				hr = dlg->lpVtbl->Show(dlg, owner);
				if (hr == HR_CANCELLED) return Outcome.Cancelled;
				if (hr < 0) return Outcome.Unavailable;
				// Read only on the path where the person confirmed: a cancelled dialog must leave the
				// caller's value exactly as it was, and the returns below are all cancellations.
				if (custom is not null && custom->lpVtbl->GetCheckButtonState(custom, CHECK_BUTTON_ID, out var state) >= 0)
				{
					checkButton!.Checked = state is not 0;
				}

				if (dlg->lpVtbl->GetResult(dlg, out var item) < 0 || item == null) return Outcome.Cancelled;
				try
				{
					if (item->lpVtbl->GetDisplayName(item, SIGDN_FILESYSPATH, out var pname) < 0 || pname == IntPtr.Zero)
					{
						return Outcome.Cancelled;   // not a place on a disk; FOS_FORCEFILESYSTEM should have prevented it
					}
					try
					{
						path = Marshal.PtrToStringUni(pname);
					}
					finally
					{
						Marshal.FreeCoTaskMem(pname);
					}
				}
				finally
				{
					item->lpVtbl->Release(item);
				}
				return string.IsNullOrEmpty(path) ? Outcome.Cancelled : Outcome.Chosen;
			}
			finally
			{
				if (custom is not null) custom->lpVtbl->Release(custom);
				dlg->lpVtbl->Release(dlg);
			}
		}
	}
}
