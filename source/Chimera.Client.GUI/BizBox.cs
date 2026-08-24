using System.Linq;
using System.Windows.Forms;

using Chimera.Client.Common;
using Chimera.Client.GUI.Properties;
using Chimera.Common;

namespace Chimera.Client.GUI
{
	public partial class BizBox : Form
	{
		public BizBox(Action/*?*/ playAboutSFX = null)
		{
			InitializeComponent();
			Icon = Resources.Logo;
			pictureBox1.Image = Resources.Chimera;
			btnCopyHash.Image = Resources.Duplicate;
			if (playAboutSFX is not null) Shown += (_, _) => playAboutSFX();
		}

		private void linkLabel1_LinkClicked(object sender, LinkLabelLinkClickedEventArgs e)
		{
			linkLabel1.LinkVisited = true;
			Util.OpenUrlExternal(VersionInfo.HomePage);
		}

		private void OK_Click(object sender, EventArgs e)
		{
			Close();
		}

		private void BizBox_Load(object sender, EventArgs e)
		{
			VersionLabel.Text = VersionInfo.GetFullVersionDetails();
			DateLabel.Text = VersionInfo.GIT_SHORTDATE;
			(linkLabel2.Text, linkLabel2.Tag) = VersionInfo.GetGitCommitLink();

			CoreInfoPanel.SuspendLayout();
			foreach (var coreAttr in CoreRegistry.Instance.AllFactories
				.Select(static f => CoreRegistry.AttributesFor(f))
				.Where(static attr => attr is not null)
				.OrderBy(static attr => attr.Released)
				.ThenByDescending(static attr => attr.CoreName, StringComparer.OrdinalIgnoreCase))
			{
				CoreInfoPanel.Controls.Add(new BizBoxInfoControl(coreAttr)
				{
					Dock = DockStyle.Top,
				});
			}
			CoreInfoPanel.ResumeLayout();
		}

		private void linkLabel2_LinkClicked(object sender, LinkLabelLinkClickedEventArgs e)
			=> Util.OpenUrlExternal((string) ((Control) sender).Tag);

		private void btnCopyHash_Click(object sender, EventArgs e)
			=> Clipboard.SetText(VersionInfo.GIT_HASH);

		private void linkLabelBizHawk_LinkClicked(object sender, LinkLabelLinkClickedEventArgs e)
		{
			linkLabelBizHawk.LinkVisited = true;
			Util.OpenUrlExternal("https://github.com/TASEmulators/BizHawk");
		}

		private void linkLabel3_LinkClicked(object sender, LinkLabelLinkClickedEventArgs e)
			=> Util.OpenUrlExternal(VersionInfo.CreditsListURI);
	}
}
