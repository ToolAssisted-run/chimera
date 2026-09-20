using System;
using System.Collections.Generic;
using System.Drawing;
using System.Linq;
using System.Windows.Forms;

using Chimera.Client.Common;
using Chimera.Client.GUI;

namespace Chimera.Tests.Client.GUI
{
	/// <summary>
	/// The obligation that keeps theming working: a window added next year has to
	/// be themed without its author remembering to theme it. That is arranged by
	/// making every window derive from ThemedForm, which paints itself on handle
	/// creation - and arranged only as long as somebody does not write
	/// "class NewThing : Form". This test is what makes that a build failure
	/// instead of a window that stays white in the dark theme.
	/// </summary>
	[TestClass]
	public class ThemingContractTests
	{
		/// <summary>
		/// Windows that are deliberately not ThemedForm. There are none, and there
		/// should stay none; a window that must not be themed overrides
		/// ThemingEnabled instead, which keeps it inside the mechanism.
		/// </summary>
		private static readonly HashSet<string> Exempt = new(StringComparer.Ordinal);

		private static IReadOnlyList<Type> EveryWindow()
			=> typeof(ThemedForm).Assembly.GetTypes()
				.Where(static t => typeof(Form).IsAssignableFrom(t))
				.Where(static t => t != typeof(ThemedForm))
				.OrderBy(static t => t.FullName, StringComparer.Ordinal)
				.ToList();

		[TestMethod]
		public void EveryWindowInTheFrontendIsAThemedForm()
		{
			var windows = EveryWindow();
			Assert.IsTrue(windows.Count > 30, $"the sweep found only {windows.Count} windows, so it is not sweeping");
			var escaped = windows
				.Where(static t => !typeof(ThemedForm).IsAssignableFrom(t))
				.Select(static t => t.Name)
				.Where(n => !Exempt.Contains(n))
				.ToList();
			Assert.AreEqual(
				0,
				escaped.Count,
				"these windows derive from Form rather than ThemedForm, so nothing will ever paint a theme onto them: "
					+ string.Join(", ", escaped)
					+ ". Change the base class; if the window genuinely must keep its own colours (a Lua script's canvas), "
					+ "derive from ThemedForm and override ThemingEnabled.");
		}

		/// <summary>
		/// Every window that can be built with no arguments, built and then painted
		/// with each theme in turn. What this is really testing is that nothing in
		/// the walk throws on a real control tree - a property that is read-only on
		/// some control, a null the walk did not expect.
		/// </summary>
		[TestMethod]
		public void EveryWindowThatCanBeBuiltCanBeThemed()
		{
			List<string> broken = new();
			var built = 0;
			foreach (var type in EveryWindow().Where(static t => !t.IsAbstract && typeof(ThemedForm).IsAssignableFrom(t)))
			{
				if (type.GetConstructor(Type.EmptyTypes) is null) continue;
				Form form;
				try
				{
					form = (Form) Activator.CreateInstance(type);
				}
				catch (Exception)
				{
					// a window that needs a running frontend to exist at all; the
					// tests that drive those build them with what they need
					continue;
				}
				built++;
				try
				{
					using (form)
					{
						form.CreateControl();
						foreach (var name in ThemeLibrary.All.Select(static t => t.Name).ToList())
						{
							ThemeEngine.Apply(form, ThemeLibrary.Select(name));
						}
					}
				}
				catch (Exception ex)
				{
					broken.Add($"{type.Name}: {ex.GetType().Name} {ex.Message}");
				}
			}
			Assert.IsTrue(built > 5, $"only {built} windows were built, so this is not testing much");
			Assert.AreEqual(0, broken.Count, "theming threw on:\n" + string.Join("\n", broken));
		}

		/// <summary>
		/// The colours a light desktop hands out, which are what an unthemed control
		/// is left holding. None of them may survive a dark theme.
		/// </summary>
		private static bool LooksUnthemed(Color c)
			=> c.ToArgb() == SystemColors.Control.ToArgb()
				|| c.ToArgb() == SystemColors.Window.ToArgb()
				|| c.ToArgb() == SystemColors.ControlLight.ToArgb()
				|| c.ToArgb() == SystemColors.ControlLightLight.ToArgb()
				|| c.ToArgb() == Color.White.ToArgb()
				|| c.ToArgb() == Color.WhiteSmoke.ToArgb();

