using System.IO;

using Chimera.Client.Common;

namespace Chimera.Tests.Client.Common
{
	/// <summary>
	/// Where a file dialog opens (chimera#189): a folder from Config > Paths when it is
	/// there, and never a folder that is not - the dialog helper refuses one in a DEBUG
	/// build, which took Export Save Data down, and Open ROM was passing the same folder.
	/// </summary>
	[TestClass]
	public class DialogDirTests
	{
		private static string NewDir()
		{
			var dir = Path.Combine(Path.GetTempPath(), "chimera-dialogdir-" + Path.GetRandomFileName());
			Directory.CreateDirectory(dir);
			return dir;
		}

		private static PathEntryCollection PathsWithRomDir(string system, string romDir)
		{
			PathEntryCollection paths = new();
			paths.Paths.Add(new(system, "Base", romDir));
			paths.Paths.Add(new(system, "ROM", romDir));
			return paths;
		}

		[TestMethod]
		public void TheFirstFolderThatIsThereIsWhereADialogOpens()
		{
			var there = NewDir();
			var alsoThere = NewDir();
			var missing = Path.Combine(there, "not made");
			try
			{
				Assert.AreEqual(there, PathEntryExtensions.FirstExistingDir(there));
				Assert.AreEqual(there, PathEntryExtensions.FirstExistingDir(missing, null, "", "   ", there, alsoThere), "what is missing, null or blank is passed over");
				Assert.AreEqual("", PathEntryExtensions.FirstExistingDir(missing), "and nothing there leaves the dialog to its own default");
				Assert.AreEqual("", PathEntryExtensions.FirstExistingDir());
				var file = Path.Combine(there, "a file");
				File.WriteAllText(file, "x");
				Assert.AreEqual("", PathEntryExtensions.FirstExistingDir(file), "a file is not a folder to open in");
			}
			finally
			{
				Directory.Delete(there, recursive: true);
				Directory.Delete(alsoThere, recursive: true);
			}
		}

		[TestMethod]
		public void ASystemsRomFolderThenTheLastRomsThenTheDialogsOwn()
		{
			var romDir = NewDir();
			var lastDir = NewDir();
			var missing = Path.Combine(romDir, "not made");
			try
			{
				var configured = PathsWithRomDir("TestSys", romDir);
				configured.LastRomPath = lastDir;
				Assert.AreEqual(romDir, configured.RomDialogDir("TestSys"), "the system's own folder, when it is there");

				var unmade = PathsWithRomDir("TestSys", missing);
				Assert.AreEqual(missing, unmade.RomAbsolutePath("TestSys"), "the folder Config > Paths names is not there");
				Assert.AreEqual("", unmade.RomDialogDir("TestSys"), "no ROM opened yet: \".\" is the unset value, not the working directory");
				unmade.LastRomPath = lastDir;
				Assert.AreEqual(lastDir, unmade.RomDialogDir("TestSys"), "else where the last ROM came from");
				unmade.LastRomPath = Path.Combine(lastDir, "gone");
				Assert.AreEqual("", unmade.RomDialogDir("TestSys"), "else the dialog's own default");

				PathEntryCollection recent = PathsWithRomDir("TestSys", romDir);
				recent.UseRecentForRoms = true;
				recent.LastRomPath = lastDir;
				Assert.AreEqual(lastDir, recent.RomDialogDir("TestSys"), "'use recent' asks for the last ROM's folder by name");
			}
			finally
			{
				Directory.Delete(romDir, recursive: true);
				Directory.Delete(lastDir, recursive: true);
			}
		}
	}
}
