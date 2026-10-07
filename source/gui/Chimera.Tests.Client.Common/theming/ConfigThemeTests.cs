using Chimera.Client.Common;

namespace Chimera.Tests.Client.Common
{
	/// <summary>
	/// Which theme a config ends up with, which is not the same question as which
	/// theme is the default.
	///
	/// A new config gets Dark, because that is what was asked for. A config that
	/// already existed and says nothing about themes was written before there were
	/// any, by somebody who has been looking at the light one for as long as they
	/// have used Chimera - changing their colours under them on an update is a
	/// surprise, not a feature, so they keep it. Either way the answer is written
	/// down, so the question is asked once and a later choice stands.
	/// </summary>
	[TestClass]
	public class ConfigThemeTests
	{
		[TestMethod]
		public void AConfigBeingCreatedNowStartsDark()
		{
			Config config = new();
			config.ResolveTheme(configExisted: false);
			Assert.AreEqual("Dark", config.Theme);
		}

		[TestMethod]
		public void AConfigWrittenBeforeThereWereThemesKeepsTheLightOne()
		{
			Config config = new();
			config.ResolveTheme(configExisted: true);
			Assert.AreEqual("Light", config.Theme);
		}

		[TestMethod]
		public void AChoiceAlreadyMadeIsLeftAlone()
		{
			Config config = new() { Theme = "Solarized Dark" };
			config.ResolveTheme(configExisted: false);
			Assert.AreEqual("Solarized Dark", config.Theme);
			config.ResolveTheme(configExisted: true);
			Assert.AreEqual("Solarized Dark", config.Theme);
		}

		/// <summary>
		/// Settling it writes it down, so the next start does not ask again - which
		/// is what stops a config created today from turning light the first time
		/// it is loaded back from a file.
		/// </summary>
		[TestMethod]
		public void OnceSettledItStaysSettled()
		{
			Config config = new();
			config.ResolveTheme(configExisted: false);
			config.ResolveTheme(configExisted: true);
			Assert.AreEqual("Dark", config.Theme);
		}

		/// <summary>
		/// Both names have to be themes that exist, or a fresh install comes up
		/// wearing the fallback and nobody notices until somebody reads the config.
		/// </summary>
		[TestMethod]
		public void BothNamedThemesAreRealThemes()
		{
			Assert.IsNotNull(ThemeLibrary.Find(ThemeLibrary.NewConfigThemeName), $"there is no theme called {ThemeLibrary.NewConfigThemeName}");
			Assert.IsNotNull(ThemeLibrary.Find(ThemeLibrary.FallbackThemeName), $"there is no theme called {ThemeLibrary.FallbackThemeName}");
			Assert.IsTrue(ThemeLibrary.Find(ThemeLibrary.NewConfigThemeName)!.IsDark, "the theme a new config starts with is not a dark one");
			Assert.IsTrue(ThemeLibrary.Find(ThemeLibrary.FallbackThemeName)!.FollowsDesktop, "the fallback is not the desktop's own palette");
		}
	}
}
