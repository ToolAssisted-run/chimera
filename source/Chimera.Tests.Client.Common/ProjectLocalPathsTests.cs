using System.IO;

using Chimera.Client.Common;
using Chimera.Emulation.Common.Engine;

namespace Chimera.Tests.Client.Common
{
	/// <summary>
	/// The sidecar that remembers where a machine keeps a project's files. It is
	/// a hint and nothing more: it may spare the resolution dialog, it may never
	/// mount bytes the project did not record, and the project file must stay
	/// free of paths so it can still be handed to someone else.
	/// </summary>
	[TestClass]
	public class ProjectLocalPathsTests
	{
		private static string _dir = "";

		[ClassInitialize]
		public static void MakePlayground(TestContext _)
		{
			_dir = Path.Combine(Path.GetTempPath(), $"chimera-local-paths-{System.Diagnostics.Process.GetCurrentProcess().Id}");
			Directory.CreateDirectory(_dir);
		}

		[ClassCleanup]
		public static void RemovePlayground() => Directory.Delete(_dir, recursive: true);

		/// <summary>a project whose rom lives somewhere else entirely</summary>
		private static (string ProjectPath, string RomPath) MakeProject(string name, string romBytes = "cartridge bytes")
		{
			var romDir = Path.Combine(_dir, $"{name}-roms");
			Directory.CreateDirectory(romDir);
			var romPath = Path.Combine(romDir, "game.nes");
			File.WriteAllText(romPath, romBytes);

			var projectDir = Path.Combine(_dir, $"{name}-work");
			Directory.CreateDirectory(projectDir);
			var projectPath = Path.Combine(projectDir, $"{name}.chimeraProject");

			using var p = EngineProject.New();
			p.SetCore("quickernes", "abc123+local", new string('B', 40));
			p.FileAdd("game.nes", "rom", romPath);
			p.Save(projectPath);
			ProjectLocalPaths.Read(projectPath).Save(projectPath, p);
			return (projectPath, romPath);
		}

		[TestMethod]
		public void TheProjectItselfNeverLearnsAPath()
		{
			var (projectPath, romPath) = MakeProject("distributable");
			var json = File.ReadAllText(projectPath);
			StringAssert.Contains(json, "game.nes", "the project names the file");
			Assert.IsFalse(json.Contains(romPath), "the project must carry no paths - it is meant to be handed to someone else");
			Assert.IsFalse(json.Contains(Path.GetDirectoryName(romPath)!), "not even the folder");
		}

		[TestMethod]
		public void TheSidecarFindsAFileThatIsNotBesideTheProject()
		{
			var (projectPath, romPath) = MakeProject("remembered");
			Assert.IsTrue(File.Exists(ProjectLocalPaths.PathFor(projectPath)), "saving the project wrote the sidecar");

			using var reopened = EngineProject.Open(projectPath);
			reopened.ResolveDir(Path.GetDirectoryName(projectPath)!);
			Assert.IsFalse(reopened.FilesOk, "the rom is not beside the project, so nothing resolved");

			Assert.AreEqual(1, ProjectLocalPaths.Read(projectPath).ApplyTo(reopened));
			Assert.IsTrue(reopened.FilesOk, "the remembered location resolved it, with no dialog");
			Assert.AreEqual(Path.GetFullPath(romPath), Path.GetFullPath(reopened.FileSourcePath(0)));
		}

		[TestMethod]
		public void ARememberedPathHoldingOtherBytesIsLeftForTheUser()
		{
			var (projectPath, romPath) = MakeProject("changed");
			File.WriteAllText(romPath, "a different cartridge entirely");

			using var reopened = EngineProject.Open(projectPath);
			reopened.ResolveDir(Path.GetDirectoryName(projectPath)!);
			Assert.AreEqual(0, ProjectLocalPaths.Read(projectPath).ApplyTo(reopened), "nothing was resolved");
			Assert.AreEqual(1, reopened.FileStatus(0), "the file is UNRESOLVED, not quietly mounted with the wrong bytes");
			Assert.IsFalse(reopened.FilesOk, "so the resolution dialog still gets its say");
		}

		[TestMethod]
		public void AMissingSidecarIsSimplyNoHint()
		{
			var (projectPath, _) = MakeProject("nohint");
			File.Delete(ProjectLocalPaths.PathFor(projectPath));

			using var reopened = EngineProject.Open(projectPath);
			var local = ProjectLocalPaths.Read(projectPath);
			Assert.AreEqual(0, local.Files.Count);
			Assert.AreEqual(0, local.ApplyTo(reopened));
		}

		[TestMethod]
		public void AnUnreadableSidecarIsSimplyNoHint()
		{
			var (projectPath, _) = MakeProject("garbled");
			File.WriteAllText(ProjectLocalPaths.PathFor(projectPath), "{ this is not json");

			var local = ProjectLocalPaths.Read(projectPath);
			Assert.AreEqual(0, local.Files.Count, "a hint that cannot be read is a hint nobody has");
		}

		[TestMethod]
		public void FirmwareLocationsAreRememberedToo()
		{
			var (projectPath, _) = MakeProject("bios");
			var biosPath = Path.Combine(_dir, "panafz1.bin");
			File.WriteAllText(biosPath, "bios bytes");

			var local = ProjectLocalPaths.Read(projectPath);
			local.RememberFirmware("panafz1", biosPath);
			using (var p = EngineProject.Open(projectPath))
			{
				local.Save(projectPath, p);
			}

			var reread = ProjectLocalPaths.Read(projectPath);
			Assert.AreEqual(Path.GetFullPath(biosPath), reread.Firmware["panafz1"]);
			Assert.IsTrue(reread.Files.ContainsKey("game.nes"), "and the files it already knew about survived");
		}
	}
}
