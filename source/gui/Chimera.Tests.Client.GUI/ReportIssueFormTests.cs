using Chimera.Client.Common;
using Chimera.Client.GUI;

namespace Chimera.Tests.Client.GUI
{
	/// <summary>Help &gt; Report an Issue (#243): the window shows the link, the text and the files it was given.</summary>
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
			StringAssert.StartsWith(form.FilesText, "No saved project, crash note or sandbox log");
		}
	}
}
