using Chimera.Emulation.Common;

namespace Chimera.Client.Common
{
	public interface IMemoryEventsApi : IExternalApi
	{
		void AddReadCallback(MemoryCallbackDelegate cb, uint? address, string domain);
		void AddWriteCallback(MemoryCallbackDelegate cb, uint? address, string domain);
		void AddExecCallback(MemoryCallbackDelegate cb, uint? address, string domain);
		void RemoveMemoryCallback(MemoryCallbackDelegate cb);
	}
}
