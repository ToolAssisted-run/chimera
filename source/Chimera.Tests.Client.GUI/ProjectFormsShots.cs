using System.Collections.Generic;
using System.IO;
using System.Windows.Forms;

using Chimera.Client.Common;
using Chimera.Client.GUI;
using Chimera.Emulation.Common;
using Chimera.Emulation.Common.Engine;
using Chimera.Emulation.Common.Waterbox;
using Chimera.Emulation.Common.Waterbox;

namespace Chimera.Tests.Client.GUI
{
	/// <summary>
	/// The project-first surface, pictured: the start screen, the wizard's three
	/// steps, and the resolution dialog with the statuses a user actually hits.
	/// Behaviour lives in ProjectFormsTests; these produce pictures for eyes.
	/// </summary>
	[TestClass]
	public class ProjectFormsShots
	{
		private static string ShotDir
			=> Environment.GetEnvironmentVariable("CHIMERA_UI_SHOTS");

		private static void Shoot(Form form, string name)
		{
			var dir = ShotDir;
			form.Show();
			form.Refresh();
			Application.DoEvents();
			using System.Drawing.Bitmap bmp = new(form.Width, form.Height);
			using (var g = System.Drawing.Graphics.FromImage(bmp))
			{
				g.CopyFromScreen(form.Location, System.Drawing.Point.Empty, form.Size);
			}
			Directory.CreateDirectory(dir);
			bmp.Save(Path.Combine(dir, $"{name}.png"), System.Drawing.Imaging.ImageFormat.Png);
		}

		private static readonly string DosboxDeclaration = """
			{
			  "slots": [
			    { "id": "floppy", "title": "Floppy disks", "min": 0, "max": -1,
			      "formats": ["img", "ima", "xdf", "fdi", "hdm", "nfd", "d88"],
			      "help": "Raw floppy disk images. The first one is in drive A: at boot; the order here is the swap order used by the Previous/Next/Swap Floppy Disk inputs." },
			    { "id": "cdrom", "title": "CD-ROMs", "min": 0, "max": -1, "formats": ["iso", "cue"],
			      "help": "CD-ROM images: a plain .iso, or a .cue sheet whose referenced track files join the project automatically." },
			    { "id": "hdd", "title": "Hard disk", "min": 0, "max": 1, "formats": ["hdd", "img"],
			      "help": "A raw hard disk image, mounted writable as drive C:." },
			    { "id": "conf", "title": "Configuration file", "min": 0, "max": 1, "formats": ["conf"],
			      "help": "Extra DOSBox-X configuration, appended after everything the sync settings compose." }
			  ],
			  "atLeastOneOf": [["floppy", "cdrom", "hdd", "conf"]]
			}
			""";

		[TestMethod]
		public void StartScreen()
		{
			if (ShotDir is null) { Assert.Inconclusive("set CHIMERA_UI_SHOTS to write screenshots"); return; }
			using StartScreenForm form = new(
				[
					"/home/you/tas/alleycat/alleycat.chimeraProject",
					"/home/you/tas/keen4/keen4-any.chimeraProject",
					"/home/you/tas/gone/moved-away.chimeraProject",
				],
				newProject: static () => null,
				openProject: static () => null);
			form.StartPosition = FormStartPosition.Manual;
			form.Location = new(0, 0);
			Shoot(form, "start-screen");
		}

		[TestMethod]
		public void WizardIdentityStep()
		{
			if (ShotDir is null) { Assert.Inconclusive("set CHIMERA_UI_SHOTS to write screenshots"); return; }
			using var form = MakeWizard();
			Shoot(form, "wizard-1-identity");
		}

		[TestMethod]
		public void WizardFileForm()
		{
			if (ShotDir is null) { Assert.Inconclusive("set CHIMERA_UI_SHOTS to write screenshots"); return; }
			using var form = MakeWizard();
			form.UseDeclaration(ProjectSlotDeclaration.Parse(DosboxDeclaration));
			form.AddFileToSlot("floppy", "/home/you/games/alleycat/disk1.img");
			form.AddFileToSlot("floppy", "/home/you/games/alleycat/disk2.img");
			form.AddFileToSlot("cdrom", "/home/you/games/alleycat/bonus.iso");
			Shoot(form, "wizard-2-files");
		}

		[TestMethod]
		public void WizardSettingsStep()
		{
			if (ShotDir is null) { Assert.Inconclusive("set CHIMERA_UI_SHOTS to write screenshots"); return; }
			using var form = MakeWizard();
			form.UseSyncSettingsDecls(
			[
				new WaterboxConfig.SettingDecl { Name = "machinePreset", Display = "Machine Preset", Type = "enum", Default = "1993_ibm_ps2_53_slc2_486", Options = [ "1993_ibm_ps2_53_slc2_486", "1983_ibm_xt5160" ], Description = "The hardware preset the machine boots as.", Sync = true },
				new WaterboxConfig.SettingDecl { Name = "cpuCycles", Display = "CPU Cycles", Type = "int", Default = -1, Description = "Fixed CPU cycles per millisecond; -1 uses the preset's value.", Sync = true },
				new WaterboxConfig.SettingDecl { Name = "memsizeMB", Display = "Memory (MB)", Type = "int", Default = -1, Description = "Installed RAM; -1 uses the preset's value.", Sync = true },
				new WaterboxConfig.SettingDecl { Name = "joystick1Enabled", Display = "Joystick 1", Type = "bool", Default = true, Description = "A joystick plugged into game port 1.", Sync = true },
				new WaterboxConfig.SettingDecl { Name = "mouseSensitivity", Display = "Mouse Sensitivity", Type = "float", Default = 3.0, Description = "Multiplier on relative mouse movement.", Sync = true },
			]);
			Shoot(form, "wizard-3-settings");
		}

