using System;
using System.Collections.Generic;
using System.Linq;
using System.Windows.Forms;

using Chimera.Client.Common;
using Chimera.Client.GUI;

namespace Chimera.Tests.Client.GUI
{
	/// <summary>
	/// The title bar is the desktop compositor's, not WinForms'. On Windows it
	/// will draw it dark if the window asks, through one call to
	/// DwmSetWindowAttribute; everywhere else there is nothing to ask.
	///
	/// What these tests cover, and what they do not, said plainly because a green
	/// run here means less than it looks like:
	/// <list type="bullet">
	/// <item>COVERED: that every window ASKS, and which theme it asks for. The
	/// request is made from ThemedForm's handle-created and theme-changed paths
	/// rather than from ApplyTheme, so a window cannot skip it by overriding
	/// ApplyTheme and forgetting the base call; this checks that no window type
	/// falls outside that, and it is checkable on any host.</item>
	/// <item>NOT COVERED: the call itself. There is no dwmapi on this machine and
	/// no title bar to look at under Xvfb, so whether Windows honours the request
	/// is answered by the screenshots taken on the Windows box, not here.</item>
	/// </list>
	/// </summary>
	[TestClass]
	public class TitleBarContractTests
	{
		[TestMethod]
		public void OnlyAThemeOfItsOwnThatIsDarkAsksForADarkTitleBar()
		{
			var light = ThemeLibrary.Find("Light")!;
			var dark = ThemeLibrary.Find("Dark")!;
			Assert.IsFalse(WindowFrame.WantsDark(light), "the desktop theme asked to change the desktop's own title bar");
			Assert.IsTrue(WindowFrame.WantsDark(dark), "the dark theme did not ask for a dark title bar");
		}

		/// <summary>
		/// Off Windows the call cannot do anything and must say so rather than
		/// throwing - a missing dwmapi is the normal case on the machine most of
		/// this is developed on.
		/// </summary>
		[TestMethod]
		public void AskingIsHarmlessWhereThereIsNobodyToAsk()
		{
			Assert.IsFalse(WindowFrame.UseDarkTitleBar(IntPtr.Zero, dark: true), "a window with no handle was reported as done");
			using Form form = new();
			form.CreateControl();
			// on Unix this is false because there is no dwmapi; on Windows it is
			// true because there is. Either way it returns rather than throwing,
			// which is the whole of what this asserts.
			WindowFrame.UseDarkTitleBar(form.Handle, dark: true);
			WindowFrame.UseDarkTitleBar(form.Handle, dark: false);
		}

		/// <summary>
		/// Every window the frontend can build, built and told the theme: each has
		/// to have asked for the title bar that theme wants. The same shape as
		/// EveryWindowInTheFrontendIsAThemedForm - it is the mechanism being
		/// reached that is under test, not the drawing, which no host here can
		/// show.
		/// </summary>
		[TestMethod]
		public void NoWindowSkipsTheTitleBar()
		{
			List<string> missed = new();
			var looked = 0;
			var dark = ThemeLibrary.Select("Dark");
			try
			{
				foreach (var type in typeof(ThemedForm).Assembly.GetTypes()
					.Where(static t => !t.IsAbstract && typeof(ThemedForm).IsAssignableFrom(t) && t != typeof(ThemedForm))
					.Where(static t => t.GetConstructor(Type.EmptyTypes) is not null)
					.OrderBy(static t => t.FullName, StringComparer.Ordinal))
				{
					ThemedForm form;
					try
					{
						form = (ThemedForm) Activator.CreateInstance(type);
					}
					catch (Exception)
					{
						// a window that needs a running frontend to exist at all
						continue;
					}
					using (form)
					{
						// a handle, but not shown: some of these windows start work
						// of their own the moment they appear. CreateControl does
						// nothing for a top-level window that is not visible, so the
						// handle is asked for directly.
						_ = form.Handle;
						form.RaiseThemeChanged(dark);
						looked++;
						var asked = WindowFrame.LastAskedFor(form);
						if (asked is null) missed.Add($"{type.Name} never asked for a title bar at all");
						else if (asked is false) missed.Add($"{type.Name} asked for a light title bar under the dark theme");
					}
				}
			}
			finally
			{
				ThemeLibrary.Select("Light");
			}
			Assert.IsTrue(looked > 5, $"only {looked} windows were built, so this is not testing much");
			Assert.AreEqual(
				0,
				missed.Count,
				"these windows do not follow the theme in their title bar:\n"
					+ string.Join("\n", missed));
		}
	}
}