		/// <summary>
		/// The real question the dark theme exists to answer: after the walk, is
		/// anything still the colour a light desktop gave it? A control here is one
		/// somebody can see, so invisible and zero-sized ones are left out, as are
		/// the two surfaces that are black in every theme on purpose.
		/// </summary>
		[TestMethod]
		public void NothingIsLeftLightUnderTheDarkTheme()
		{
			var dark = ThemeLibrary.Select("Dark");
			Assert.IsNotNull(dark);
			List<string> stragglers = new();
			var checkedControls = 0;
			foreach (var type in EveryWindow().Where(static t => !t.IsAbstract && typeof(ThemedForm).IsAssignableFrom(t)))
			{
				if (type.GetConstructor(Type.EmptyTypes) is null) continue;
				Form form;
				try
				{
					form = (Form) Activator.CreateInstance(type);
				}
				catch (Exception)
				{
					continue;
				}
				using (form)
				{
					form.CreateControl();
					ThemeEngine.Apply(form, dark!);
					Walk(form, type.Name, ref checkedControls, stragglers);
				}
			}
			Assert.IsTrue(checkedControls > 50, $"only {checkedControls} controls were looked at, so this is not testing much");
			Assert.AreEqual(0, stragglers.Count, "still light after the dark theme was applied:\n" + string.Join("\n", stragglers));
			ThemeLibrary.Select("Light"); // leave the process as we found it
		}

		private static void Walk(Control c, string window, ref int seen, List<string> stragglers)
		{
			var skip = c is ProgressBar // drawn by the OS; its colours are not ours to set
				|| (c.BackColor.ToArgb() == Color.Transparent.ToArgb());
			if (!skip)
			{
				seen++;
				if (LooksUnthemed(c.BackColor))
				{
					stragglers.Add($"{window} > {Describe(c)}: BackColor is {c.BackColor}");
				}
				if (LooksUnthemed(c.ForeColor))
				{
					stragglers.Add($"{window} > {Describe(c)}: ForeColor is {c.ForeColor}");
				}
			}
			foreach (Control child in c.Controls) Walk(child, window, ref seen, stragglers);
		}

		private static string Describe(Control c)
			=> string.IsNullOrEmpty(c.Name) ? c.GetType().Name : $"{c.GetType().Name} {c.Name}";

		/// <summary>
		/// A control whose colour was declared as a ROLE follows the theme; a
		/// control the walk coloured by its type does too. Both matter: the second
		/// is most of the frontend and the first is every place a colour means
		/// something. Under Light the ordinary label is not assigned at all - it
		/// already has the desktop's colour, which is what the role resolves to.
		/// </summary>
		[TestMethod]
		public void ARoleFollowsTheThemeAndTheTypeTableDoesToo()
		{
			using Form form = new();
			Label error = new();
			Label ordinary = new();
			form.Controls.Add(error);
			form.Controls.Add(ordinary);
			error.SetForeRole(ThemeColorRole.AccentError);

			var light = ThemeLibrary.Select("Light");
			var dark = ThemeLibrary.Find("Dark")!;

			ThemeEngine.Apply(form, light);
			Assert.AreEqual(light[ThemeColorRole.AccentError].ToArgb(), error.ForeColor.ToArgb());
			Assert.AreEqual(light[ThemeColorRole.WindowText].ToArgb(), ordinary.ForeColor.ToArgb());

			ThemeEngine.Apply(form, dark);
			Assert.AreEqual(dark[ThemeColorRole.AccentError].ToArgb(), error.ForeColor.ToArgb());
			Assert.AreEqual(dark[ThemeColorRole.WindowText].ToArgb(), ordinary.ForeColor.ToArgb());
			Assert.AreEqual(dark[ThemeColorRole.WindowBackground].ToArgb(), form.BackColor.ToArgb());
		}

