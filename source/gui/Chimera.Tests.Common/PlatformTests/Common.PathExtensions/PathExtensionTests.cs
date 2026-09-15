using Chimera.Common;
using Chimera.Common.PathExtensions;

using PE = Chimera.Common.PathExtensions.PathExtensions;

namespace Chimera.Tests.Common.PathExtensions
{
	[TestClass]
	public class PathExtensionTests
	{
		[TestMethod]
		public void TestNullability()
		{
			PlatformTestUtils.RunEverywhere();

			var p = OSTailoredCode.IsUnixHost ? "/" : @"C:\";

			Assert.IsFalse(PE.IsSubfolderOf(childPath: null, parentPath: p), "IsSubfolderOf(null, *)");
			Assert.IsFalse(PE.IsSubfolderOf(childPath: p, parentPath: null), "IsSubfolderOf(*, null)");
			Assert.IsFalse(PE.IsSubfolderOf(childPath: null, parentPath: null), "IsSubfolderOf(null, null)");

			Assert.IsNull(PE.GetRelativePath(fromPath: null, toPath: p), "GetRelativePath(null, *)");
			Assert.IsNull(PE.GetRelativePath(fromPath: p, toPath: null), "GetRelativePath(*, null)");
			Assert.IsNull(PE.GetRelativePath(fromPath: null, toPath: null), "GetRelativePath(null, null)");

			Assert.IsNull(PE.MakeRelativeTo(absolutePath: null, basePath: p), "MakeRelativeTo(null, *)");
			Assert.IsNull(PE.MakeRelativeTo(absolutePath: p, basePath: null), "MakeRelativeTo(*, null)");
			Assert.IsNull(PE.MakeRelativeTo(absolutePath: null, basePath: null), "MakeRelativeTo(null, null)");
		}

		[TestMethod]
		[DataRow(true, "/usr/share", "/usr")]
		[DataRow(false, "/usr/share", "/bin")]
		[DataRow(false, "/usr", "/usr/share")]
		[DataRow(true, "/usr", "/usr")]
		[DataRow(true, "/usr", "/usr/")]
		[DataRow(false, "/etc/rmdir", "/etc/rm")] // not naive StartsWith; these don't exist but the implementation uses `realpath -m` so they will be classed as two real and distinct dirs
#if false // don't work on NixOS and probably other distros, presumably all 32-bit distros
		[DataRow(true, "/usr/lib64", "/usr/lib")] // symlink to same dir
		[DataRow(true, "/usr/lib64/gconv", "/usr/lib")] // same symlink, checking child
#endif
		public void TestIsSubfolderOfUnix(bool expectedIsSubfolder, string childPath, string parentPath)
		{
			PlatformTestUtils.OnlyRunOnRealUnix();

			Assert.AreEqual(expectedIsSubfolder, childPath.IsSubfolderOf(parentPath), "child.IsSubfolderOf(parent)");
		}

		[TestMethod]
		[DataRow(true, @"C:\Users\Public", @"C:\Users")]
		[DataRow(false, @"C:\Users\Public", @"C:\Program Files")]
		[DataRow(false, @"C:\Users", @"C:\Users\Public")]
		[DataRow(true, @"C:\Users", @"C:\Users")]
		[DataRow(true, @"C:\Users", @"C:\Users\")]
		[DataRow(false, @"C:\Program Files (x86)", @"C:\Program Files")] // not naive StartsWith
		public void TestIsSubfolderOfWindows(bool expectedIsSubfolder, string childPath, string parentPath)
		{
			PlatformTestUtils.OnlyRunOnWindows();

			Assert.AreEqual(expectedIsSubfolder, childPath.IsSubfolderOf(parentPath), "child.IsSubfolderOf(parent)");
		}

		[TestMethod]
		[DataRow("./share", true, "/usr/share", "/usr")]
		[DataRow(".", true, "/usr", "/usr")]
		[DataRow("..", false, "/usr", "/usr/share")]
		[DataRow("../bin", false, "/usr/bin", "/usr/share")]
		[DataRow("../../etc", false, "/etc", "/usr/share")]
		public void TestGetRelativeUnix(string expectedRelPath, bool isChild, string absolutePath, string basePath)
		{
			PlatformTestUtils.OnlyRunOnRealUnix();

			Assert.AreEqual(expectedRelPath, PE.GetRelativePath(fromPath: basePath, toPath: absolutePath), "GetRelativePath(base, absolute)"); // params swapped w.r.t. `MakeRelativeTo`
			// `MakeRelativeTo` is supposed to return an absolute path (the receiver is assumed to be absolute) iff the receiver isn't a child of the given base path
			Assert.AreEqual(isChild ? expectedRelPath : absolutePath, absolutePath.MakeRelativeTo(basePath), "absolute.MakeRelativeTo(base)");
		}

		[TestMethod]
		[DataRow(@".\SysWOW64", true, @"C:\Windows\SysWOW64", @"C:\Windows")]
		[DataRow(@".", true, @"C:\Windows", @"C:\Windows")]
		[DataRow(@"..", false, @"C:\Windows", @"C:\Windows\SysWOW64")]
		[DataRow(@"..\System32", false, @"C:\Windows\System32", @"C:\Windows\SysWOW64")]
		[DataRow(@"..\..\Program Files", false, @"C:\Program Files", @"C:\Windows\SysWOW64")]
		public void TestGetRelativeWindows(string expectedRelPath, bool isChild, string absolutePath, string basePath)
		{
			PlatformTestUtils.OnlyRunOnWindows();

			Assert.AreEqual(expectedRelPath, PE.GetRelativePath(fromPath: basePath, toPath: absolutePath), "GetRelativePath(base, absolute)"); // params swapped w.r.t. `MakeRelativeTo`
			// `MakeRelativeTo` is supposed to return an absolute path (the receiver is assumed to be absolute) iff the receiver isn't a child of the given base path
			Assert.AreEqual(isChild ? expectedRelPath : absolutePath, absolutePath.MakeRelativeTo(basePath), "absolute.MakeRelativeTo(base)");
		}

		/// <summary>
		/// Issue #77: WSLg mounts the distro's root again, read-only, at
		/// /mnt/wslg/distro, and a path picked through it could not be written.
		/// </summary>
		[TestMethod]
		[DataRow("/mnt/wslg/distro/home/tas/SonicUnleashed.iso", true, "/home/tas/SonicUnleashed.iso")]
		[DataRow("/mnt/wslg/distro", true, "/")]
		[DataRow("/mnt/wslg/distro/", true, "/")]
		[DataRow("/mnt/wslg/distros/x", true, "/mnt/wslg/distros/x")] // a longer name, not the mirror
		[DataRow("/home/tas/a.iso", true, "/home/tas/a.iso")]
		[DataRow("/mnt/wslg/distro/home/tas/a.iso", false, "/mnt/wslg/distro/home/tas/a.iso")] // only WSL has the mirror
		[DataRow("", true, "")]
		public void TestWithoutWslgMirror(string path, bool onWsl, string expected)
		{
			PlatformTestUtils.RunEverywhere();
			Assert.AreEqual(expected, PE.WithoutWslgMirror(path, onWsl));
		}
	}
}
