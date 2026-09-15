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
		private static readonly Guid IID_IShellItem = new("43826d1e-e718-42ee-bc55-a1e261c37bfe");

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
				public IntPtr QueryInterface;
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
		public static Outcome Show(IntPtr owner, string? title, string? initialFolder, out string? path)
		{
			path = null;
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

				hr = dlg->lpVtbl->Show(dlg, owner);
				if (hr == HR_CANCELLED) return Outcome.Cancelled;
				if (hr < 0) return Outcome.Unavailable;

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
				dlg->lpVtbl->Release(dlg);
			}
		}
	}
}
