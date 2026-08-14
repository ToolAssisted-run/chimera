using System.IO;
using System.Text;

using BizHawk.Client.Common;

namespace BizHawk.Tests.Client.Common.Bundles
{
	/// <summary>
	/// A bundle is a catalogue of files sitting beside it. These check what it will and
	/// will not point at, and that its identity is the identity of its CONTENTS - which is
	/// the only reason a movie can meaningfully cite one.
	/// </summary>
	[TestClass]
	public class GameBundleTests
	{
		private string _dir;

		[TestInitialize]
		public void MakeDir()
		{
			_dir = Path.Combine(Path.GetTempPath(), $"minihawk-bundle-{Path.GetRandomFileName()}");
			Directory.CreateDirectory(_dir);
		}

		[TestCleanup]
		public void RemoveDir()
		{
			try { Directory.Delete(_dir, recursive: true); } catch { /* a leftover temp dir is not a test failure */ }
		}

		private string Write(string name, string content)
		{
			var path = Path.Combine(_dir, name);
			File.WriteAllBytes(path, Encoding.ASCII.GetBytes(content));
			return path;
		}

		private string WriteBundle(string json, string name = "game.bundle")
		{
			var path = Path.Combine(_dir, name);
			File.WriteAllText(path, json);
			return path;
		}

		[TestMethod]
		public void ARomAndAnAttachmentResolveToFilesBesideTheBundle()
		{
			Write("smb3.nes", "rom bytes");
			Write("smb3.sram", "save bytes");
			var path = WriteBundle("""
				{ "bundle": 1, "name": "after world 1",
				  "rom": { "file": "smb3.nes" },
				  "attach": [ { "core": "QuickerNesHawk", "id": "sram", "file": "smb3.sram" } ] }
				""");

			var bundle = GameBundle.Load(path);
			Assert.AreEqual("after world 1", bundle.Name);
			Assert.AreEqual(Path.Combine(_dir, "smb3.nes"), bundle.ResolveFile(bundle.Rom));
			CollectionAssert.AreEqual(Encoding.ASCII.GetBytes("save bytes"), bundle.ReadFile(bundle.Attach[0]));
			Assert.IsNotNull(bundle.FindAttachment("quickerneshawk", "SRAM"), "core and id match case-insensitively");
			Assert.IsNull(bundle.FindAttachment("quickerNES", "sram"), "an attachment belongs to ONE core");
		}

		/// <summary>A bundle that can name any path on the machine is not something you can hand to anyone.</summary>
		[TestMethod]
		public void ABundleMayOnlyNameFilesBesideIt()
		{
			foreach (var bad in new[] { "/etc/passwd", "../outside.nes", "sub/../../outside.nes" })
			{
				var path = WriteBundle($$"""{ "bundle": 1, "rom": { "file": {{System.Text.Json.JsonSerializer.Serialize(bad)}} } }""", $"b{bad.GetHashCode()}.bundle");
				Assert.ThrowsException<InvalidOperationException>(() => GameBundle.Load(path), $"should have refused {bad}");
			}
		}

		[TestMethod]
		public void AFileThatIsNotWhatTheBundleNamesIsRefused()
		{
			var rom = Write("smb3.nes", "rom bytes");
			var sha1 = GameBundle.Sha1Of(File.ReadAllBytes(rom));
			var path = WriteBundle($$"""{ "bundle": 1, "rom": { "file": "smb3.nes", "sha1": "{{sha1}}" } }""");

			var bundle = GameBundle.Load(path);
			CollectionAssert.AreEqual(Encoding.ASCII.GetBytes("rom bytes"), bundle.ReadFile(bundle.Rom));

			Write("smb3.nes", "different bytes");
			var ex = Assert.ThrowsException<InvalidOperationException>(() => bundle.ReadFile(bundle.Rom));
			StringAssert.Contains(ex.Message, "smb3.nes");
		}

		/// <summary>A hand-written bundle pins nothing, and then nothing is checked - but it also has no identity to cite.</summary>
		[TestMethod]
		public void WithoutHashesThereIsNoIdentity()
		{
			Write("smb3.nes", "rom bytes");
			var bundle = GameBundle.Load(WriteBundle("""{ "bundle": 1, "rom": { "file": "smb3.nes" } }"""));
			Assert.IsNull(bundle.ContentId);
			CollectionAssert.AreEqual(Encoding.ASCII.GetBytes("rom bytes"), bundle.ReadFile(bundle.Rom), "an unpinned file is taken as it is");
		}

		/// <summary>
		/// Identity is over the parts, not over the bundle file: renaming it, or writing it out
		/// again with different formatting, must not change what a movie recorded.
		/// </summary>
		[TestMethod]
		public void IdentityIsTheContentsNotTheFile()
		{
			var rom = Write("smb3.nes", "rom bytes");
			var sram = Write("smb3.sram", "save bytes");
			var composed = GameBundle.Compose(Path.Combine(_dir, "a.bundle"), rom, "QuickerNesHawk", "sram", sram, name: "one");
			composed.Save(Path.Combine(_dir, "a.bundle"));

			var reloaded = GameBundle.Load(Path.Combine(_dir, "a.bundle"));
			Assert.AreEqual(composed.ContentId, reloaded.ContentId);

			// same parts, different name and different file name
			var other = GameBundle.Compose(Path.Combine(_dir, "b.bundle"), rom, "QuickerNesHawk", "sram", sram, name: "two");
			Assert.AreEqual(composed.ContentId, other.ContentId);

			// change what the save says, and it is a different bundle
			File.WriteAllBytes(sram, Encoding.ASCII.GetBytes("other save"));
			var changed = GameBundle.Compose(Path.Combine(_dir, "c.bundle"), rom, "QuickerNesHawk", "sram", sram);
			Assert.AreNotEqual(composed.ContentId, changed.ContentId);
		}

		[TestMethod]
		public void WritingAnAttachmentRepinsIt()
		{
			var rom = Write("smb3.nes", "rom bytes");
			var sram = Write("smb3.sram", "save bytes");
			var bundlePath = Path.Combine(_dir, "a.bundle");
			var bundle = GameBundle.Compose(bundlePath, rom, "QuickerNesHawk", "sram", sram);
			var before = bundle.ContentId;

			bundle.WriteAttachment(bundle.Attach[0], Encoding.ASCII.GetBytes("later save"));
			bundle.Save(bundlePath);

			var reloaded = GameBundle.Load(bundlePath);
			CollectionAssert.AreEqual(Encoding.ASCII.GetBytes("later save"), reloaded.ReadFile(reloaded.Attach[0]), "the file was rewritten and still matches its pin");
			Assert.AreNotEqual(before, reloaded.ContentId);
		}

		[TestMethod]
		public void ABundleWithNoRomIsRefused()
		{
			Assert.ThrowsException<InvalidOperationException>(
				() => GameBundle.Load(WriteBundle("""{ "bundle": 1, "attach": [] }""")));
		}

		[TestMethod]
		public void ANewerFormatIsRefusedRatherThanMisread()
		{
			Write("smb3.nes", "rom bytes");
			var ex = Assert.ThrowsException<InvalidOperationException>(
				() => GameBundle.Load(WriteBundle("""{ "bundle": 2, "rom": { "file": "smb3.nes" } }""")));
			StringAssert.Contains(ex.Message, "version 2");
		}

		[TestMethod]
		public void GarbageIsRefusedWithAReadableMessage()
		{
			var ex = Assert.ThrowsException<InvalidOperationException>(
				() => GameBundle.Load(WriteBundle("this is not json at all")));
			StringAssert.Contains(ex.Message, "game.bundle");
		}
	}
}
