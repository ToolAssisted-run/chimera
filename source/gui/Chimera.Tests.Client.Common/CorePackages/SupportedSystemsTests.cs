using System.Linq;

using Chimera.Client.Common;

namespace Chimera.Tests.Client.Common.CorePackages
{
	/// <summary>The systems the cores run, once each, with their cores (#172).</summary>
	[TestClass]
	public class SupportedSystemsTests
	{
		/// <summary>
		/// What each core says it runs, in its own words: the names are the
		/// cores', as a roster row or an installed package carries them. Nothing
		/// in the frontend knows what an "NES" is.
		/// </summary>
		private static readonly (string, System.Collections.Generic.IReadOnlyList<(string Id, string Name)>, bool)[] Cores =
		[
			("ares", [ ("PS1", "PlayStation"), ("GB", "Game Boy"), ("NES", "Nintendo Entertainment System") ], false),
			("quickerNES", [ ("NES", "Nintendo Entertainment System") ], true),
			("Genesis Plus GX", [ ("GEN", "Mega Drive / Genesis"), ("SMS", "Master System") ], true),
			("Dolphin", [ ("GC", "GameCube"), ("Wii", "Wii") ], false),
		];

		/// <summary>
		/// Two cores may run one system and call it differently, and a core may
		/// give no name at all. The first that gives one is heard, in the order
		/// the cores are listed, so the list is the same whoever has what
		/// installed.
		/// </summary>
		[TestMethod]
		public void ASystemIsCalledWhatTheFirstCoreToNameItCallsIt()
		{
			var systems = SupportedSystems.From(new (string, System.Collections.Generic.IReadOnlyList<(string Id, string Name)>, bool)[]
			{
				("old package", [ ("NES", "NES") ], true),              // names nothing: its id comes back
				("first", [ ("NES", "Famicom / NES") ], false),
				("second", [ ("NES", "Nintendo Entertainment System") ], true),
				("anonymous", [ ("XYZ9", "XYZ9") ], true),
			});
			Assert.AreEqual("Famicom / NES", systems.Single(e => e.Id == "NES").Name);
			CollectionAssert.AreEqual(new[] { "first", "old package", "second" }, systems.Single(e => e.Id == "NES").Cores.ToArray());
			// a system nobody named still reads, as its id
			Assert.AreEqual("XYZ9", systems.Single(e => e.Id == "XYZ9").Name);
		}

		[TestMethod]
		public void EachSystemOnceWithEveryCoreThatRunsIt()
		{
			var systems = SupportedSystems.From(Cores);
			var nes = systems.Single(e => e.Id == "NES");
			Assert.AreEqual("Nintendo Entertainment System", nes.Name);
			CollectionAssert.AreEqual(new[] { "ares", "quickerNES" }, nes.Cores.ToArray());
			Assert.IsTrue(nes.Installed, "one of its cores is installed");
			Assert.IsFalse(systems.Single(e => e.Id == "PS1").Installed, "and none of this one's is");
			Assert.AreEqual(7, systems.Count, "PS1 GB NES GEN SMS GC Wii: NES once");
		}

		[TestMethod]
		public void SortedByTheNameAPersonReads()
		{
			var names = SupportedSystems.From(Cores).Select(static e => e.Name).ToList();
			CollectionAssert.AreEqual(names.OrderBy(static n => n, System.StringComparer.OrdinalIgnoreCase).ToList(), names);
		}

		[TestMethod]
		public void TheFilterReadsNamesIdsAndCores()
		{
			var systems = SupportedSystems.From(Cores);
			string[] Shown(string f) => systems.Where(e => SupportedSystems.Matches(e, f)).Select(static e => e.Id).ToArray();
			CollectionAssert.AreEqual(new[] { "PS1" }, Shown("playstation"), "by name, any case");
			CollectionAssert.AreEquivalent(new[] { "GB", "NES", "PS1" }, Shown("ares"), "by core");
			CollectionAssert.AreEquivalent(new[] { "SMS" }, Shown("SMS"), "by id");
			Assert.AreEqual(systems.Count, Shown("  ").Length, "an empty filter shows everything");
		}
	}
}
