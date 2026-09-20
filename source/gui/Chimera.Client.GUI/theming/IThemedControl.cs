#nullable enable

using Chimera.Client.Common;

namespace Chimera.Client.GUI
{
	/// <summary>
	/// A control that paints itself and therefore has to be told the colours
	/// rather than have them set on it. The walker in <see cref="ThemeEngine"/>
	/// hands the theme to every one of these it passes, before and instead of
	/// guessing from the control's type.
	/// </summary>
	public interface IThemedControl
	{
		/// <summary>
		/// Take the colours. Called on the UI thread, on construction and again
		/// every time the theme changes; it must be safe to call more than once and
		/// must not assume a window handle exists yet.
		/// </summary>
		void ApplyTheme(Theme theme);
	}
}
