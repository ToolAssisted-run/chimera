#nullable enable

using System.Collections.Generic;

namespace Chimera.Client.Common
{
	public interface IPhysicalInputSource
	{
		InputEvent? DequeueEvent();

		KeyValuePair<string, int>[] GetAxisValues();
	}
}
