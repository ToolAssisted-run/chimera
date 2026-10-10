#nullable enable

using System;
using System.Collections.Generic;
using System.Drawing;
using System.Runtime.CompilerServices;
using System.Windows.Forms;

using Chimera.Common;

namespace Chimera.Client.GUI
{
	/// <summary>
	/// Keeps the text of a button visible on Linux (issue #246).
	///
	/// On Mono a button draws its text only when a whole line of it fits in the
	/// button; a line that is one pixel too tall is not clipped, it is left out.
	/// Mono promises the line the height the font reports, and for some fonts a
	/// line is really one pixel taller than that. With the font most desktops
	/// choose (DejaVu Sans) a line still fits in the standard button, 23 pixels
	/// high. With a taller one (Noto Sans, which Kubuntu chooses) it does not, and
	/// every button of that height is blank: TAStudio's playback buttons, the OK
	/// of a message box.
	///
	/// Which font the desktop chooses is not Chimera's to decide, so the button
	/// is made to fit instead: where a line does not fit, the button's font is
	/// made smaller in quarter points until one does. The button keeps its size
	/// and its place. Windows draws a button's text differently and is left alone.
	/// </summary>
	public static class ButtonTextFit
	{
		/// <summary>How far below its own size a button's font may go. Past that the text is left as it is.</summary>
		public const float MostPointsSmaller = 3f;

		/// <summary>What Mono keeps free around a button's text, above and below together.</summary>
		private const int MonoTextMargin = 8;

		private sealed class Seen
		{
			/// <summary>The size the button's font had before it was made to fit.</summary>
			public float OwnSize;
		}

		private static readonly ConditionalWeakTable<Control, Seen> Controls = new();

		private static bool _watching;

		/// <summary>True while this class is the one changing a button's font.</summary>
		[ThreadStatic]
		private static bool _fitting;

		/// <summary>
		/// Whether Mono draws the text of a button this high: the room it gives
		/// the text is the button without its margins, or the height the font
		/// reports if that is more, and a whole line has to fit in it.
		/// </summary>
		public static bool Shows(int buttonHeight, int verticalPadding, int fontHeight, int lineHeight)
			=> Math.Max(buttonHeight - verticalPadding - MonoTextMargin, fontHeight) >= lineHeight;

		/// <summary>
		/// The largest size, from <paramref name="size"/> down in quarter points,
		/// at which the button shows its text; <paramref name="size"/> itself when
		/// none within <see cref="MostPointsSmaller"/> does.
		/// </summary>
		/// <param name="metrics">for a size in points: the height the font reports and the height of a line of it</param>
		public static float SizeThatShows(float size, int buttonHeight, int verticalPadding, Func<float, (int FontHeight, int LineHeight)> metrics)
		{
			for (var smaller = 0f; smaller <= MostPointsSmaller && size - smaller >= 4f; smaller += 0.25f)
			{
				var (fontHeight, lineHeight) = metrics(size - smaller);
				if (Shows(buttonHeight, verticalPadding, fontHeight, lineHeight)) return size - smaller;
			}
			return size;
		}

		/// <summary>
		/// Starts looking after windows Chimera did not make - a message box, a
		/// file dialog - whenever the interface has nothing else to do. Chimera's
		/// own windows are looked after as they are created
		/// (<see cref="ThemedForm"/>). Does nothing on Windows.
		/// </summary>
		public static void Watch()
		{
			if (_watching || !OSTailoredCode.IsUnixHost) return;
			_watching = true;
			Application.Idle += static (_, _) => FitOpenForms();
		}

		/// <summary>Looks after every open window that has not been seen yet. Cheap when there is none.</summary>
		internal static void FitOpenForms()
		{
			List<Form>? unseen = null;
			foreach (Form form in Application.OpenForms)
			{
				if (!Controls.TryGetValue(form, out _)) (unseen ??= new()).Add(form);
			}
			if (unseen is null) return;
			foreach (var form in unseen) Fit(form);
		}

		/// <summary>
		/// Makes every button under <paramref name="root"/> show its text, now and
		/// when it is resized, given another font, or joined by new controls. Safe
		/// to call again for the same control. Does nothing on Windows.
		/// </summary>
		public static void Fit(Control root)
		{
			if (!OSTailoredCode.IsUnixHost) return;
			if (Controls.TryGetValue(root, out _)) return;
			Controls.Add(root, new Seen { OwnSize = root.Font.SizeInPoints });
			root.ControlAdded += static (_, e) => { if (e.Control is not null) Fit(e.Control); };
			if (DrawsAsAButton(root))
			{
				root.SizeChanged += static (sender, _) => FitOne((Control) sender!);
				root.FontChanged += static (sender, _) =>
				{
					var changed = (Control) sender!;
					// a font given by anybody else is the button's own from now on
					if (!_fitting && Controls.TryGetValue(changed, out var seen)) seen.OwnSize = changed.Font.SizeInPoints;
					FitOne(changed);
				};
				FitOne(root);
			}
			foreach (Control child in root.Controls) Fit(child);
		}

		/// <summary>The controls Mono lays out as a button: a button, and a tick box or an option drawn as one.</summary>
		private static bool DrawsAsAButton(Control control) => control switch
		{
			Button => true,
			CheckBox { Appearance: Appearance.Button } => true,
			RadioButton { Appearance: Appearance.Button } => true,
			_ => false,
		};

		private static void FitOne(Control button)
		{
			if (_fitting || !Controls.TryGetValue(button, out var seen)) return;
			var own = seen.OwnSize;
			var current = button.Font;
			var fits = SizeThatShows(own, button.Height, button.Padding.Vertical, size =>
			{
				if (Math.Abs(size - current.SizeInPoints) < 0.01f) return Metrics(current);
				using Font candidate = new(current.FontFamily, size, current.Style, GraphicsUnit.Point);
				return Metrics(candidate);
			});
			if (Math.Abs(fits - current.SizeInPoints) < 0.01f) return;
			_fitting = true;
			try
			{
				button.Font = new Font(current.FontFamily, fits, current.Style, GraphicsUnit.Point);
			}
			finally
			{
				_fitting = false;
			}
		}

		[ThreadStatic]
		private static Bitmap? _canvas;

		/// <summary>The height a font reports, and the height a line of it really takes.</summary>
		private static (int FontHeight, int LineHeight) Metrics(Font font)
		{
			_canvas ??= new Bitmap(1, 1);
			using var graphics = Graphics.FromImage(_canvas);
			return (font.Height, (int) Math.Ceiling(graphics.MeasureString("Ag", font).Height));
		}
	}
}
