using System.Collections.Generic;

namespace BizHawk.Emulation.Common
{
	/// <summary>
	/// Auxiliary picture surfaces rendered BY THE CORE for its own viewer tools -
	/// nametables, pattern tables, sprite lists, tilemaps, whatever the system has.
	///
	/// This is deliberately the opposite of BizHawk's approach, where the frontend
	/// tool (e.g. the NES PPU viewer) understood the system and did the drawing from
	/// core-exposed internals. Here the core knows what it wants to show and draws
	/// it; the frontend only asks "how many surfaces, what are they called, how big
	/// are they" and blits the result. So a nametable viewer costs the frontend no
	/// NES knowledge at all, and a core for a system nobody anticipated gets viewer
	/// windows for free.
	///
	/// Surfaces are debug views: they are re-rendered on demand from current state
	/// and must not affect emulation.
	/// </summary>
	public interface ICoreSurfaces : IEmulatorService
	{
		/// <summary>Display names, in index order. Index into this list is the surface index.</summary>
		IReadOnlyList<string> SurfaceNames { get; }

		/// <summary>Fixed dimensions of surface <paramref name="index"/>, in pixels.</summary>
		void GetSurfaceSize(int index, out int width, out int height);

		/// <summary>
		/// Renders surface <paramref name="index"/> from current emulation state and
		/// returns it as width*height packed 0xAARRGGBB pixels, top row first.
		/// The returned buffer is owned by the core and is reused between calls.
		/// </summary>
		int[] RenderSurface(int index);
	}
}
