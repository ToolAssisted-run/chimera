using System.IO;
using System.Linq;

using Chimera.Client.Common;
using Chimera.Emulation.Common;

namespace Chimera.Tests.Client.Common
{
	/// <summary>
	/// A watch's On Screen box (RAM Watch's first column): off for a new watch, and kept in the
	/// list's file as one line after the watches, which names them by their places in the file
	/// and which a build without the box skips.
	/// </summary>
	[TestClass]
	public class WatchOnScreenTests
	{
		private static MemoryDomainByteArray Block() => new("Game State", MemoryDomain.Endian.Little, new byte[64], writable: true, wordSize: 1);

		private static string TempFile() => Path.Combine(Path.GetTempPath(), $"chimera-watches-{Path.GetRandomFileName()}.wch");

		private static WatchList List(MemoryDomainByteArray block, IGameProperties properties)
			=> new(new MemoryDomainList(new[] { (MemoryDomain)block }), "GAME") { GameProperties = properties };

		private static Watch Byte(MemoryDomain block, long address, string note)
			=> Watch.GenerateWatch(block, address, WatchSize.Byte, WatchDisplayType.Unsigned, bigEndian: false, note);

		[TestMethod]
		public void TheTicksAreSavedAfterTheWatchesAndReadBack()
		{
			var block = Block();
			FakeGameProperties properties = new(GamePropertyWatchesTests.Table);
			var list = List(block, properties);
			list.Add(Byte(block, 0, "Kid"));
			list.Add(SeparatorWatch.Instance);
			list.Add(Byte(block, 2, "Guard"));
			list.Add(GamePropertyWatches.WatchOf(properties, properties.Find("Frames")!, block));
			Assert.IsFalse(list.Any(static w => w.OnScreen), "a new watch is not on the screen");
			list[0].OnScreen = list[3].OnScreen = true;

			var path = TempFile();
			try
			{
				Assert.IsFalse(list.SaveAs(new FileInfo(path)).IsError);
				var lines = File.ReadAllLines(path).Where(static l => l.Length is not 0).ToArray();
				Assert.AreEqual("OnScreen\t0,3", lines[^1], "the places of the ticked ones, last");
				Assert.AreEqual(4, lines.Count(static l => l.Count(static c => c == '\t') == 5), "every watch's line is as it was");
				Assert.AreNotEqual(5, lines[^1].Count(static c => c == '\t'), "a build without the box skips the line");

				var back = List(block, properties);
				Assert.IsTrue(back.Load(path, append: false));
				Assert.AreEqual(4, back.Count, "the line is not a watch");
				CollectionAssert.AreEqual(new[] { true, false, false, true }, back.Select(static w => w.OnScreen).ToArray());
				CollectionAssert.AreEqual(new[] { "Kid", "Frames" }, back.OnScreenWatches.Select(static w => w.Notes).ToArray(), "the screen shows the ticked ones, in order");

				// a list saved with none ticked writes no line, as before the box
				back[0].OnScreen = back[3].OnScreen = false;
				Assert.IsFalse(back.Save().IsError);
				Assert.IsFalse(File.ReadAllLines(path).Any(static l => l.StartsWith("OnScreen")));
			}
			finally
			{
				File.Delete(path);
			}
		}

		[TestMethod]
		public void AFileWithoutTheLineHasNoneTicked()
		{
			var block = Block();
			var path = TempFile();
			try
			{
				File.WriteAllLines(path, [ "SystemID GAME", "0000\tb\tu\t0\tGame State\tKid", "0002\tb\tu\t0\tGame State\tGuard" ]);
				var list = List(block, properties: null);
				Assert.IsTrue(list.Load(path, append: false));
				Assert.AreEqual(2, list.Count);
				Assert.IsFalse(list.Any(static w => w.OnScreen));
			}
			finally
			{
				File.Delete(path);
			}
		}

		[TestMethod]
		public void AWatchThatIsNotFoundAgainMovesNoOtherTick()
		{
			var block = Block();
			var path = TempFile();
			try
			{
				// the property is gone from the core now running: its line gives no watch, and the
				// tick on the file's third line is still Guard's
				File.WriteAllLines(path,
				[
					"SystemID GAME",
					"0000\tp\tu\t0\tGame State\tGone",
					"0000\tb\tu\t0\tGame State\tKid",
					"0002\tb\tu\t0\tGame State\tGuard",
					"OnScreen\t2",
				]);
				var list = List(block, new FakeGameProperties(GamePropertyWatchesTests.Table));
				Assert.IsTrue(list.Load(path, append: false));
				CollectionAssert.AreEqual(new[] { "Kid", "Guard" }, list.Select(static w => w.Notes).ToArray());
				CollectionAssert.AreEqual(new[] { false, true }, list.Select(static w => w.OnScreen).ToArray());
			}
			finally
			{
				File.Delete(path);
			}
		}

		[TestMethod]
		public void TheListSortsByTheBox()
		{
			var block = Block();
			var list = List(block, properties: null);
			list.Add(Byte(block, 0, "Kid"));
			list.Add(Byte(block, 1, "Guard"));
			list.Add(Byte(block, 2, "Level"));
			list[1].OnScreen = true;
			list.OrderWatches(WatchList.OnScreen, reverse: false);
			CollectionAssert.AreEqual(new[] { "Guard", "Kid", "Level" }, list.Select(static w => w.Notes).ToArray(), "the ticked ones first");
		}
	}
}
