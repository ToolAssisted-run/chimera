using System.Linq;

using Chimera.Client.Common;
using Chimera.Emulation.Common;

namespace Chimera.Tests.Client.Common
{
	/// <summary>
	/// A game core's properties in the watch tools (docs/game-cores.md): an element is a
	/// watch under its own name - one of the tools' own when it fits one as it is, and the
	/// engine's otherwise - and a freeze made from one holds its value and keeps its name.
	/// </summary>
	[TestClass]
	public class GamePropertyWatchesTests
	{
		/// <summary>The engine's description of a table (ce_session_property_table).</summary>
		internal const string Table = @"{ ""properties"": [
			{ ""name"": ""Kid.X"", ""domain"": ""Game State"", ""offset"": 0, ""type"": ""u8"", ""size"": 1, ""count"": 1, ""stride"": 1, ""group"": ""Kid"" },
			{ ""name"": ""Guard.HP"", ""domain"": ""Game State"", ""offset"": 2, ""type"": ""s16"", ""size"": 2, ""count"": 1, ""stride"": 2, ""group"": ""Guard"" },
			{ ""name"": ""Score"", ""domain"": ""Game State"", ""offset"": 4, ""type"": ""u32"", ""size"": 4, ""count"": 1, ""stride"": 4, ""endian"": ""big"" },
			{ ""name"": ""Speed"", ""domain"": ""Game State"", ""offset"": 8, ""type"": ""f32"", ""size"": 4, ""count"": 1, ""stride"": 4 },
			{ ""name"": ""Direction"", ""domain"": ""Game State"", ""offset"": 12, ""type"": ""s8"", ""size"": 1, ""count"": 1, ""stride"": 1, ""values"": { ""-1"": ""Left"" } },
			{ ""name"": ""Frames"", ""domain"": ""Game State"", ""offset"": 16, ""type"": ""u64"", ""size"": 8, ""count"": 1, ""stride"": 8 },
			{ ""name"": ""Level Name"", ""domain"": ""Game State"", ""offset"": 24, ""type"": ""string"", ""size"": 8, ""count"": 1, ""stride"": 8, ""encoding"": ""ascii"" },
			{ ""name"": ""Door"", ""domain"": ""Game State"", ""offset"": 32, ""type"": ""u8"", ""size"": 1, ""count"": 1, ""stride"": 1, ""bit"": 3, ""bits"": 1 },
			{ ""name"": ""Guards.X"", ""domain"": ""Game State"", ""offset"": 40, ""type"": ""s16"", ""size"": 2, ""count"": 3, ""stride"": 6, ""group"": ""Guards"" }
		], ""problems"": [] }";

		private static MemoryDomainByteArray Block() => new("Game State", MemoryDomain.Endian.Little, new byte[64], writable: true, wordSize: 1);

		[TestMethod]
		public void AnElementFitsAPlainWatchOrIsTheEngines()
		{
			FakeGameProperties properties = new(Table);
			var block = Block();
			var watches = properties.Properties.SelectMany(static p => p.Elements)
				.Select(e => GamePropertyWatches.WatchOf(properties, e, block)).ToList();
			CollectionAssert.AreEqual(
				new[] { "Kid.X", "Guard.HP", "Score", "Speed", "Direction", "Frames", "Level Name", "Door", "Guards.X[0]", "Guards.X[1]", "Guards.X[2]" },
				watches.Select(static w => w.Notes).ToArray());
			CollectionAssert.AreEqual(
				new[] { WatchSize.Byte, WatchSize.Word, WatchSize.DWord, WatchSize.DWord, WatchSize.Property, WatchSize.Property, WatchSize.Property, WatchSize.Property, WatchSize.Word, WatchSize.Word, WatchSize.Word },
				watches.Select(static w => w.Size).ToArray(),
				"named values, 64 bits, text and a bit field are the engine's to show; the rest fit a plain watch");
			Assert.AreEqual(WatchDisplayType.Signed, watches[1].Type);
			Assert.AreEqual(WatchDisplayType.Float, watches[3].Type);
			Assert.IsTrue(watches[2].BigEndian, "a big-endian property is watched big-endian");
			CollectionAssert.AreEqual(new[] { 40L, 46L, 52L }, watches.Skip(8).Select(static w => w.Address).ToArray(), "each element at its stride");
			Assert.AreEqual(8, watches[6].ByteSize);
		}

		/// <summary>A dynamic table (a Flash movie's variables): the engine's description says so, and of each property whether the last listing had it.</summary>
		internal const string Moving = @"{ ""dynamic"": true, ""properties"": [
			{ ""name"": ""_root.lives"", ""domain"": ""Game State"", ""offset"": 16, ""type"": ""u8"", ""size"": 1, ""count"": 1, ""stride"": 1, ""group"": ""_root"", ""listed"": true },
			{ ""name"": ""_root.hp"", ""domain"": ""Game State"", ""offset"": 24, ""type"": ""f64"", ""size"": 8, ""count"": 1, ""stride"": 8, ""group"": ""_root"", ""listed"": true },
			{ ""name"": ""_root.old"", ""domain"": ""Game State"", ""offset"": 40, ""type"": ""f64"", ""size"": 8, ""count"": 1, ""stride"": 8, ""group"": ""_root"", ""listed"": false }
		], ""problems"": [] }";

		[TestMethod]
		public void APropertyThatMovesIsTheEnginesToWatchWhateverItsSize()
		{
			FakeGameProperties properties = new(Moving);
			Assert.IsTrue(properties.IsDynamic);
			var lives = properties.Find("_root.lives")!;
			Assert.IsFalse(lives.Property.FitsAPlainWatch, "a byte watch at a fixed address would lose it the first time it moved");
			var watch = GamePropertyWatches.WatchOf(properties, lives, Block());
			Assert.IsInstanceOfType(watch, typeof(PropertyWatch));

			properties.Values["_root.lives"] = "3";
			Assert.AreEqual("3", watch.ValueString);
			Assert.AreEqual("10", watch.AddressString, "where the table said it was");
			properties.Addresses["_root.lives"] = 0x2C;
			Assert.AreEqual("2C", watch.AddressString, "and where it is now, once it has moved");
			properties.Addresses["_root.lives"] = -1;
			Assert.AreEqual("-", watch.AddressString);
			Assert.AreEqual(PropertyWatch.Gone, watch.ValueString, "a variable the movie no longer has reads as not there, not as nothing");
			properties.Addresses["_root.lives"] = 0x30;
			Assert.AreEqual("3", watch.ValueString, "and reads again when it is back");

			FakeGameProperties still = new(Table);
			Assert.IsFalse(still.IsDynamic);
			Assert.IsTrue(still.Find("Kid.X")!.Property.FitsAPlainWatch, "a fixed table's byte is still a byte watch");
			Assert.IsFalse(still.Find("Kid.X")!.Property.Dynamic);
			Assert.IsFalse(properties.Find("_root.old")!.Property.Listed, "what the last listing did not have keeps its name and is marked");
		}

		[TestMethod]
		public void AnArrayTheGameCountsFromOneIsNamedAsItCounts()
		{
			FakeGameProperties properties = new(@"{ ""properties"": [
				{ ""name"": ""Rooms.Left"", ""domain"": ""Game State"", ""offset"": 0, ""type"": ""u8"", ""size"": 1, ""count"": 24, ""first"": 1, ""stride"": 1 }
			], ""problems"": [] }");
			var rooms = properties.Properties[0];
			Assert.AreEqual("Rooms.Left[1]", rooms.Element(0).Name);
			Assert.AreEqual("Rooms.Left[24]", rooms.Element(23).Name);
			Assert.AreEqual(23L, rooms.Element(23).Offset, "the element counted from 0 is where it lives");
			var watch = GamePropertyWatches.WatchOf(properties, rooms.Element(4), Block());
			Assert.AreEqual("Rooms.Left[5]", watch.Notes);
		}

		[TestMethod]
		public void APropertyWatchShowsAndPokesWhatTheEngineSays()
		{
			FakeGameProperties properties = new(Table);
			var frames = (PropertyWatch)GamePropertyWatches.WatchOf(properties, properties.Find("Frames")!, Block());
			properties.Values["Frames"] = "18446744073709551615";
			Assert.AreEqual("18446744073709551615", frames.ValueString);
			Assert.IsTrue(frames.Poke("12"));
			Assert.AreEqual(("Frames", "12"), properties.Sets.Single());

			properties.Refusals["Frames"] = "does not fit";
			Assert.IsFalse(frames.Poke("-1"));
			Assert.AreEqual("does not fit", frames.LastPokeError);
		}

		[TestMethod]
		public void AWatchOnAnElementTakesItsNameUnlessItHasOne()
		{
			FakeGameProperties properties = new(Table);
			var block = Block();
			Assert.AreEqual("Guards.X[1]", GamePropertyWatches.Named(Watch.GenerateWatch(block, 46, WatchSize.Word, WatchDisplayType.Hex, false), properties).Notes);
			Assert.AreEqual("mine", GamePropertyWatches.Named(Watch.GenerateWatch(block, 46, WatchSize.Word, WatchDisplayType.Hex, false, note: "mine"), properties).Notes);
			Assert.AreEqual("", GamePropertyWatches.Named(Watch.GenerateWatch(block, 47, WatchSize.Byte, WatchDisplayType.Hex, false), properties).Notes,
				"the middle of an element is not the element");
			Assert.AreEqual("", GamePropertyWatches.Named(Watch.GenerateWatch(block, 44, WatchSize.Byte, WatchDisplayType.Hex, false), properties).Notes,
				"a byte between an array's elements is none of them");
			Assert.AreEqual("", GamePropertyWatches.Named(Watch.GenerateWatch(block, 0, WatchSize.Byte, WatchDisplayType.Hex, false), null).Notes);
		}

		[TestMethod]
		public void AFrozenPropertyHoldsItsValueAndKeepsItsName()
		{
			FakeGameProperties properties = new(Table);
			var block = Block();

			// one the engine holds: the freeze keeps its value as the engine's text
			properties.Values["Level Name"] = "Dungeon";
			var name = GamePropertyWatches.WatchOf(properties, properties.Find("Level Name")!, block);
			Cheat nameFreeze = new(name, name.Value);
			Assert.AreEqual("Level Name", nameFreeze.Name, "the freeze is listed by the property's name");
			Assert.AreEqual("Dungeon", nameFreeze.ValueStr);
			properties.Values["Level Name"] = "Palace";
			nameFreeze.Pulse();
			Assert.AreEqual("Dungeon", properties.Values["Level Name"], "and pulsed before the next step, puts it back");
			Assert.IsTrue(nameFreeze.Contains(24) && nameFreeze.Contains(31) && !nameFreeze.Contains(32), "and covers every byte of it");

			// one that fits a plain watch is frozen as any address is
			block.Data[2] = 0xFD;
			block.Data[3] = 0xFF;
			var hp = GamePropertyWatches.WatchOf(properties, properties.Find("Guard.HP")!, block);
			Cheat hpFreeze = new(hp, hp.Value);
			Assert.AreEqual("Guard.HP", hpFreeze.Name);
			block.Data[2] = 40;
			block.Data[3] = 0;
			hpFreeze.Pulse();
			Assert.AreEqual(0xFD, block.Data[2]);
			Assert.AreEqual(0xFF, block.Data[3]);

			// a copy of a property's freeze holds the same value
			Cheat copy = new(nameFreeze);
			Assert.AreEqual("Dungeon", copy.ValueStr);
			Assert.AreEqual("Level Name", copy.Name);
		}

		[TestMethod]
		public void AFrozenPropertyIsFoundAgainByNameOrDropped()
		{
			FakeGameProperties before = new(Table);
			var frames = GamePropertyWatches.WatchOf(before, before.Find("Frames")!, Block());
			CheatCollection cheats = new(dialogParent: null!);
			cheats.Add(new Cheat(frames, frames.Value));

			var block = Block();
			MemoryDomainList domains = new(new[] { (MemoryDomain)block });
			FakeGameProperties after = new(Table);
			after.Values["Frames"] = "7";
			cheats.UpdateDomains(domains, after);
			Assert.AreEqual(1, cheats.Count);
			cheats.Pulse();
			Assert.AreEqual(("Frames", "0"), after.Sets.Single(), "the freeze now writes to the core that is running");

			cheats.UpdateDomains(domains, new FakeGameProperties(@"{ ""properties"": [], ""problems"": [] }"));
			Assert.AreEqual(0, cheats.Count, "a core without the property has nothing to freeze");
		}
	}
}
