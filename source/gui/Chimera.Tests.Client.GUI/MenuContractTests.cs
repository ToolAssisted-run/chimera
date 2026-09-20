using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Text.RegularExpressions;

namespace Chimera.Tests.Client.GUI
{
	/// <summary>
	/// The Theme menu shipped in a state where clicking it did nothing at all,
	/// and 873 tests were green while it did. Every one of them reached the theme
	/// engine directly; the only route a person has to the feature was dead.
	///
	/// The cause is a WinForms rule with no warning attached to it: a
	/// ToolStripMenuItem whose DropDownItems is EMPTY does not open a dropdown, so
	/// a menu that fills itself in DropDownOpened never fills itself and never
	/// opens. The frontend has seven such menus. This is what makes the eighth
	/// somebody writes a failing test rather than a bug report.
	///
	/// It reads the Designer files rather than a running window because the menus
	/// live on MainForm and the tool windows, none of which can be built without a
	/// frontend around them - and because the mistake IS in the Designer file.
	/// </summary>
	[TestClass]
	public class MenuContractTests
	{
		/// <summary>
		/// The checkout this assembly was built in, found by walking up for the
		/// file only the repository root has - the same way the core-package tests
		/// find build/Cores.
		/// </summary>
		private static string? SourceRoot()
		{
			var dir = new DirectoryInfo(AppDomain.CurrentDomain.BaseDirectory);
			while (dir is not null && !File.Exists(Path.Combine(dir.FullName, "official-cores.json")))
			{
				dir = dir.Parent;
			}
			return dir is null ? null : Path.Combine(dir.FullName, "source", "gui", "Chimera.Client.GUI");
		}

		[TestMethod]
		public void AMenuThatFillsItselfWhenItOpensHasSomethingInItToOpenWith()
		{
			var root = SourceRoot();
			if (root is null || !Directory.Exists(root))
			{
				Assert.Inconclusive("the source tree is not beside this assembly, so the Designer files cannot be read");
				return;
			}

			Regex opens = new(@"this\.(\w+)\.DropDownOpened \+=", RegexOptions.Compiled);
			Regex seeded = new(@"this\.(\w+)\.DropDownItems\.(AddRange|Add)\(", RegexOptions.Compiled);

			List<string> dead = new();
			var looked = 0;
			foreach (var file in Directory.EnumerateFiles(root, "*.Designer.cs", SearchOption.AllDirectories)
				.OrderBy(static p => p, StringComparer.Ordinal))
			{
				var text = File.ReadAllText(file);
				var filled = new HashSet<string>(seeded.Matches(text).Cast<Match>().Select(static m => m.Groups[1].Value), StringComparer.Ordinal);
				foreach (Match m in opens.Matches(text))
				{
					looked++;
					var name = m.Groups[1].Value;
					if (!filled.Contains(name))
					{
						dead.Add($"{Path.GetFileName(file)}: {name} is filled in when it opens, but starts with nothing in it, "
							+ "so it never opens and the handler never runs");
					}
				}
			}

			Assert.IsTrue(looked > 5, $"only {looked} menus that fill themselves were found, so the sweep is not sweeping");
			Assert.AreEqual(
				0,
				dead.Count,
				"these menus cannot open:\n" + string.Join("\n", dead)
					+ "\nSeed them with a separator in the designer, the way RecentProjectSubMenu does; "
					+ "the handler clears it on the way in, so nothing shows.");
		}
	}
}
