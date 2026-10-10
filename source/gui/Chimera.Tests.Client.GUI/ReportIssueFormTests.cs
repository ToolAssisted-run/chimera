using System.IO;
using System.Linq;

using Chimera.Client.Common;
using Chimera.Client.GUI;

namespace Chimera.Tests.Client.GUI
{
	/// <summary>
	/// Help &gt; Report an Issue (#243): the window shows the link, the text and the
	/// files it was given, and saves what is ticked as one zip.
	/// </summary>
	[TestClass]
	public class ReportIssueFormTests
	{
		[TestMethod]
		public void TheWindowShowsTheLinkTheTextAndTheFiles()
		{
			const string Report = "**Chimera build:** Commit 1ab07c06c\n**Game:**\n- disc: Game (USA).iso";
			using ReportIssueForm form = new(Report, [ "/home/me/run.chimeraProject", "/home/me/Crashes/2026-10-09 pid2.txt" ]);
			form.Show();
			Assert.AreEqual(IssueReport.FormUrl, form.LinkText);
			// what Copy Text copies is the report as it was made, line for line
			Assert.AreEqual(Report, form.ReportText);
			Assert.AreEqual("/home/me/run.chimeraProject\n/home/me/Crashes/2026-10-09 pid2.txt", form.FilesText);
		}

		[TestMethod]
		public void WithNoFilesTheWindowSaysThatNoneWereFound()
		{
			using ReportIssueForm form = new("**Chimera build:** x", [ ]);
			form.Show();
			Assert.AreEqual("", form.FilesText);
			StringAssert.StartsWith(form.StatusText, "No saved project, crash note or log was found");
		}

		[TestMethod]
		public void TheZipHoldsTheTextAndOnlyWhatIsTicked()
		{
			var root = Path.Combine(Path.GetTempPath(), "chimera-report-form-" + Path.GetRandomFileName());
			Directory.CreateDirectory(root);
			try
			{
				var project = Path.Combine(root, "run.chimeraProject");
				var log = Path.Combine(root, "minibox-diag.log");
				File.WriteAllText(project, "{}");
				File.WriteAllText(log, "log");
				using ReportIssueForm form = new("**Chimera build:** x", [ project, log ], gamePicturePng: [ 9, 9 ]);
				form.Show();
				// the picture of the game is a row of its own, before the files
				StringAssert.StartsWith(form.FilesText, "A picture of the game as it is now (game.png)\n");

				var all = Path.Combine(root, "all.zip");
				form.SaveZip(all);
				StringAssert.StartsWith(form.StatusText, "Saved all.zip (");
				using (var zip = System.IO.Compression.ZipFile.OpenRead(all))
				{
					CollectionAssert.AreEquivalent(
						new[] { "report.md", "game.png", "run.chimeraProject", "minibox-diag.log" },
						zip.Entries.Select(static e => e.FullName).ToList());
				}

				// unticked: the picture (row 0) and the project (row 1)
				form.SetTicked(0, false);
				form.SetTicked(1, false);
				var some = Path.Combine(root, "some.zip");
				form.SaveZip(some);
				using (var zip = System.IO.Compression.ZipFile.OpenRead(some))
				{
					CollectionAssert.AreEquivalent(
						new[] { "report.md", "minibox-diag.log" },
						zip.Entries.Select(static e => e.FullName).ToList());
				}

				// a zip that cannot be written is said, not thrown
				form.SaveZip(Path.Combine(root, "no-such-folder", "x.zip"));
				StringAssert.StartsWith(form.StatusText, "The zip was not saved:");
			}
			finally
			{
				Directory.Delete(root, recursive: true);
			}
		}
	}
}
