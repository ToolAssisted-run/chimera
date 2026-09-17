using System.Linq;
using System.Runtime.InteropServices;

using Chimera.Client.Common;
using Chimera.Client.Common.RamSearchEngine;
using Chimera.Emulation.Common;

namespace Chimera.Tests.Client.Common.tools
{
	/// <summary>
	/// RAM Search through the real engine. The search's own semantics are held
	/// to an oracle engine-side (test_ram_search.cpp); what is pinned here is the
	/// frontend's half: both ways a domain's memory reaches the engine, the enum
	/// translation, and that a domain past the old 64 MiB refusal is searched
	/// (issue #89).
	/// </summary>
	[TestClass]
	public class RamSearchEngineTests
	{
		private static RamSearchEngine Make(MemoryDomain domain, WatchSize size = WatchSize.Byte, SearchMode mode = SearchMode.Fast)
		{
			var domains = new MemoryDomainList(new[] { domain });
			var settings = new SearchEngineSettings(domains, useUndoHistory: true)
			{
				Domain = domain,
				Size = size,
				Mode = mode,
				BigEndian = false,
			};
			var engine = new RamSearchEngine(settings, domains);
			engine.Start();
			return engine;
		}

		[TestMethod]
		public void ADomainPastTheOldLimitIsSearchedThroughItsPointer()
		{
			const long size = 80L * 1024 * 1024;
			var memory = Marshal.AllocHGlobal((IntPtr) size);
			try
			{
				var zeros = new byte[1024 * 1024];
				for (long at = 0; at < size; at += zeros.Length) Marshal.Copy(zeros, 0, (IntPtr) ((long) memory + at), zeros.Length);
				var domain = new MemoryDomainIntPtr("Physical RAM", MemoryDomain.Endian.Little, memory, size, writable: true, wordSize: 1);
				using var search = Make(domain);
				Assert.AreEqual(size, search.Count);

				domain.PokeByte(5, 1);
				domain.PokeByte(70L * 1024 * 1024, 2);
				domain.PokeByte(size - 1, 3);
				search.Operator = ComparisonOperator.NotEqual;
				search.CompareTo = Compare.Previous;
				Assert.AreEqual(size - 3, search.DoSearch());
				Assert.AreEqual(3, search.Count);
				Assert.AreEqual(70L * 1024 * 1024, search[1].Address);
				Assert.AreEqual(2u, search[1].Previous, "previous moves to the value found, after the search");
				Assert.AreEqual(2, search.IndexOf(size - 1));

				Assert.IsTrue(search.CanUndo, "the first search can be taken back");
				Assert.AreEqual(size - 3, search.Undo());
				Assert.AreEqual(size, search.Count);
				Assert.AreEqual(size - 1, search.AddressAt(size - 1));
			}
			finally
			{
				Marshal.FreeHGlobal(memory);
			}
		}

		[TestMethod]
		public void ADomainWithNoPointerIsReadThroughTheFrontend()
		{
			var bytes = new byte[3 * 1024 * 1024 + 1];
			BitConverter.GetBytes(-40).CopyTo(bytes, 8);
			BitConverter.GetBytes(1000).CopyTo(bytes, 2 * 1024 * 1024);
			var domain = new MemoryDomainByteArray("Array", MemoryDomain.Endian.Little, bytes, writable: true, wordSize: 1);
			using var search = Make(domain, WatchSize.DWord);
			Assert.AreEqual(bytes.Length / 4, search.Count);

			search.SetType(WatchDisplayType.Signed);
			search.CompareTo = Compare.SpecificValue;
			search.Operator = ComparisonOperator.LessThan;
			search.CompareValue = 0;
			search.DoSearch();
			Assert.AreEqual(1, search.Count);
			Assert.AreEqual(8, search[0].Address);

			search.Undo();
			search.Operator = ComparisonOperator.NotEqual;
			search.DoSearch();
			CollectionAssert.AreEqual(new long[] { 8, 2 * 1024 * 1024 }, new[] { search[0].Address, search[1].Address });
			search.Sort(WatchList.Value, reverse: true);
			Assert.AreEqual(2 * 1024 * 1024, search[0].Address);

			search.ConvertTo(WatchSize.Word);
			CollectionAssert.AreEqual(
				new long[] { 2 * 1024 * 1024, 2 * 1024 * 1024 + 2, 8, 10 },
				Enumerable.Range(0, (int) search.Count).Select(i => search[i].Address).ToArray());
		}

		[TestMethod]
		public void DetailedModeCountsChanges()
		{
			var bytes = new byte[4096];
			var domain = new MemoryDomainByteArray("Array", MemoryDomain.Endian.Little, bytes, writable: true, wordSize: 1);
			using var search = Make(domain, WatchSize.Byte, SearchMode.Detailed);
			for (byte frame = 1; frame <= 3; frame++)
			{
				bytes[100] = frame;
				if (frame is 2) bytes[200] = 9;
				search.Update();
			}

			search.CompareTo = Compare.Changes;
			search.Operator = ComparisonOperator.GreaterThanEqual;
			search.CompareValue = 1;
			search.DoSearch();
			Assert.AreEqual(2, search.Count);
			Assert.AreEqual(3, search[0].ChangeCount);
			Assert.AreEqual(1, search[1].ChangeCount);
		}

		[TestMethod]
		public void ASearchThatLacksItsValueIsRefusedBeforeItRuns()
		{
			var domain = new MemoryDomainByteArray("Array", MemoryDomain.Endian.Little, new byte[64], writable: true, wordSize: 1);
			using var search = Make(domain);
			search.CompareTo = Compare.SpecificValue;
			search.CompareValue = null;
			_ = Assert.Throws<InvalidOperationException>(() => search.DoSearch());
			_ = Assert.Throws<InvalidOperationException>(() => search.CompareTo = Compare.Changes);
			Assert.AreEqual(64, search.Count);
		}
	}
}
