using System.Collections.Generic;

using Chimera.Client.Common;
using Chimera.Emulation.Common.Waterbox;

using Newtonsoft.Json.Linq;

namespace Chimera.Tests.Client.Common.config
{
	/// <summary>
	/// The project was renamed from BizHawk/Chimera to Chimera, but a decade of
	/// configs and movies embed the old names - in "$type" annotations and in the
	/// per-core settings keys. These pin the promise that all of them keep
	/// loading, forever.
	/// </summary>
	[TestClass]
	public class LegacyNameCompatTests
	{
		[TestMethod]
		public void ALegacyDollarTypeStillDeserializes()
		{
			// exactly what a pre-rename movie's SyncSettings.json carried
			const string LEGACY = @"{""$type"":""BizHawk.Emulation.Common.Waterbox.WaterboxCoreSettings, BizHawk.Emulation.Common"",""Values"":{""initFillByte"":171}}";
			var restored = JToken.Parse(LEGACY).ToObject<object>(ConfigService.Serializer);
			var settings = restored as WaterboxCoreSettings;
			Assert.IsNotNull(settings, $"legacy $type deserialized to {restored?.GetType().FullName ?? "null"}");
			Assert.AreEqual(171L, Convert.ToInt64(settings.Values["initFillByte"]));
		}

		[TestMethod]
		public void ALegacySettingsKeyStillAnswers()
		{
			Config config = new();
			// a pre-rename config remembered sync settings under the old type name
			config.CoreSettings["BizHawk.Emulation.Common.Waterbox.WaterboxCore"] =
				JToken.FromObject(new WaterboxCoreSettings { Values = new Dictionary<string, object> { ["initFillByte"] = 42 } }, ConfigService.Serializer);

			var restored = (WaterboxCoreSettings)config.GetCoreSettings(typeof(WaterboxCore), typeof(WaterboxCoreSettings));
			Assert.IsNotNull(restored, "settings filed under the legacy key were not found");
			Assert.AreEqual(42L, Convert.ToInt64(restored.Values["initFillByte"]));

			// writing them back migrates the entry to the new key
			config.PutCoreSettings(restored, typeof(WaterboxCore));
			Assert.IsFalse(config.CoreSettings.ContainsKey("BizHawk.Emulation.Common.Waterbox.WaterboxCore"), "legacy key should be migrated away on write");
			Assert.IsTrue(config.CoreSettings.ContainsKey(typeof(WaterboxCore).ToString()));
		}
	}
}
