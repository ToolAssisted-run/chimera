namespace Chimera.Emulation.Common
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
	/// A load that failed for a reason the user can do something about: firmware that
	/// has not been provided, a rom this core does not support, a save file that
	/// belongs to another game. The MESSAGE is the whole point - it comes from
	/// whoever knows why (usually the core itself, in its own words) and is shown as
	/// a sentence, because a stack trace tells a person nothing they can act on.
	///
	/// Anything NOT of this type is a surprise, and a surprise still gets its stack
	/// trace: that is a bug report, not a configuration problem.
	/// </summary>
	public class CoreLoadException : Exception
	{
		public CoreLoadException(string message)
			: base(message)
		{
		}
	}

	/// <summary>A core was handed a rom it can load but is missing a file the user has to provide (see <see cref="CoreFirmwareDecl"/>).</summary>
	public class MissingFirmwareException : CoreLoadException
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
