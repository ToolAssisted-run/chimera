#nullable enable

using System.Collections.Generic;
using System.Linq;

using Newtonsoft.Json.Linq;

namespace Chimera.Client.Common
{
	/// <summary>
	/// A core package's file_slots.json: the core-owned answer to "which files
	/// does this machine take", rendered by the project wizard as empty slots
	/// with cardinality, accepted formats and tooltip help (docs/project.md).
	/// The engine validates a manifest against the same file; this parser only
	/// serves the form.
	/// </summary>
	public sealed class ProjectSlotDeclaration
	{
		/// <summary>the declaration exactly as the package wrote it, for the engine's slot gate</summary>
		public string RawJson { get; private init; } = "{}";

		public sealed class Slot
		{
			public string Id { get; init; } = "";
			public string Title { get; init; } = "";
			public int Min { get; init; }
			/// <summary>-1 = unbounded</summary>
			public int Max { get; init; } = -1;
			public IReadOnlyList<string> Formats { get; init; } = [ ];
			public string Help { get; init; } = "";

			/// <summary>
			/// True when the slot's exposedWhen condition reads only SETTINGS
			/// (directly or through combinators) - the machine decides it, and
			/// the answer cannot change while files are being picked. A wizard
			/// may hide such a slot outright; one gated by other slots' contents
			/// must stay visible (greyed), because unloading a file brings it
			/// back. False also when there is no condition at all.
			/// </summary>
			public bool SettingGatedOnly { get; init; }

			public string CardinalityText => (Min, Max) switch
			{
				(1, 1) => "exactly one",
				(0, 1) => "at most one",
				(0, -1) => "any number",
				(_, -1) => $"at least {Min}",
				_ => $"{Min} to {Max}",
			};
		}

		public IReadOnlyList<Slot> Slots { get; private init; } = [ ];

		/// <summary>groups of slot ids of which at least one file must be present overall</summary>
		public IReadOnlyList<IReadOnlyList<string>> AtLeastOneOf { get; private init; } = [ ];

		/// <returns>null when the text is not a usable declaration</returns>
		public static ProjectSlotDeclaration? Parse(string? json)
		{
			if (string.IsNullOrWhiteSpace(json)) return null;
			try
			{
				var root = JObject.Parse(json);
				if (root["slots"] is not JArray slots) return null;
				List<Slot> parsed = new();
				foreach (var item in slots.OfType<JObject>())
				{
					var id = item.Value<string>("id");
					if (string.IsNullOrEmpty(id)) return null;
					parsed.Add(new Slot
					{
						Id = id!,
						Title = item.Value<string>("title") ?? id!,
						Min = item.Value<int?>("min") ?? 0,
						Max = item.Value<int?>("max") ?? -1,
						Formats = item["formats"] is JArray formats
							? formats.Values<string>().OfType<string>().ToList()
							: [ ],
						Help = item.Value<string>("help") ?? "",
						SettingGatedOnly = item["exposedWhen"] is JToken when && ReadsOnlySettings(when),
					});
				}
				if (parsed.Count is 0) return null;
				List<IReadOnlyList<string>> groups = new();
				if (root["atLeastOneOf"] is JArray atLeast)
				{
					foreach (var group in atLeast.OfType<JArray>())
					{
						var ids = group.Values<string>().OfType<string>().ToList();
						if (ids.Count is not 0) groups.Add(ids);
					}
				}
				return new() { Slots = parsed, AtLeastOneOf = groups, RawJson = json! };
			}
			catch (Newtonsoft.Json.JsonException)
			{
				return null;
			}
		}

		/// <summary>
		/// Whether a condition tree (the language of docs/project.md) contains
		/// only setting tests: combinators may nest, but no {"slot": ...} leaf.
		/// </summary>
		private static bool ReadsOnlySettings(JToken condition)
		{
			if (condition is not JObject obj) return false;
			if (obj["slot"] is not null) return false;
			if (obj["setting"] is not null) return true;
			foreach (var key in new[] { "all", "any" })
			{
				if (obj[key] is JArray children)
					return children.All(static c => c is JObject && ReadsOnlySettings(c));
			}
			if (obj["not"] is JObject sub) return ReadsOnlySettings(sub);
			return false;
		}

		/// <summary>The wizard's file-picker filter for one slot ("*.img;*.ima"), or null for any file.</summary>
		public static string? FilterFor(Slot slot)
			=> slot.Formats.Count is 0 ? null : string.Join(";", slot.Formats.Select(static f => $"*.{f}"));
	}
}
