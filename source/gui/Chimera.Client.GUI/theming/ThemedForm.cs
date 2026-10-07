#nullable enable

using System.ComponentModel;
using System.Windows.Forms;

using Chimera.Client.Common;

namespace Chimera.Client.GUI
{
	/// <summary>
	/// The root every window in the frontend grows from, and the reason a window
	/// added next year is themed without anybody remembering to theme it: the
	/// colours are put on in <see cref="OnHandleCreated"/>, before the window is
	/// ever painted, and put on again whenever the theme changes while it is open.
	///
	/// It deliberately carries nothing else. <see cref="FormBase"/> sits on top of
	/// it and adds the window-title contract; a window that cannot live with that
	/// contract still derives from this, so it is still themed. ThemingContractTests
	/// fails if a Form is added that derives from neither.
	///
	/// It is also where the title bar is asked to go dark - see
	/// <see cref="WindowFrame"/> - for the same reason: every window is here, so
	/// no window can be forgotten.
	/// </summary>
	public class ThemedForm : Form
	{
		/// <summary>
		/// Whether the theme is painted onto this window at all. False only for a
		/// window that is not frontend chrome - a Lua script's own canvas, whose
		/// colours belong to the script.
		/// </summary>
		[Browsable(false)]
		[DesignerSerializationVisibility(DesignerSerializationVisibility.Hidden)]
		protected virtual bool ThemingEnabled => true;

		protected override void OnHandleCreated(EventArgs e)
		{
			base.OnHandleCreated(e);
			if (DesignMode) return;
			TakeTheme(ThemeLibrary.Current);
		}

		/// <summary>
		/// Everything a window takes from a theme. The title bar is done here and
		/// not in <see cref="ApplyTheme"/> on purpose: ApplyTheme is the hook a
		/// window overrides, and a window that overrides it and forgets to call the
		/// base would otherwise be left with the desktop's title bar on a dark
		/// window. It also applies to a window whose contents are not ours to
		/// colour - a Lua script's canvas is the script's, but the frame around it
		/// is still Chimera's.
		/// </summary>
		private void TakeTheme(Theme theme)
		{
			WindowFrame.Apply(this, theme);
			if (ThemingEnabled) ApplyTheme(theme);
		}

		/// <summary>
		/// Paints the theme onto this window. Override to colour anything the walk
		/// cannot reach - a bitmap built once, a colour held in a field - and call
		/// the base first.
		/// </summary>
		protected virtual void ApplyTheme(Theme theme)
		{
			if (!ThemingEnabled) return;
			ThemeEngine.Apply(this, theme);
		}

		/// <summary>Called by <see cref="ThemeEngine.ApplyToOpenForms"/>; not part of the window's own API.</summary>
		internal void RaiseThemeChanged(Theme theme) => TakeTheme(theme);
	}
}
