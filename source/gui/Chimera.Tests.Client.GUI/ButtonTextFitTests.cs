using System.Drawing;
using System.Windows.Forms;

using Chimera.Client.GUI;
using Chimera.Common;

namespace Chimera.Tests.Client.GUI
{
	/// <summary>
	/// A button keeps its text on Linux whatever font the desktop chose (issue
	/// #246): Mono leaves a button's text out when a line of it is taller than the
	/// room the button has, and <see cref="ButtonTextFit"/> makes the font fit.
	/// </summary>
	[TestClass]
	public class ButtonTextFitTests
	{
		[TestMethod]
		public void MonoShowsALineOnlyWhenAllOfItFits()
		{
			// a 23-pixel button has 15 pixels for its text; DejaVu Sans 8.25pt takes 14, Noto Sans 16
			Assert.IsTrue(ButtonTextFit.Shows(buttonHeight: 23, verticalPadding: 0, fontHeight: 13, lineHeight: 14));
			Assert.IsFalse(ButtonTextFit.Shows(buttonHeight: 23, verticalPadding: 0, fontHeight: 15, lineHeight: 16));
			// the height the font reports is always granted, however small the button
			Assert.IsTrue(ButtonTextFit.Shows(buttonHeight: 10, verticalPadding: 0, fontHeight: 18, lineHeight: 18));
			Assert.IsFalse(ButtonTextFit.Shows(buttonHeight: 23, verticalPadding: 4, fontHeight: 13, lineHeight: 14));
		}

		[TestMethod]
		public void TheFontGoesDownInQuarterPointsUntilALineFits()
		{
			// a made-up font: the line is twice the size in pixels, and the font reports one pixel less
			static (int, int) Metrics(float size) => ((int) (size * 2) - 1, (int) (size * 2));
			Assert.AreEqual(7.75f, ButtonTextFit.SizeThatShows(8.25f, 23, 0, Metrics));
			// a font that already fits is left alone
			Assert.AreEqual(7f, ButtonTextFit.SizeThatShows(7f, 23, 0, Metrics));
			// and one that would have to shrink beyond reason is left alone too
			Assert.AreEqual(20f, ButtonTextFit.SizeThatShows(20f, 23, 0, Metrics));
		}

		/// <summary>How many dark pixels the inside of a white button has: its text, or nothing.</summary>
		private static int Ink(Control button)
		{
			Application.DoEvents();
			using Bitmap picture = new(button.Width, button.Height);
			button.DrawToBitmap(picture, new Rectangle(0, 0, button.Width, button.Height));
			var ink = 0;
			for (var y = 3; y < picture.Height - 3; y++)
			{
				for (var x = 3; x < picture.Width - 3; x++)
				{
					var c = picture.GetPixel(x, y);
					if (c.R < 100 && c.G < 100 && c.B < 100) ink++;
				}
			}
			return ink;
		}

		private static Button StandardButton(Font font, FlatStyle style) => new()
		{
			BackColor = Color.White,
			FlatStyle = style,
			Font = font,
			ForeColor = Color.Black,
			Location = new Point(10, 10),
			Size = new Size(85, 23),
			Text = "OK",
		};

		/// <summary>A size of the default font at which a standard button is blank here, or 0 when there is none.</summary>
		private static float ASizeMonoLeavesBlank(Form form, FlatStyle style)
		{
			foreach (var size in new[] { 8.25f, 9f, 9.5f, 10f, 10.5f, 11f, 11.5f, 12f, 13f, 14f })
			{
				using Font font = new(form.Font.FontFamily, size);
				using var button = StandardButton(font, style);
				form.Controls.Add(button);
				var blank = Ink(button) is 0;
				form.Controls.Remove(button);
				if (blank) return size;
			}
			return 0f;
		}

		[TestMethod]
		[DataRow(FlatStyle.Standard)]
		[DataRow(FlatStyle.Flat)]
		public void AButtonMonoWouldLeaveBlankShowsItsText(FlatStyle style)
		{
			if (!OSTailoredCode.IsUnixHost) Assert.Inconclusive("Windows draws a button's text differently; there is nothing to fit.");
			// a plain Form: what a window Chimera did not make is, such as a message box
			using Form form = new() { ClientSize = new Size(300, 120) };
			form.Show();
			var size = ASizeMonoLeavesBlank(form, style);
			if (size is 0f) Assert.Inconclusive("no size of this machine's default font leaves a standard button blank");

			using Font font = new(form.Font.FontFamily, size);
			using var button = StandardButton(font, style);
			form.Controls.Add(button);
			Assert.AreEqual(0, Ink(button), "the size chosen should leave the button blank before it is looked after");

			ButtonTextFit.FitOpenForms();
			Assert.IsTrue(Ink(button) > 0, $"the button is still blank at {size}pt after being looked after (its font is now {button.Font.SizeInPoints}pt)");
			Assert.IsTrue(button.Font.SizeInPoints < size, "the font should have been made smaller");
			Assert.AreEqual(new Size(85, 23), button.Size, "the button keeps its size");

			// a button added later is looked after as it arrives
			using var later = StandardButton(font, style);
			later.Location = new Point(110, 10);
			form.Controls.Add(later);
			Assert.IsTrue(Ink(later) > 0, "a button added after the window was seen is blank");

			// and a button made tall enough gets its own size back
			button.Height = 40;
			Assert.AreEqual(size, button.Font.SizeInPoints, 0.01f);
			Assert.IsTrue(Ink(button) > 0);
		}

		private sealed class WindowWithASmallButton : FormBase
		{
			public readonly Button Small;

			protected override string WindowTitleStatic => "Small button";

			public WindowWithASmallButton(float size, FlatStyle style)
			{
				ClientSize = new Size(300, 120);
				Small = StandardButton(new Font(Font.FontFamily, size), style);
				Controls.Add(Small);
			}
		}

		[TestMethod]
		public void AWindowOfChimerasOwnIsLookedAfterBeforeItIsShown()
		{
			if (!OSTailoredCode.IsUnixHost) Assert.Inconclusive("Windows draws a button's text differently; there is nothing to fit.");
			float size;
			using (Form probe = new())
			{
				probe.Show();
				size = ASizeMonoLeavesBlank(probe, FlatStyle.Standard);
			}
			if (size is 0f) Assert.Inconclusive("no size of this machine's default font leaves a standard button blank");

			using WindowWithASmallButton form = new(size, FlatStyle.Standard);
			form.Show();
			Assert.IsTrue(form.Small.Font.SizeInPoints < size, "the window's button was not looked after when the window was created");
		}
	}
}
