using System.Linq;
using System.Text;
using System.Diagnostics.CodeAnalysis;
using System.Drawing;
using System.Collections.Generic;

using Chimera.Emulation.Common;

namespace Chimera.Client.Common
{
	public class OSDManager
	{
		private Config _config;

		private IEmulator _emulator;

		private readonly InputManager _inputManager;

		private readonly IMovieSession _movieSession;

		public OSDManager(Config config, IEmulator emulator, InputManager inputManager, IMovieSession movieSession)
		{
			_config = config;
			_emulator = emulator;
			_inputManager = inputManager;
			_movieSession = movieSession;
		}

		public void UpdateGlobals(Config config, IEmulator emulator)
		{
			_config = config;
			_emulator = emulator;
		}

		public string Fps { get; set; }

		/// <summary>
		/// An OSD colour: the one in config if somebody chose it there (Config &gt;
		/// Messages), otherwise the theme's. The four in config default to the same
		/// values the built-in themes carry, so "still at its default" is the same
		/// question as "nobody has chosen", and a theme can move them without
		/// overriding a choice.
		/// </summary>
		private static Color Osd(int configured, int builtInDefault, ThemeColorRole role)
			=> configured == builtInDefault ? ThemeLibrary.Current[role] : Color.FromArgb(configured);

		public Color FixedMessagesColor
			=> Osd(_config.MessagesColor, DefaultMessagePositions.MessagesColor, ThemeColorRole.OsdMessage);

		public Color FixedAlertMessageColor
			=> Osd(_config.AlertMessageColor, DefaultMessagePositions.AlertMessageColor, ThemeColorRole.OsdAlert);

		private Color MovieInputColor
			=> Osd(_config.MovieInputColor, DefaultMessagePositions.MovieInputColor, ThemeColorRole.OsdMovieInput);

		private Color LastInputColor
			=> Osd(_config.LastInputColor, DefaultMessagePositions.LastInputColor, ThemeColorRole.OsdLastInput);



		private static Point GetCoordinates(IBlitter g, MessagePosition position, string message)
		{
			var size = g.MeasureString(message);
			var x = position.Anchor.IsLeft()
				? position.X * g.Scale
				: g.ClipBounds.Width - position.X * g.Scale - size.Width;

			var y = position.Anchor.IsTop()
				? position.Y * g.Scale
				: g.ClipBounds.Height - position.Y * g.Scale - size.Height;

			return new Point((int)Math.Round(x), (int)Math.Round(y));
		}

		/// <summary>
		/// The frame counter: where the machine is, over where the RUN ends.
		///
		/// The total is the last frame with anything pressed on it, not the
		/// length of the log. A log goes on past its last press - every frame
		/// recorded, seeked through or played past the end adds an empty entry
		/// - so FrameCount answers "how many entries are stored", which is not
		/// what a person reading a counter wants to know. LastNonEmptyInputFrame
		/// is where the input actually stops.
		///
		/// "(Finished)" is asked of the FRAME, not of MovieMode.Finished, which
		/// is a different question: that mode is set by MovieEndAction when
		/// PLAYBACK reaches the end, so it never arrives while recording, and
		/// not at all when the end action is Record or Stop. This is simply "we
		/// are past the last input frame", whatever the mode.
		///
		/// A movie that is not a TAS movie has no last-press to ask about, so it
		/// keeps the old length-and-mode answer.
		/// </summary>
		private string MakeFrameCounter()
		{
			if (_movieSession.Movie is ITasMovie tasMovie && tasMovie.IsActive())
			{
				// zero BOTH for an empty log and for one with nothing pressed
				// anywhere, so an empty movie must not read as finished
				var lastInput = tasMovie.LastNonEmptyInputFrame;
				var sb = new StringBuilder();
				sb
					.Append(_emulator.Frame)
					.Append('/')
					.Append(lastInput);
				if (tasMovie.InputLogLength > 0 && _emulator.Frame > lastInput)
				{
					sb.Append(" (Finished)");
				}

				return sb.ToString();
			}

			if (_movieSession.Movie.IsFinished())
			{
				var sb = new StringBuilder();
				sb
					.Append(_emulator.Frame)
					.Append('/')
					.Append(_movieSession.Movie.FrameCount)
					.Append(" (Finished)");
				return sb.ToString();
			}

			if (_movieSession.Movie.IsPlayingOrFinished())
			{
				var sb = new StringBuilder();
				sb
					.Append(_emulator.Frame)
					.Append('/')
					.Append(_movieSession.Movie.FrameCount);

				return sb.ToString();
			}

			return _emulator.Frame.ToString();
		}

