using System.Collections.Generic;
using System.Linq;
using System.Drawing;
using System.Drawing.Imaging;
using System.IO;
using System.Windows.Forms;

using System.Reflection;

using BizHawk.Client.Common;
using BizHawk.Client.EmuHawk;
using BizHawk.Emulation.Common;
using BizHawk.Emulation.Common.Waterbox;

namespace BizHawk.Tests.Client.EmuHawk
{
	/// <summary>
	/// Renders windows to PNG so a person can look at them.
	///
	/// Everything else in this project asserts behaviour, which is the part a
	/// machine can judge. Whether a window is legible, sensibly laid out and not
	/// embarrassing is not, so these produce pictures instead: run with
	/// MINIHAWK_UI_SHOTS=&lt;dir&gt; (tests/ui/run-ui-tests.sh --shots) and look at
	/// what comes out. Without that variable they report inconclusive, so an
	/// ordinary test run neither writes files nor fails.
	/// </summary>
	[TestClass]
	public class UiScreenshots
	{
		private static string ShotDir
			=> Environment.GetEnvironmentVariable("MINIHAWK_UI_SHOTS");

		private static void Shoot(Form form, string name)
		{
			var dir = ShotDir;
			form.Show();
			form.Refresh();
			Application.DoEvents();
			using Bitmap bmp = new(form.Width, form.Height);
			using (var g = Graphics.FromImage(bmp))
			{
				// the window is real and on a (headless) screen, so grab it from there:
				// DrawToBitmap skips the non-client area and mis-renders ListViews on Mono
				g.CopyFromScreen(form.Location, Point.Empty, form.Size);
			}
			Directory.CreateDirectory(dir);
			bmp.Save(Path.Combine(dir, $"{name}.png"), ImageFormat.Png);
		}

		private static DiscoveredCorePackage Pkg(
			string name,
			string path,
			string sha1,
			string system = "NES",
			string error = null,
			params string[] extensions)
		{
			Dictionary<string, string> exts = new();
			foreach (var ext in extensions) exts[ext] = system;
			return new DiscoveredCorePackage
			{
				Name = name,
				Path = path,
				Sha1 = sha1,
				Systems = [ system ],
				Extensions = exts,
				Error = error,
			};
		}

		[TestMethod]
		public void CorePackagesWindow()
		{
			if (ShotDir is null) { Assert.Inconclusive("set MINIHAWK_UI_SHOTS to write screenshots"); return; }

			// a plausible Cores/ folder: two working packages, a dev build sitting there
			// as a directory, and one that will not parse
			var quicker = Pkg("quickerNES", "/home/you/miniHawk/Cores/quickernes.zip", "3f2a91c47b0e5d8a1122334455667788990aabbc", "NES", null, ".nes", ".fds");
			var synth = Pkg("synth", "/home/you/miniHawk/Cores/synth-box.zip", "c1d2e3f405162738495a6b7c8d9e0f1122334455", "SYNTH", null, ".synth");
			var dev = Pkg("quickerNES-dev", "/home/you/quickerNES/waterbox/bin", null, "NES", null, ".nes");
			var broken = Pkg("mystery.zip", "/home/you/miniHawk/Cores/mystery.zip", "aabbccddeeff00112233445566778899aabbccdd", "?", "waterbox.config is missing systemId");

			List<CoreRegistry.LoadedCorePackage> loaded =
			[
				new() { Name = "quickerNES", Path = quicker.Path, Sha1 = quicker.Sha1, CoreNames = [ "quickerNES" ] },
				new() { Name = "synth", Path = synth.Path, Sha1 = synth.Sha1, CoreNames = [ "synth" ] },
			];

			using CorePackagesForm form = new(
				() => CorePackageList.Build([ quicker, synth, dev, broken ], loaded, [ ]),
				() => { },
				() => { },
				[ "/home/you/miniHawk/Cores" ]);
			form.StartPosition = FormStartPosition.Manual;
			form.Location = new Point(0, 0);
			ListOfFirst(form).Items[0].Selected = true;
			Shoot(form, "core-packages");
		}

