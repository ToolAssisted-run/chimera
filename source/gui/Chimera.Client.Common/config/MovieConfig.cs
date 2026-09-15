using System;

namespace Chimera.Client.Common
{
	public interface IMovieConfig
	{
		MovieEndAction MovieEndAction { get; }
		bool EnableBackupMovies { get; }
		int MovieCompressionLevel { get; }
		bool VBAStyleMovieLoadState { get; }

		/// <summary>
		/// What the state history may hold, in megabytes - all of it in memory.
		/// Beyond it the far band is thinned and then the oldest of it dropped,
		/// which costs replaying to reach a frame that used to be stored. Nothing
		/// goes to disk while a project is open; the disk is written when the
		/// project is saved (user-decided, 2026-09-15; docs/state-manager.md).
		/// </summary>
		int GreenzoneBudgetMb { get; }
	}

	public class MovieConfig : IMovieConfig
	{
		public MovieEndAction MovieEndAction { get; set; } = MovieEndAction.Pause;
		public bool EnableBackupMovies { get; set; } = true;
		public int MovieCompressionLevel { get; set; } = 2;
		public bool VBAStyleMovieLoadState { get; set; }

		// Deltas made density affordable: a frame of xemu costs about two
		// megabytes here where a whole state was two hundred, so this holds far
		// more run than the same number used to. Four gigabytes is a machine with
		// sixteen giving a quarter of them to the run being edited - and it is a
		// CEILING, not a reservation: a history takes what the run needs and the
		// engine halves this on its own if the machine turns out not to have it.
		public int GreenzoneBudgetMb { get; set; } = 4 * 1024;

		/// <summary>
		/// The smallest memory budget that means anything.
		///
		/// The engine reads a budget of zero as "no history at all", which is a
		/// perfectly good thing for a headless tool to ask for and never what a
		/// person editing a number meant. A run needs room for one anchor and a
		/// few deltas before it can hold a single frame, so anything under this
		/// is read as this and the history stays a history.
		/// </summary>
		public const int MinimumBudgetMb = 64;

	}
}