		private readonly List<UIMessage> _messages = new(5);
		private readonly List<UIDisplay> _guiTextList = [ ];
		private readonly List<UIDisplay> _ramWatchList = [ ];

		/// <summary>Clears the queue used by <see cref="AddMessage"/>. You probably don't want to do this.</summary>
		public void ClearRegularMessages()
			=> _messages.Clear();

		[Obsolete("use via IDialogParent.AddOnScreenMessage")]
		public void AddMessage(string message, [LiteralExpected] int? duration = null)
			=> _messages.Add(new() {
				Message = message,
				ExpireAt = DateTime.Now + TimeSpan.FromSeconds(Math.Max(_config.OSDMessageDuration, duration ?? 0)),
			});

		public void ClearRamWatches()
			=> _ramWatchList.Clear();

		public void AddRamWatch(string message, MessagePosition pos, Color backGround, Color foreColor)
		{
			_ramWatchList.Add(new UIDisplay
			{
				Message = message,
				Position = pos,
				BackGround = backGround,
				ForeColor = foreColor,
			});
		}

		public void AddGuiText(string message, MessagePosition pos, Color backGround, Color foreColor)
		{
			_guiTextList.Add(new UIDisplay
			{
				Message = message,
				Position = pos,
				BackGround = backGround,
				ForeColor = foreColor,
			});
		}

		public void ClearGuiText()
			=> _guiTextList.Clear();

		private void DrawMessage(IBlitter g, UIMessage message, int yOffset)
		{
			var point = GetCoordinates(g, _config.Messages, message.Message);
			var y = point.Y + yOffset; // TODO: clean me up
			g.DrawString(message.Message, FixedMessagesColor, point.X, y);
		}

		public void DrawMessages(IBlitter g)
		{
			if (!_config.DisplayMessages)
			{
				return;
			}

			_messages.RemoveAll(m => DateTime.Now > m.ExpireAt);

			if (_messages.Count is not 0)
			{
				if (_config.StackOSDMessages)
				{
					var line = 1;
					for (var i = _messages.Count - 1; i >= 0; i--, line++)
					{
						var yOffset = (int)Math.Round((line - 1) * 18 * g.Scale);
						if (!_config.Messages.Anchor.IsTop())
						{
							yOffset = 0 - yOffset;
						}

						DrawMessage(g, _messages[i], yOffset);
					}
				}
				else
				{
					var message = _messages[^1];
					DrawMessage(g, message, 0);
				}
			}

			foreach (var text in _guiTextList.Concat(_ramWatchList))
			{
				try
				{
					var point = GetCoordinates(g, text.Position, text.Message);
					if (point.Y >= g.ClipBounds.Height) continue; // simple optimisation; don't bother drawing off-screen
					g.DrawString(text.Message, text.ForeColor, point.X, point.Y);
				}
				catch (Exception)
				{
					return;
				}
			}
		}

		private string InputStrMovie()
		{
			var state = _movieSession.Movie?.GetInputState(_emulator.Frame - 1);
			return state is not null ? MakeStringFor(state) : "";
		}

		private string InputStrCurrent()
			=> MakeStringFor(_movieSession.MovieIn);

		// returns an input string for inputs pressed solely by the sticky controller
		private string InputStrSticky()
			=> MakeStringFor(_movieSession.MovieIn.And(_inputManager.StickyController));

		private static string MakeStringFor(IController controller)
		{
			return InputDisplayGenerator.Generate(controller);
		}

		private string MakeIntersectImmediatePrevious()
		{
			if (_movieSession.Movie.IsRecording())
			{
				var movieInput = _movieSession.Movie.GetInputState(_emulator.Frame - 1);
				return MakeStringFor(_movieSession.MovieIn.And(movieInput));
			}

			return "";
		}

		public string MakeRerecordCount()
		{
			return _movieSession.Movie.IsActive()
				? _movieSession.Movie.Rerecords.ToString()
				: "";
		}

		private static void DrawOsdMessage(IBlitter g, string message, Color color, int x, int y)
			=> g.DrawString(message, color, x, y);

