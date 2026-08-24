using Chimera.Emulation.Common;

namespace Chimera.Client.Common
{
	public class TasClipboardEntry
	{
		public TasClipboardEntry(int frame, IController controllerState)
		{
			Frame = frame;
			ControllerState = controllerState;
		}

		public int Frame { get; }
		public IController ControllerState { get; }
	}
}
