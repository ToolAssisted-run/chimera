using System.IO;

using Chimera.Emulation.Common.Engine;

namespace Chimera.Tests.Emulation.Common.Engine
{
	/// <summary>
	/// The .chimeraProject through the managed wrapper - the marshalling half of
	/// what engine/tests/test_project.cpp pins in full. The rules live in the
	/// engine; these prove the ABI carries them intact through BizInvoke.
	/// </summary>
	[TestClass]
	public class EngineProjectTests
	{
		private static string _dir = "";

		[ClassInitialize]
		public static void MakePlayground(TestContext _)
		{
			_dir = Path.Combine(Path.GetTempPath(), $"chimera-project-tests-{System.Diagnostics.Process.GetCurrentProcess().Id}");
			Directory.CreateDirectory(_dir);
			File.WriteAllText(Path.Combine(_dir, "game.img"), "floppy bytes");
			File.WriteAllText(Path.Combine(_dir, "extra.img"), "more floppy bytes");
		}

		[ClassCleanup]
		public static void RemovePlayground() => Directory.Delete(_dir, recursive: true);

		[TestMethod]
		public void AProjectRoundTripsThroughTheEngine()
		{
			var path = Path.Combine(_dir, "roundtrip.chimeraProject");
			using (var p = EngineProject.New())
			{
				p.Title = "Alley Cat in 4:03";
				p.Description = "the funny cat game";
				p.SetCore("dosbox-x", "abc+local", new string('A', 40));
				p.Rerecords = 421;
				p.SetSettingsJson("""{"cpuCycles":3000}""");
				p.LogText = "[Input]\nLogKey:#P1 Up|\n|U|\n[/Input]\n";
				p.FileAdd("game.img", "floppy", Path.Combine(_dir, "game.img"));
				p.MarkerAdd(500, "later");
				p.MarkerAdd(10, "power on");
				p.BranchAdd("risky", 400, "|U|\n");
				p.Save(path);
			}

			using (var p = EngineProject.Open(path))
			{
				Assert.AreEqual("Alley Cat in 4:03", p.Title);
				Assert.AreEqual("the funny cat game", p.Description);
				Assert.AreEqual("dosbox-x", p.CoreName);
				Assert.AreEqual("abc+local", p.CoreVersion);
				Assert.AreEqual(new string('A', 40), p.CoreSha1);
				Assert.AreEqual(421UL, p.Rerecords);
				StringAssert.Contains(p.SettingsJson, "\"cpuCycles\":3000");
				Assert.AreEqual("[Input]\nLogKey:#P1 Up|\n|U|\n[/Input]\n", p.LogText);
				Assert.AreEqual(2, p.MarkerCount);
				Assert.AreEqual(10L, p.MarkerFrame(0), "markers keep frame order");
				Assert.AreEqual("later", p.MarkerText(1));
				Assert.AreEqual(1, p.BranchCount);
				Assert.AreEqual("risky", p.BranchName(0));
				Assert.AreEqual(400L, p.BranchFrame(0));
				Assert.AreEqual("|U|\n", p.BranchLogText(0));

				// files come back unresolved, then resolve by folder
				Assert.AreEqual(1, p.FileCount);
				Assert.AreEqual("game.img", p.FileName(0));
				Assert.AreEqual("floppy", p.FileSlot(0));
				Assert.AreEqual(1, p.FileStatus(0));
				Assert.IsNull(p.FileData(0));
				Assert.IsFalse(p.FilesOk);
				Assert.AreEqual(1, p.ResolveDir(_dir));
				Assert.IsTrue(p.FilesOk);
				Assert.AreEqual("floppy bytes".Length, p.FileData(0)!.Length);
				StringAssert.Contains(p.SlotsJson, "\"floppy\":[\"game.img\"]");
			}
		}

		[TestMethod]
		public void TheEngineRulesSurfaceAsExceptionsWithReasons()
		{
			using var p = EngineProject.New();
			var refused = Assert.ThrowsException<InvalidOperationException>(
				() => p.FileAdd("a/b.img", "floppy", Path.Combine(_dir, "game.img")));
			StringAssert.Contains(refused.Message, "bare");
			Assert.ThrowsException<InvalidOperationException>(() => p.SetSettingsJson("[]"));
			Assert.ThrowsException<InvalidOperationException>(
				() => EngineProject.Open(Path.Combine(_dir, "does-not-exist.chimeraProject")));
		}

		[TestMethod]
		public void ValidationSpeaksTheCoresDeclaration()
		{
			using var p = EngineProject.New();
			p.FileAdd("game.img", "floppy", Path.Combine(_dir, "game.img"));
			const string decl = """{"slots":[{"id":"floppy","min":0,"max":-1,"formats":["img"]}]}""";
			Assert.IsNull(p.Validate(decl));
			const string other = """{"slots":[{"id":"cdrom","min":0,"max":-1}]}""";
			StringAssert.Contains(p.Validate(other)!, "floppy");
		}

		[TestMethod]
		public void AMismatchIsAStatusAndTheActualHashIsVisible()
		{
			var path = Path.Combine(_dir, "mismatch.chimeraProject");
			using (var p = EngineProject.New())
			{
				p.FileAdd("game.img", "floppy", Path.Combine(_dir, "game.img"));
				p.Save(path);
			}
			using (var p = EngineProject.Open(path))
			{
				p.FileResolve(0, Path.Combine(_dir, "extra.img")); // wrong bytes, valid path
				Assert.AreEqual(2, p.FileStatus(0));
				Assert.IsFalse(p.FilesOk);
				Assert.AreNotEqual(p.FileSha1(0), p.FileActualSha1(0));
			}
		}
	}
}
