// Does the WINDOWS folder picker actually carry the "Include sub-folders" tick
// box, and is what the person left it at what the caller gets back?
//
// The automated suite cannot answer that. It runs on Linux, on Mono, under
// Xvfb, and the tick box is a control added to the Windows shell's own common
// item dialog through IFileDialogCustomize - raw COM vtables, against a shell
// that is not there. Every Linux test of this feature stops at the delegate:
// it proves the wizard offers the right value and honours what comes back, and
// proves nothing whatever about the dialog. This is the other half.
//
// It asks three questions of the real dialog:
//
//   1. is the box there at all, written with the label it was given, and
//      opened in the state it was given?
//   2. does unticking it and confirming come back as false?
//   3. does CANCELLING leave the caller's value exactly as it was?
//
// It drives the dialog from a second thread (the dialog owns the first one, as
// every modal dialog does), by finding the check box among the dialog's child
// windows and clicking it the way a mouse would. It photographs with
// PrintWindow, which renders ONE window - the dialog this program opened - into
// a bitmap of ours. It reads no screen pixels, so nothing else on the desktop
// can be captured even by accident.
//
// Run it with folder-picker-checkbox.sh. Windows only. Pass --by-hand to drive
// the dialogs yourself instead, which is the check nothing automated can
// replace: that the box LOOKS like part of the dialog and can be clicked.
using System;
using System.Collections.Generic;
using System.Drawing;
using System.Drawing.Imaging;
using System.IO;
using System.Runtime.InteropServices;
using System.Text;
using System.Threading;
using System.Windows.Forms;

using Chimera.Client.GUI;

internal static class FolderPickerCheckBox
{
	private const string Label = "Include sub-folders";

	[DllImport("user32.dll")] private static extern int PrintWindow(IntPtr hwnd, IntPtr hdc, uint flags);
	[DllImport("user32.dll")] private static extern bool EnumWindows(EnumProc proc, IntPtr lParam);
	[DllImport("user32.dll")] private static extern bool EnumChildWindows(IntPtr parent, EnumProc proc, IntPtr lParam);
	[DllImport("user32.dll")] private static extern uint GetWindowThreadProcessId(IntPtr hwnd, out uint pid);
	[DllImport("user32.dll", CharSet = CharSet.Unicode)] private static extern int GetClassNameW(IntPtr hwnd, StringBuilder text, int max);
	[DllImport("user32.dll", CharSet = CharSet.Unicode)] private static extern int GetWindowTextW(IntPtr hwnd, StringBuilder text, int max);
	[DllImport("user32.dll")] private static extern bool IsWindowVisible(IntPtr hwnd);
	[DllImport("user32.dll")] private static extern bool IsWindow(IntPtr hwnd);
	[DllImport("user32.dll")] private static extern IntPtr GetDlgItem(IntPtr dlg, int id);
	[DllImport("user32.dll", CharSet = CharSet.Unicode)] private static extern IntPtr SendMessageW(IntPtr hwnd, uint msg, IntPtr wParam, IntPtr lParam);

	private delegate bool EnumProc(IntPtr hwnd, IntPtr lParam);

	private const uint BM_GETCHECK = 0x00F0;
	private const uint BM_CLICK = 0x00F5;
	private const uint PW_RENDERFULLCONTENT = 2;

	private static string TextOf(IntPtr hwnd)
	{
		var sb = new StringBuilder(512);
		GetWindowTextW(hwnd, sb, sb.Capacity);
		return sb.ToString();
	}

	private static string ClassOf(IntPtr hwnd)
	{
		var sb = new StringBuilder(256);
		GetClassNameW(hwnd, sb, sb.Capacity);
		return sb.ToString();
	}

	// the dialog is a #32770 of this process, titled with the Description we passed
	private static IntPtr FindDialog(string title)
	{
		var pid = (uint) System.Diagnostics.Process.GetCurrentProcess().Id;
		var found = IntPtr.Zero;
		EnumWindows(delegate(IntPtr hwnd, IntPtr _)
		{
			uint owner;
			GetWindowThreadProcessId(hwnd, out owner);
			if (owner != pid || !IsWindowVisible(hwnd)) return true;
			if (ClassOf(hwnd) != "#32770") return true;
			if (TextOf(hwnd) != title) return true;
			found = hwnd;
			return false;
		}, IntPtr.Zero);
		return found;
	}

