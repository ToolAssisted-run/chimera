using Chimera.Client.Common;
using Chimera.Client.GUI;

namespace Chimera.Tests.Client.GUI
{
	/// <summary>
	/// The Greenzone box's choice travels in the project (issue #158): TAStudio's
	/// part of the .chimeraProject is the MovieClientSettings written here, and a
	/// project reopened reads it back. The user decided on the project file
	/// (2026-09-28); it used to open on every frame whatever it was saved with.
	/// </summary>
	[TestClass]
	public class GreenzoneChoiceSavedTests
	{
		/// <summary>Every frame, one in N and off each come back as they went in.</summary>
		[TestMethod]
		public void TheChoiceRoundTripsThroughTheLayout()
		{
			foreach (var period in new[] { 0, 1, 7, 999 })
			{
				var saved = ConfigService.SaveWithType(new TAStudio.MovieClientSettings { GreenzonePeriod = period });
				var loaded = (TAStudio.MovieClientSettings)ConfigService.LoadWithType(saved);
				Assert.AreEqual(period, loaded.GreenzonePeriod, $"saved as {period}");
			}
		}

		/// <summary>
		/// A project saved before the choice was kept has a layout without it, and
		/// opens storing every frame - what every project used to open on.
		/// </summary>
		[TestMethod]
		public void ALayoutFromBeforeOpensOnEveryFrame()
		{
			var saved = ConfigService.SaveWithType(new TAStudio.MovieClientSettings { GreenzonePeriod = 7 });
			var older = saved.Replace("\"GreenzonePeriod\":7", "").Replace(",}", "}").Replace("{,", "{");
			StringAssert.DoesNotMatch(older, new System.Text.RegularExpressions.Regex("GreenzonePeriod"));
			var loaded = (TAStudio.MovieClientSettings)ConfigService.LoadWithType(older);
			Assert.AreEqual(1, loaded.GreenzonePeriod);
		}
	}
}
