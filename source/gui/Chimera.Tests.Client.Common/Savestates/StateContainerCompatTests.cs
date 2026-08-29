using System.IO;
using System.IO.Compression;
using System.Linq;
using System.Text;

using Chimera.Client.Common;
using Chimera.Common;

namespace Chimera.Tests.Client.Common.Savestates
{
	/// <summary>
	/// The container crossed implementations: files written by the OLD machinery
	/// (System.IO.Compression + streaming zstd) must read through the engine,
	/// and engine-written files must read with stock .NET + zstd - otherwise
	/// existing savestates and movies die with this migration.
	/// </summary>
	[TestClass]
	public class StateContainerCompatTests
	{
		private static string TempFile() => Path.Combine(Path.GetTempPath(), $"chimera-state-{Path.GetRandomFileName()}");

		private static byte[] SomeCoreState()
		{
			var state = new byte[100_000];
			for (var i = 0; i < state.Length; i++) state[i] = (byte)(i * 31);
			return state;
		}

		/// <summary>Writes a file exactly as the pre-engine ZipStateSaver did, tarbomb layout.</summary>
		private static void WriteLegacyArchive(string path, byte[] coreState, string topDir = "")
		{
			using var fs = new FileStream(path, FileMode.Create, FileAccess.Write);
			using var zip = new ZipArchive(fs, ZipArchiveMode.Create);
			using Zstd zstd = new();

			void PutText(string name, string text)
			{
				using var s = zip.CreateEntry(topDir + name, CompressionLevel.Optimal).Open();
				var bytes = Encoding.UTF8.GetBytes(text);
				s.Write(bytes, 0, bytes.Length);
			}
			PutText("ChimeraState 1.0", "3\n");
			PutText("ChimeraVersion.txt", "2.10 (legacy)\n");
			PutText("Header.txt", "MovieVersion Chimera Tasproj v1.1\n\n");

			// zstd lump the old way: stored entry, STREAMING compression (frames
			// with no content size in the header - the engine must cope)
			using (var s = zip.CreateEntry($"{topDir}Core.bin.zst", CompressionLevel.NoCompression).Open())
			using (var z = zstd.CreateZstdCompressionStream(s, 11))
			{
				z.Write(coreState, 0, coreState.Length);
			}
		}

		[TestMethod]
		public void ALegacyArchiveReadsThroughTheEngine()
		{
			var path = TempFile();
			var coreState = SomeCoreState();
			try
			{
				WriteLegacyArchive(path, coreState);
				using var loader = ZipStateLoader.LoadAndDetect(path);
				Assert.IsNotNull(loader);
				Assert.AreEqual(3, loader.Version);
				byte[] read = null;
				Assert.IsTrue(loader.GetLump(BinaryStateLump.Corestate, abort: false, br => read = br.ReadBytes(coreState.Length + 1)));
				CollectionAssert.AreEqual(coreState, read, "streaming-compressed zstd lump survived");
				var header = "";
				loader.GetLump(BinaryStateLump.Movieheader, abort: true, (TextReader tr) => header = tr.ReadToEnd());
				StringAssert.Contains(header, "MovieVersion");
			}
			finally
			{
				File.Delete(path);
			}
		}

		[TestMethod]
		public void TheOldTopLevelDirLayoutStillReads()
		{
			var path = TempFile();
			var coreState = SomeCoreState();
			try
			{
				WriteLegacyArchive(path, coreState, topDir: "ChimeraState/");
				using var loader = ZipStateLoader.LoadAndDetect(path);
				Assert.IsNotNull(loader, "pre-tarbomb layout must keep loading");
				Assert.AreEqual(3, loader.Version);
				Assert.IsTrue(loader.GetLump(BinaryStateLump.Corestate, abort: false, (BinaryReader br) => { }));
			}
			finally
			{
				File.Delete(path);
			}
		}

		[TestMethod]
		public void AnEngineArchiveReadsWithStockDotNet()
		{
			var path = TempFile();
			var coreState = SomeCoreState();
			try
			{
				var create = ZipStateSaver.Create(path, compressionLevel: 5);
				Assert.IsFalse(create.IsError);
				var saver = create.Value!;
				saver.PutLump(BinaryStateLump.Movieheader, (TextWriter tw) => tw.Write("MovieVersion Chimera Tasproj v1.1\n\n"));
				saver.PutLump(BinaryStateLump.Corestate, (BinaryWriter bw) => bw.Write(coreState));
				Assert.IsFalse(saver.CloseAndDispose().IsError);

				using var zip = new ZipArchive(new FileStream(path, FileMode.Open, FileAccess.Read), ZipArchiveMode.Read);
				var names = zip.Entries.Select(static e => e.FullName).ToList();
				CollectionAssert.Contains(names, "ChimeraState 1.0", $"actual entries: {string.Join("|", names)}");
				CollectionAssert.Contains(names, "Header.txt");
				CollectionAssert.Contains(names, "Core.bin.zst");

				using var zstd = new Zstd();
				using var entry = zip.Entries.Single(static e => e.FullName == "Core.bin.zst").Open();
				using var z = zstd.CreateZstdDecompressionStream(entry);
				using MemoryStream decompressed = new();
				z.CopyTo(decompressed);
				CollectionAssert.AreEqual(coreState, decompressed.ToArray(), "old builds can decompress what the engine wrote");
			}
			finally
			{
				File.Delete(path);
			}
		}

		[TestMethod]
		public void AFullSaveLoadRoundTripThroughTheEngine()
		{
			var path = TempFile();
			var coreState = SomeCoreState();
			try
			{
				var saver = ZipStateSaver.Create(path, compressionLevel: 2).Value!;
				saver.PutLump(BinaryStateLump.Corestate, (BinaryWriter bw) => bw.Write(coreState));
				saver.PutLump(BinaryStateLump.UserData, (TextWriter tw) => tw.Write("some user data"));
				Assert.IsFalse(saver.CloseAndDispose().IsError);

				using var loader = ZipStateLoader.LoadAndDetect(path);
				Assert.IsNotNull(loader);
				byte[] read = null;
				loader.GetCoreState(br => read = br.ReadBytes(coreState.Length + 1), tr => { });
				CollectionAssert.AreEqual(coreState, read);
			}
			finally
			{
				File.Delete(path);
			}
		}

		[TestMethod]
		public void GarbageIsQuietlyRefused()
		{
			var path = TempFile();
			try
			{
				File.WriteAllText(path, "this is no zip");
				Assert.IsNull(ZipStateLoader.LoadAndDetect(path));
			}
			finally
			{
				File.Delete(path);
			}
		}
	}
}
