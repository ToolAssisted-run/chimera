namespace Chimera.Emulation.Common
{
	/// <summary>
	/// Implemented by a core that CAN have its pictures drawn by a GPU outside
	/// the sandbox.
	///
	/// That is much faster than a software rasteriser and it is not
	/// deterministic: the GPU is outside the savestate, and two machines with
	/// different drivers do not produce the same pixels. So a movie records it,
	/// and a replay that desyncs somewhere else has something to point at.
	/// </summary>
	public interface IGpuRendered
	{
		/// <summary>
		/// What the driver calls itself when one drew this run, and empty when
		/// none did - which is every ordinary run, including one by a core that
		/// could have used a GPU and was not given one.
		/// </summary>
		string GpuRenderer { get; }
	}
}
