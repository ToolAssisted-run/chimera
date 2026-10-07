using System.Collections.Generic;
using System.Linq;
using System.Text.RegularExpressions;
using System.Windows.Forms;

using Chimera.Client.GUI;
using Chimera.Common;

namespace Chimera.Tests.Client.GUI
{
	/// <summary>
	/// The About box is six lines and no more (issue #193): the logo, the name,
	/// the build - its commit a link, with the commit's date and time - the
	/// repository, the site, and OK. One column, each line centred in it.
	/// </summary>
	[TestClass]
	public class AboutBoxTests
	{
		private static List<Control> Lines(AboutBox box)
			=> box.Controls.Cast<Control>().OrderBy(static c => c.Top).ToList();

		[TestMethod]
		public void ItIsTheSixLinesInTheirOrder()
		{
			using AboutBox box = new();
			box.Show();
			var lines = Lines(box);
			CollectionAssert.AreEqual(
				new[] { "LogoBox", "NameLabel", "CommitLink", "RepoLink", "SiteLink", "OK" },
				lines.Select(static c => c.Name).ToArray(),
				"the lines, top to bottom");
			Assert.IsInstanceOfType(lines[0], typeof(PictureBox));
			Assert.IsNotNull(((PictureBox) lines[0]).Image, "the logo");
			// the designer says 64 pixels and the form scales with its font, so "small" is said as a share of the window
			Assert.IsTrue(lines[0].Height * 3 <= box.ClientSize.Height, $"and small: {lines[0].Height} of the window's {box.ClientSize.Height} pixels");
			Assert.AreEqual("Chimera", lines[1].Text);
			Assert.IsInstanceOfType(lines[5], typeof(Button));
			for (var i = 1; i < lines.Count; i++)
			{
				Assert.IsTrue(lines[i].Top >= lines[i - 1].Bottom, $"{lines[i].Name} is under {lines[i - 1].Name}, not over it");
			}
			Assert.IsTrue(lines[5].Bottom <= box.ClientSize.Height, "and OK is inside the window");
		}

		[TestMethod]
		public void TheBuildIsItsCommitAsALinkWithTheCommitsDateAndTime()
		{
			using AboutBox box = new();
			box.Show();
			var commit = (LinkLabel) box.Controls["CommitLink"];
			StringAssert.Matches(commit.Text, new Regex(@"^Commit [0-9a-f]{9} \(\d{4}-\d{2}-\d{2} \d{2}:\d{2} UTC\)$"));
			Assert.AreEqual(VersionInfo.GIT_SHORTHASH, commit.Text.Substring(commit.LinkArea.Start, commit.LinkArea.Length), "only the commit is the link");
			Assert.AreEqual($"https://github.com/ToolAssisted-run/chimera/commit/{VersionInfo.GIT_HASH}", (string) commit.Tag);
		}

		[TestMethod]
		public void TheOtherTwoLinksAreTheRepositoryAndTheSite()
		{
			using AboutBox box = new();
			box.Show();
			var repo = (LinkLabel) box.Controls["RepoLink"];
			var site = (LinkLabel) box.Controls["SiteLink"];
			Assert.AreEqual("github.com/ToolAssisted-run/chimera", repo.Text);
			Assert.AreEqual("https://github.com/ToolAssisted-run/chimera", (string) repo.Tag);
			Assert.AreEqual("toolAssisted.run", site.Text);
			Assert.AreEqual("https://toolassisted.run", (string) site.Tag);
		}

		[TestMethod]
		public void EveryLineIsCentred()
		{
			using AboutBox box = new();
			box.Show();
			foreach (var line in Lines(box))
			{
				var off = (line.Left + line.Right) - box.ClientSize.Width;
				Assert.IsTrue(Math.Abs(off) <= 2, $"{line.Name} sits {off / 2} pixels off the middle");
				Assert.IsTrue(line.Left >= 0 && line.Right <= box.ClientSize.Width, $"{line.Name} fits the window");
			}
		}
	}
}
