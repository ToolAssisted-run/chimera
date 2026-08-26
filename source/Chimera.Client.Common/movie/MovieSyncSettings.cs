#nullable enable

using System.Collections.Generic;

using Chimera.Emulation.Common.Waterbox;

using Newtonsoft.Json;
using Newtonsoft.Json.Linq;

namespace Chimera.Client.Common
{
	/// <summary>
	/// A movie's sync settings ARE the project's flat settings map, carried as
	/// {"Values":{...}} (TasMovie.WrapSyncSettings). This turns that text back
	/// into the one settings object every core takes. The old $type-encapsulated
	/// encoding died with the legacy movie formats; nothing decodes it any more.
	/// </summary>
	public static class MovieSyncSettings
	{
		/// <returns>null when the text holds no usable settings</returns>
		public static WaterboxCoreSyncSettings? Decode(string? json)
		{
			if (string.IsNullOrWhiteSpace(json)) return null;
			try
			{
				var root = JObject.Parse(json);
				// wrapped movie shape, or the project's bare flat map
				var values = root["Values"] as JObject ?? root;
				Dictionary<string, object> map = new();
				foreach (var prop in values.Properties())
				{
					if (prop.Value is JValue { Value: { } v }) map[prop.Name] = v;
				}
				return new WaterboxCoreSyncSettings { Values = map };
			}
			catch (JsonException)
			{
				return null;
			}
		}
	}
}
