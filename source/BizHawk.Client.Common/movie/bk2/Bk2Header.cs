using System.Collections.Generic;

namespace BizHawk.Client.Common
{
	/// <remarks>the Header.txt lump itself is parsed and rendered by the engine</remarks>
	public class Bk2Header : Dictionary<string, string>
	{
		public new string this[string key]
		{
			get => TryGetValue(key, out var s) ? s : string.Empty;
			set => base[key] = value;
		}
	}
}