	private static List<IntPtr> Descendants(IntPtr parent)
	{
		var all = new List<IntPtr>();
		EnumChildWindows(parent, delegate(IntPtr hwnd, IntPtr _) { all.Add(hwnd); return true; }, IntPtr.Zero);
		return all;
	}

	private static string Plain(string s) { return s.Replace("&", "").Trim(); }

	private static IntPtr FindButton(IntPtr dialog, string text)
	{
		foreach (var child in Descendants(dialog))
		{
			if (!ClassOf(child).StartsWith("Button", StringComparison.OrdinalIgnoreCase)) continue;
			if (Plain(TextOf(child)) == Plain(text)) return child;
		}
		return IntPtr.Zero;
	}

	private static Bitmap Shoot(IntPtr hwnd)
	{
		var rect = new RECT();
		GetWindowRect(hwnd, ref rect);
		var w = Math.Max(1, rect.Right - rect.Left);
		var h = Math.Max(1, rect.Bottom - rect.Top);
		var shot = new Bitmap(w, h, PixelFormat.Format32bppArgb);
		using (var g = Graphics.FromImage(shot))
		{
			var hdc = g.GetHdc();
			try { PrintWindow(hwnd, hdc, PW_RENDERFULLCONTENT); }
			finally { g.ReleaseHdc(hdc); }
		}
		return shot;
	}

	[StructLayout(LayoutKind.Sequential)] private struct RECT { public int Left, Top, Right, Bottom; }
	[DllImport("user32.dll")] private static extern bool GetWindowRect(IntPtr hwnd, ref RECT rect);

	/// <summary>What the driver thread saw and did, read by the main thread once the dialog is gone.</summary>
	private sealed class Report
	{
		public bool DialogFound;
		public bool BoxFound;
		public bool BoxCheckedAtOpen;
		public string Trouble = "";
		public string Shot = "";
	}

	// Waits for the dialog, reports the tick box, optionally clicks it, then
	// presses Select Folder or Cancel. Runs OFF the dialog's thread on purpose:
	// the dialog owns that one until it closes.
	private static Report Drive(string title, bool clickTheBox, bool confirm, string shotPath)
	{
		var seen = new Report();
		var dialog = IntPtr.Zero;
		for (var i = 0; i < 100 && dialog == IntPtr.Zero; i++)
		{
			Thread.Sleep(100);
			dialog = FindDialog(title);
		}
		if (dialog == IntPtr.Zero) { seen.Trouble = "the dialog never appeared"; return seen; }
		seen.DialogFound = true;
		Thread.Sleep(600); // let it lay its controls out

		var box = FindButton(dialog, Label);
		if (box == IntPtr.Zero)
		{
			seen.Trouble = "no control on the dialog is written \"" + Label + "\"";
		}
		else
		{
			seen.BoxFound = true;
			seen.BoxCheckedAtOpen = SendMessageW(box, BM_GETCHECK, IntPtr.Zero, IntPtr.Zero) != IntPtr.Zero;
		}

		if (shotPath.Length != 0)
		{
			using (var shot = Shoot(dialog)) { shot.Save(shotPath, ImageFormat.Png); }
			seen.Shot = shotPath;
		}

		if (box != IntPtr.Zero && clickTheBox)
		{
			SendMessageW(box, BM_CLICK, IntPtr.Zero, IntPtr.Zero);
			Thread.Sleep(200);
		}

		// IDOK / IDCANCEL. The common item dialog answers to both; the text
		// search is the fallback, because the OK button is relabelled here.
		var press = GetDlgItem(dialog, confirm ? 1 : 2);
		if (press == IntPtr.Zero) press = FindButton(dialog, confirm ? "Select Folder" : "Cancel");
		if (press == IntPtr.Zero)
		{
			seen.Trouble += (seen.Trouble.Length == 0 ? "" : "; ") + "no " + (confirm ? "Select Folder" : "Cancel") + " button found";
			return seen;
		}
		for (var i = 0; i < 40 && IsWindow(dialog) && IsWindowVisible(dialog); i++)
		{
			SendMessageW(press, BM_CLICK, IntPtr.Zero, IntPtr.Zero);
			Thread.Sleep(150);
		}
		return seen;
	}

	private static int Failures;

	private static void Check(bool ok, string what)
	{
		Console.WriteLine((ok ? "  ok   " : "  FAIL ") + what);
		if (!ok) Failures++;
	}

