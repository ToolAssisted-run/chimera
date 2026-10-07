// Asks, on Windows, whether an item marked Alignment = Right in a FLOWING
// MenuStripEx lands on the right edge of its row - which a stock MenuStrip in
// Flow layout never does - at a wide window, at a narrow one where the menu
// wraps, after being hidden and shown again, and after the window is resized.
// The main window's "Show TAStudio >>" is such an item (user request,
// 2026-09-24). Writes a picture of each case and exits non-zero on a miss.
using System;
using System.Collections.Generic;
using System.Drawing;
using System.Drawing.Imaging;
using System.Windows.Forms;

using Chimera.WinForms.Controls;

internal static class MenuRightAlign
{
	private static int _failures;
	private static Form _form;
	private static MenuStripEx _menu;
	private static ToolStripMenuItem _right;
	private static readonly List<Bitmap> Shots = new List<Bitmap>();

	// csc.exe of .NET Framework 4 is C# 5: no interpolation, no local functions
	private static void Check(string what)
	{
		Application.DoEvents();
		int gap = _menu.DisplayRectangle.Right - _right.Bounds.Right;
		ToolStripItem last = _menu.Items[_menu.Items.Count - 2];
		bool sameRowAsLast = _right.Bounds.Top == last.Bounds.Top;
		// on Help's row it must sit after Help; where it does not fit there it wraps
		// onto a row of its own, and is pushed right on that one
		bool placed = sameRowAsLast ? _right.Bounds.Left >= last.Bounds.Right : _right.Bounds.Top > last.Bounds.Top;
		bool ok = _right.Available && gap >= 0 && gap <= 4 && placed;
		Console.WriteLine("{0} {1}: width {2}, item {3}, gap to edge {4}, row {5}",
			ok ? "PASS" : "FAIL", what, _form.ClientSize.Width, _right.Bounds, gap, sameRowAsLast ? "shared with Help" : "its own");
		if (!ok) _failures++;
		var shot = new Bitmap(_form.Width, _form.Height);
		_form.DrawToBitmap(shot, new Rectangle(0, 0, _form.Width, _form.Height));
		Shots.Add(shot);
	}

	[STAThread]
	private static int Main(string[] args)
	{
		string picture = args.Length > 0 ? args[0] : "menu-right-align.png";
		_form = new Form { Width = 800, Height = 120, StartPosition = FormStartPosition.Manual, Location = new Point(40, 40), Text = "MenuRightAlign" };
		_menu = new MenuStripEx { LayoutStyle = ToolStripLayoutStyle.Flow };
		foreach (string name in new[] { "&File", "&Emulation", "&View", "&Config", "&Tools", "&NES", "&Help" })
			_menu.Items.Add(new ToolStripMenuItem(name));
		_right = new ToolStripMenuItem("Show TAStudio >>") { Alignment = ToolStripItemAlignment.Right, Available = false };
		_menu.Items.Add(_right);
		_form.Controls.Add(_menu);
		_form.MainMenuStrip = _menu;
		_form.Show();
		Application.DoEvents();

		_right.Available = true;
		Check("wide window");
		_form.Width = 300;
		Check("narrow window, the menu wraps");
		_right.Available = false;
		Application.DoEvents();
		_form.Width = 640;
		_right.Available = true;
		Check("hidden, resized, shown again");
		_form.Width = 1000;
		Check("widened while shown");
		_form.Width = 420;
		Check("narrowed while shown");

		int w = 0, h = 0;
		foreach (var s in Shots) { w = Math.Max(w, s.Width); h += s.Height + 4; }
		using (var all = new Bitmap(w, h))
		using (var g = Graphics.FromImage(all))
		{
			g.Clear(Color.DimGray);
			int y = 0;
			foreach (var s in Shots) { g.DrawImage(s, 0, y); y += s.Height + 4; }
			all.Save(picture, ImageFormat.Png);
		}
		_form.Close();
		Console.WriteLine(_failures == 0 ? "all cases right-aligned" : _failures + " case(s) missed");
		return _failures == 0 ? 0 : 1;
	}
}
