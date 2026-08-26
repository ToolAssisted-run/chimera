#nullable enable

using System.Collections.Generic;

using Chimera.Emulation.Common.Waterbox;

using Newtonsoft.Json;
using Newtonsoft.Json.Linq;

namespace Chimera.Client.Common
{
	/// <summary>
	/// A movie's settings ARE the project's flat settings map, carried as
	/// {"Values":{...}} (TasMovie.WrapSettings). This turns that text back
	/// into the one settings object every core takes. The old $type-encapsulated
	/// encoding died with the legacy movie formats; nothing decodes it any more.
	/// </summary>
	public static class MovieSettings
	{
		/// <returns>null when the text holds no usable settings</returns>
		public static WaterboxCoreSettings? Decode(string? json)
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
				return new WaterboxCoreSettings { Values = map };
			}
			catch (JsonException)
			{
				return null;
			}
		}
	}
}
