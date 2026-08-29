#nullable enable

namespace Chimera.Input
{
	[Flags]
	public enum HostInputType
	{
		None = 0,
		Mouse = 1,
		Keyboard = 2,
		Pad = 4,
		Ignored = 8,
	}
}
