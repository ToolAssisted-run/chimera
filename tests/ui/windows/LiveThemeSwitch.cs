// Does choosing a theme change a window that is ALREADY OPEN, on Windows?
//
// The automated suite cannot answer that. It runs on Linux, on Mono, under
// Xvfb; Chimera's users run .NET Framework WinForms on Windows, where a
// different toolkit decides what an assignment to BackColor does, whether a
// control repaints, and whether visual styles override any of it. A green run
// of tests/ui/run-ui-tests.sh says nothing about that toolkit.
//
// So this is the same question asked there. It loads the real frontend
// assembly, builds a window out of the controls the walk treats differently,
// and puts it through the case the frontend actually lands in: born under the
// theme Chimera starts on, then told to be the other one. A window born Dark
// and told Light has to end up identical to one born Light - which is the bug
// reported twice from a build whose 889 tests were green, because every one of
// them built its windows under Light.
//
// It photographs with PrintWindow, which renders the window into a bitmap of
// ours. It reads no screen pixels, so no other window on the desktop can be
// captured even by accident, and it works with the session locked.
//
// Run it with live-theme-switch.sh. Windows only; there is nothing here for a
// Linux box to run.
using System;
using System.Drawing;
using System.Drawing.Imaging;
using System.Runtime.InteropServices;
using System.Threading;
using System.Windows.Forms;

using Chimera.Client.Common;
using Chimera.Client.GUI;

internal sealed class Probe : ThemedForm
{
	public Probe()
	{
		Text = "live theme switch";
		StartPosition = FormStartPosition.Manual;
		Location = new Point(60, 60);
		ClientSize = new Size(860, 560);

		var menu = new MenuStrip();
		var cfg = new ToolStripMenuItem("&Config");
		cfg.DropDownItems.Add(new ToolStripMenuItem("Theme"));
		menu.Items.Add(cfg);
		menu.Items.Add(new ToolStripMenuItem("&File"));
		MainMenuStrip = menu;

		var list = new ListView
		{
			View = View.Details,
			FullRowSelect = true,
			Location = new Point(12, 40),
			Size = new Size(420, 220),
			HideSelection = false,
		};
		list.Columns.Add("Core", 140);
		list.Columns.Add("System", 120);
		for (var i = 0; i < 7; i++) list.Items.Add(new ListViewItem(new[] { "core " + i, "system " + i }));
		list.Items[2].Selected = true;

		var box = new GroupBox { Text = "A group", Location = new Point(450, 40), Size = new Size(380, 220) };
		box.Controls.Add(new Label { Text = "A label", Location = new Point(12, 24), AutoSize = true });
		box.Controls.Add(new TextBox { Text = "editable", Location = new Point(12, 48), Width = 160 });
		box.Controls.Add(new TextBox { Text = "read only", Location = new Point(12, 76), Width = 160, ReadOnly = true });
		box.Controls.Add(new CheckBox { Text = "A checkbox", Location = new Point(12, 104), AutoSize = true });
		box.Controls.Add(new Button { Text = "A button", Location = new Point(12, 132), Size = new Size(100, 26) });
		box.Controls.Add(new ComboBox { Location = new Point(12, 164), Width = 160 });

		var plain = new ListBox { Location = new Point(12, 272), Size = new Size(420, 140) };
		for (var i = 0; i < 6; i++) plain.Items.Add("row " + i);
		plain.SelectedIndex = 1;

		var panel = new Panel { Location = new Point(450, 272), Size = new Size(380, 140), BorderStyle = BorderStyle.Fixed3D };
		panel.Controls.Add(new Label { Text = "in a panel", Location = new Point(10, 10), AutoSize = true });

		var status = new StatusStrip();
		status.Items.Add(new ToolStripStatusLabel("a status bar"));

		Controls.Add(list);
		Controls.Add(box);
		Controls.Add(plain);
		Controls.Add(panel);
		Controls.Add(status);
		Controls.Add(menu);
	}
}

internal static class LiveThemeSwitch
{
	[DllImport("user32.dll")]
	private static extern int PrintWindow(IntPtr hwnd, IntPtr hdc, uint flags);

	private const uint PW_RENDERFULLCONTENT = 2;

	private static void Pump()
	{
		for (var i = 0; i < 12; i++) { Application.DoEvents(); Thread.Sleep(40); }
	}

