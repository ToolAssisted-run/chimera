#nullable disable

using System.Threading;

namespace Chimera.Emulation.Common
{
	[Core("NullCore", "")]
	[ServiceNotApplicable(
		typeof(IVideoProvider),
		typeof(IBoardInfo),
		typeof(ICodeDataLogger),
		typeof(IDebuggable),
		typeof(IDisassemblable),
		typeof(IInputPollable),
		typeof(IMemoryDomains),
		typeof(IRegionable),
		typeof(ISettable<>),
		typeof(ISoundProvider),
		typeof(IStatable),
		typeof(ITraceable)
	)]
	public class NullEmulator : IEmulator
	{
		public NullEmulator()
		{
			ServiceProvider = new BasicServiceProvider(this);
		}

		public IEmulatorServiceProvider ServiceProvider { get; }

		public ControllerDefinition ControllerDefinition => NullController.Instance.Definition;

		public bool FrameAdvance(IController controller, bool render, bool renderSound)
		{
			// real cores wouldn't do something like this, but this just keeps speed reasonable
			// if all throttles are off
			Thread.Sleep(5);
			return true;
		}

		public int Frame => 0;

		/// <summary>
		/// What <see cref="SystemId"/> says when no machine is running. Not a
		/// system: the word for there being none. (It is also half of a name
		/// config files already hold, <c>Global_NULL</c>, so it cannot change.)
		/// </summary>
		public const string NullSystemId = "NULL";

		public string SystemId => NullSystemId;

		public bool DeterministicEmulation => true;

		public void ResetCounters()
		{
		}

		public void Dispose()
		{
		}
	}
}
