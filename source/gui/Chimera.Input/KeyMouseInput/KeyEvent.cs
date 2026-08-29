#nullable enable

namespace Chimera.Input
{
	public readonly record struct KeyEvent(DistinctKey Key, bool Pressed);
}
