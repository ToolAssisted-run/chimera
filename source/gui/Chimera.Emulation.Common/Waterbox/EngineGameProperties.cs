#nullable enable

using System.Collections.Generic;
using System.Globalization;
using System.Linq;

using Chimera.Emulation.Common.Engine;

using Newtonsoft.Json.Linq;

namespace Chimera.Emulation.Common.Waterbox
{
	/// <summary>
	/// A game core's properties (docs/game-cores.md) over the engine, which owns what
	/// their bytes mean: this lists the table the engine understood and forwards every
	/// read and write to it (engine.h, ce_session_property_*).
	/// </summary>
	public sealed class EngineGameProperties : IGameProperties
	{
		private readonly EngineSession _session;

		public IReadOnlyList<GameProperty> Properties { get; private set; }

		public IReadOnlyList<string> Problems { get; private set; }

		public bool IsDynamic { get; }

		public EngineGameProperties(EngineSession session)
		{
			_session = session;
			IsDynamic = session.PropertyDynamic;
			(Properties, Problems) = Describe(session.PropertyTableJson);
		}

		public void Refresh()
		{
			if (!IsDynamic || _session.Disposed) return;
			_session.PropertyRefresh();
			(Properties, Problems) = Describe(_session.PropertyTableJson);
		}

		public long AddressNow(GamePropertyElement element)
			=> !IsDynamic ? element.Offset
				: _session.Disposed ? -1
				: _session.PropertyOffset(element.Property.Index, (uint)element.Index);

		/// <summary>The engine's description of its table, as the frontend lists it.</summary>
		public static (IReadOnlyList<GameProperty> Properties, IReadOnlyList<string> Problems) Describe(string tableJson)
		{
			List<GameProperty> properties = new();
			List<string> problems = new();
			if (string.IsNullOrWhiteSpace(tableJson)) return (properties, problems);
			var root = JObject.Parse(tableJson);
			var dynamic = (bool?)root["dynamic"] ?? false;
			var index = 0;
			foreach (var p in root["properties"] as JArray ?? new JArray())
			{
				Dictionary<long, string> values = new();
				foreach (var v in (p["values"] as JObject ?? new JObject()).Properties())
				{
					if (long.TryParse(v.Name, NumberStyles.Integer, CultureInfo.InvariantCulture, out var key)) values[key] = (string?)v.Value ?? "";
				}
				properties.Add(new GameProperty
				{
					Index = index++,
					Name = (string?)p["name"] ?? "",
					Domain = (string?)p["domain"] ?? "",
					Offset = (long?)p["offset"] ?? 0,
					Type = TypeOf((string?)p["type"]),
					Size = (int?)p["size"] ?? 1,
					Count = (int?)p["count"] ?? 1,
					First = (int?)p["first"] ?? 0,
					Stride = (int?)p["stride"] ?? 1,
					BigEndian = (string?)p["endian"] is "big",
					Encoding = (string?)p["encoding"] ?? "",
					Bit = (int?)p["bit"] ?? 0,
					Bits = (int?)p["bits"] ?? 0,
					Group = (string?)p["group"] ?? "",
					Description = (string?)p["description"] ?? "",
					Writable = (bool?)p["writable"] ?? true,
					Dynamic = dynamic,
					Listed = (bool?)p["listed"] ?? true,
					Values = values,
				});
			}
			problems.AddRange((root["problems"] as JArray ?? new JArray()).Select(static t => (string?)t ?? ""));
			return (properties, problems);
		}

		private static GamePropertyType TypeOf(string? name)
			=> System.Enum.TryParse<GamePropertyType>(name, ignoreCase: true, out var type) ? type : GamePropertyType.Bytes;

		public GamePropertyElement? Find(string name)
		{
			if (_session.Disposed) return null;
			var index = _session.PropertyFind(name, out var element);
			// a dynamic table takes on a name it was asked for and had not listed: the
			// engine's table is then longer than the copy here
			if (IsDynamic && index >= Properties.Count) (Properties, Problems) = Describe(_session.PropertyTableJson);
			return index >= 0 && index < Properties.Count ? Properties[index].Element((int)element) : null;
		}

		public GamePropertyElement? At(string domain, long address, out bool starts)
		{
			starts = false;
			if (_session.Disposed) return null;
			var index = _session.PropertyAt(domain, address, out var element, out starts);
			return index >= 0 && index < Properties.Count ? Properties[index].Element((int)element) : null;
		}

		public string Text(GamePropertyElement element, bool named = true)
			=> _session.Disposed ? "" : _session.PropertyText(element.Property.Index, (uint)element.Index, named);

		public string? SetText(GamePropertyElement element, string text)
			=> _session.Disposed ? "the core has stopped" : _session.PropertySetText(element.Property.Index, (uint)element.Index, text);

		public object? Get(GamePropertyElement element)
			=> _session.Disposed ? null : _session.PropertyGet(element.Property.Index, (uint)element.Index);

		public string? Set(GamePropertyElement element, object value)
			=> _session.Disposed ? "the core has stopped" : _session.PropertySet(element.Property.Index, (uint)element.Index, value);

		public long? GameTimeMs => _session.Disposed ? null : _session.GameTimeMs;
	}
}