		/// <summary>
		/// The Light theme is the desktop's own colours, and the proof that it
		/// cannot change how anything looks is that it does not assign anything: a
		/// control with a colour of its own keeps it, and so does one with the
		/// colour the toolkit gave it. Only what the frontend declared by name is
		/// set. This is what makes "Light is the palette Chimera already had" a
		/// property of the code rather than of ninety hex values being right.
		/// </summary>
		[TestMethod]
		public void TheDesktopThemeAssignsNothingItWasNotAskedFor()
		{
			var light = ThemeLibrary.Select("Light");
			Assert.IsTrue(light.FollowsDesktop, "the Light theme no longer says it is the desktop's palette");

			using Form form = new();
			Label deliberate = new() { BackColor = Color.Magenta, ForeColor = Color.Lime };
			TextBox box = new();
			Button button = new();
			ListView list = new() { View = View.Details };
			Label declared = new();
			form.Controls.AddRange([ deliberate, box, button, list, declared ]);
			declared.SetForeRole(ThemeColorRole.AccentError);
			form.CreateControl();

			var wasBoxBack = box.BackColor;
			var wasButtonBack = button.BackColor;
			var wasButtonVisualStyle = button.UseVisualStyleBackColor;
			var wasListBorder = list.BorderStyle;

			ThemeEngine.Apply(form, light);

			Assert.AreEqual(Color.Magenta.ToArgb(), deliberate.BackColor.ToArgb(), "a control's own colour was overwritten");
			Assert.AreEqual(Color.Lime.ToArgb(), deliberate.ForeColor.ToArgb(), "a control's own text colour was overwritten");
			Assert.AreEqual(wasBoxBack.ToArgb(), box.BackColor.ToArgb());
			Assert.AreEqual(wasButtonBack.ToArgb(), button.BackColor.ToArgb());
			Assert.AreEqual(wasButtonVisualStyle, button.UseVisualStyleBackColor, "the button stopped using visual styles");
			Assert.AreEqual(wasListBorder, list.BorderStyle, "the list's border was flattened");
			Assert.IsFalse(list.OwnerDraw, "the list's headers were taken over");
			Assert.AreEqual(light[ThemeColorRole.AccentError].ToArgb(), declared.ForeColor.ToArgb(), "a declared role was not applied");
		}

		/// <summary>
		/// What Config &gt; Theme does when a theme is picked: every window that is
		/// open repaints, including ones that were opened before the choice and
		/// ones that override ApplyTheme to colour something the walk cannot reach.
		/// </summary>
		[TestMethod]
		public void PickingAThemeRepaintsEveryWindowThatIsOpen()
		{
			using Recorder first = new();
			using Recorder second = new();
			first.Show();
			second.Show();
			try
			{
				var dark = ThemeLibrary.Select("Dark");
				ThemeEngine.ApplyToOpenForms(dark);

				Assert.AreEqual(dark[ThemeColorRole.WindowBackground].ToArgb(), first.BackColor.ToArgb());
				Assert.AreEqual(dark[ThemeColorRole.WindowBackground].ToArgb(), second.BackColor.ToArgb());
				Assert.AreEqual("Dark", first.LastTold, "the window was not told which theme it is now wearing");
				Assert.AreEqual("Dark", second.LastTold);

				var light = ThemeLibrary.Select("Light");
				ThemeEngine.ApplyToOpenForms(light);
				Assert.AreEqual("Light", first.LastTold);
			}
			finally
			{
				ThemeLibrary.Select("Light");
				first.Close();
				second.Close();
			}
		}

		/// <summary>A window that colours something of its own, so the repaint can be seen to reach it.</summary>
		private sealed class Recorder : ThemedForm
		{
			public string LastTold { get; private set; } = "";

			protected override void ApplyTheme(Theme theme)
			{
				base.ApplyTheme(theme);
				LastTold = theme.Name;
			}
		}

		/// <summary>A control that asked to be left alone is left alone, and so is everything in it.</summary>
		[TestMethod]
		public void SkippedControlsAreLeftAlone()
		{
			using Form form = new();
			Panel canvas = new() { BackColor = Color.Magenta };
			Label inside = new() { ForeColor = Color.Lime };
			canvas.Controls.Add(inside);
			form.Controls.Add(canvas);
			canvas.SkipTheming();

			ThemeEngine.Apply(form, ThemeLibrary.Select("Dark"));
			ThemeLibrary.Select("Light");
			Assert.AreEqual(Color.Magenta.ToArgb(), canvas.BackColor.ToArgb());
			Assert.AreEqual(Color.Lime.ToArgb(), inside.ForeColor.ToArgb());
		}

