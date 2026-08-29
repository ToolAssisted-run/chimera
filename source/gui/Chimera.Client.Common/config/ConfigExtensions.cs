using System.Collections.Generic;
using System.Linq;

using Chimera.Common.StringExtensions;
using Chimera.Emulation.Common;

using Newtonsoft.Json.Linq;

namespace Chimera.Client.Common
{
	public static class ConfigExtensions
	{
		private class TypeNameEncapsulator
		{
			public object o;
		}
		private static JToken Serialize(object o)
		{
			var tne = new TypeNameEncapsulator { o = o };
			return JToken.FromObject(tne, ConfigService.Serializer)["o"];

			// Maybe todo:  This code is identical to the code above, except that it does not emit the legacy "$type"
			// parameter that we no longer need here.  Leaving that in to make bisecting during this dev phase easier, and such.
			// return JToken.FromObject(o, ConfigService.Serializer);
		}
		private static object Deserialize(JToken j, Type type)
		{
			try
			{
				return j?.ToObject(type, ConfigService.Serializer);
			}
			catch
			{
				// presumably some sort of config mismatch.  Anywhere we can expose this usefully?
				return null;
			}
		}

		/// <summary>
		/// Returns the core settings for a core
		/// </summary>
		/// <returns>null if no settings were saved, or there was an error deserializing</returns>
		public static object GetCoreSettings(this Config config, Type coreType, Type settingsType)
		{
			_ = config.CoreSettings.TryGetValue(coreType.ToString(), out var j);
			return Deserialize(j, settingsType);
		}

		/// <summary>
		/// Returns the core settings for a core
		/// </summary>
		/// <returns>null if no settings were saved, or there was an error deserializing</returns>
		public static TSetting GetCoreSettings<TCore, TSetting>(this Config config)
			where TCore : IEmulator
		{
			return (TSetting)config.GetCoreSettings(typeof(TCore), typeof(TSetting));
		}

		/// <summary>
		/// saves the core settings for a core
		/// </summary>
		/// <param name="o">null to remove settings for that core instead</param>
		public static void PutCoreSettings(this Config config, object o, Type coreType)
		{
			if (o != null)
			{
				config.CoreSettings[coreType.ToString()] = Serialize(o);
			}
			else
			{
				config.CoreSettings.Remove(coreType.ToString());
			}
		}

		public static void ReplaceKeysInBindings(this Config config, IReadOnlyDictionary<string, string> replMap)
		{
			string ReplMulti(string multiBind)
				=> multiBind.TransformFields(',', bind => bind.TransformFields('+', button => replMap.TryGetValue(button, out var repl) ? repl : button));
			foreach (var k in config.HotkeyBindings.Keys.ToList()) config.HotkeyBindings[k] = ReplMulti(config.HotkeyBindings[k]);
			foreach (var bindCollection in new[] { config.AllTrollers, config.AllTrollersAutoFire }) // analog and feedback binds can only be bound to (host) gamepads, not keyboard
			{
				foreach (var k in bindCollection.Keys.ToArray()) bindCollection[k] = bindCollection[k].ToDictionary(static kvp => kvp.Key, kvp => ReplMulti(kvp.Value));
			}
		}
	}
}
