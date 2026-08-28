using System.Collections.Generic;

namespace Chimera.Client.Common
{
	// Represents the defaults used in defctrl.json
	public class DefaultControls
	{
		public Dictionary<string, Dictionary<string, string>> AllTrollers { get; set; }
			= new Dictionary<string, Dictionary<string, string>>();

		public Dictionary<string, Dictionary<string, string>> AllTrollersAutoFire { get; set; }
			= new Dictionary<string, Dictionary<string, string>>();

		public Dictionary<string, Dictionary<string, AnalogBind>> AllTrollersAnalog { get; set; }
			= new Dictionary<string, Dictionary<string, AnalogBind>>();

		public Dictionary<string, Dictionary<string, FeedbackBind>> AllTrollersFeedbacks { get; set; }
			= new Dictionary<string, Dictionary<string, FeedbackBind>>();

		/// <summary>Copies entries from <paramref name="fallback"/> for controller names this instance has no entry for.</summary>
		public void OverlayMissingFrom(DefaultControls fallback)
		{
			foreach (var kvp in fallback.AllTrollers)
			{
				if (!AllTrollers.ContainsKey(kvp.Key)) AllTrollers[kvp.Key] = new Dictionary<string, string>(kvp.Value);
			}
			foreach (var kvp in fallback.AllTrollersAutoFire)
			{
				if (!AllTrollersAutoFire.ContainsKey(kvp.Key)) AllTrollersAutoFire[kvp.Key] = new Dictionary<string, string>(kvp.Value);
			}
			foreach (var kvp in fallback.AllTrollersAnalog)
			{
				if (!AllTrollersAnalog.ContainsKey(kvp.Key)) AllTrollersAnalog[kvp.Key] = new Dictionary<string, AnalogBind>(kvp.Value);
			}
			foreach (var kvp in fallback.AllTrollersFeedbacks)
			{
				if (!AllTrollersFeedbacks.ContainsKey(kvp.Key)) AllTrollersFeedbacks[kvp.Key] = new Dictionary<string, FeedbackBind>(kvp.Value);
			}
		}
	}
}
