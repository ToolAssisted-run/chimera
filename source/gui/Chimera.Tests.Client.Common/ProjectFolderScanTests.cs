using System.IO;
using System.Linq;

using Chimera.Client.Common;
using Chimera.Emulation.Common.Engine;

namespace Chimera.Tests.Client.Common
{
	[TestClass]
	public class ProjectFolderScanTests
	{
		private static string _dir = "";

		[ClassInitialize]
		public static void MakePlayground(TestContext _)
		{
			_dir = Path.Combine(Path.GetTempPath(), $"chimera-scan-{System.Guid.NewGuid():N}");
			Directory.CreateDirectory(_dir);
		}

		[ClassCleanup(ClassCleanupBehavior.EndOfClass)]
		public static void RemovePlayground() => Directory.Delete(_dir, recursive: true);

		/// <summary>
		/// Enumerate descends by default and stops when told not to. The
		/// default is what every caller relied on before the parameter existed,
		/// so a change to it would silently narrow every scan in the frontend.
		/// </summary>
		[TestMethod]
		public void SubFoldersAreWalkedUnlessTheScanIsToldNotTo()
		{
			var root = Path.Combine(_dir, "recurse-case");
			var nested = Path.Combine(root, "deeper", "deeper still");
			Directory.CreateDirectory(nested);
			var atTop = Path.Combine(root, "top.rom");
			var buried = Path.Combine(nested, "buried.rom");
			File.WriteAllText(atTop, "top");
			File.WriteAllText(buried, "buried");

			var withSubfolders = ProjectFolderScan.Enumerate(root).ToList();
			CollectionAssert.Contains(withSubfolders, atTop);
			CollectionAssert.Contains(withSubfolders, buried, "the default must still descend");

			var topOnly = ProjectFolderScan.Enumerate(root, recurse: false).ToList();
			CollectionAssert.Contains(topOnly, atTop);
			CollectionAssert.DoesNotContain(topOnly, buried, "unticked, the scan must stay in the folder it was given");
		}

		/// <summary>a saved two-file project whose files live nowhere near it</summary>
		private static string MakeProject(string name, out string romBytes, out string biosBytes)
		{
			romBytes = $"{name} rom bytes";
			biosBytes = $"{name} bios bytes";
			var srcDir = Path.Combine(_dir, $"{name}-src");
			Directory.CreateDirectory(srcDir);
			var rom = Path.Combine(srcDir, "game.nes");
			var bios = Path.Combine(srcDir, "bios.bin");
			File.WriteAllText(rom, romBytes);
			File.WriteAllText(bios, biosBytes);

			var projectDir = Path.Combine(_dir, $"{name}-work");
			Directory.CreateDirectory(projectDir);
			var path = Path.Combine(projectDir, $"{name}.chimeraProject");
			using var p = EngineProject.New();
			p.SetCore("quickernes", "abc123+local", new string('B', 40));
			p.FileAdd("game.nes", "rom", rom);
			p.FileAdd("bios.bin", "support", bios);
			p.Save(path);
			// the scan must do the finding: the originals go away
			Directory.Delete(srcDir, recursive: true);
			// and so does the record of where they were, or the scan is answered
			// before it starts
			var remembered = ProjectLocalPaths.PathFor(p);
			if (File.Exists(remembered)) File.Delete(remembered);
			return path;
		}

		[TestMethod]
		public void HashResolvesARenamedFileInASubfolder()
		{
			var path = MakeProject("renamed", out var romBytes, out var biosBytes);
			var stash = Path.Combine(_dir, "renamed-stash", "deep", "deeper");
			Directory.CreateDirectory(stash);
			// names say nothing; only the bytes match
			File.WriteAllText(Path.Combine(stash, "whatever.bin"), romBytes);
			File.WriteAllText(Path.Combine(_dir, "renamed-stash", "also-not-its-name.dat"), biosBytes);

			using var p = EngineProject.Open(path);
			Assert.IsFalse(p.FilesOk);
			Assert.AreEqual(2, ProjectFolderScan.Resolve(p, Path.Combine(_dir, "renamed-stash")));
			Assert.IsTrue(p.FilesOk, "the hash is the identity; the on-disk names play no part");
		}

