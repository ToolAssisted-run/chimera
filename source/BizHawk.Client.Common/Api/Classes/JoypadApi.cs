using System.Collections.Generic;

using BizHawk.Emulation.Common;

namespace BizHawk.Client.Common
{
	public sealed class JoypadApi : IJoypadApi
	{
		private readonly InputManager _inputManager;

		private readonly IMovieSession _movieSession;

		private readonly Action<string> LogCallback;

		public JoypadApi(Action<string> logCallback, InputManager inputManager, IMovieSession movieSession)
		{
			LogCallback = logCallback;
			_inputManager = inputManager;
			_movieSession = movieSession;
		}

		public IReadOnlyDictionary<string, object> Get(int? controller = null)
		{
			return _movieSession.MovieIn.ToDictionary(controller);
		}

		public IReadOnlyDictionary<string, object> GetWithMovie(int? controller = null)
		{
			return _inputManager.ControllerOutput.ToDictionary(controller);
		}

		public IReadOnlyDictionary<string, object> GetImmediate(int? controller = null)
		{
			return _inputManager.ActiveController.ToDictionary(controller);
		}

		public void SetFromMnemonicStr(string inputLogEntry)
		{
			var controller = _movieSession.GenerateMovieController(_inputManager.ActiveController.Definition);
			try
			{
				controller.SetFromMnemonic(inputLogEntry);
			}
			catch (Exception)
			{
				LogCallback($"invalid mnemonic string: {inputLogEntry}");
				return;
			}
			foreach (var button in controller.Definition.BoolButtons) _inputManager.ButtonOverrideAdapter.SetButton(button, controller.IsPressed(button));
			foreach (var axis in controller.Definition.Axes.Keys) _inputManager.ButtonOverrideAdapter.SetAxis(axis, controller.AxisValue(axis));
		}

		public void Set(IReadOnlyDictionary<string, bool> buttons, int? controller = null)
		{
			// If a controller is specified, we need to iterate over unique button names. If not, we iterate over
			// ALL button names with P{controller} prefixes
			//
			// Record every button first, then re-latch and apply the overrides ONCE.
			// Doing that per button (as calling Set in the loop would) runs the whole
			// input pipeline N times per frame for an N-button definition, and each
			// pass is itself proportional to N - quadratic, and very visible on a
			// 34-button definition driven from a script every frame. The end state is
			// the same either way: the loop's last iteration is what survives.
			try
			{
				foreach (var button in _inputManager.ActiveController.ToBoolButtonNameList(controller))
				{
					var buttonToSet = controller == null ? button : $"P{controller} {button}";
					if (buttons.TryGetValue(button, out var state)) _inputManager.ButtonOverrideAdapter.SetButton(buttonToSet, state);
					else _inputManager.ButtonOverrideAdapter.UnSet(buttonToSet);
				}

				_inputManager.ActiveController.LatchFromPhysical(_inputManager.ControllerInputCoalescer);
				_inputManager.ActiveController.Overrides(_inputManager.ButtonOverrideAdapter);
			}
			catch
			{
				// ignored, as in the single-button setter
			}
		}

		public void Set(string button, bool? state = null, int? controller = null)
		{
			try
			{
				var buttonToSet = controller == null ? button : $"P{controller} {button}";
				if (state == null) _inputManager.ButtonOverrideAdapter.UnSet(buttonToSet);
				else _inputManager.ButtonOverrideAdapter.SetButton(buttonToSet, state.Value);

				//"Overrides" is a gross line of code in that flushes overrides into the current controller.
				//That's not really the way it was meant to work which was that it should pull all its values through the filters before ever using them.
				//Of course the code that does that is in the main loop and the lua API wouldnt know how to do it.
				//I regret the whole hotkey filter chain OOP soup approach. Anyway, the code that

				//in a crude, CRUDE, *CRUDE* approximation of what the main loop does, we need to pull the physical input again before it's freshly overridded
				//but really, everything the main loop does needs to be done here again.
				//I'm not doing that now.
				_inputManager.ActiveController.LatchFromPhysical(_inputManager.ControllerInputCoalescer);

				//and here's where the overrides managed by this API are pushed in
				_inputManager.ActiveController.Overrides(_inputManager.ButtonOverrideAdapter);
			}
			catch
			{
				// ignored
			}
		}

		/// <summary>
		/// The analog counterpart of <see cref="Set(string,bool?,int?)"/>: pushes the
		/// value through the override adapter, so it reaches the core THIS frame.
		/// <see cref="SetAnalog(string,int?,int?)"/> is a different thing - a sticky
		/// autohold, which never reaches the output controller from a script.
		/// </summary>
		public void SetAxis(string control, int value, int? controller = null)
		{
			try
			{
				_inputManager.ButtonOverrideAdapter.SetAxis(controller == null ? control : $"P{controller} {control}", value);
				_inputManager.ActiveController.LatchFromPhysical(_inputManager.ControllerInputCoalescer);
				_inputManager.ActiveController.Overrides(_inputManager.ButtonOverrideAdapter);
			}
			catch
			{
				// ignored, as with the other setters
			}
		}

		public void SetAnalog(IReadOnlyDictionary<string, int?> controls, int? controller = null)
		{
			foreach (var (k, v) in controls) SetAnalog(k, v, controller);
		}

		public void SetAnalog(string control, int? value = null, int? controller = null)
		{
			try
			{
				_inputManager.StickyHoldController.SetAxisHold(controller == null ? control : $"P{controller} {control}", value);
			}
			catch
			{
				// ignored
			}
		}
	}
}
