namespace Chimera.Emulation.Common.Engine
{
	/// <summary>
	/// The core's machine died during a frame: it aborted, halted on finding its own memory
	/// corrupt, followed a wild pointer, exited, or asked for something the sandbox does not
	/// provide. The process is fine - miniBox hands control back - but the machine runs nothing
	/// more until a state is loaded, which brings back a machine that did exist. What to do
	/// about it is the frontend's choice, and none of the choices may cost the user's inputs.
	/// </summary>
	public sealed class CoreStoppedException : System.InvalidOperationException
	{
		public CoreStoppedException(string reason)
			: base("The core stopped: " + reason)
		{
			Reason = reason;
		}

		/// <summary>What the sandbox said, in one line - with the core's own last words when it wrote any.</summary>
		public string Reason { get; }
	}
}
