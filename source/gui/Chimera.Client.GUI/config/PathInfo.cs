using System.Windows.Forms;

namespace Chimera.Client.GUI
{
	public partial class PathInfo : ThemedForm
	{
		public PathInfo()
		{
			InitializeComponent();
		}

		private void Ok_Click(object sender, EventArgs e)
		{
			Close();
		}
	}
}
