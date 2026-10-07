using System.Collections.Generic;

using Chimera.Emulation.Common;
using Chimera.Emulation.Common.Waterbox;

namespace Chimera.Tests.Emulation.Common
{
	/// <summary>
	/// A machine's memory is offered to the tools only when it has some (issue
	/// #197). RAM Watch, RAM Search and the hex editor require the memory
	/// service and start from its first domain; a Flash movie describes none, the
	/// service was registered with an empty list, the tools were enabled, and RAM
	/// Search threw "Index was out of range" on opening.
	/// </summary>
	[TestClass]
	public class WaterboxMemoryTests
	{
		[TestMethod]
		public void AMachineWithNoMemoryOffersNoMemoryService()
		{
			BasicServiceProvider services = new(new NullEmulator());
			WaterboxCore.PublishMemory(services, new List<MemoryDomain>());
			Assert.IsFalse(services.HasService<IMemoryDomains>(),
				"an empty list enables the memory tools, which then fail on opening");
		}

		[TestMethod]
		public void AMachineWithMemoryOffersIt()
		{
			BasicServiceProvider services = new(new NullEmulator());
			List<MemoryDomain> domains = [ new MemoryDomainByteArray("RAM", MemoryDomain.Endian.Little, new byte[16], writable: true, wordSize: 1) ];
			WaterboxCore.PublishMemory(services, domains);
			Assert.IsTrue(services.HasService<IMemoryDomains>());
			Assert.AreEqual("RAM", services.GetService<IMemoryDomains>().MainMemory.Name);
		}
	}
}
