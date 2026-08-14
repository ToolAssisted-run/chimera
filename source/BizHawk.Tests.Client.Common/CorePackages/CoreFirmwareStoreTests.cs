using System.Collections.Generic;
using System.IO;
using System.Linq;

using BizHawk.Client.Common;
using BizHawk.Emulation.Common;

namespace BizHawk.Tests.Client.Common.CorePackages
{
	/// <summary>
	/// A core package declares the files it cannot ship (a disk-system BIOS, say)
	/// and the user provides them once. These check the verdict the frontend reaches
	/// about a provided file, which is the whole of its side of the deal: it never
	/// knows what any of these files are.
	/// </summary>
	[TestClass]
	public class CoreFirmwareStoreTests
	{
		private static CoreFirmwareDecl Decl(int size = 8192, params string[] sha1)
			=> new()
			{
				Id = "bios",
				Display = "FDS BIOS",
				Size = size,
				Sha1 = sha1.Length is 0 ? null : new List<string>(sha1),
			};

		/// <summary>A file of <paramref name="size"/> deterministic bytes, deleted with the test run.</summary>
		private static string FileOf(int size, byte seed = 0)
		{
			var path = Path.Combine(Path.GetTempPath(), $"minihawk-fw-{size}-{seed}-{Path.GetRandomFileName()}");
			var bytes = new byte[size];
			for (var i = 0; i < size; i++) bytes[i] = (byte) (i + seed);
			File.WriteAllBytes(path, bytes);
			return path;
		}

		private static Config WithPath(string coreName, string id, string path)
		{
			Config config = new();
			CoreFirmwareStore.SetPath(config, coreName, id, path);
			return config;
		}

		[TestMethod]
		public void NothingProvidedIsMissing()
		{
			var entry = CoreFirmwareStore.Describe(new Config(), "QuickerNesHawk", Decl());
			Assert.AreEqual(CoreFirmwareState.Missing, entry.State);
			Assert.IsFalse(entry.Usable);
		}

		[TestMethod]
		public void AFileThatWentAwayIsReportedRatherThanForgotten()
		{
			var path = FileOf(8192);
			var config = WithPath("Core", "bios", path);
			File.Delete(path);
			var entry = CoreFirmwareStore.Describe(config, "Core", Decl());
			Assert.AreEqual(CoreFirmwareState.Unreadable, entry.State);
			Assert.IsFalse(entry.Usable);
		}

		[TestMethod]
		public void WrongSizeIsRefused()
		{
			var path = FileOf(4096);
			var entry = CoreFirmwareStore.Describe(WithPath("Core", "bios", path), "Core", Decl());
			Assert.AreEqual(CoreFirmwareState.WrongSize, entry.State);
			Assert.IsFalse(entry.Usable);
			File.Delete(path);
		}

		/// <summary>A dump nobody listed is still a dump; refusing it would make the core unusable for a good file the declaration has never seen.</summary>
		[TestMethod]
		public void UnknownHashIsUsableButFlagged()
		{
			var path = FileOf(8192);
			var entry = CoreFirmwareStore.Describe(WithPath("Core", "bios", path), "Core", Decl(8192, "0123456789ABCDEF0123456789ABCDEF01234567"));
			Assert.AreEqual(CoreFirmwareState.Unrecognised, entry.State);
			Assert.IsTrue(entry.Usable);
			File.Delete(path);
		}

		[TestMethod]
		public void MatchingHashIsGood()
		{
			var path = FileOf(8192);
			var sha1 = CoreFirmwareStore.Sha1Of(File.ReadAllBytes(path));
			var entry = CoreFirmwareStore.Describe(WithPath("Core", "bios", path), "Core", Decl(8192, "0123456789ABCDEF0123456789ABCDEF01234567", sha1));
			Assert.AreEqual(CoreFirmwareState.Good, entry.State);
			Assert.AreEqual(sha1, entry.Sha1);
			File.Delete(path);
		}

		/// <summary>A declaration with no hashes at all (a core that only cares about size) accepts what it is given.</summary>
		[TestMethod]
		public void NoDeclaredHashesMeansAnyFileOfTheRightSizeIsGood()
		{
			var path = FileOf(8192);
			var entry = CoreFirmwareStore.Describe(WithPath("Core", "bios", path), "Core", Decl());
			Assert.AreEqual(CoreFirmwareState.Good, entry.State);
			File.Delete(path);
		}

