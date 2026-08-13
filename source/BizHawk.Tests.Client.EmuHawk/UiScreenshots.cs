using System.Collections.Generic;
using System.Drawing;
using System.Drawing.Imaging;
using System.IO;
using System.Windows.Forms;

using BizHawk.Client.Common;
using BizHawk.Client.EmuHawk;

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