	// The CLIENT area only. The frame around it is drawn by the desktop compositor
	// and differs between an active window and an inactive one, which is not what
	// this is asking; the title bar has a check of its own (TitleBarContractTests,
	// and WindowFrame.LastAskedFor).
	private static Bitmap Shoot(Form f)
	{
		Pump();
		var whole = new Bitmap(f.Width, f.Height, PixelFormat.Format32bppArgb);
		using (var g = Graphics.FromImage(whole))
		{
			var hdc = g.GetHdc();
			try { PrintWindow(f.Handle, hdc, PW_RENDERFULLCONTENT); }
			finally { g.ReleaseHdc(hdc); }
		}
		var origin = f.PointToScreen(Point.Empty);
		var inset = new Rectangle(origin.X - f.Left, origin.Y - f.Top, f.ClientSize.Width, f.ClientSize.Height);
		inset.Intersect(new Rectangle(0, 0, whole.Width, whole.Height));
		var client = whole.Clone(inset, whole.PixelFormat);
		whole.Dispose();
		return client;
	}

	private static string Describe(Bitmap b)
	{
		long r = 0, g = 0, bl = 0;
		var n = 0;
		for (var y = 0; y < b.Height; y += 3)
		for (var x = 0; x < b.Width; x += 3)
		{
			var c = b.GetPixel(x, y);
			r += c.R; g += c.G; bl += c.B; n++;
		}
		return string.Format("mean rgb {0},{1},{2}", r / n, g / n, bl / n);
	}

	private static int Differing(Bitmap a, Bitmap b)
	{
		if (a.Width != b.Width || a.Height != b.Height) return int.MaxValue;
		var n = 0;
		for (var y = 0; y < a.Height; y++)
		for (var x = 0; x < a.Width; x++)
			if (a.GetPixel(x, y).ToArgb() != b.GetPixel(x, y).ToArgb()) n++;
		return n;
	}

	private static Bitmap Born(string born, string told)
	{
		ThemeLibrary.Select(born);
		var probe = new Probe();
		probe.Show();
		Pump();
		if (told != born) ThemeEngine.ApplyToOpenForms(ThemeLibrary.Select(told));
		var shot = Shoot(probe);
		probe.Close();
		Pump();
		return shot;
	}

	[STAThread]
	private static int Main(string[] args)
	{
		Application.EnableVisualStyles();
		Application.SetCompatibleTextRenderingDefault(false);
		var sheet = args.Length > 0 ? args[0] : "live-theme-switch.png";

		var bornDark = Born("Dark", "Dark");
		var toldLight = Born("Dark", "Light");
		var bornLight = Born("Light", "Light");
		var toldDark = Born("Light", "Dark");

		Console.WriteLine("born Dark             " + Describe(bornDark));
		Console.WriteLine("born Light            " + Describe(bornLight));
		var changed = Differing(bornDark, toldLight);
		Console.WriteLine("born Dark, told Light " + Describe(toldLight)
			+ "   changed " + changed + " pixels");

		var wrongLight = Differing(bornLight, toldLight);
		var wrongDark = Differing(bornDark, toldDark);
		Console.WriteLine("differs from one born Light: " + wrongLight + "   (must be 0)");
		Console.WriteLine("differs from one born Dark:  " + wrongDark + "   (must be 0)");

		var w = bornDark.Width;
		using (var strip = new Bitmap((w * 4) + 36, bornDark.Height, PixelFormat.Format32bppArgb))
		{
			using (var g = Graphics.FromImage(strip))
			{
				g.Clear(Color.Magenta);
				g.DrawImage(bornDark, 0, 0);
				g.DrawImage(toldLight, w + 12, 0);
				g.DrawImage(bornLight, (w * 2) + 24, 0);
				g.DrawImage(toldDark, (w * 3) + 36, 0);
			}
			strip.Save(sheet, ImageFormat.Png);
		}
		Console.WriteLine("wrote " + sheet + " (born Dark | told Light | born Light | told Dark)");

		if (changed == 0)
		{
			Console.WriteLine("FAIL: choosing a theme changed nothing in a window that was already open");
			return 1;
		}
		if (wrongLight != 0 || wrongDark != 0)
		{
			Console.WriteLine("FAIL: a window does not end up where one born in that theme is");
			return 1;
		}
		Console.WriteLine("PASS");
		return 0;
	}
}
