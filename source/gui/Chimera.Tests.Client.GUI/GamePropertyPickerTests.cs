using System.Linq;

using Chimera.Client.GUI;
using Chimera.Tests.Client.Common;

namespace Chimera.Tests.Client.GUI
{
	/// <summary>
	/// RAM Watch's Add Game Properties (docs/game-cores.md): the core's properties by
	/// name under their groups, an array element by element, and only the ones not yet
	/// watched can be ticked.
	/// </summary>
	[TestClass]
	public class GamePropertyPickerTests
	{
		private const string Table = @"{ ""properties"": [
			{ ""name"": ""Kid.X"", ""domain"": ""Game State"", ""offset"": 0, ""type"": ""u8"", ""size"": 1, ""count"": 1, ""stride"": 1, ""group"": ""Kid"" },
			{ ""name"": ""Kid.Y"", ""domain"": ""Game State"", ""offset"": 1, ""type"": ""u8"", ""size"": 1, ""count"": 1, ""stride"": 1, ""group"": ""Kid"" },
			{ ""name"": ""Guards.HP"", ""domain"": ""Game State"", ""offset"": 2, ""type"": ""s8"", ""size"": 1, ""count"": 2, ""stride"": 4, ""group"": ""Guards"" },
			{ ""name"": ""Level Name"", ""domain"": ""Game State"", ""offset"": 16, ""type"": ""string"", ""size"": 8, ""count"": 1, ""stride"": 8 }
		], ""problems"": [] }";

		[TestMethod]
		public void PropertiesSitUnderTheirGroupsElementByElementAndOnlyNewOnesTick()
		{
			FakeGameProperties properties = new(Table);
			using GamePropertyPicker picker = new(properties, static e => e.Name, static e => e.Name is "Kid.Y");
			picker.Show();
			CollectionAssert.AreEqual(
				new[] { "Kid", "Kid.X", "Kid.Y", "Guards", "Guards.HP[0]", "Guards.HP[1]", "Level Name" },
				picker.Rows.ToArray());

			picker.Tick("Kid");     // a heading
			picker.Tick("Kid.Y");   // already watched
			picker.Tick("Guards.HP[1]");
			picker.Tick("Level Name");
			CollectionAssert.AreEqual(new[] { "Guards.HP[1]", "Level Name" }, picker.Chosen.Select(static e => e.Name).ToArray());
		}

		private static string Moving(params (string Name, int Offset, bool Listed)[] entries)
			=> @"{ ""dynamic"": true, ""properties"": ["
				+ string.Join(",", entries.Select(static e =>
					$@"{{ ""name"": ""{e.Name}"", ""domain"": ""Heap"", ""offset"": {e.Offset}, ""type"": ""f64"", ""size"": 8, ""count"": 1, ""stride"": 8, ""group"": ""{e.Name.Substring(0, e.Name.LastIndexOf('.'))}"", ""listed"": {(e.Listed ? "true" : "false")} }}"))
				+ @"], ""problems"": [] }";

		[TestMethod]
		public void AMoviesVariablesAreReadWhenTheWindowOpensAndAgainOnRefresh()
		{
			FakeGameProperties properties = new(Moving(("_root.old", 8, true)))
			{
				NextTable = Moving(("_root.v", 0x20A9AD8, true), ("_root.hero.hp", 64, true), ("_root.old", 8, false)),
			};
			using GamePropertyPicker picker = new(properties, static _ => "7", static _ => false);
			picker.Show();
			Assert.AreEqual(1, properties.Refreshes, "the list is the movie as it is now, not as it was when the core was loaded");
			CollectionAssert.AreEqual(new[] { "_root", "_root.v", "_root.hero", "_root.hero.hp" }, picker.Rows.ToArray(), "what the listing no longer has is not offered");
			CollectionAssert.AreEqual(new[] { "_root.v", "f64", "20A9AD8", "7", "" }, picker.Cells("_root.v").ToArray(), "with its type, where it is, and what it reads");

			picker.Tick("_root.v");
			properties.NextTable = Moving(("_root.v", 0x30, true), ("_root.hero.hp", 64, false), ("_root.boss.hp", 96, true));
			picker.PressRefresh();
			Assert.AreEqual(2, properties.Refreshes);
			CollectionAssert.AreEqual(new[] { "_root", "_root.v", "_root.boss", "_root.boss.hp" }, picker.Rows.ToArray());
			CollectionAssert.AreEqual(new[] { "_root.v" }, picker.Chosen.Select(static e => e.Name).ToArray(), "a tick stays on what is still there");
			Assert.AreEqual("30", picker.Cells("_root.v")[2], "at the place it has moved to");
		}

		[TestMethod]
		public void TheBoxAtTheTopNarrowsTheListAndKeepsTheTicks()
		{
			FakeGameProperties properties = new(Table);
			using GamePropertyPicker picker = new(properties, static e => e.Name, static _ => false);
			picker.Show();
			Assert.AreEqual(0, properties.Refreshes, "a fixed table is not asked for again");
			picker.Tick("Kid.X");
			picker.Narrow("guards");
			CollectionAssert.AreEqual(new[] { "Guards", "Guards.HP[0]", "Guards.HP[1]" }, picker.Rows.ToArray(), "any case, anywhere in the name; a group with nothing left is not listed");
			picker.Tick("Guards.HP[1]");
			picker.Narrow("no such thing");
			Assert.AreEqual(0, picker.Rows.Count);
			CollectionAssert.AreEqual(new[] { "Kid.X", "Guards.HP[1]" }, picker.Chosen.Select(static e => e.Name).ToArray(), "what was ticked is still chosen when it is not in view");
			picker.Narrow("");
			Assert.AreEqual(7, picker.Rows.Count);
		}

		[TestMethod]
		public void ALongListStopsAndSaysHowManyMoreThereAre()
		{
			var many = Enumerable.Range(0, GamePropertyPicker.MaxRows + 250).Select(static i => ($"_root.n{i}", i * 8, true)).ToArray();
			FakeGameProperties properties = new(Moving(many));
			using GamePropertyPicker picker = new(properties, static _ => "", static _ => false);
			picker.Show();
			Assert.AreEqual(GamePropertyPicker.MaxRows + 1, picker.Rows.Count, "the heading and the rows that fit, and one line for the rest");
			Assert.AreEqual("... and 251 more: type part of a name above", picker.Rows[picker.Rows.Count - 1]);
			picker.Narrow($"n{GamePropertyPicker.MaxRows + 249}");
			CollectionAssert.AreEqual(new[] { "_root", $"_root.n{GamePropertyPicker.MaxRows + 249}" }, picker.Rows.ToArray(), "and the box finds one that was past the end");
		}
	}
}