		/// <summary>
		/// Display all screen info objects like fps, frame counter, lag counter, and input display
		/// </summary>
		public void DrawScreenInfo(IBlitter g)
		{
			if (_config.DisplayFrameCounter && !_emulator.IsNull())
			{
				var message = MakeFrameCounter();
				var point = GetCoordinates(g, _config.FrameCounter, message);
				DrawOsdMessage(g, message, FixedMessagesColor, point.X, point.Y);

				if (_emulator.CanPollInput() && _emulator.AsInputPollable().IsLagFrame)
				{
					DrawOsdMessage(g, _emulator.Frame.ToString(), FixedAlertMessageColor, point.X, point.Y);
				}
			}

			if (_config.DisplayInput)
			{
				if (_movieSession.Movie.IsPlaying())
				{
					var input = InputStrMovie();
					var point = GetCoordinates(g, _config.InputDisplay, input);
					var c = MovieInputColor;
					g.DrawString(input, c, point.X, point.Y);
				}
				else // TODO: message config -- allow setting of "mixed", and "auto"
				{
					var previousColor = _movieSession.Movie.IsRecording() ? LastInputColor : MovieInputColor;
					var currentColor = FixedMessagesColor;
					var stickyColor = ThemeLibrary.Current[ThemeColorRole.OsdStickyInput];
					var currentAndPreviousColor = ThemeLibrary.Current[ThemeColorRole.OsdCurrentAndPreviousInput];

					// now, we're going to render these repeatedly, with higher priority draws overwriting all lower priority draws
					// in order of highest priority to lowest, we are effectively displaying (in different colors):
					// 1. currently pressed input that was also pressed on the previous frame (movie active + recording mode only)
					// 2. currently pressed input that is being pressed by sticky autohold or sticky autofire
					// 3. currently pressed input by the user (non-sticky)
					// 4. input that was pressed on the previous frame (movie active only)

					var previousInput = InputStrMovie();
					var currentInput = InputStrCurrent();
					var stickyInput = InputStrSticky();
					var currentAndPreviousInput = MakeIntersectImmediatePrevious();

					// calculate origin for drawing all strings. Mainly relevant when right-anchoring
					var point = GetCoordinates(g, _config.InputDisplay, currentInput);

					// draw previous input first. Currently pressed input will overwrite this
					g.DrawString(previousInput, previousColor, point.X, point.Y);
					// draw all currently pressed input with the current color (including sticky input)
					g.DrawString(currentInput, currentColor, point.X, point.Y);
					// re-draw all currently pressed sticky input with the sticky color
					g.DrawString(stickyInput, stickyColor, point.X, point.Y);
					// re-draw all currently pressed inputs that were also pressed on the previous frame in their own color
					g.DrawString(currentAndPreviousInput, currentAndPreviousColor, point.X, point.Y);
				}
			}

			if (_config.DisplayFps && Fps != null)
			{
				var point = GetCoordinates(g, _config.Fps, Fps);
				DrawOsdMessage(g, Fps, FixedMessagesColor, point.X, point.Y);
			}

			if (_config.DisplayLagCounter && _emulator.CanPollInput())
			{
				var counter = _emulator.AsInputPollable().LagCount.ToString();
				var point = GetCoordinates(g, _config.LagCounter, counter);
				DrawOsdMessage(g, counter, FixedAlertMessageColor, point.X, point.Y);
			}

			if (_config.DisplayRerecordCount)
			{
				var rerecordCount = MakeRerecordCount();
				var point = GetCoordinates(g, _config.ReRecordCounter, rerecordCount);
				DrawOsdMessage(g, rerecordCount, FixedMessagesColor, point.X, point.Y);
			}

			if (_inputManager.ClientControls["Autohold"] || _inputManager.ClientControls["Autofire"])
			{
				var sb = new StringBuilder("Held: ");

				foreach (var sticky in _inputManager.StickyHoldController.CurrentHolds)
				{
					sb.Append(sticky).Append(' ');
				}

				foreach (var autoSticky in _inputManager.StickyAutofireController.CurrentAutofires)
				{
					sb
						.Append("Auto-")
						.Append(autoSticky)
						.Append(' ');
				}

				var message = sb.ToString();
				var point = GetCoordinates(g, _config.Autohold, message);
				g.DrawString(message, ThemeLibrary.Current[ThemeColorRole.OsdAutoHold], point.X, point.Y);
			}

			if (_movieSession.Movie.IsActive() && _config.DisplaySubtitles)
			{
				var subList = _movieSession.Movie.Subtitles.GetSubtitles(_emulator.Frame);

				foreach (var sub in subList)
				{
					DrawOsdMessage(g, sub.Message, Color.FromArgb((int)sub.Color), sub.X, sub.Y);
				}
			}
		}
	}
}
