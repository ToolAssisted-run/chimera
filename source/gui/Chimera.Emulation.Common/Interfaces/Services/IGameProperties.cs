#nullable enable

using System.Collections.Generic;
using System.Linq;

namespace Chimera.Emulation.Common
{
	/// <summary>A game property's type, as the table spells it (docs/game-cores.md).</summary>
	public enum GamePropertyType
	{
		U8,
		S8,
		U16,
		S16,
		U32,
		S32,
		U64,
		S64,
		F32,
		F64,

		/// <summary>One byte, 0 false and anything else true.</summary>
		Bool,

		/// <summary>Text in a fixed number of bytes, ended early by a NUL.</summary>
		String,

		/// <summary>A fixed number of raw bytes.</summary>
		Bytes,
	}

	/// <summary>
	/// One of a game core's properties as the engine understood its table: a named
	/// place in one of the core's memory domains, of one type, or an array of them
	/// (docs/game-cores.md). Only a description - what its bytes mean is the engine's
	/// to say, through <see cref="IGameProperties"/>.
	/// </summary>
	public sealed class GameProperty
	{
		/// <summary>Its place in the engine's table, which is how the engine is asked about it.</summary>
		public int Index { get; init; }

		/// <summary>Unique within the core, and what watches, freezes and scripts keep - never the offset.</summary>
		public string Name { get; init; } = "";

		/// <summary>The memory domain it lives in.</summary>
		public string Domain { get; init; } = "";

		/// <summary>Where in the domain its first element is, in bytes.</summary>
		public long Offset { get; init; }

		public GamePropertyType Type { get; init; }

		/// <summary>How many bytes one element takes.</summary>
		public int Size { get; init; } = 1;

		/// <summary>How many elements; 1 for a property that is not an array.</summary>
		public int Count { get; init; } = 1;

		/// <summary>The number an array's first element is called by: 0, or 1 when the game counts rooms from 1.</summary>
		public int First { get; init; }

		/// <summary>Bytes from one element to the next.</summary>
		public int Stride { get; init; } = 1;

		public bool BigEndian { get; init; }

		/// <summary>A string's encoding ("ascii", "latin1", "utf8", "utf16le"); "" for any other type.</summary>
		public string Encoding { get; init; } = "";

		/// <summary>A bit field's lowest bit, when <see cref="Bits"/> is not 0.</summary>
		public int Bit { get; init; }

		/// <summary>A bit field's width; 0 for the whole value.</summary>
		public int Bits { get; init; }

		/// <summary>For listing ("Kid", "Guard"); "" when the core gave none.</summary>
		public string Group { get; init; } = "";

		/// <summary>One line for a tooltip; "" when the core gave none.</summary>
		public string Description { get; init; } = "";

		/// <summary>False for what the game works out afresh every step, and for a domain that cannot be written.</summary>
		public bool Writable { get; init; } = true;

		/// <summary>
		/// One of a table the core makes anew when asked (a movie's variables): where it is
		/// can change from one frame to the next, so <see cref="Offset"/> is only where it
		/// was when the table was last read and <see cref="IGameProperties.AddressNow"/>
		/// says where it is.
		/// </summary>
		public bool Dynamic { get; init; }

		/// <summary>False for a dynamic table's property that the core's latest list no longer has; it keeps its name and its index.</summary>
		public bool Listed { get; init; } = true;

		/// <summary>Names for an enumeration's values, shown instead of the number. Empty for a plain number.</summary>
		public IReadOnlyDictionary<long, string> Values { get; init; } = new Dictionary<long, string>();

		public bool IsArray => Count > 1;

		public bool IsBitField => Bits is not 0;

		/// <summary>The type as the table spells it (<c>u16</c>, <c>string</c>).</summary>
		public string TypeName => Type.ToString().ToLowerInvariant();

		/// <summary>The type the way a list shows it: <c>s16[5]</c>, <c>string(12)</c>, <c>u8:1</c>.</summary>
		public string TypeText
			=> TypeName
				+ (Type is GamePropertyType.String or GamePropertyType.Bytes ? $"({Size})" : "")
				+ (IsBitField ? $":{Bits}" : "")
				+ (IsArray ? $"[{Count}]" : "");

