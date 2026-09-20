using System;
using System.Collections.Generic;
using System.Drawing;
using System.IO;
using System.Linq;

using Chimera.Client.Common;

namespace Chimera.Tests.Client.Common
{
	/// <summary>
	/// The file format itself: what a theme file may say, and what happens when it
	/// says something the build cannot use. The rule the whole feature rests on is
	/// that a theme is taken whole or refused whole - there is no half-applied
	/// theme, because a window that is half dark is worse than one that is not
	/// dark at all and much harder to report.
	/// </summary>
	[TestClass]
	public class ThemeFileTests
	{
		private static string Colours(params (ThemeColorRole Role, string Value)[] overrides)
		{
			Dictionary<ThemeColorRole, string> all = new();
			foreach (var role in Theme.AllRoles) all[role] = "#123456";
			foreach (var (role, value) in overrides) all[role] = value;
			return string.Join(",\n", all.Select(kvp => $"\t\t\"{kvp.Key}\": \"{kvp.Value}\""));
		}

		private static string Complete(string name = "Test", string extra = "")
			=> "{\n"
				+ $"\t\"name\": \"{name}\",\n"
				+ extra
				+ "\t\"colors\": {\n" + Colours() + "\n\t}\n}";

		[TestMethod]
		public void ACompleteFileLoads()
		{
			var theme = ThemeFile.Parse(Complete(), "test");
			Assert.AreEqual("Test", theme.Name);
			Assert.AreEqual(Color.FromArgb(0x12, 0x34, 0x56), theme[ThemeColorRole.WindowBackground]);
		}

		[TestMethod]
		public void AMissingColourIsRefusedAndNamed()
		{
			var json = Complete().Replace($"\t\t\"{ThemeColorRole.WindowText}\": \"#123456\",\n", "");
			var ex = Assert.ThrowsException<ThemeFormatException>(() => ThemeFile.Parse(json, "mytheme.json"));
			StringAssert.Contains(ex.Message, nameof(ThemeColorRole.WindowText));
			StringAssert.Contains(ex.Message, "mytheme.json");
			StringAssert.Contains(ex.Message, "basedOn");
		}

		[TestMethod]
		public void AMisspelledColourIsRefusedWithTheNameItMeant()
		{
			var json = Complete().Replace($"\"{ThemeColorRole.WindowText}\"", "\"WindowTxet\"");
			var ex = Assert.ThrowsException<ThemeFormatException>(() => ThemeFile.Parse(json, "t"));
			StringAssert.Contains(ex.Message, "WindowTxet");
			StringAssert.Contains(ex.Message, nameof(ThemeColorRole.WindowText));
		}

		[TestMethod]
		public void AColourThatIsNotAColourIsRefused()
		{
			foreach (var bad in new[] { "blue", "#12345", "#GGGGGG", "", "rgb(1,2,3)" })
			{
				var json = Complete().Replace($"\"{ThemeColorRole.WindowText}\": \"#123456\"", $"\"{ThemeColorRole.WindowText}\": \"{bad}\"");
				var ex = Assert.ThrowsException<ThemeFormatException>(() => ThemeFile.Parse(json, "t"), $"\"{bad}\" was accepted as a colour");
				StringAssert.Contains(ex.Message, nameof(ThemeColorRole.WindowText));
			}
		}

		[TestMethod]
		public void AFileWithNoNameIsRefused()
		{
			var json = Complete().Replace("\t\"name\": \"Test\",\n", "");
			Assert.ThrowsException<ThemeFormatException>(() => ThemeFile.Parse(json, "t"));
		}

		[TestMethod]
		public void AStraySettingIsRefused()
		{
			var json = Complete(extra: "\t\"darkmode\": true,\n");
			var ex = Assert.ThrowsException<ThemeFormatException>(() => ThemeFile.Parse(json, "t"));
			StringAssert.Contains(ex.Message, "darkmode");
		}

		[TestMethod]
		public void BrokenJsonIsRefusedWithoutThrowingSomethingElse()
		{
			var ex = Assert.ThrowsException<ThemeFormatException>(() => ThemeFile.Parse("{ not json", "t"));
			StringAssert.Contains(ex.Message, "JSON");
		}

		[TestMethod]
		public void BasedOnInheritsEverythingNotGiven()
		{
			var parent = ThemeFile.Parse(Complete("Parent"), "parent");
			var json = "{\n\t\"name\": \"Child\",\n\t\"basedOn\": \"Parent\",\n\t\"colors\": { \"WindowText\": \"#ABCDEF\" }\n}";
			var child = ThemeFile.Parse(json, "child", n => string.Equals(n, "Parent", StringComparison.OrdinalIgnoreCase) ? parent : null);
			Assert.AreEqual(Color.FromArgb(0xAB, 0xCD, 0xEF), child[ThemeColorRole.WindowText]);
			Assert.AreEqual(parent[ThemeColorRole.WindowBackground], child[ThemeColorRole.WindowBackground]);
		}

