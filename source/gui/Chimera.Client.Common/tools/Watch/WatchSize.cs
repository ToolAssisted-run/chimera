namespace Chimera.Client.Common
{
	/// <summary>
	/// This enum specify the size of a <see cref="Watch"/>
	/// </summary>
	public enum WatchSize : int
	{
		/// <summary>
		/// One byte (8 bits)
		/// Use this for <see cref="ByteWatch"/>
		/// </summary>
		Byte = 1,

		/// <summary>
		/// 2 bytes (16 bits)
		/// Use this for <see cref="WordWatch"/>
		/// </summary>
		Word = 2,

		/// <summary>
		/// 4 bytes (32 bits)
		/// Use this for <see cref="DWordWatch"/>
		/// </summary>
		DWord = 4,

		/// <summary>
		/// Special case used for a separator in ram tools
		/// Use this for <see cref="SeparatorWatch"/>
		/// </summary>
		Separator = 0,

		/// <summary>
		/// A game core's property (docs/game-cores.md) that no 1-, 2- or 4-byte watch can
		/// hold as it is - 64 bits, text, bytes, a bit field, named values. Use this for
		/// <see cref="PropertyWatch"/>, which the engine reads.
		/// </summary>
		Property = 16,
	}
}
