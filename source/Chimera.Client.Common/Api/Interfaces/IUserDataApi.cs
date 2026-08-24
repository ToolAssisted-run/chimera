using System.Collections.Generic;

namespace Chimera.Client.Common
{
	public interface IUserDataApi : IExternalApi
	{
#if NET5_0_OR_GREATER
		IReadOnlySet<string> Keys { get; }
#else
		IReadOnlyCollection<string> Keys { get; }
#endif

		void Set(string name, object value);
		object Get(string key);
		void Clear();
		bool Remove(string key);
		bool ContainsKey(string key);
	}
}