		/// <summary>
		/// Whether an element fits one of the watch tools' own 1-, 2- or 4-byte watches
		/// as it is: an integer, an f32 or a bool of that width, not a bit field and
		/// without names for its values - which a plain watch would not show. Anything
		/// else is watched through the engine - and so is anything in a dynamic table,
		/// which a watch at a fixed address would lose the first time it moved.
		/// </summary>
		public bool FitsAPlainWatch
			=> !Dynamic
				&& Type is GamePropertyType.U8 or GamePropertyType.S8 or GamePropertyType.U16 or GamePropertyType.S16
						or GamePropertyType.U32 or GamePropertyType.S32 or GamePropertyType.F32 or GamePropertyType.Bool
				&& !IsBitField
				&& Values.Count is 0;

		public GamePropertyElement Element(int element) => new(this, element);

		/// <summary>Every element, in order; the property itself for one that is not an array.</summary>
		public IEnumerable<GamePropertyElement> Elements => Enumerable.Range(0, Count).Select(Element);

		public override string ToString() => Name;
	}

	/// <summary>
	/// One element of a property - the property itself when it is not an array - which
	/// is what a watch, a freeze or a script works on. <see cref="Index"/> counts from 0;
	/// the name uses the number the game calls the element by, from the array's
	/// <see cref="GameProperty.First"/>: <c>Guards.X[2]</c>, <c>Rooms.Left[24]</c>.
	/// </summary>
	public sealed record GamePropertyElement(GameProperty Property, int Index)
	{
		public string Name => Property.IsArray ? $"{Property.Name}[{Property.First + Index}]" : Property.Name;

		/// <summary>Where the element starts in its domain.</summary>
		public long Offset => Property.Offset + ((long)Index * Property.Stride);

		public override string ToString() => Name;
	}

	/// <summary>
	/// A game core's properties (docs/game-cores.md), which the engine reads, writes,
	/// shows and parses by one set of rules. Offered only by a core whose table names at
	/// least one, or whose table is dynamic; the tools that name addresses ask for it
	/// optionally.
	/// </summary>
	public interface IGameProperties : ISpecializedEmulatorService
	{
		/// <summary>
		/// Every property, in the order the core listed them. A dynamic table's is the list
		/// as of the last <see cref="Refresh"/>, plus whatever was asked for by name since.
		/// </summary>
		IReadOnlyList<GameProperty> Properties { get; }

		/// <summary>
		/// Whether the core makes the table anew when asked (a movie's variables, which come,
		/// move and go while it runs) instead of once for good.
		/// </summary>
		bool IsDynamic { get; }

		/// <summary>Has a dynamic table listed again; nothing for any other.</summary>
		void Refresh();

		/// <summary>Where the element is in its domain now; -1 for a dynamic table's that is not there.</summary>
		long AddressNow(GamePropertyElement element);

		/// <summary>What in the core's table was left out, and why; empty for a sound table.</summary>
		IReadOnlyList<string> Problems { get; }

		/// <summary>"Name" or "Name[3]", any case; null for a name there is not.</summary>
		GamePropertyElement? Find(string name);

		/// <summary>
		/// The element whose bytes include <paramref name="address"/> in <paramref name="domain"/>, or
		/// null; <paramref name="starts"/> says whether the address is its first byte.
		/// </summary>
		GamePropertyElement? At(string domain, long address, out bool starts);

		/// <summary>The value as a person reads it: an enumeration's name when <paramref name="named"/> and it has one.</summary>
		string Text(GamePropertyElement element, bool named = true);

		/// <summary>Sets the value from text, the inverse of <see cref="Text"/>; null when set, else why not.</summary>
		string? SetText(GamePropertyElement element, string text);

		/// <summary>The value: a long (a signed integer), a ulong, a double, a bool, a string or a byte[].</summary>
		object? Get(GamePropertyElement element);

		/// <summary>Sets the value from any of the kinds <see cref="Get"/> gives; null when set, else why not.</summary>
		string? Set(GamePropertyElement element, object value);

		/// <summary>
		/// The game's own elapsed time in milliseconds, as the game counts it now (the table's
		/// "gameTimer", docs/game-cores.md); null when the core names no timer.
		/// </summary>
		long? GameTimeMs { get; }
	}
}
