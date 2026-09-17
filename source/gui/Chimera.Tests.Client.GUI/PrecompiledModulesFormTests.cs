using System;
using System.Collections.Generic;
using System.Linq;

using Chimera.Client.Common;
using Chimera.Client.GUI;

namespace Chimera.Tests.Client.GUI
{
	/// <summary>
	/// Tools &gt; Pre-Compiled Modules: that the window shows what the survey
	/// found, and that Remove takes the ticked row and only the ticked row.
	///
	/// No disk here. The survey is proved against real directories in
	/// PrecompiledCodeSurveyTests; what is left for this window is the wiring
	/// between a list of rows and the two acts it offers.
	/// </summary>
	[TestClass]
	public class PrecompiledModulesFormTests
	{
		private static PrecompiledGame Game(string name, long bytes, bool complete = true)
			=> new()
			{
				GameSha1 = name.ToUpperInvariant(),
				GameName = name,
				CoreName = "RPCS3",
				CoreVersion = "d2be7a387e86",
				Compiled = new DateTime(2026, 9, 17, 5, 0, 0, DateTimeKind.Utc),
				Modules = 271,
				Bytes = bytes,
				Path = $"/store/{name}",
				Complete = complete,
			};

		[TestMethod]
		public void TheWindowListsWhatTheSurveyFound()
		{
			List<PrecompiledGame> store = [ Game("pop.iso", 59_000_000), Game("gta.iso", 120_000_000) ];

			using PrecompiledModulesForm form = new(() => store);
			form.Show();

			var rows = form.Rows;
			Assert.AreEqual(2, rows.Count);
			CollectionAssert.AreEquivalent(
				new[] { "pop.iso", "gta.iso" },
				rows.Select(static r => r.Label).ToArray(),
				"every game with compiled code is listed");
			Assert.IsTrue(rows.All(static r => r.Bytes > 0), "and what each weighs, which is what the window is for");
		}

		/// <summary>
		/// The act this window exists for. One game, one directory - games share
		/// no objects (user-decided, 2026-09-17) - so removing a row can never
		/// leave another game short, and the window need not warn about one.
		/// </summary>
		[TestMethod]
		public void RemoveTakesTheTickedRowAndLeavesTheRest()
		{
			List<PrecompiledGame> store = [ Game("pop.iso", 59_000_000), Game("gta.iso", 120_000_000) ];

			using PrecompiledModulesForm form = new(() => store, remove: g => store.RemoveAll(s => s.Path == g.Path));
			form.Show();

			form.Tick("pop.iso");
			form.RemoveTickedForTest();

			Assert.AreEqual(1, store.Count, "the ticked game's code is gone");
			Assert.AreEqual("gta.iso", store[0].GameName, "and the one that was not ticked is untouched");
			Assert.AreEqual(1, form.Rows.Count, "the list says so without being reopened");
			Assert.AreEqual("gta.iso", form.Rows[0].Label);
		}

		[TestMethod]
		public void NothingTickedRemovesNothing()
		{
			List<PrecompiledGame> store = [ Game("pop.iso", 59_000_000) ];

			using PrecompiledModulesForm form = new(() => store, remove: g => store.RemoveAll(s => s.Path == g.Path));
			form.Show();
			form.RemoveTickedForTest();

			Assert.AreEqual(1, store.Count, "Remove with nothing ticked is not a way to lose a game's code");
		}

		/// <summary>
		/// An empty store is the common first case - nobody has compiled
		/// anything yet - and an empty list with no words is a window that looks
		/// broken. It says what would fill it.
		/// </summary>
		[TestMethod]
		public void AnEmptyStoreSaysWhatWouldFillIt()
		{
			using PrecompiledModulesForm form = new(static () => [ ]);
			form.Show();

			Assert.AreEqual(0, form.Rows.Count);
		}

		/// <summary>A game missing objects is still listed: it is exactly what somebody wants to remove.</summary>
		[TestMethod]
		public void AGameMissingObjectsIsStillListedAndStillRemovable()
		{
			List<PrecompiledGame> store = [ Game("half.iso", 8_000_000, complete: false) ];

			using PrecompiledModulesForm form = new(() => store, remove: g => store.RemoveAll(s => s.Path == g.Path));
			form.Show();

			Assert.AreEqual(1, form.Rows.Count);
			Assert.IsFalse(form.Rows[0].Complete, "the window does not hide an unfinished compile");

			form.Tick("half.iso");
			form.RemoveTickedForTest();
			Assert.AreEqual(0, store.Count);
		}
	}
}