		/// <summary>
		/// Resolve carries the same choice down to Enumerate. It is a separate
		/// check from the one above because Resolve calls Enumerate itself, and
		/// a caller who passes the flag to Resolve has no other way to know it
		/// arrived.
		/// </summary>
		[TestMethod]
		public void ResolveStaysInTheFolderWhenToldNotToDescend()
		{
			var path = MakeProject("shallow", out var romBytes, out var biosBytes);
			var stash = Path.Combine(_dir, "shallow-stash");
			Directory.CreateDirectory(Path.Combine(stash, "below"));
			File.WriteAllText(Path.Combine(stash, "game.nes"), romBytes);
			File.WriteAllText(Path.Combine(stash, "below", "bios.bin"), biosBytes);

			using (var shallow = EngineProject.Open(path))
			{
				Assert.AreEqual(1, ProjectFolderScan.Resolve(shallow, stash, recurse: false),
					"only the file in the folder itself; the one below it was not to be looked at");
				Assert.IsFalse(shallow.FilesOk);
			}

			using var deep = EngineProject.Open(path);
			Assert.AreEqual(2, ProjectFolderScan.Resolve(deep, stash),
				"and the default reaches the one below");
			Assert.IsTrue(deep.FilesOk);
		}

		[TestMethod]
		public void NameIsTheFallbackAndItsVerdictIsHonest()
		{
			var path = MakeProject("edited", out _, out _);
			var stash = Path.Combine(_dir, "edited-stash");
			Directory.CreateDirectory(stash);
			// right names, wrong bytes - resolved, but marked, like Locate would
			File.WriteAllText(Path.Combine(stash, "game.nes"), "not the recorded bytes");
			File.WriteAllText(Path.Combine(stash, "bios.bin"), "these neither");

			using var p = EngineProject.Open(path);
			Assert.AreEqual(2, ProjectFolderScan.Resolve(p, stash));
			Assert.IsFalse(p.FilesOk, "a name match with other bytes is a MISMATCH, not a resolution");
			Assert.AreEqual(2, p.FileStatus(0));
			Assert.AreEqual(2, p.FileStatus(1));
		}

		[TestMethod]
		public void TheExactHashBeatsTheRightName()
		{
			var path = MakeProject("both", out var romBytes, out _);
			var stash = Path.Combine(_dir, "both-stash");
			Directory.CreateDirectory(stash);
			File.WriteAllText(Path.Combine(stash, "game.nes"), "an impostor with the right name");
			File.WriteAllText(Path.Combine(stash, "the-real-one.dump"), romBytes);

			using var p = EngineProject.Open(path);
			ProjectFolderScan.Resolve(p, stash);
			Assert.AreEqual(0, p.FileStatus(0), "the exact bytes win over the right name");
			StringAssert.Contains(p.FileSourcePath(0), "the-real-one.dump");
		}

		[TestMethod]
		public void AResolvedFileIsLeftAlone()
		{
			var path = MakeProject("settled", out var romBytes, out var biosBytes);
			var good = Path.Combine(_dir, "settled-good");
			Directory.CreateDirectory(good);
			var chosen = Path.Combine(good, "game.nes");
			File.WriteAllText(chosen, romBytes);
			File.WriteAllText(Path.Combine(good, "bios.bin"), biosBytes);

			using var p = EngineProject.Open(path);
			p.FileResolve(0, chosen);
			// a second copy of the rom elsewhere must not steal the resolution
			var other = Path.Combine(_dir, "settled-other");
			Directory.CreateDirectory(other);
			File.WriteAllText(Path.Combine(other, "copy.nes"), romBytes);
			File.WriteAllText(Path.Combine(other, "bios.bin"), biosBytes);

			ProjectFolderScan.Resolve(p, other);
			Assert.AreEqual(chosen, p.FileSourcePath(0), "what was already resolved stays put");
			Assert.IsTrue(p.FilesOk);
		}
	}
}
