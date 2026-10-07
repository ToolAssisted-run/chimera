using System.Collections.Generic;
using System.Linq;

using Chimera.Emulation.Common;
using Chimera.Emulation.Common.Waterbox;

namespace Chimera.Tests.Client.Common
{
	/// <summary>
	/// A game core's properties without an engine, for the tools' own tests: the table is
	/// the engine's description of one (what <see cref="EngineGameProperties.Describe"/>
	/// reads), and values are text kept per element name. What the bytes mean is the
	/// engine's, tested there (source/engine/tests/test_game_properties.cpp); these tests
	/// are about what the tools do with a property, not what it holds.
	/// </summary>
	/// <remarks>Linked into Chimera.Tests.Client.GUI as well.</remarks>
	internal sealed class FakeGameProperties : IGameProperties
	{
		public IReadOnlyList<GameProperty> Properties { get; private set; }

		public IReadOnlyList<string> Problems { get; private set; }

		/// <summary>Whether the table says it is dynamic (<c>"dynamic": true</c>), as the engine's description does.</summary>
		public bool IsDynamic { get; }

		/// <summary>The table a dynamic core would list next; <see cref="Refresh"/> takes it.</summary>
		public string? NextTable { get; set; }

		/// <summary>How many times the list was asked for again.</summary>
		public int Refreshes { get; private set; }

		public void Refresh()
		{
			Refreshes++;
			if (NextTable is not null) (Properties, Problems) = EngineGameProperties.Describe(NextTable);
		}

		/// <summary>Where a dynamic table's elements are now, by name; one not here is where the table says.</summary>
		public Dictionary<string, long> Addresses { get; } = new();

		public long AddressNow(GamePropertyElement element)
			=> Addresses.TryGetValue(element.Name, out var address) ? address : element.Offset;

		/// <summary>Each element's value as text, by name; an element not here reads as "0".</summary>
		public Dictionary<string, string> Values { get; } = new();

		/// <summary>Every text set, in order: element name and text.</summary>
		public List<(string Name, string Text)> Sets { get; } = new();

		/// <summary>Elements that refuse to be set, and why.</summary>
		public Dictionary<string, string> Refusals { get; } = new();

		public FakeGameProperties(string engineTableJson)
		{
			(Properties, Problems) = EngineGameProperties.Describe(engineTableJson);
			IsDynamic = (bool?)Newtonsoft.Json.Linq.JObject.Parse(engineTableJson)["dynamic"] ?? false;
		}

		public GamePropertyElement? Find(string name)
			=> Properties.SelectMany(static p => p.Elements).FirstOrDefault(e => string.Equals(e.Name, name, System.StringComparison.OrdinalIgnoreCase))
				?? Properties.FirstOrDefault(p => string.Equals(p.Name, name, System.StringComparison.OrdinalIgnoreCase))?.Element(0);

		public GamePropertyElement? At(string domain, long address, out bool starts)
		{
			foreach (var element in Properties.Where(p => p.Domain == domain).SelectMany(static p => p.Elements))
			{
				if (address >= element.Offset && address < element.Offset + element.Property.Size)
				{
					starts = address == element.Offset;
					return element;
				}
			}
			starts = false;
			return null;
		}

		/// <summary>"" for an element that is not there now (<see cref="Addresses"/> says -1), as the engine reads one.</summary>
		public string Text(GamePropertyElement element, bool named = true)
			=> AddressNow(element) < 0 ? ""
				: Values.TryGetValue(element.Name, out var text) ? text
				: "0";

		public string? SetText(GamePropertyElement element, string text)
		{
			if (Refusals.TryGetValue(element.Name, out var why)) return why;
			Sets.Add((element.Name, text));
			Values[element.Name] = text;
			return null;
		}

		public object? Get(GamePropertyElement element)
			=> long.TryParse(Text(element), out var n) ? n : Text(element);

		public string? Set(GamePropertyElement element, object value) => SetText(element, value.ToString() ?? "");

		/// <summary>What the game's timer says; null for a core without one.</summary>
		public long? GameTimeMs { get; set; }
	}
}
