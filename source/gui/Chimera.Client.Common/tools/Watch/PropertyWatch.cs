using System.Collections.Generic;

using Chimera.Emulation.Common;

namespace Chimera.Client.Common
{
	/// <summary>
	/// A watch on an element of a game core's property (docs/game-cores.md) that no 1-, 2-
	/// or 4-byte watch can hold as it is: 64 bits, a double, text, bytes, a bit field, or
	/// a number whose values have names. The engine reads it, shows it and parses what is
	/// poked into it, so it reads here exactly as it does in a script or the Game State.
	///
	/// Its note is the element's name and is not edited: a property is known by its name,
	/// never its offset, and that is how a watch file, a paste or a reloaded core finds it
	/// again.
	/// </summary>
	public sealed class PropertyWatch : Watch
	{
		private IGameProperties _properties;
		private string _value;
		private string _previous;

		public GamePropertyElement Element { get; private set; }

		public PropertyWatch(IGameProperties properties, GamePropertyElement element, MemoryDomain domain)
			: base(domain, element.Offset, WatchSize.Property, WatchDisplayType.Unsigned, element.Property.BigEndian, element.Name)
		{
			_properties = properties;
			Element = element;
			_value = _previous = Read();
		}

		/// <summary>The watch on the element by that name, or null when the core has no such property (or none at all).</summary>
		public static PropertyWatch Find(IGameProperties properties, IMemoryDomains domains, string name)
			=> properties?.Find(name) is { } element && domains[element.Property.Domain] is { } domain
				? new PropertyWatch(properties, element, domain)
				: null;

		/// <summary>
		/// Finds the element again in a core that was reloaded, by name; false when that core
		/// no longer has it.
		/// </summary>
		public bool Rebind(IGameProperties properties)
		{
			if (properties?.Find(Element.Name) is not { } element) return false;
			_properties = properties;
			Element = element;
			return true;
		}

		/// <summary>The value as text a poke takes back exactly - the number, not its name - which is what a freeze holds.</summary>
		public string RawText => _properties.Text(Element, named: false);

		/// <summary>Why the last <see cref="Poke"/> was refused; "" when it was not.</summary>
		public string LastPokeError { get; private set; } = "";

		/// <summary>What a property of a dynamic table reads as while the game has no such thing.</summary>
		public const string Gone = "(not there)";

		private string Read()
		{
			var text = _properties.Text(Element, named: true);
			return text.Length is 0 && _properties.IsDynamic && _properties.AddressNow(Element) < 0 ? Gone : text;
		}

		/// <summary>
		/// Where it is now: a dynamic table's property (a movie's variable) moves, and the
		/// engine follows it by name; "-" while it is not there.
		/// </summary>
		public override string AddressString
			=> !_properties.IsDynamic ? base.AddressString
				: _properties.AddressNow(Element) is >= 0 and var now ? FormatAddress(now)
				: "-";

		public override int ByteSize => Element.Property.Size;

		public override IReadOnlyList<WatchDisplayType> AvailableTypes() => [ WatchDisplayType.Unsigned ];

		public override void ResetPrevious() => _previous = _value = Read();

		public override void Update(PreviousType previousType)
		{
			switch (previousType)
			{
				case PreviousType.Original:
					return;
				case PreviousType.LastChange:
					var before = _value;
					_value = Read();
					if (_value != before)
					{
						_previous = before;
						ChangeCount++;
					}
					break;
				case PreviousType.LastFrame:
					_previous = _value;
					_value = Read();
					if (_value != _previous) ChangeCount++;
					break;
			}
		}

		public override string Diff => "";

		public override uint MaxValue => uint.MaxValue;

		/// <summary>The low 32 bits of a whole number, for sorting; 0 for anything else.</summary>
		public override int Value
			=> _properties.Get(Element) switch
			{
				long l => unchecked((int)l),
				ulong u => unchecked((int)u),
				bool b => b ? 1 : 0,
				_ => 0,
			};

		public override string ValueString => Read();

		public override bool IsValid => Domain is not null;

		/// <summary>Sets the element from text as the engine parses it; false, with <see cref="LastPokeError"/>, when refused.</summary>
		public override bool Poke(string value)
		{
			LastPokeError = _properties.SetText(Element, value) ?? "";
			if (LastPokeError.Length is not 0) return false;
			_value = Read();
			return true;
		}

		public override uint Previous => 0;

		public override string PreviousStr => _previous;
	}
}
