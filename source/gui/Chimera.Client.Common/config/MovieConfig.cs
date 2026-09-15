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

		/// <summary>
		/// How close the greenzone stays behind the playhead: at most one frame in
		/// this many is kept there, 1 to 32. The history thins the frames nearest
		/// the playhead when storing every one costs too much of the run, and on a
		/// heavy core it used to climb to one in 32 - so the frames a person rewinds
		/// to were the sparsest it held (user-decided, 2026-09-15). Lower is denser
		/// and slower; docs/state-manager.md has what each step cost on Ruffle.
		/// </summary>
		int GreenzoneMaxNearStride { get; }
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

		// One frame in four by default: a rewind right behind the playhead replays
		// at most three frames. On a Ruffle project on a GTX 1060 that ran 2500
		// frames in 41 s, where every frame took 71 s and the uncapped one in 32
		// took 22 s (user-decided, 2026-09-15).
		public int GreenzoneMaxNearStride { get; set; } = 4;

		/// <summary>The range the near band's stride may be capped to.</summary>
		public const int MinimumNearStride = 1;
		public const int MaximumNearStride = 32;

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