		[TestMethod]
		public void WizardFirmwareStep()
		{
			if (ShotDir is null) { Assert.Inconclusive("set CHIMERA_UI_SHOTS to write screenshots"); return; }
			var dir = Path.Combine(Path.GetTempPath(), $"chimera-shot-fw-{System.Diagnostics.Process.GetCurrentProcess().Id}");
			Directory.CreateDirectory(dir);
			try
			{
				using var form = MakeWizard();
				var (cfg, index) = MakeFirmwareFixture(dir);
				form.UseFirmwareNeeds(cfg,
				[
					("bios_cd", true),
					("disksys.rom", true),
					("ltn0.pgf", false),
				], index);
				Shoot(form, "wizard-4-firmware");
			}
			finally
			{
				Directory.Delete(dir, recursive: true);
			}
		}

		internal static (WaterboxConfig Cfg, IReadOnlyList<FirmwareLocator.IndexedFile> Index) MakeFirmwareFixture(string dir)
		{
			// two dumps sit in the "Firmware folder": one satisfies the US CD
			// bios candidate, one satisfies nothing
			File.WriteAllText(Path.Combine(dir, "my_us_bios.bin"), "US CD BIOS BYTES");
			File.WriteAllText(Path.Combine(dir, "random.bin"), "NOT FIRMWARE");
			var usSha1 = Chimera.Emulation.Common.Engine.ChimeraEngine.Sha1Hex(
				System.Text.Encoding.ASCII.GetBytes("US CD BIOS BYTES"));
			var jpSha1 = Chimera.Emulation.Common.Engine.ChimeraEngine.Sha1Hex(
				System.Text.Encoding.ASCII.GetBytes("JP CD BIOS BYTES"));
			var fdsSha1 = Chimera.Emulation.Common.Engine.ChimeraEngine.Sha1Hex(
				System.Text.Encoding.ASCII.GetBytes("FDS BIOS BYTES"));
			WaterboxConfig cfg = new()
			{
				Firmware =
				[
					new()
					{
						Id = "bios_cd", Display = "Sega CD BIOS",
						Candidates =
						[
							new() { Sha1 = usSha1, Name = "bios_CD_U.bin", Label = "US v1.10" },
							new() { Sha1 = jpSha1, Name = "bios_CD_J.bin", Label = "JP v1.00" },
						],
					},
					new()
					{
						Id = "disksys.rom", Display = "Famicom Disk System BIOS",
						Candidates = [ new() { Sha1 = fdsSha1, Name = "disksys.rom", Label = "Nintendo" } ],
					},
					new() { Id = "ltn0.pgf", Display = "System Font ltn0" },
				],
			};
			var index = FirmwareLocator.BuildIndex([ dir ]);
			return (cfg, index);
		}

		private static NewProjectWizard MakeWizard()
		{
			List<DiscoveredCorePackage> cores =
			[
				new()
				{
					Name = "dosbox-x", Path = "/home/you/Chimera/Cores/dosbox-x.zip",
					Sha1 = "9803F96728C7B454E5B9797E1A0CDEF6C2E19A4B", Systems = [ "DOS" ],
					Version = "8750be5a1c2d",
				},
				new()
				{
					Name = "quickernes", Path = "/home/you/Chimera/Cores/quickernes.zip",
					Sha1 = "3F2A91C47B0E5D8A1122334455667788990AABBC", Systems = [ "NES" ],
					Version = "0eebbf6d4e5f",
				},
			];
			NewProjectWizard form = new(cores, static () => null, static _ => [ ]);
			form.StartPosition = FormStartPosition.Manual;
			form.Location = new(0, 0);
			form.Show();
			return form;
		}

		[TestMethod]
		public void ResolutionDialog()
		{
			if (ShotDir is null) { Assert.Inconclusive("set CHIMERA_UI_SHOTS to write screenshots"); return; }
			var dir = Path.Combine(Path.GetTempPath(), $"chimera-shot-resolve-{System.Diagnostics.Process.GetCurrentProcess().Id}");
			Directory.CreateDirectory(dir);
			try
			{
				File.WriteAllText(Path.Combine(dir, "disk1.img"), "first disk");
				File.WriteAllText(Path.Combine(dir, "disk2.img"), "second disk");
				File.WriteAllText(Path.Combine(dir, "bonus.iso"), "bonus bytes");
				var projectPath = Path.Combine(dir, "game.chimeraProject");
				using (var maker = EngineProject.New())
				{
					maker.FileAdd("disk1.img", "floppy", Path.Combine(dir, "disk1.img"));
					maker.FileAdd("disk2.img", "floppy", Path.Combine(dir, "disk2.img"));
					maker.FileAdd("bonus.iso", "cdrom", Path.Combine(dir, "bonus.iso"));
					maker.Save(projectPath);
				}
				// one file tampered, one gone: the dialog's whole vocabulary
				File.WriteAllText(Path.Combine(dir, "disk2.img"), "TAMPERED disk");
				File.Delete(Path.Combine(dir, "bonus.iso"));

				using var project = EngineProject.Open(projectPath);
				project.ResolveDir(dir);
				using ProjectResolutionForm form = new(project, static _ => null);
				form.StartPosition = FormStartPosition.Manual;
				form.Location = new(0, 0);
				Shoot(form, "project-resolution");
			}
			finally
			{
				Directory.Delete(dir, recursive: true);
			}
		}
	}
}