		[TestMethod]
		public void BasedOnSomethingThatIsNotThereIsRefused()
		{
			var json = "{\n\t\"name\": \"Child\",\n\t\"basedOn\": \"Nowhere\",\n\t\"colors\": { \"WindowText\": \"#ABCDEF\" }\n}";
			var ex = Assert.ThrowsException<ThemeFormatException>(() => ThemeFile.Parse(json, "child", static _ => null));
			StringAssert.Contains(ex.Message, "Nowhere");
		}

		[TestMethod]
		public void AnAlphaColourKeepsItsAlpha()
		{
			var json = Complete().Replace($"\"{ThemeColorRole.WindowText}\": \"#123456\"", $"\"{ThemeColorRole.WindowText}\": \"#80112233\"");
			var theme = ThemeFile.Parse(json, "t");
			Assert.AreEqual(Color.FromArgb(0x80, 0x11, 0x22, 0x33), theme[ThemeColorRole.WindowText]);
		}

		/// <summary>What Config &gt; Theme &gt; Write a Copy to Edit produces has to be readable again.</summary>
		[TestMethod]
		public void WhatItWritesItCanReadBack()
		{
			foreach (var theme in ThemeLibrary.All)
			{
				var again = ThemeFile.Parse(ThemeFile.Write(theme), "round trip");
				Assert.AreEqual(theme.Name, again.Name);
				foreach (var role in Theme.AllRoles)
				{
					// ToArgb, not the Color: a named colour and the same value written
					// out in hex are the same pixels and never Equals each other
					Assert.AreEqual(theme[role].ToArgb(), again[role].ToArgb(), $"{theme.Name}: {role} did not survive a write and a read");
				}
			}
		}

		[TestMethod]
		public void TheTwoBuiltInThemesAreThere()
		{
			var names = ThemeLibrary.All.Select(static t => t.Name).ToList();
			CollectionAssert.Contains(names, "Light");
			CollectionAssert.Contains(names, "Dark");
			Assert.IsFalse(ThemeLibrary.Find("Light")!.IsDark);
			Assert.IsTrue(ThemeLibrary.Find("Dark")!.IsDark);
		}

		[TestMethod]
		public void AnUnknownThemeNameFallsBackToLight()
			=> Assert.AreEqual("Light", ThemeLibrary.Select("a theme somebody deleted").Name);

		/// <summary>
		/// A folder of files: the good ones load, the bad one is left out with a
		/// reason, and the good ones are not lost because of it.
		/// </summary>
		[TestMethod]
		public void ABrokenFileInTheFolderDoesNotTakeTheGoodOnesWithIt()
		{
			var dir = Path.Combine(Path.GetTempPath(), "chimera-theme-test-" + Guid.NewGuid().ToString("N"));
			Directory.CreateDirectory(dir);
			try
			{
				File.WriteAllText(Path.Combine(dir, "good.json"), Complete("Good"));
				File.WriteAllText(Path.Combine(dir, "bad.json"), Complete("Bad").Replace($"\t\t\"{ThemeColorRole.WindowText}\": \"#123456\",\n", ""));
				(var themes, var failures) = ThemeLibrary.Read(dir);
				var names = themes.Select(static t => t.Name).ToList();
				CollectionAssert.Contains(names, "Good");
				CollectionAssert.DoesNotContain(names, "Bad");
				Assert.AreEqual(1, failures.Count);
				StringAssert.Contains(failures[0].Message, nameof(ThemeColorRole.WindowText));
			}
			finally
			{
				Directory.Delete(dir, recursive: true);
			}
		}

		/// <summary>Two files both calling themselves "Light" must not shadow the built-in.</summary>
		[TestMethod]
		public void AFileCannotTakeTheNameOfAThemeAlreadyLoaded()
		{
			var dir = Path.Combine(Path.GetTempPath(), "chimera-theme-test-" + Guid.NewGuid().ToString("N"));
			Directory.CreateDirectory(dir);
			try
			{
				File.WriteAllText(Path.Combine(dir, "mine.json"), Complete("Light"));
				(var themes, var failures) = ThemeLibrary.Read(dir);
				Assert.AreEqual(1, themes.Count(static t => string.Equals(t.Name, "Light", StringComparison.OrdinalIgnoreCase)));
				Assert.AreEqual(1, failures.Count);
				var light = themes.First(static t => t.Name == "Light");
				Assert.AreEqual(SystemColors.Window.ToArgb(), light[ThemeColorRole.InputBackground].ToArgb());
			}
			finally
			{
				Directory.Delete(dir, recursive: true);
			}
		}
	}
}
