using System.Collections.Generic;

namespace Chimera.Client.Common
{
	/// <remarks>the Header.txt lump itself is parsed and rendered by the engine</remarks>
	public class MovieHeader : Dictionary<string, string>
	{
		public new string this[string key]
		{
			get => TryGetValue(key, out var s) ? s : string.Empty;
			set => base[key] = value;
		}
	}
}
