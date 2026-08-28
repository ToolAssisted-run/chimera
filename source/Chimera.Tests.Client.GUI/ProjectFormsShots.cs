using System.Collections.Generic;
using System.IO;
using System.Windows.Forms;

using Chimera.Client.Common;
using Chimera.Client.GUI;
using Chimera.Emulation.Common;
using Chimera.Emulation.Common.Engine;
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
		public void WizardCoreStep()
		{
			if (ShotDir is null) { Assert.Inconclusive("set CHIMERA_UI_SHOTS to write screenshots"); return; }
			using var form = MakeWizard();
			Shoot(form, "wizard-1-core");
		}

		/// <summary>
		/// The same page for a core that draws two ways: the renderer is a choice,
		/// and it starts on the one the core declared - the faster of the two.
		/// </summary>
		[TestMethod]
		public void WizardCoreStepWithRenderers()
		{
			if (ShotDir is null) { Assert.Inconclusive("set CHIMERA_UI_SHOTS to write screenshots"); return; }
			using var form = MakeWizard();
			form.UseRenderersFrom(WaterboxConfig.FromJson("""
				{
				  "coreName": "flycast",
				  "systemId": "DC",
				  "video": { "width": 640, "height": 480 },
				  "audio": { "samplesPerFrame": 1024 },
				  "input": { "buttons": [] },
				  "settings": [
				    { "name": "renderer", "type": "enum", "options": ["software", "opengl"], "default": "software" }
				  ]
				}
				"""));
			Shoot(form, "wizard-1-core-renderers");
		}

		[TestMethod]
		public void WizardFileForm()
		{
			if (ShotDir is null) { Assert.Inconclusive("set CHIMERA_UI_SHOTS to write screenshots"); return; }
			var dir = Path.Combine(Path.GetTempPath(), $"chimera-shot-files-{System.Diagnostics.Process.GetCurrentProcess().Id}");
			Directory.CreateDirectory(dir);
			try
			{
				// a real cue with real tracks, so the row shows them riding along
				File.WriteAllText(Path.Combine(dir, "bonus.cue"),
					"FILE \"bonus (track 1).bin\" BINARY\n  TRACK 01 MODE1/2352\nFILE \"bonus (track 2).bin\" BINARY\n  TRACK 02 AUDIO\n");
				File.WriteAllText(Path.Combine(dir, "bonus (track 1).bin"), "data track");
				File.WriteAllText(Path.Combine(dir, "bonus (track 2).bin"), "audio track");
				using var form = MakeWizard();
				form.UseDeclaration(ProjectSlotDeclaration.Parse(DosboxDeclaration));
				form.AddFileToSlot("floppy", "/home/you/games/alleycat/disk1.img");
				form.AddFileToSlot("floppy", "/home/you/games/alleycat/disk2.img");
				form.AddFileToSlot("cdrom", Path.Combine(dir, "bonus.cue"));
				Shoot(form, "wizard-2-files");
			}
			finally
			{
				Directory.Delete(dir, recursive: true);
			}
		}

		[TestMethod]
		public void WizardFileFormAdapts()
		{
			if (ShotDir is null) { Assert.Inconclusive("set CHIMERA_UI_SHOTS to write screenshots"); return; }
			using var form = MakeWizard();
			// mutually exclusive slots: the picked disk greys the cartridge
			form.UseDeclaration(ProjectSlotDeclaration.Parse("""
				{
				  "slots": [
				    { "id": "cart", "title": "Cartridge", "min": 1, "max": 1, "formats": ["nes"],
				      "help": "An iNES cartridge image.",
				      "exposedWhen": { "not": { "slot": "fds" } } },
				    { "id": "fds", "title": "Famicom disk", "min": 1, "max": 1, "formats": ["fds"],
				      "help": "A Famicom Disk System image - makes the FDS BIOS a firmware requirement.",
				      "exposedWhen": { "not": { "slot": "cart" } } }
				  ]
				}
				"""));
			form.AddFileToSlot("fds", "/home/you/games/zelda no densetsu.fds");
			Shoot(form, "wizard-2-files-adapts");
		}

		[TestMethod]
		public void WizardSettingsStep()
		{
			if (ShotDir is null) { Assert.Inconclusive("set CHIMERA_UI_SHOTS to write screenshots"); return; }
			using var form = MakeWizard();
			form.UseSettingsDecls(
			[
				new WaterboxConfig.SettingDecl { Name = "machinePreset", Display = "Machine Preset", Type = "enum", Default = "1993_ibm_ps2_53_slc2_486", Options = [ "1993_ibm_ps2_53_slc2_486", "1983_ibm_xt5160" ], Description = "The hardware preset the machine boots as." },
				new WaterboxConfig.SettingDecl { Name = "cpuCycles", Display = "CPU Cycles", Type = "int", Default = -1, Description = "Fixed CPU cycles per millisecond; -1 uses the preset's value." },
				new WaterboxConfig.SettingDecl { Name = "memsizeMB", Display = "Memory (MB)", Type = "int", Default = -1, Description = "Installed RAM; -1 uses the preset's value." },
				new WaterboxConfig.SettingDecl { Name = "joystick1Enabled", Display = "Joystick 1", Type = "bool", Default = true, Description = "A joystick plugged into game port 1." },
				new WaterboxConfig.SettingDecl { Name = "mouseSensitivity", Display = "Mouse Sensitivity", Type = "float", Default = 3.0, Description = "Multiplier on relative mouse movement." },
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
				// the decisions selected the US bios (entry 0) and a Famicom disk
				form.UseFirmwareNeeds(cfg, [ ("bios_cd", 0), ("bios", 2) ], index);
				Shoot(form, "wizard-4-firmware");
			}
			finally
			{
				Directory.Delete(dir, recursive: true);
			}
		}

		/// <summary>
		/// The PS2 shape: one entry per known bios dump, all under one id, each
		/// nailed to a value of the Boot ROM setting - so the page names the one
		/// file this project wants, and its SHA1.
		/// </summary>
		[TestMethod]
		public void WizardFirmwareStepOneExactDump()
		{
			if (ShotDir is null) { Assert.Inconclusive("set CHIMERA_UI_SHOTS to write screenshots"); return; }
			using var form = MakeWizard();
			var cfg = WaterboxConfig.FromJson("""
				{
				  "coreName": "PCSX2",
				  "systemId": "PS2",
				  "video": { "width": 640, "height": 448 },
				  "audio": { "samplesPerFrame": 1024 },
				  "input": { "buttons": [] },
				  "settings": [
				    { "name": "bios", "display": "Boot ROM", "type": "enum",
				      "options": ["ps2-0160e-20011004", "ps2-0230a-20080220"],
				      "default": "ps2-0230a-20080220" }
				  ],
				  "firmware": [
				    { "id": "bios.bin", "display": "PlayStation 2 BIOS",
				      "size": 4194304, "name": "ps2-0160e-20011004.bin",
				      "sha1": "8361D615CC895962E0F0838489337574DBDC9173",
				      "label": "Europe v1.60 (2001-10-04)",
				      "description": "The console's own bios, 4MB, dumped from a PlayStation 2 you own.",
				      "requiredWhen": { "setting": "bios", "is": "ps2-0160e-20011004" } },
				    { "id": "bios.bin", "display": "PlayStation 2 BIOS",
				      "size": 4194304, "name": "ps2-0230a-20080220.bin",
				      "sha1": "F9229FE159D0353B9F0632F3FDC66819C9030458",
				      "label": "USA v2.30 (2008-02-20)",
				      "description": "The console's own bios, 4MB, dumped from a PlayStation 2 you own.",
				      "requiredWhen": { "setting": "bios", "is": "ps2-0230a-20080220" } }
				  ]
				}
				""");
			// the Boot ROM setting chose the USA v2.30 dump: entry 1, and only it
			form.UseFirmwareNeeds(cfg, [ ("bios.bin", 1) ], [ ]);
			Shoot(form, "wizard-4-firmware-one-dump");
		}

		internal static (WaterboxConfig Cfg, IReadOnlyList<FirmwareLocator.IndexedFile> Index) MakeFirmwareFixture(string dir)
		{
			// the Firmware folder holds the US CD bios under a foreign name, and
			// one file that is no firmware at all
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
				// one id, one entry per dump - a sync setting picks which applies
				Firmware =
				[
					new() { Id = "bios_cd", Display = "Sega CD BIOS", Label = "US v1.10", Name = "bios_CD_U.bin", Sha1 = usSha1 },
					new() { Id = "bios_cd", Display = "Sega CD BIOS", Label = "JP v1.00", Name = "bios_CD_J.bin", Sha1 = jpSha1 },
					new() { Id = "bios", Display = "Famicom Disk System BIOS", Label = "Nintendo FDS", Name = "disksys.rom", Sha1 = fdsSha1 },
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
					Name = "dosbox-x", Path = "/home/you/Chimera/Cores/dosbox-x.chimeraCore",
					Sha1 = "9803F96728C7B454E5B9797E1A0CDEF6C2E19A4B", Systems = [ "DOS" ],
					Version = "8750be5a1c2d",
				},
				new()
				{
					Name = "quickernes", Path = "/home/you/Chimera/Cores/quickernes.chimeraCore",
					Sha1 = "3F2A91C47B0E5D8A1122334455667788990AABBC", Systems = [ "NES" ],
					Version = "0eebbf6d4e5f",
				},
			];
			NewProjectWizard form = new(cores, static _ => [ ]);
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