	[STAThread]
	private static int Main(string[] args)
	{
		Application.EnableVisualStyles();
		Application.SetCompatibleTextRenderingDefault(false);
		var byHand = Array.IndexOf(args, "--by-hand") >= 0;
		var shotDir = args.Length > 0 && !args[0].StartsWith("--") ? args[0] : Path.GetTempPath();

		var folder = Path.Combine(Path.GetTempPath(), "chimera-folder-picker-check");
		Directory.CreateDirectory(folder);
		Directory.CreateDirectory(Path.Combine(folder, "a sub-folder"));

		Console.WriteLine("platform says the picker can carry a tick box: " + FolderBrowserEx.CanShowCheckBox);
		Check(FolderBrowserEx.CanShowCheckBox, "this Windows should say it can");

		if (byHand)
		{
			ByHand("ticked going in", folder, true);
			ByHand("unticked going in", folder, false);
			return 0;
		}

		// 1 + 2: the box is there, ticked as asked, and unticking it and
		// confirming is what the caller gets back.
		var picker = new FolderBrowserEx
		{
			Description = "Scan a folder for firmware files",
			SelectedPath = folder,
			CheckBoxLabel = Label,
			CheckBoxChecked = true,
		};
		var shot = Path.Combine(shotDir, "folder-picker-checkbox.png");
		Report seen = null;
		var driver = new Thread(delegate() { seen = Drive(picker.Description, true, true, shot); });
		driver.Start();
		var result = picker.ShowDialog();
		driver.Join(30000);

		Console.WriteLine("dialog found: " + seen.DialogFound
			+ "   box found: " + seen.BoxFound
			+ "   box state at open: " + seen.BoxCheckedAtOpen
			+ (seen.Trouble.Length == 0 ? "" : "   trouble: " + seen.Trouble));
		if (seen.Shot.Length != 0) Console.WriteLine("picture of the dialog: " + seen.Shot);
		Console.WriteLine("came back: " + result + "   path " + picker.SelectedPath
			+ "   CheckBoxShown " + picker.CheckBoxShown + "   CheckBoxChecked " + picker.CheckBoxChecked);

		Check(seen.BoxFound, "the dialog draws a control labelled \"" + Label + "\"");
		Check(seen.BoxCheckedAtOpen, "it opens in the state the caller passed in (ticked)");
		Check(picker.CheckBoxShown, "FolderBrowserEx reports that the box was shown");
		Check(result == DialogResult.OK, "Select Folder came back as OK");
		Check(string.Equals(picker.SelectedPath, folder, StringComparison.OrdinalIgnoreCase),
			"the folder it opened on is the folder it returned");
		Check(!picker.CheckBoxChecked, "unticking the box and confirming comes back FALSE");

		// 3: cancelling changes nothing.
		var cancelled = new FolderBrowserEx
		{
			Description = "Scan a folder for the project's files",
			SelectedPath = folder,
			CheckBoxLabel = Label,
			CheckBoxChecked = true,
		};
		Report seenCancel = null;
		var driver2 = new Thread(delegate() { seenCancel = Drive(cancelled.Description, true, false, ""); });
		driver2.Start();
		var result2 = cancelled.ShowDialog();
		driver2.Join(30000);

		Console.WriteLine("cancelled run: box found " + seenCancel.BoxFound
			+ "   came back " + result2 + "   CheckBoxChecked " + cancelled.CheckBoxChecked
			+ (seenCancel.Trouble.Length == 0 ? "" : "   trouble: " + seenCancel.Trouble));
		Check(result2 == DialogResult.Cancel, "Cancel came back as Cancel");
		Check(cancelled.CheckBoxChecked,
			"the box was UNTICKED and then the dialog cancelled: the caller's value must be untouched");

		Console.WriteLine(Failures == 0 ? "PASS" : "FAIL: " + Failures + " of the checks above");
		return Failures == 0 ? 0 : 1;
	}

	private static void ByHand(string what, string folder, bool initial)
	{
		Console.WriteLine();
		Console.WriteLine("--- " + what + " --- tick or untick as you like, then Select Folder or Cancel");
		var picker = new FolderBrowserEx
		{
			Description = "Scan a folder for firmware files",
			SelectedPath = folder,
			CheckBoxLabel = Label,
			CheckBoxChecked = initial,
		};
		var result = picker.ShowDialog();
		Console.WriteLine("came back: " + result + "   path " + picker.SelectedPath
			+ "   CheckBoxShown " + picker.CheckBoxShown + "   CheckBoxChecked " + picker.CheckBoxChecked);
	}
}
