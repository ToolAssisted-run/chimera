using System.Linq;
using System.Windows.Forms;

using Chimera.Client.Common;
using Chimera.Client.GUI.Properties;
using Chimera.Common;

namespace Chimera.Client.GUI
{
	/// <summary>
	/// What this is and which build of it: the logo, the name, the commit it was
	/// built from (a link to that commit) with the commit's date and time, where
	/// the code lives, where the project lives, and a way out. Nothing else
	/// (issue #193).
	/// </summary>
	public partial class AboutBox : ThemedForm
	{
		private const string CommitPrefix = "Commit ";

		public AboutBox()
		{
			InitializeComponent();
			Icon = Resources.Logo;
			LogoBox.Image = Resources.Chimera;
		}

		private void AboutBox_Load(object sender, EventArgs e)
		{
			var build = VersionInfo.GetBuildName();
			CommitLink.Text = build;
			// only the commit is the link; its date reads as text beside it
			CommitLink.LinkArea = build.StartsWith(CommitPrefix, StringComparison.Ordinal)
				? new(CommitPrefix.Length, VersionInfo.GIT_SHORTHASH.Length)
				: new(0, build.Length);
			CommitLink.Tag = VersionInfo.CommitURI;
			RepoLink.Text = Bare(VersionInfo.HomePage);
			RepoLink.Tag = VersionInfo.HomePage;
			SiteLink.Text = "toolAssisted.run";
			SiteLink.Tag = VersionInfo.SiteURI;
			// one column, every line centred in it
			foreach (var line in Controls.Cast<Control>()) line.Left = (ClientSize.Width - line.Width) / 2;
		}

		/// <summary>An address as it is read, not as it is typed into a browser.</summary>
		private static string Bare(string uri)
			=> uri.StartsWith("https://", StringComparison.Ordinal) ? uri.Substring("https://".Length) : uri;

		private void Link_LinkClicked(object sender, LinkLabelLinkClickedEventArgs e)
		{
			var link = (LinkLabel) sender;
			link.LinkVisited = true;
			Util.OpenUrlExternal((string) link.Tag);
		}

		private void OK_Click(object sender, EventArgs e)
			=> Close();
	}
}
