using System.Collections.Generic;
using System.Linq;
using System.Drawing;
using System.Drawing.Imaging;
using System.IO;
using System.Windows.Forms;

using System.Reflection;

using Chimera.Client.Common;
using Chimera.Client.GUI;
using Chimera.Emulation.Common;
using Chimera.Emulation.Common.Waterbox;

namespace Chimera.Tests.Client.GUI
{
	/// <summary>
	/// Renders windows to PNG so a person can look at them.
	///
	/// Everything else in this project asserts behaviour, which is the part a
	/// machine can judge. Whether a window is legible, sensibly laid out and not
	/// embarrassing is not, so these produce pictures instead: run with
	/// CHIMERA_UI_SHOTS=&lt;dir&gt; (tests/ui/run-ui-tests.sh --shots) and look at
	/// what comes out. Without that variable they report inconclusive, so an
	/// ordinary test run neither writes files nor fails.
	/// </summary>
	[TestClass]
	public class UiScreenshots
	{
		private static string ShotDir
			=> Environment.GetEnvironmentVariable("CHIMERA_UI_SHOTS");

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
		public void OpenCoreWindow()
		{
			if (ShotDir is null) { Assert.Inconclusive("set CHIMERA_UI_SHOTS to write screenshots"); return; }

			// a plausible Cores/ folder: two working packages, a dev build sitting there
			// as a directory, and one that will not parse
			var quicker = Pkg("quickerNES", "/home/you/Chimera/Cores/quickernes.zip", "3f2a91c47b0e5d8a1122334455667788990aabbc", "NES", null, ".nes", ".fds");
			var synth = Pkg("synth", "/home/you/Chimera/Cores/synth-box.zip", "c1d2e3f405162738495a6b7c8d9e0f1122334455", "SYNTH", null, ".synth");
			var dev = Pkg("quickerNES-dev", "/home/you/quickerNES/waterbox/bin", null, "NES", null, ".nes");
			var broken = Pkg("mystery.zip", "/home/you/Chimera/Cores/mystery.zip", "aabbccddeeff00112233445566778899aabbccdd", "?", "waterbox.config is missing systemId");

			List<CoreRegistry.LoadedCorePackage> loaded =
			[
				new() { Name = "quickerNES", Path = quicker.Path, Sha1 = quicker.Sha1, CoreNames = [ "quickerNES" ] },
				new() { Name = "synth", Path = synth.Path, Sha1 = synth.Sha1, CoreNames = [ "synth" ] },
			];

			using OpenCoreForm form = new(
				() => CorePackageList.Build([ quicker, synth, dev, broken ], loaded, [ ]),
				() => { },
				() => { },
				[ "/home/you/Chimera/Cores" ],
				_ => true);
			form.StartPosition = FormStartPosition.Manual;
			form.Location = new Point(0, 0);
			ListOfFirst(form).Items[0].Selected = true;
			Shoot(form, "open-core");
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
			if (ShotDir is null) { Assert.Inconclusive("set CHIMERA_UI_SHOTS to write screenshots"); return; }

			CoreFirmwareEntry Entry(string core, string id, string display, string description, CoreFirmwareState state, string path, bool required = true)
				=> new()
				{
					CoreName = core,
					Decl = new()
					{
						Id = id,
						Display = display,
						Description = description,
						Size = 8192,
						Required = required,
						Sha1 = "57FE1BDEE955BB48D357E463CCBF129496930B62",
					},
					Path = path,
					State = state,
					Sha1 = state switch
					{
						CoreFirmwareState.Good => "57FE1BDEE955BB48D357E463CCBF129496930B62",
						CoreFirmwareState.Missing => null,
						_ => "9C1D5A0B77E4F3128899AABBCCDDEEFF00112233",
					},
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
					CoreFirmwareState.Unrecognised, "/home/you/firmware/boot-alt.rom"),
				Entry("synth", "char", "Character generator",
					"The font the machine draws text with.",
					CoreFirmwareState.Custom, "/home/you/firmware/custom.bin"),
			];

			using CoreFirmwareForm form = new(() => entries, (_, _) => { });
			form.StartPosition = FormStartPosition.Manual;
			form.Location = new Point(0, 0);
			Shoot(form, "firmware");
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
