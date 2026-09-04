using System;
using System.IO;
using System.Linq;

using Chimera.Client.Common;
using Chimera.Emulation.Common;

namespace Chimera.Tests.Client.Common.CorePackages
{
	/// <summary>
	/// The index that answers "where is this file": what it will hash, and what
	/// it will answer with. Both were wrong for one real machine - see the two
	/// tests - and both were wrong quietly, which is what these are here to stop.
	/// </summary>
	[TestClass]
	public class FirmwareLocatorTests
	{
		private static string MakeFolder()
		{
			var dir = Path.Combine(Path.GetTempPath(), $"chimera-loc-{Path.GetRandomFileName()}");
			Directory.CreateDirectory(dir);
			return dir;
		}

		private static string Sha1OfStream(string path)
		{
			using var sha1 = System.Security.Cryptography.SHA1.Create();
			using var stream = File.OpenRead(path);
			return string.Concat(sha1.ComputeHash(stream).Select(static b => b.ToString("X2")));
		}

		/// <summary>
		/// A firmware bigger than a gigabyte is a real machine's real file: the
		/// Xbox hard disk image xemu declares. The index skipped it, so a project
		/// made with it could never find it again and said so as a hash mismatch,
		/// which is not what was wrong (issue #38). The only honest bar is what
		/// the frontend could actually hand to a core.
		/// </summary>
		[TestMethod]
		public void ADiskImageIsFirmwareToo()
		{
			var dir = MakeFolder();
			try
			{
				var path = Path.Combine(dir, "xbox_hdd.qcow2");
				const long size = 1200L * 1024 * 1024; // over the bar that used to be
				try
				{
					using FileStream f = new(path, FileMode.Create, FileAccess.Write);
					f.SetLength(size); // sparse: no gigabyte is written anywhere
				}
				catch (IOException e)
				{
					Assert.Inconclusive($"no room for the image: {e.Message}");
					return;
				}

				var index = FirmwareLocator.BuildIndex([ dir ]);
				var found = index.FirstOrDefault(f => f.Path.EndsWith("xbox_hdd.qcow2", StringComparison.Ordinal));
				Assert.IsNotNull(found, "a file this size is still firmware");
				Assert.AreEqual(size, found.Length);
				// and the digest is the digest: taken by reading through the file,
				// it must equal what an unrelated implementation makes of it
				Assert.AreEqual(Sha1OfStream(path), found.Sha1);

				CoreFirmwareDecl decl = new() { Id = "hdd", Display = "Hard Disk Image", Sha1 = found.Sha1 };
				Assert.AreEqual(found.Path, FirmwareLocator.FindFor(decl, index)?.Path);
			}
			finally
			{
				Directory.Delete(dir, recursive: true);
			}
		}

		/// <summary>
		/// A hash is the identity where there is one. Where there is none - a
		/// disk image, a console's own dump, files no two people share - the
		/// declared name is the only lead there is, and it is compared the way
		/// the platform that wrote it would.
		/// </summary>
		[TestMethod]
		public void WithNoHashToAskByTheDeclaredNameAsks()
		{
			var dir = MakeFolder();
			try
			{
				File.WriteAllText(Path.Combine(dir, "Complex_4627.bin"), "flash");
				File.WriteAllText(Path.Combine(dir, "something_else.bin"), "not it");
				var index = FirmwareLocator.BuildIndex([ dir ]);

				CoreFirmwareDecl unpinned = new() { Id = "bios", Display = "Flash ROM", Name = "complex_4627.bin" };
				Assert.IsNull(FirmwareLocator.FindFor(unpinned, index), "there is no hash to find it by");
				StringAssert.Contains(FirmwareLocator.FindNamed(unpinned, index)?.Path, "Complex_4627.bin");
				StringAssert.Contains(FirmwareLocator.FindEither(unpinned, index)?.Path, "Complex_4627.bin");

				// and where there IS a hash, the name plays no part: only the
				// bytes answer, and a wrong file with the right name does not
				CoreFirmwareDecl pinned = new()
				{
					Id = "bios",
					Display = "Flash ROM",
					Name = "complex_4627.bin",
					Sha1 = new string('A', 40),
				};
				Assert.IsNull(FirmwareLocator.FindEither(pinned, index));
			}
			finally
			{
				Directory.Delete(dir, recursive: true);
			}
		}
	}
}