		/// <summary>
		/// The firmware window, over a package that wants two files and another that
		/// wants one. The states are what a user actually hits - provided, never
		/// provided, and the wrong file - so the picture shows whether they read
		/// clearly enough to act on.
		/// </summary>
		[TestMethod]
		public void FirmwareWindow()
		{
			if (ShotDir is null) { Assert.Inconclusive("set MINIHAWK_UI_SHOTS to write screenshots"); return; }

			CoreFirmwareEntry Entry(string core, string id, string display, string description, CoreFirmwareState state, string path, bool required = true)
				=> new()
				{
					CoreName = core,
					Decl = new() { Id = id, Display = display, Description = description, Size = 8192, Required = required },
					Path = path,
					State = state,
					Sha1 = state is CoreFirmwareState.Good ? "57FE1BDEE955BB48D357E463CCBF129496930B62" : null,
				};

			List<CoreFirmwareEntry> entries =
			[
				Entry("QuickerNesHawk", "bios", "Family Computer Disk System BIOS",
					"The 8 KiB boot rom in the RAM adapter. Any disk image needs it; cartridges do not.",
					CoreFirmwareState.Good, "/home/you/firmware/disksys.rom"),
				Entry("QuickerNesHawk", "expansion", "Expansion audio rom",
					"Optional. Without it the expansion channels are silent.",
					CoreFirmwareState.Missing, null, required: false),
				Entry("synth", "boot", "Boot rom",
					"Runs before the cartridge does.",
					CoreFirmwareState.WrongSize, "/home/you/firmware/oops.bin"),
			];

			using CoreFirmwareForm form = new(() => entries, (_, _) => { });
			form.StartPosition = FormStartPosition.Manual;
			form.Location = new Point(0, 0);
			Shoot(form, "firmware");
		}

		/// <summary>
		/// The core settings dialog, over the settings a real package declares. The
		/// grid rows are synthesized from those declarations, so this is the only way
		/// to see whether a core's own words come out legible.
		/// </summary>
		[TestMethod]
		public void CoreSettingsWindow()
		{
			if (ShotDir is null) { Assert.Inconclusive("set MINIHAWK_UI_SHOTS to write screenshots"); return; }

			WaterboxConfig.SettingDecl port1 = new()
			{
				Name = "port1", Display = "Left Port Peripheral", Type = "enum", Default = "gamepad", Sync = true,
				Options = [ "none", "gamepad", "fourScore", "arkanoidNES", "arkanoidFamicom" ],
				Description = "What is plugged into controller port 1. Changing it changes the machine, so it reboots the core.",
			};
			WaterboxConfig.SettingDecl port2 = new()
			{
				Name = "port2", Display = "Right Port Peripheral", Type = "enum", Default = "none", Sync = true,
				Options = [ "none", "gamepad", "fourScore" ],
				Description = "What is plugged into controller port 2.",
			};
			WaterboxConfig.SettingDecl sprites = new()
			{
				Name = "spriteLimit", Display = "Visible Sprites", Type = "int", Min = 0, Max = 64, Default = 8, Sync = false,
				Description = "Sprites drawn per scanline. 8 is what the console does (and what makes sprites flicker); 64 draws them all.",
			};

			WaterboxCoreSettings settings = new() { Declarations = [ sprites ] };
			WaterboxCoreSyncSettings sync = new() { Declarations = [ port1, port2 ] };
			var form = (Form) typeof(GenericCoreConfig)
				.GetConstructors(BindingFlags.NonPublic | BindingFlags.Instance)[0]
				.Invoke([ new StubAdapter(settings, sync), false, false, false ]);
			form.Text = "quickerNES Settings";
			form.StartPosition = FormStartPosition.Manual;
			form.Location = new Point(0, 0);
			using (form)
			{
				Shoot(form, "core-settings");
				// the sync tab is the half that matters most - it is what a movie
				// records - so it gets its own picture rather than being taken on trust
				var tabs = form.Controls.OfType<TabControl>().First();
				tabs.SelectedIndex = tabs.TabCount - 1;
				Shoot(form, "core-settings-sync");
			}
		}

		/// <summary>Hands the dialog a fixed pair of settings objects and drops what comes back.</summary>
		private sealed class StubAdapter : ISettingsAdapter
		{
			private readonly object _s, _ss;

			public StubAdapter(object s, object ss) { _s = s; _ss = ss; }

			public bool HasSettings => true;

			public bool HasSyncSettings => true;

			public object GetSettings() => _s;

			public object GetSyncSettings() => _ss;

			public void PutCoreSettings(object s) { }

			public void PutCoreSyncSettings(object ss) { }
		}

		private static ListView ListOfFirst(Form form)
		{
			foreach (Control c in form.Controls)
			{
				if (c is ListView lv) return lv;
			}
			throw new InvalidOperationException("no ListView on the form");
		}
	}
}
