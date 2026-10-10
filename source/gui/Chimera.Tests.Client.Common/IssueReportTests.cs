using System.Collections.Generic;
using System.IO;

using Chimera.Client.Common;
using Chimera.Emulation.Common.Waterbox;

namespace Chimera.Tests.Client.Common
{
	/// <summary>
	/// Help &gt; Report an Issue (issue #243): the first lines of the bug report
	/// form, filled in from the session, and the files worth attaching.
	/// </summary>
	[TestClass]
	public class IssueReportTests
	{
		private const string Build = "Commit 1ab07c06c (2026-10-06 17:05 UTC)";

		[TestMethod]
		public void TheLinkIsTheProjectsOwnIssueForm()
			=> Assert.AreEqual("https://github.com/ToolAssisted-run/chimera/issues/new/choose", IssueReport.FormUrl);

		[TestMethod]
		public void ASessionWithAProjectFillsEveryLine()
		{
			var text = IssueReport.Text(new()
			{
				Build = Build,
				CoreName = "PCSX2",
				CoreVersion = "2026-10-10 (18cd581)",
				Os = "Microsoft Windows 10.0.19045 (X64)",
				Gpu = "4.6.0 NVIDIA 536.23 on GeForce GTX 1060/PCIe/SSE2",
				Files = [ new("disc", "Game (USA).iso"), new("savedata", "memcard1.ps2") ],
				Settings = [ new("Renderer", "opengl-hw", "software"), new("Internal Resolution", "2", "1") ],
			});
			Assert.AreEqual(
				"**Chimera build:** " + Build + "\n"
				+ "**Core and version:** PCSX2 2026-10-10 (18cd581)\n"
				+ "**OS and GPU:** Microsoft Windows 10.0.19045 (X64), graphics driver 4.6.0 NVIDIA 536.23 on GeForce GTX 1060/PCIe/SSE2\n"
				+ "**Game:**\n- disc: Game (USA).iso\n- savedata: memcard1.ps2\n"
				+ "**Renderer and non-default settings:**\n- Renderer: opengl-hw (default: software)\n- Internal Resolution: 2 (default: 1)",
				text);
		}

		[TestMethod]
		public void WhatIsNotKnownIsLeftAsANoteTheFinishedReportDoesNotShow()
		{
			var text = IssueReport.Text(new() { Build = Build, Os = "Linux 6.8 (X64)" });
			Assert.AreEqual(
				"**Chimera build:** " + Build + "\n"
				+ "**Core and version:** <!-- no core was running -->\n"
				+ "**OS and GPU:** Linux 6.8 (X64), <!-- type your graphics card here -->\n"
				+ "**Game:** <!-- no project was open -->\n"
				+ "**Renderer and non-default settings:** <!-- no core was running -->",
				text);
		}

		[TestMethod]
		public void AProjectWithNoFilesAndDefaultSettingsSaysSo()
		{
			var text = IssueReport.Text(new() { Build = Build, CoreName = "SDLPoP", Os = "x", Files = [ ], Settings = [ ] });
			StringAssert.Contains(text, "**Game:** the project has no files of its own\n");
			StringAssert.EndsWith(text, "**Renderer and non-default settings:** every setting is at its default");
		}

		[TestMethod]
		public void OnlySettingsThatDifferFromTheCoresDefaultAreListed()
		{
			WaterboxCoreSettings settings = new()
			{
				Declarations =
				[
					new() { Name = "renderer", Display = "Renderer", Type = "enum", Options = [ "software", "opengl-hw" ], Default = "software" },
					new() { Name = "scale", Display = "Internal Resolution", Type = "int", Default = 1L, Min = 1, Max = 8 },
					new() { Name = "fast_boot", Type = "bool", Default = false },
					new() { Name = "speed", Display = "Speed", Type = "float", Default = 1.0 },
					new() { Name = "untouched", Type = "int", Default = 5L },
				],
				Values = new Dictionary<string, object>
				{
					["renderer"] = "opengl-hw",
					// a value a settings file brought back as another number type is still the default
					["scale"] = 1,
					["fast_boot"] = true,
					["speed"] = 1.5,
					["not declared"] = "ignored",
				},
			};
			var changed = IssueReport.ChangedSettings(settings);
			CollectionAssert.AreEqual(
				new IssueReport.ChangedSetting[]
				{
					new("Renderer", "opengl-hw", "software"),
					new("fast_boot", "true", "false"),
					new("Speed", "1.5", "1"),
				},
				(System.Collections.ICollection) changed);
		}

		[TestMethod]
		public void SettingsWithNoDeclarationsListNothing()
		{
			Assert.AreEqual(0, IssueReport.ChangedSettings(null).Count);
			Assert.AreEqual(0, IssueReport.ChangedSettings(new WaterboxCoreSettings { Values = new() { ["a"] = 1 } }).Count);
		}

		[TestMethod]
		public void TheFilesToAttachAreTheOnesThatExist()
		{
			var root = Path.Combine(Path.GetTempPath(), "chimera-issue-report-" + Path.GetRandomFileName());
			var crashes = Path.Combine(root, "Crashes");
			var program = Path.Combine(root, "program");
			Directory.CreateDirectory(crashes);
			Directory.CreateDirectory(program);
			try
			{
				var project = Path.Combine(root, "run.chimeraProject");
				File.WriteAllText(project, "{}");
				var older = Path.Combine(crashes, "2026-10-01 pid1.txt");
				var newer = Path.Combine(crashes, "2026-10-09 pid2.txt");
				File.WriteAllText(older, "old");
				File.WriteAllText(Path.ChangeExtension(older, ".dmp"), "old dump");
				File.WriteAllText(newer, "new");
				File.WriteAllText(Path.ChangeExtension(newer, ".dmp"), "new dump");
				File.SetLastWriteTimeUtc(older, new System.DateTime(2026, 10, 1, 0, 0, 0, System.DateTimeKind.Utc));
				File.SetLastWriteTimeUtc(newer, new System.DateTime(2026, 10, 9, 0, 0, 0, System.DateTimeKind.Utc));
				var log = Path.Combine(program, "minibox-diag.log");
				File.WriteAllText(log, "log");

				CollectionAssert.AreEqual(
					new[] { project, newer, Path.ChangeExtension(newer, ".dmp"), log },
					(System.Collections.ICollection) IssueReport.FilesToAttach(project, crashes, [ Path.Combine(root, "nowhere"), program, program ]));

				// a project that was never saved, no crash and no log: nothing to list
				Assert.AreEqual(0, IssueReport.FilesToAttach(Path.Combine(root, "unsaved.chimeraProject"), Path.Combine(root, "none"), [ root ]).Count);
				Assert.AreEqual(0, IssueReport.FilesToAttach(null, Path.Combine(root, "none"), [ ]).Count);
			}
			finally
			{
				Directory.Delete(root, recursive: true);
			}
		}
	}
}
