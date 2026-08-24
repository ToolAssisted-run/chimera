using System.Drawing;
using System.Linq;
using System.Windows.Forms;

using Chimera.Emulation.Common;
using Chimera.Client.Common;

namespace Chimera.Client.GUI
{
	public partial class PlatformChooser : Form
	{
		private readonly Config _config;

		public PlatformChooser(Config config)
		{
			_config = config;
			InitializeComponent();
		}

		public RomGame RomGame { get; set; }
		public string PlatformChoice { get; set; }

		private RadioButton SelectedRadio => PlatformsGroupBox.Controls.OfType<RadioButton>().FirstOrDefault(x => x.Checked);

		private void PlatformChooser_Load(object sender, EventArgs e)
		{
			RomSizeLabel.Text = RomGame.RomData.Length > 10 * 1024 * 1024
				? $"{RomGame.RomData.Length / 1024 / 1024:n0}mb"
				: $"{RomGame.RomData.Length / 1024:n0}kb";

			ExtensionLabel.Text = RomGame.Extension.ToLowerInvariant();
			HashBox.Text = RomGame.GameInfo.Hash;
			// choosable platforms are the systems provided by loaded core packages
			int count = 0;
			int spacing = 25;
			foreach (var systemId in CoreRegistry.Instance.AllFactories.SelectMany(static f => f.SystemIds).Distinct())
			{
				var radio = new RadioButton
				{
					Text = systemId,
					Location = UIHelper.Scale(new Point(15, 15 + (count * spacing))),
					Size = UIHelper.Scale(new Size(200, 23)),
				};

				PlatformsGroupBox.Controls.Add(radio);
				count++;
			}

			PlatformsGroupBox.Controls
				.OfType<RadioButton>()
				.FirstOrDefault()
				?.Select();
		}

		private void CancelButton_Click(object sender, EventArgs e)
		{
			Close();
		}

		private void OkBtn_Click(object sender, EventArgs e)
		{
			PlatformChoice = SelectedRadio != null ? SelectedRadio.Text : "";

			if (AlwaysCheckbox.Checked)
			{
				_config.PreferredPlatformsForExtensions[RomGame.Extension.ToLowerInvariant()] = PlatformChoice;
			}

			Close();
		}

		private void label4_Click(object sender, EventArgs e)
			=> AlwaysCheckbox.Checked = !AlwaysCheckbox.Checked;
	}
}
