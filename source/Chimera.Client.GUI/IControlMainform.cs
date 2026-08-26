namespace Chimera.Client.GUI
{
	public interface IControlMainform
	{
		bool WantsToControlReboot { get; }
		void RebootCore();


		bool WantsToControlReadOnly { get; }

		/// <summary>
		/// Function that is called by Mainform instead of using its own code
		/// when a Tool sets WantsToControlReadOnly.
		/// Should not be called directly.
		/// </summary>
		void ToggleReadOnly();

		bool WantsToControlStopMovie { get; }

		/// <summary>
		/// Function that is called by Mainform instead of using its own code
		/// when a Tool sets WantsToControlStopMovie.
		/// Should not be called directly.
		/// <remarks>Like MainForm's StopMovie(), saving the movie is part of this function's responsibility.</remarks>
		/// </summary>
		void StopMovie(bool suppressSave);

		bool WantsToControlRewind { get; }

		void CaptureRewind();

		/// <summary>
		/// Function that is called by Mainform instead of using its own code
		/// when a Tool sets WantsToControlRewind
		/// </summary>
		/// <returns>Returns true if a frame advance is required.</returns>
		bool Rewind();

		bool WantsToControlRestartMovie { get; }

		bool RestartMovie();

		bool WantsToBypassMovieEndAction { get; }
	}
}
