#nullable enable

namespace Chimera.Client.Common
{
	/// <summary>
	/// Which cores a list shows (docs/game-cores.md): every kind, or one of the two. The lists of
	/// cores are one list each, with a Type column and this choice above them, where they used to
	/// carry divider rows (user-decided, 2026-09-29).
	/// </summary>
	public enum CoreKindFilter
	{
		All = 0,
		Emulators = 1,
		Games = 2,
	}

	public static class CoreKindFilterExtensions
	{
		/// <summary>Whether a core of this kind is listed under the filter.</summary>
		public static bool Shows(this CoreKindFilter filter, bool isGameCore)
			=> filter switch
			{
				CoreKindFilter.Emulators => !isGameCore,
				CoreKindFilter.Games => isGameCore,
				_ => true,
			};

		/// <summary>What a list's Type column says.</summary>
		public static string KindText(bool isGameCore) => isGameCore ? "Game" : "Emulator";
	}
}
