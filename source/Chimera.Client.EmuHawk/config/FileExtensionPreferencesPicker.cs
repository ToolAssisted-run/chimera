using System.Collections.Generic;
using System.Linq;
using System.Windows.Forms;

using Chimera.Client.Common;
using Chimera.Emulation.Common;

namespace Chimera.Client.EmuHawk
{
	public partial class FileExtensionPreferencesPicker : UserControl
	{
		private readonly IDictionary<string, string> _preferredPlatformsForExtensions;

		public FileExtensionPreferencesPicker(IDictionary<string, string> preferredPlatformsForExtensions)
		{
			_preferredPlatformsForExtensions = preferredPlatformsForExtensions;
			InitializeComponent();
		}

		public string FileExtension { get; set; }
		public string OriginalPreference { get; set; }

		public string CurrentlySelectedSystemId
			=> PlatformDropdown.SelectedIndex > 0
				? PlatformDropdown.SelectedItem.ToString()
				: "";

		private void PopulatePlatforms()
		{
			PlatformDropdown.Items.Add("Ask me on load");
			// choosable platforms are the systems provided by loaded core packages
			foreach (var systemId in CoreRegistry.Instance.AllFactories.SelectMany(static f => f.SystemIds).Distinct())
			{
				PlatformDropdown.Items.Add(systemId);
			}
		}

		private void FileExtensionPreferencesPicker_Load(object sender, EventArgs e)
		{
			PopulatePlatforms();

			var selectedSystemId = _preferredPlatformsForExtensions[FileExtension];
			if (!string.IsNullOrEmpty(selectedSystemId))
			{
				if (PlatformDropdown.Items.Contains(selectedSystemId))
				{
					PlatformDropdown.SelectedItem = selectedSystemId;
				}
				else
				{
					PlatformDropdown.SelectedIndex = 0;
				}
			}
			else
			{
				PlatformDropdown.SelectedIndex = 0;
			}

			FileExtensionLabel.Text = FileExtension;
		}
	}
}
