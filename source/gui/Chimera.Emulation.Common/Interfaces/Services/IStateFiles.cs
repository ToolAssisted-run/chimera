namespace Chimera.Emulation.Common
{
	/// <summary>
	/// A core that can put its whole state in a file, and take it back, without the state ever
	/// being one array in between. <see cref="IStatable"/> hands a state over as bytes in a
	/// stream the caller holds, and a managed array stops at 2 GiB; a PS3's state does not
	/// (issue #84: creating a TAStudio branch threw an overflow). The engine streams the machine
	/// through zstd to the file instead, so there is no size it cannot be.
	/// </summary>
	public interface IStateFiles : IEmulatorService
	{
		/// <exception cref="System.InvalidOperationException">the state could not be written; the message says why</exception>
		void SaveStateToFile(string path);

		/// <exception cref="System.InvalidOperationException">the machine refused the state, or the file is not one</exception>
		void LoadStateFromFile(string path);
	}
}
