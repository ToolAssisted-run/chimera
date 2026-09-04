#nullable enable

using System.Collections.Generic;
using System.Linq;
using Chimera.Common.StringExtensions;
using Chimera.Emulation.Common;

namespace Chimera.Client.Common
{
	public class InputCoalescer : SimpleController
	{
		public InputCoalescer()
			: base(NullController.Instance.Definition) {} // is Definition ever read on these subclasses? --yoshi

		/// <summary>
		/// A button pressed since the last frame was read, whose release has been
		/// held back so the press is not invisible.
		/// </summary>
		private readonly HashSet<string> _pressedThisFrame = new();
		private readonly HashSet<string> _heldRelease = new();

		/// <summary>
		/// One physical button's level, with a press that has not been read yet
		/// held down until it has. The core sees LEVELS, sampled once a frame: a
		/// press and its release arriving between two samples leave the level
		/// where it started, and the core is told nothing happened. That is a
		/// click that never occurred - and a Flash movie at 12fps has frames long
		/// enough for a whole human click to fall inside one.
		///
		/// So a release waits for the frame after the press it belongs to. Every
		/// tap becomes exactly one frame of input: seen by the core, and recorded
		/// in the movie as one frame, which is what makes it replayable.
		/// </summary>
		protected void SetLevel(string button, bool state)
		{
			if (state)
			{
				Buttons[button] = true;
				_pressedThisFrame.Add(button);
				_heldRelease.Remove(button);
			}
			else if (_pressedThisFrame.Contains(button))
			{
				_heldRelease.Add(button);
			}
			else
			{
				Buttons[button] = false;
			}
		}

		/// <summary>
		/// The frame has been read: let go of the releases held for it. Called
		/// once per frame, after everything that latches from this.
		/// </summary>
		public void EndFrame()
		{
			foreach (var button in _heldRelease) Buttons[button] = false;
			_heldRelease.Clear();
			_pressedThisFrame.Clear();
		}

		/// <remarks>
		/// Plain, deliberately. Scripted input says exactly which frame it means,
		/// and a hotkey is not emulated input; only a HUMAN at a physical button
		/// can tap one between two samples, so only that path holds a release.
		/// </remarks>
		protected virtual void ProcessInput(string button, bool state)
		{
			Buttons[button] = state;
		}

		public void Receive(InputEvent ie)
		{
			var state = ie.EventType is InputEventType.Press;
			var button = ie.LogicalButton.ToString();
			ProcessInput(button, state);
			if (state) return;
			// when a button or modifier key is released, all modified key variants with it are released as well
			foreach (var k in Buttons.Keys.Where(k =>
						k.EndsWithOrdinal($"+{ie.LogicalButton.Button}") || k.StartsWithOrdinal($"{ie.LogicalButton.Button}+") || k.Contains($"+{ie.LogicalButton.Button}+"))
						.ToArray())
				Buttons[k] = false;
		}
	}

	public sealed class ControllerInputCoalescer : InputCoalescer
	{
		protected override void ProcessInput(string button, bool state)
		{
			// For controller input, we want Shift+X to register as both Shift and X (for Keyboard controllers)
			foreach (var s in Controller.SplitButtons(button)) SetLevel(s, state);
		}

		public override bool IsPressed(string button)
		{
			// Since we split all inputs into their separate physical buttons, we need to check combinations here.
			string[] buttons = Controller.SplitButtons(button);
			return buttons.All(Buttons.GetValueOrDefault);
		}
	}

	public sealed class ApiInputCoalescer : InputCoalescer
	{
		protected override void ProcessInput(string button, bool state)
		{
			// For controller input, we want Shift+X to register as both Shift and X
			foreach (var s in Controller.SplitButtons(button)) Buttons[s] = state;
			// AND as the combination
			base.ProcessInput(button, state);
		}
	}
}
