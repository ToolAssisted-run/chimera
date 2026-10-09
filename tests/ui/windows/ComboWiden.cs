// Asks, on Windows, whether a combo box under the Dark theme is drawn right
// after the window it is anchored in has been widened (chimera#230).
//
// A themed combo box is flat, and a flat combo box paints its own button. A
// resize asks a control to repaint only what it uncovered, so the button it
// had drawn at its old right edge stayed there, beside the new one: the New
// Project window, maximised, looked as if every box had a second box behind
// it. The theme engine now has a themed combo box repaint whole.
//
// What it compares is what the window HOLDS (its own device context, not
// DrawToBitmap - asking a control to draw itself afresh is exactly what does
// not show a stale pixel): a box widened from 300 to 760 against a box that
// was 760 wide from the start. They must be the same picture. Run with
// "unthemed-control" it widens a flat box the theme engine never saw, which
// must NOT be: that is the control, and it is how every box behaved.
using System;
using System.Drawing;
using System.Drawing.Imaging;
using System.Threading;
using System.Windows.Forms;

using Chimera.Client.Common;
using Chimera.Client.GUI;

internal static class ComboWiden
{
	[System.Runtime.InteropServices.DllImport("gdi32.dll")]
	private static extern bool BitBlt(IntPtr target, int x, int y, int width, int height, IntPtr source, int sourceX, int sourceY, int operation);

	private static Bitmap Shoot(int startWidth, int endWidth, bool themed)
	{
		Form form = new Form { ClientSize = new Size(startWidth, 90), StartPosition = FormStartPosition.Manual, Location = new Point(60, 60), Text = "ComboWiden", TopMost = true };
		ComboBox box = new ComboBox
		{
			Anchor = AnchorStyles.Top | AnchorStyles.Left | AnchorStyles.Right,
			DropDownStyle = ComboBoxStyle.DropDownList,
			Location = new Point(10, 10),
			Width = startWidth - 20,
		};
		box.Items.Add("Nintendo 64");
		box.SelectedIndex = 0;
		Button elsewhere = new Button { Location = new Point(10, 50), Text = "focus" };
		form.Controls.Add(box);
		form.Controls.Add(elsewhere);
		if (themed) ThemeEngine.Apply(form, ThemeLibrary.Select("Dark"));
		else { box.FlatStyle = FlatStyle.Flat; box.BackColor = Color.FromArgb(40, 40, 40); box.ForeColor = Color.White; }
		form.Show();
		form.ActiveControl = elsewhere;
		Settle();
		if (endWidth != startWidth)
		{
			form.ClientSize = new Size(endWidth, 90);
			Settle();
		}
		// what the window HOLDS, read from its own device context: not a
		// repaint (which is what does not show a stale pixel), and not the
		// screen (which shows whatever window happens to be in front)
		Bitmap shot = new Bitmap(box.ClientSize.Width, box.ClientSize.Height);
		using (Graphics from = Graphics.FromHwnd(box.Handle))
		using (Graphics to = Graphics.FromImage(shot))
		{
			IntPtr source = from.GetHdc();
			IntPtr target = to.GetHdc();
			BitBlt(target, 0, 0, shot.Width, shot.Height, source, 0, 0, 0x00CC0020 /* SRCCOPY */);
			to.ReleaseHdc(target);
			from.ReleaseHdc(source);
		}
		form.Close();
		form.Dispose();
		Settle();
		return shot;
	}

	private static void Settle()
	{
		for (int i = 0; i < 10; i++) { Application.DoEvents(); Thread.Sleep(30); }
	}

	private static int Differing(Bitmap a, Bitmap b)
	{
		if (a.Size != b.Size) return int.MaxValue;
		int n = 0;
		for (int y = 0; y < a.Height; y++)
			for (int x = 0; x < a.Width; x++)
				if (a.GetPixel(x, y).ToArgb() != b.GetPixel(x, y).ToArgb()) n++;
		return n;
	}

	[STAThread]
	private static int Main(string[] args)
	{
		string picture = args.Length > 0 ? args[0] : "combo-widen.png";
		bool control = args.Length > 1 && args[1] == "unthemed-control";
		Application.EnableVisualStyles();

		Bitmap widened = Shoot(300, 760, !control);
		Bitmap born = Shoot(760, 760, !control);
		int differing = Differing(widened, born);

		using (Bitmap sheet = new Bitmap(Math.Max(widened.Width, born.Width), widened.Height + born.Height + 6))
		{
			using (Graphics g = Graphics.FromImage(sheet))
			{
				g.Clear(Color.Magenta);
				g.DrawImage(widened, 0, 0);
				g.DrawImage(born, 0, widened.Height + 6);
			}
			sheet.Save(picture, ImageFormat.Png);
		}

		bool same = differing == 0;
		if (control)
		{
			Console.WriteLine("{0} control: a flat box the theme engine never saw, widened, differs from one born wide in {1} pixels (it must differ: that is the fault)",
				same ? "FAIL" : "PASS", differing);
			return same ? 1 : 0;
		}
		Console.WriteLine("{0} a themed box widened from 300 to 760 is the picture of one born 760 wide ({1} pixels differ)", same ? "PASS" : "FAIL", differing);
		return same ? 0 : 1;
	}
}
