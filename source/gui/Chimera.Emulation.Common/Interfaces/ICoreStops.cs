namespace Chimera.Emulation.Common
{
	/// <summary>
	/// A core whose machine can die mid-frame and say so (miniBox hands control back, and the engine
	/// reports the reason). The frame that saw it did not run; nothing runs until a state is loaded.
	/// The frontend turns this into a <see cref="Engine.CoreStoppedException"/> from its own frame.
	/// </summary>
	public interface ICoreStops
	{
		/// <summary>Why the last frame advance did not run, or null when it ran.</summary>
		string CoreStopped { get; }
	}
}
