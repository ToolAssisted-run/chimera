#nullable enable

using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using System.Windows.Forms;

using Chimera.Client.Common;
using Chimera.Common;

namespace Chimera.Client.GUI
{
	/// <summary>
	/// The one part of a window WinForms cannot colour and Windows can: the title
	/// bar. The desktop compositor draws it, and since Windows 10 1809 it will
	/// draw it dark if the window asks, through one call to
	/// <c>DwmSetWindowAttribute</c>. There is nothing to draw and nothing to
	/// theme - the window says "I am dark" and the compositor does the rest, so
	/// the buttons, the hover states and the inactive shade all come out right.
	///
	/// Everywhere that is not Windows this does nothing at all: there is no
	/// dwmapi, and on a Unix host the title bar belongs to the window manager,
	/// which has its own opinion and is not ours to argue with.
	/// </summary>
	public static class WindowFrame
	{
		/// <summary>
		/// <c>DWMWA_USE_IMMERSIVE_DARK_MODE</c>. It was 19 while the feature was
		/// unofficial and became 20 in build 18985; both are tried, older first,
		/// because a build that does not know an attribute answers with an error
		/// rather than doing something unexpected.
		/// </summary>
		private const int DarkModeBefore20H1 = 19;

		private const int DarkMode = 20;

		[DllImport("dwmapi.dll", PreserveSig = true)]
		private static extern int DwmSetWindowAttribute(IntPtr hwnd, int attribute, ref int value, int size);

		/// <summary>
		/// Asks the compositor for a dark or a light title bar on this window.
		/// Silent about everything: a host with no dwmapi, a build too old to know
		/// the attribute, a window whose handle has gone. None of those are worth
		/// a message - the window simply keeps the title bar it had.
		/// </summary>
		/// <returns>true if Windows accepted the request, false if nothing happened.</returns>
		public static bool UseDarkTitleBar(IntPtr handle, bool dark)
		{
			if (OSTailoredCode.IsUnixHost || handle == IntPtr.Zero) return false;
			var value = dark ? 1 : 0;
			foreach (var attribute in new[] { DarkMode, DarkModeBefore20H1 })
			{
				try
				{
					if (DwmSetWindowAttribute(handle, attribute, ref value, sizeof(int)) is 0) return true;
				}
				catch (DllNotFoundException)
				{
					return false;
				}
				catch (EntryPointNotFoundException)
				{
					return false;
				}
			}
			return false;
		}

		/// <summary>
		/// The title bar this theme wants. A theme that follows the desktop asks
		/// for nothing - those ARE the desktop's colours, and the desktop's title
		/// bar is already one of them.
		/// </summary>
		public static bool WantsDark(Theme theme) => theme.IsDark && !theme.FollowsDesktop;

		private sealed class Request
		{
			public bool Dark;
		}

		/// <summary>
		/// What each window last asked for, kept whether or not Windows was there
		/// to hear it. The call itself cannot be exercised anywhere but Windows, so
		/// this is what a test on any host can check: that every window ASKED. A
		/// window that overrides ApplyTheme and forgets to call the base is the way
		/// this gets missed, and it is the one thing worth catching automatically.
		/// </summary>
		private static readonly ConditionalWeakTable<Form, Request> Asked = new();

		/// <summary>What this window last asked for, or null if it never has.</summary>
		public static bool? LastAskedFor(Form form)
			=> Asked.TryGetValue(form, out var request) ? request.Dark : null;

		/// <summary>Puts this window's title bar in step with the theme.</summary>
		public static void Apply(Form form, Theme theme)
		{
			if (form.IsDisposed || !form.IsHandleCreated) return;
			var dark = WantsDark(theme);
			Asked.GetValue(form, static _ => new Request()).Dark = dark;
			UseDarkTitleBar(form.Handle, dark);
		}
	}
}
