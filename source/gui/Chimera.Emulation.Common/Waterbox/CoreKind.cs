using System;

namespace Chimera.Emulation.Common.Waterbox
{
	/// <summary>
	/// The two sorts of core (docs/game-cores.md, user-decided 2026-09-28): an emulator,
	/// which is a machine that plays games from their files, and a game core, which is one
	/// game - open, or reconstructed - built as a core and played from that game's own
	/// files. A package says which in waterbox.config's <c>kind</c>, and the roster says
	/// it for the cores nobody has downloaded yet.
	/// </summary>
	public static class CoreKind
	{
		public const string Emulator = "emulator";

		public const string Game = "game";

		/// <summary>
		/// Whether a declared kind is a game core. Anything else is an emulator: absence is
		/// every package from before game cores existed, and a kind this build has never
		/// heard of is listed with the machines rather than refused.
		/// </summary>
		public static bool IsGame(string? kind)
			=> string.Equals(kind?.Trim(), Game, StringComparison.OrdinalIgnoreCase);
	}
}
