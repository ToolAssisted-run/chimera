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

				// other files are listed when they exist, once each
				var coreLog = Path.Combine(root, "core log.txt");
				File.WriteAllText(coreLog, "core");
				CollectionAssert.AreEqual(
					new[] { project, newer, Path.ChangeExtension(newer, ".dmp"), log, coreLog },
					(System.Collections.ICollection) IssueReport.FilesToAttach(project, crashes, [ program ], [ coreLog, null, "", Path.Combine(root, "gone.txt"), log ]));

				// a project that was never saved, no crash and no log: nothing to list
				Assert.AreEqual(0, IssueReport.FilesToAttach(Path.Combine(root, "unsaved.chimeraProject"), Path.Combine(root, "none"), [ root ]).Count);
				Assert.AreEqual(0, IssueReport.FilesToAttach(null, Path.Combine(root, "none"), [ ]).Count);
			}
			finally
			{
				Directory.Delete(root, recursive: true);
			}
		}

		private static Dictionary<string, string> Entries(string zipPath)
		{
			Dictionary<string, string> entries = new();
			using var zip = System.IO.Compression.ZipFile.OpenRead(zipPath);
			foreach (var entry in zip.Entries)
			{
				using StreamReader reader = new(entry.Open());
				entries[entry.FullName] = reader.ReadToEnd();
			}
			return entries;
		}

		[TestMethod]
		public void TheZipHoldsTheTextThePictureAndTheFilesUnderTheirOwnNames()
		{
			var root = Path.Combine(Path.GetTempPath(), "chimera-issue-zip-" + Path.GetRandomFileName());
			Directory.CreateDirectory(Path.Combine(root, "a"));
			Directory.CreateDirectory(Path.Combine(root, "b"));
			try
			{
				var project = Path.Combine(root, "a", "run.chimeraProject");
				var log = Path.Combine(root, "a", "minibox-diag.log");
				var sameName = Path.Combine(root, "b", "minibox-diag.log");
				File.WriteAllText(project, "{project}");
				File.WriteAllText(log, "first log");
				File.WriteAllText(sameName, "second log");
				var zipPath = Path.Combine(root, "report.zip");
				IssueReport.ZipResult result;
				// a log this program still has open for writing is read all the same
				using (var open = new FileStream(log, FileMode.Open, FileAccess.Write, FileShare.Read))
				{
					result = IssueReport.WriteZip(zipPath, "**Chimera build:** x\r\n**Game:** y", [ 1, 2, 3 ], [ project, log, sameName, Path.Combine(root, "gone.dmp") ]);
				}

				CollectionAssert.AreEqual(new[] { Path.Combine(root, "gone.dmp") }, (System.Collections.ICollection) result.NotRead);
				Assert.AreEqual(new FileInfo(zipPath).Length, result.Bytes);
				var entries = Entries(zipPath);
				CollectionAssert.AreEquivalent(
					new[] { "report.md", "game.png", "run.chimeraProject", "minibox-diag.log", "minibox-diag (2).log" },
					entries.Keys);
				Assert.AreEqual("**Chimera build:** x\n**Game:** y\n", entries["report.md"]);
				Assert.AreEqual("\u0001\u0002\u0003", entries["game.png"]);
				Assert.AreEqual("{project}", entries["run.chimeraProject"]);
				Assert.AreEqual("first log", entries["minibox-diag.log"]);
				Assert.AreEqual("second log", entries["minibox-diag (2).log"]);

				// with nothing but the text, the zip holds the text
				var textOnly = Path.Combine(root, "text.zip");
				IssueReport.WriteZip(textOnly, "t", null, [ ]);
				CollectionAssert.AreEquivalent(new[] { "report.md" }, Entries(textOnly).Keys);
			}
			finally
			{
				Directory.Delete(root, recursive: true);
			}
		}

		/// <summary>Whether the call failed the one way callers are told about: an IOException, or a kind of one.</summary>
		private static bool Refused(System.Action write)
		{
			try
			{
				write();
				return false;
			}
			catch (IOException)
			{
				return true;
			}
		}

		[TestMethod]
		public void AZipThatCannotBeWrittenLeavesNoFileBehind()
		{
			var root = Path.Combine(Path.GetTempPath(), "chimera-issue-zip-" + Path.GetRandomFileName());
			Directory.CreateDirectory(root);
			try
			{
				// a folder is where the zip should go: it cannot be created
				var blocked = Path.Combine(root, "is-a-folder.zip");
				Directory.CreateDirectory(blocked);
				Assert.IsTrue(Refused(() => IssueReport.WriteZip(blocked, "t", null, [ ])), "writing over a folder should fail");
				Assert.IsTrue(Directory.Exists(blocked), "the folder in the way was not touched");
				var nowhere = Path.Combine(root, "no-such-folder", "x.zip");
				Assert.IsTrue(Refused(() => IssueReport.WriteZip(nowhere, "t", null, [ ])), "writing into a folder that is not there should fail");
				Assert.IsFalse(File.Exists(nowhere));
			}
			finally
			{
				Directory.Delete(root, recursive: true);
			}
		}
	}
}
