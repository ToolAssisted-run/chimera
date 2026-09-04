using Chimera.Client.Common;


namespace Chimera.Tests.Client.Common.Input
{
	/// <summary>
	/// The core sees LEVELS, sampled once a frame. Anything that happens and
	/// un-happens between two samples is invisible to it, and a click is exactly
	/// that shape.
	/// </summary>
	[TestClass]
	public class InputCoalescerTests
	{
		private static InputEvent Press(string button)
			=> new() { EventType = InputEventType.Press, LogicalButton = new(button, 0, static () => [ ]) };

		private static InputEvent Release(string button)
			=> new() { EventType = InputEventType.Release, LogicalButton = new(button, 0, static () => [ ]) };

		[TestMethod]
		public void AClickInsideOneFrameIsStillSeen()
		{
			// A human click is 50-100ms. A Flash movie at 12fps has 83ms frames,
			// so a whole click falls inside one - it went down and came back up
			// with the core never sampling it, and the click simply vanished.
			ControllerInputCoalescer c = new();
			c.Receive(Press("WMouse L"));
			c.Receive(Release("WMouse L"));

			Assert.IsTrue(c.IsPressed("WMouse L"), "the frame that contains a click must show it pressed");

			c.EndFrame();
			Assert.IsFalse(c.IsPressed("WMouse L"), "and the frame after it must show it released");
		}

		[TestMethod]
		public void AHeldButtonIsNotStretched()
		{
			// the hold must cost a real press nothing: down stays down, and its
			// release lands on the frame it actually arrived in
			ControllerInputCoalescer c = new();
			c.Receive(Press("WMouse L"));
			c.EndFrame();
			Assert.IsTrue(c.IsPressed("WMouse L"), "still held");

			c.EndFrame();
			Assert.IsTrue(c.IsPressed("WMouse L"), "still held, several frames later");

			c.Receive(Release("WMouse L"));
			Assert.IsFalse(c.IsPressed("WMouse L"), "released in the frame it was released");
		}

		[TestMethod]
		public void ATapIsOneFrameLongNotTwo()
		{
			// one frame per tap, or a movie replays a different machine
			ControllerInputCoalescer c = new();
			c.Receive(Press("WMouse L"));
			c.Receive(Release("WMouse L"));
			c.EndFrame();
			c.EndFrame();
			Assert.IsFalse(c.IsPressed("WMouse L"), "a tap does not go on being pressed");
		}
	}
}
