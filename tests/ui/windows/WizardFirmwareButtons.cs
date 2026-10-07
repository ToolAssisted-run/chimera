// Does the new-project wizard's firmware page lay out its row of buttons -
// Select File..., Clear, Scan Folder... - without one covering the next, on
// the WinForms Chimera's users actually see? (chimera issue #160)
//
// The automated suite cannot answer it. It runs on Mono under Xvfb, whose
// default font is wider than Windows' Segoe UI, so every fixed position is
// scaled up by about 1.17 while a button keeps its 75-pixel default width -
// and at that scale the old fixed positions happened to leave a gap. On
// Windows at 100% they did not: Clear, at x=110 and never narrower than 75,
// ended at 185 and covered the first seven pixels of Scan Folder at 178.
//
// It opens the real wizard, finds the three buttons, and fails if any one
// reaches past the start of the next. An AutoSize button grows but never
// shrinks below its default size, so its right edge is the wider of Width
// and PreferredSize. It also draws the row, so a person can see it.
//
// Written for the C# 5 compiler that ships with Windows (csc.exe): no string
// interpolation, no null-conditional operators, no local functions.

using System;
using System.Collections.Generic;
using System.Drawing;
using System.Drawing.Imaging;
using System.Linq;
using System.Windows.Forms;

using Chimera.Client.Common;
using Chimera.Client.GUI;

static class WizardFirmwareButtons
{
	static string PickFolder(ref bool includeSubfolders) { return ""; }

	static IEnumerable<Control> All(Control root)
	{
		foreach (Control c in root.Controls)
		{
			yield return c;
			foreach (var d in All(c)) yield return d;
		}
	}

	static int RightOf(Control c) { return c.Left + Math.Max(c.Width, c.PreferredSize.Width); }

	[STAThread]
	static int Main(string[] args)
	{
		Application.EnableVisualStyles();
		var form = new NewProjectWizard(new DiscoveredCorePackage[0], slot => new string[0], pickFirmwareFolder: PickFolder);
		form.Show();
		Application.DoEvents();

		var all = All(form).ToList();
		Func<string, Control> find = text => all.Single(c => c.Text == text);
		var row = new[] { find("Select File..."), find("Clear"), find("Scan Folder...") };

		int failures = 0;
		foreach (var b in row)
		{
			Console.WriteLine(string.Format("{0,-15} left {1,4}  width {2,4}  preferred {3,4}  right {4,4}",
				b.Text, b.Left, b.Width, b.PreferredSize.Width, RightOf(b)));
		}
		for (int i = 0; i + 1 < row.Length; i++)
		{
			if (RightOf(row[i]) > row[i + 1].Left)
			{
				Console.WriteLine(string.Format("FAIL: \"{0}\" ends at {1}, past the start of \"{2}\" at {3}",
					row[i].Text, RightOf(row[i]), row[i + 1].Text, row[i + 1].Left));
				failures++;
			}
			if (row[i].Top != row[i + 1].Top)
			{
				Console.WriteLine(string.Format("FAIL: \"{0}\" and \"{1}\" are not on one row", row[i].Text, row[i + 1].Text));
				failures++;
			}
		}

		// the row as drawn, each button where it sits, the later ones on top as on screen
		if (args.Length > 0)
		{
			int width = RightOf(row[row.Length - 1]) + 10, height = row[0].Height + 10;
			using (var bmp = new Bitmap(width, height))
			{
				using (var g = Graphics.FromImage(bmp)) g.Clear(SystemColors.Control);
				foreach (var b in row) b.DrawToBitmap(bmp, new Rectangle(b.Left, 5, b.Width, b.Height));
				bmp.Save(args[0] + "\\wizard-firmware-buttons.png", ImageFormat.Png);
			}
		}

		Console.WriteLine(failures == 0 ? "PASS: no button on the firmware row covers the next" : "FAILED");
		form.Close();
		return failures == 0 ? 0 : 1;
	}
}