		/// <summary>
		/// Theming a window must not bring its controls to life.
		///
		/// This is not tidiness. Asking a ListView for SelectedIndices or TopItem
		/// CREATES its handle, and creating a ListView's handle pushes its items
		/// into the native control one at a time, raising ItemChecked for each. The
		/// walk runs when the WINDOW's handle is made, which is before its children
		/// have theirs - so the walk was manufacturing a burst of tick events inside
		/// a window that had not finished opening, and Pre-Compiled Modules died of
		/// it with a NullReferenceException in its own tick handler.
		///
		/// The two halves are checked separately because they fail separately: a
		/// handle that should not exist, and events that should not have been
		/// raised.
		///
		/// BE CLEAR ABOUT WHAT THIS PROVES. Neither half fails on Mono when the
		/// guard is taken out, because Mono's SelectedIndices and TopItem do not
		/// create a handle and Mono raises nothing when one is made. It was checked:
		/// the guard was removed and this test stayed green. So what it holds here
		/// is the SHAPE of the rule, against a change gross enough for either
		/// toolkit to notice. The version of it with teeth runs on Windows, in
		/// tests/ui/windows/live-theme-switch.sh, where the toolkit does both.
		/// </summary>
		[TestMethod]
		public void TheWalkDoesNotBringAListToLife()
		{
			using Form form = new();
			ListView list = new() { View = View.Details, CheckBoxes = true };
			list.Columns.Add("Name", 120);
			foreach (var name in new[] { "one", "two", "three" })
			{
				list.Items.Add(new ListViewItem(name));
			}
			list.Items[1].Checked = true;
			var ticks = 0;
			list.ItemChecked += (_, _) => ticks++;
			form.Controls.Add(list);

			Assert.IsFalse(list.IsHandleCreated, "the list should not have a handle before its window is shown");

			ThemeEngine.Apply(form, ThemeLibrary.Select("Dark"));
			var madeAHandle = list.IsHandleCreated;
			var raised = ticks;
			ThemeLibrary.Select("Light");

			Assert.IsFalse(
				madeAHandle,
				"the theme walk created the list's handle. Creating a ListView's handle inserts its items, "
					+ "and every insertion raises ItemChecked inside a window that is still being built - "
					+ "which is a crash in somebody's handler, not a repaint.");
			Assert.AreEqual(
				0,
				raised,
				$"the theme walk raised {raised} ItemChecked events. Painting a window is not an instruction "
					+ "to tell it its rows have been ticked.");
		}

		/// <summary>
		/// The Light theme must leave a tool strip exactly as the frontend left it
		/// before there were themes: a colour table built out of system colours
		/// still does not draw what the system renderer draws, and the strips are
		/// the surface where that shows most.
		/// </summary>
		[TestMethod]
		public void TheLightThemeDoesNotTakeOverTheMenuRenderer()
		{
			using Form form = new();
			MenuStrip strip = new();
			strip.Items.Add(new ToolStripMenuItem("File"));
			form.Controls.Add(strip);

			ThemeEngine.Apply(form, ThemeLibrary.Select("Light"));
			Assert.IsFalse(strip.Renderer is ThemeToolStripRenderer, "the Light theme replaced the menu renderer");

			ThemeEngine.Apply(form, ThemeLibrary.Select("Dark"));
			ThemeLibrary.Select("Light");
			Assert.IsTrue(strip.Renderer is ThemeToolStripRenderer, "the Dark theme did not reach the menu renderer");
			Assert.AreEqual(
				ThemeLibrary.Find("Dark")![ThemeColorRole.MenuText].ToArgb(),
				strip.Items[0].ForeColor.ToArgb(),
				"a menu item kept its own text colour");
		}

		/// <summary>Every role the frontend can ask for is answered by every theme on offer.</summary>
		[TestMethod]
		public void EveryThemeAnswersEveryRole()
		{
			foreach (var theme in ThemeLibrary.All)
			{
				foreach (var role in Theme.AllRoles)
				{
					Assert.AreNotEqual(0, theme[role].ToArgb() & unchecked((int) 0xFF000000), $"{theme.Name}: {role} is fully transparent, which is not a colour");
				}
			}
		}

	}
}
