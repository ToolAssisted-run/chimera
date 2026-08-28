#nullable enable

using System.Collections.Generic;

namespace Chimera.Bizware.Input
{
	internal interface IKeyMouseInput : IDisposable
	{
		IEnumerable<KeyEvent> UpdateKeyInputs(bool handleAltKbLayouts);

		(int DeltaX, int DeltaY) UpdateMouseInput();
	}
}
