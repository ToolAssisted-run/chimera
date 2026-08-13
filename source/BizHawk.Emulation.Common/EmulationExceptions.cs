namespace BizHawk.Emulation.Common
{
	/// <summary>
	/// indicates that this core does not support the game, but it may be valid
	/// </summary>
	public class UnsupportedGameException : InvalidOperationException
	{
		public UnsupportedGameException(string message)
			: base(message)
		{
		}
	}

	public class NoAvailableCoreException : Exception
	{
		public NoAvailableCoreException(string system)
			: base($"System is currently NOT emulated: {system}")
		{
		}
	}

	/// <summary>
	/// A core was handed a rom it can load but is missing a file the user has to
	/// provide (see <see cref="CoreFirmwareDecl"/>). Its own type because it is the
	/// one load failure the user can act on, so it is shown as a sentence rather
	/// than as a stack trace.
	/// </summary>
	public class MissingFirmwareException : Exception
	{
		public MissingFirmwareException(string message)
			: base(message)
		{
		}
	}

	public class SavestateSizeMismatchException : InvalidOperationException
	{
		public SavestateSizeMismatchException(string message)
			: base(message)
		{
		}
	}

	public class InternalCoreException : Exception
	{
		public InternalCoreException(string message) : base(message)
		{
		}
	}
}