		[TestMethod]
		public void SizeZeroAcceptsAnySize()
		{
			var path = FileOf(1234);
			var entry = CoreFirmwareStore.Describe(WithPath("Core", "bios", path), "Core", Decl(size: 0));
			Assert.AreEqual(CoreFirmwareState.Good, entry.State);
			File.Delete(path);
		}

		/// <summary>The core gets bytes only when the file is usable - a wrong file must fail as "provide this", not as a crash inside the sandbox.</summary>
		[TestMethod]
		public void ProviderHandsOverOnlyUsableFiles()
		{
			var good = FileOf(8192);
			var small = FileOf(64);

			var provider = CoreFirmwareStore.ProviderFor(WithPath("Core", "bios", good), "Core");
			CollectionAssert.AreEqual(File.ReadAllBytes(good), provider(Decl()));

			provider = CoreFirmwareStore.ProviderFor(WithPath("Core", "bios", small), "Core");
			Assert.IsNull(provider(Decl()));

			provider = CoreFirmwareStore.ProviderFor(new Config(), "Core");
			Assert.IsNull(provider(Decl()));

			File.Delete(good);
			File.Delete(small);
		}

		/// <summary>
		/// What a movie records about firmware: one canonical line per core, ordered, so a
		/// replay reports a difference only when the machine really is different.
		/// </summary>
		[TestMethod]
		public void TheRecordedFirmwareLineIsCanonical()
		{
			var bios = FileOf(8192);
			var chars = FileOf(8192, seed: 7);
			Config config = new();
			CoreFirmwareStore.SetPath(config, "Core", "bios", bios);
			CoreFirmwareStore.SetPath(config, "Core", "charset", chars);

			var biosSha1 = CoreFirmwareStore.Sha1Of(File.ReadAllBytes(bios));
			var charsSha1 = CoreFirmwareStore.Sha1Of(File.ReadAllBytes(chars));

			// declared in one order, recorded in another: the line must not depend on it
			var forwards = Record(config, "Core", Decl(), Decl2("charset"));
			var backwards = Record(config, "Core", Decl2("charset"), Decl());
			Assert.AreEqual($"bios={biosSha1} charset={charsSha1}", forwards);
			Assert.AreEqual(forwards, backwards);

			// another core's files are another core's business
			Assert.AreEqual("", Record(config, "OtherCore", Decl()));

			File.Delete(bios);
			File.Delete(chars);
		}

		/// <summary>Nothing provided means nothing recorded, rather than an empty pair.</summary>
		[TestMethod]
		public void FirmwareThatWasNeverProvidedIsNotRecorded()
			=> Assert.AreEqual("", Record(new Config(), "Core", Decl()));

		private static CoreFirmwareDecl Decl2(string id)
			=> new() { Id = id, Display = id, Size = 8192 };

		/// <summary>Runs the record over a registry that declares exactly these files.</summary>
		private static string Record(Config config, string coreName, params CoreFirmwareDecl[] decls)
			=> string.Join(" ", decls
				.OrderBy(static d => d.Id, StringComparer.Ordinal)
				.Select(d => CoreFirmwareStore.Describe(config, coreName, d))
				.Where(static e => e.Sha1 is not null)
				.Select(static e => $"{e.Decl.Id}={e.Sha1}"));

		/// <summary>Choices are remembered per core and id, so two cores wanting the same-named file do not collide.</summary>
		[TestMethod]
		public void PathsAreKeyedByCoreAndId()
		{
			Config config = new();
			CoreFirmwareStore.SetPath(config, "CoreA", "bios", "/a/bios.rom");
			CoreFirmwareStore.SetPath(config, "CoreB", "bios", "/b/bios.rom");
			Assert.AreEqual("/a/bios.rom", CoreFirmwareStore.GetPath(config, "CoreA", "bios"));
			Assert.AreEqual("/b/bios.rom", CoreFirmwareStore.GetPath(config, "CoreB", "bios"));

			CoreFirmwareStore.SetPath(config, "CoreA", "bios", null);
			Assert.IsNull(CoreFirmwareStore.GetPath(config, "CoreA", "bios"));
			Assert.AreEqual("/b/bios.rom", CoreFirmwareStore.GetPath(config, "CoreB", "bios"));
		}
	}
}
