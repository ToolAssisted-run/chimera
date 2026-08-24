#nullable enable

namespace Chimera.Bizware.Input
{
	public readonly record struct KeyEvent(DistinctKey Key, bool Pressed);
}
