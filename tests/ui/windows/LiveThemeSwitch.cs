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

// A window with a ticked list on it, for the question Mono cannot be asked:
// does painting a theme onto a window raise events in it?
//
// On Windows, reading a ListView's SelectedIndices or TopItem creates its
// handle, and creating the handle pushes the items into the native control one
// at a time, raising ItemChecked for each - inside a window that has not
// finished opening. Pre-Compiled Modules died of exactly that, with a
// NullReferenceException in its own tick handler, because the handler walks
// ListView.Items and ListView.Items cannot be walked half way through being
// rebuilt. Mono neither creates the handle nor raises the events, so no Linux
// test can see any of it.
internal sealed class Ticked : ThemedForm
{
	// when false this window is not themed at all, which is the baseline: a
	// ListView raises ItemChecked as its handle is made whatever anybody paints
	private readonly bool _themed;

	protected override bool ThemingEnabled { get { return _themed; } }

	public readonly ListView List;

	public int Ticks;

	public string Trouble = "";

	public Ticked(bool themed)
	{
		_themed = themed;
		Text = "ticked list";
		StartPosition = FormStartPosition.Manual;
		Location = new Point(60, 60);
		ClientSize = new Size(420, 200);
		List = new ListView
		{
			Dock = DockStyle.Fill,
			View = View.Details,
			CheckBoxes = true,
			FullRowSelect = true,
		};
		List.Columns.Add("Name", 260);
		List.Columns.Add("Size", 120);
		for (var i = 0; i < 5; i++)
		{
			var row = new ListViewItem("game " + i);
			row.SubItems.Add((i * 1024) + " KB");
			List.Items.Add(row);
		}
		List.Items[1].Checked = true;
		List.Items[3].Checked = true;
		Controls.Add(List);
		// The shape the real window has, and the reason this reproduces anything:
		// BeginUpdate creates the list's handle, so the list is ALREADY live when
		// the theme is painted on - and giving a live list a different border
		// recreates its handle, which rebuilds the items and raises a tick for each.
		// Without this the handle is made later, by the window opening, and the
		// recreation - the thing that crashed - never happens.
		List.BeginUpdate();
		List.EndUpdate();
		// the shape of the handler that died
		List.ItemChecked += delegate
		{
			Ticks++;
			try
			{
				var n = 0;
				foreach (ListViewItem row in List.Items) { if (row.Checked) n++; }
			}
			catch (Exception ex)
			{
				if (Trouble.Length == 0) Trouble = ex.GetType().Name + ": " + ex.Message;
			}
		};
	}
}

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

	// Opens the real Pre-Compiled Modules window on a dark theme. That window died
	// on being opened, with a NullReferenceException out of the toolkit's own
	// ListView enumerator: a tick event had arrived while the list was being
	// rebuilt, and the window's handler answered it by walking ListView.Items.
	//
	// The rows are filled in by reflection because PrecompiledGame's properties are
	// init-only and the compiler this is built with predates that. Only the fields
	// the window shows are set.
	private static PrecompiledGame Game(string sha, string name, long bytes)
	{
		var game = (PrecompiledGame) Activator.CreateInstance(typeof(PrecompiledGame));
		Set(game, "GameSha1", sha);
		Set(game, "GameName", name);
		Set(game, "CoreName", "ares");
		Set(game, "Bytes", bytes);
		Set(game, "Modules", 8);
		Set(game, "Complete", true);
		Set(game, "Path", @"C:\nowhere\" + sha);
		return game;
	}

	private static void Set(object target, string name, object value)
	{
		var property = typeof(PrecompiledGame).GetProperty(name);
		if (property != null) property.GetSetMethod(true).Invoke(target, new object[] { value });
	}

	private static string RealWindow()
	{
		try
		{
			var rows = new System.Collections.Generic.List<PrecompiledGame>();
			rows.Add(Game("aaaaaa", "one.rom", 1234567));
			rows.Add(Game("bbbbbb", "two.rom", 222222));
			Func<System.Collections.Generic.IReadOnlyList<PrecompiledGame>> survey = delegate { return rows; };

			var form = new PrecompiledModulesForm(survey, delegate { });
			form.StartPosition = FormStartPosition.Manual;
			form.Location = new Point(60, 60);
			form.Show();
			Pump();
			form.Close();
			Pump();
			return "";
		}
		catch (Exception ex)
		{
			var inner = ex.InnerException == null ? ex : ex.InnerException;
			return inner.GetType().Name + ": " + inner.Message;
		}
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

		// ...and the other thing Mono cannot be asked: does PAINTING a theme raise
		// events in the window it is painting?
		//
		// The window is opened bare and left to settle first, so that everything
		// the toolkit raises while a window opens has already happened and been
		// forgotten. What is counted after that is the walk's alone. Giving a live
		// ListView a different border recreates its handle, and WinForms rebuilds a
		// recreated list by pushing every item back into it - a tick event each,
		// into a handler that has no way to know they are not real. That is what
		// killed Pre-Compiled Modules on being opened.
		ThemeLibrary.Select("Light");
		var ticked = new Ticked(themed: false);
		ticked.Show();
		Pump();
		ticked.Ticks = 0;
		ticked.Trouble = "";
		ThemeEngine.Apply(ticked, ThemeLibrary.Select("Dark"));
		Pump();
		var raised = ticked.Ticks;
		var trouble = ticked.Trouble;
		ticked.Close();
		Pump();
		Console.WriteLine("tick events raised by painting the theme: " + raised + "   (must be 0)");
		Console.WriteLine("what the tick handler hit: " + (trouble.Length == 0 ? "nothing" : trouble));

		// and the window it actually happened to
		ThemeLibrary.Select("Dark");
		var realWindow = RealWindow();
		Console.WriteLine("opening the real Pre-Compiled Modules window on Dark: "
			+ (realWindow.Length == 0 ? "no exception" : realWindow));

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
		if (raised != 0 || trouble.Length != 0)
		{
			Console.WriteLine("FAIL: painting a theme onto a window raised events inside it");
			return 1;
		}
		if (realWindow.Length != 0)
		{
			Console.WriteLine("FAIL: a real window of the frontend does not open on a dark theme");
			return 1;
		}
		Console.WriteLine("PASS");
		return 0;
	}
}
