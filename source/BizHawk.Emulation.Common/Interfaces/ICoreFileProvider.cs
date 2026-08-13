namespace BizHawk.Emulation.Common
{
	/// <summary>
	/// Defines the means by which firmware, bios and other necessary files are provided to a core that needs them
	/// </summary>
	public interface ICoreFileProvider
	{
		/// <summary>
		/// produces a 'user' path for a given system id
		/// can produce an empty temp folder, suitable for movies
		/// </summary>
		string GetUserPath(string sysID, bool temp);

		/// <param name="msg">warning message to show on failure</param>
		/// <returns><see langword="null"/> iff failed</returns>
		byte[]? GetFirmware(FirmwareID id, string? msg = null);

		/// <param name="msg">exception message to show on failure</param>
		/// <exception cref="MissingFirmwareException">if not found</exception>
		byte[] GetFirmwareOrThrow(FirmwareID id, string? msg = null);

		/// <param name="msg">exception message to show on failure</param>
		/// <exception cref="MissingFirmwareException">if not found</exception>
		(byte[] FW, GameInfo Game) GetFirmwareWithGameInfoOrThrow(FirmwareID id, string? msg = null);
	}
}
