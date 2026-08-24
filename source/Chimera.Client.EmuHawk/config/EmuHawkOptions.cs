using System.Windows.Forms;
using Chimera.Client.Common;
using Chimera.Common;
using Chimera.Common.NumberExtensions;

namespace Chimera.Client.EmuHawk
{
	public partial class EmuHawkOptions : Form
	{
		private readonly Action _reinitHostKeybinds;

		private readonly Config _config;

		public EmuHawkOptions(Config config, Action reinitHostKeybinds)
		{
			_reinitHostKeybinds = reinitHostKeybinds;
			_config = config;
			InitializeComponent();
			cbEnableGCAdapterSupport.Text = OSTailoredCode.IsUnixHost
				? "Enable Wii U/Switch GameCube Adapter Support (via libusb)"
				: "Enable Wii U/Switch GameCube Adapter Support (via Zadig/WinUSB)";
		}

		private void GuiOptions_Load(object sender, EventArgs e)
		{
			StartFullScreenCheckbox.Checked = _config.StartFullscreen;
			StartPausedCheckbox.Checked = _config.StartPaused;
			PauseWhenMenuActivatedCheckbox.Checked = _config.PauseWhenMenuActivated;
			EnableContextMenuCheckbox.Checked = _config.ShowContextMenu;
			RunInBackgroundCheckbox.Checked = _config.RunInBackground;
			AcceptBackgroundInputCheckbox.Checked = _config.AcceptBackgroundInput;
			AcceptBackgroundInputControllerOnlyCheckBox.Checked = _config.AcceptBackgroundInputControllerOnly;
			HandleAlternateKeyboardLayoutsCheckBox.Checked = _config.HandleAlternateKeyboardLayouts;
			NeverAskSaveCheckbox.Checked = _config.SuppressAskSave;
			cbMergeLAndRModifierKeys.Checked = _config.MergeLAndRModifierKeys;
			SingleInstanceModeCheckbox.Checked = _config.SingleInstanceMode;
			SingleInstanceModeCheckbox.Enabled = !OSTailoredCode.IsUnixHost;

			FrameAdvSkipLagCheckbox.Checked = _config.SkipLagFrame;
			LuaDuringTurboCheckbox.Checked = _config.RunLuaDuringTurbo;
			cbSkipWaterboxIntegrityChecks.Checked = _config.SkipWaterboxIntegrityChecks;
			NoMixedKeyPriorityCheckBox.Checked = _config.NoMixedInputHokeyOverride;
			cbEnableGCAdapterSupport.Checked = _config.GCAdapterSupportEnabled;
		}

		private void OkBtn_Click(object sender, EventArgs e)
		{
			if (cbMergeLAndRModifierKeys.Checked != _config.MergeLAndRModifierKeys)
			{
				var merging = cbMergeLAndRModifierKeys.Checked;
				var result = MessageBox.Show(
					this,
					text: $"Would you like to replace {(merging ? "LShift and RShift with Shift" : "Shift with LShift")},\nand the same for the other modifier keys,\nin existing keybinds for hotkeys and all systems' gamepads?",
					caption: "Rewrite keybinds now?",
					MessageBoxButtons.YesNoCancel,
					MessageBoxIcon.Question);
				if (result is DialogResult.Cancel) return;
				if (result is DialogResult.Yes)
				{
					_config.ReplaceKeysInBindings(merging ? Input.ModifierKeyPreMap : Input.ModifierKeyInvPreMap);
					_reinitHostKeybinds();
				}
			}

			_config.StartFullscreen = StartFullScreenCheckbox.Checked;
			_config.StartPaused = StartPausedCheckbox.Checked;
			_config.PauseWhenMenuActivated = PauseWhenMenuActivatedCheckbox.Checked;
			_config.ShowContextMenu = EnableContextMenuCheckbox.Checked;
			_config.RunInBackground = RunInBackgroundCheckbox.Checked;
			_config.AcceptBackgroundInput = AcceptBackgroundInputCheckbox.Checked;
			_config.AcceptBackgroundInputControllerOnly = AcceptBackgroundInputControllerOnlyCheckBox.Checked;
			_config.HandleAlternateKeyboardLayouts = HandleAlternateKeyboardLayoutsCheckBox.Checked;
			_config.SuppressAskSave = NeverAskSaveCheckbox.Checked;
			_config.MergeLAndRModifierKeys = cbMergeLAndRModifierKeys.Checked;
			_config.SingleInstanceMode = SingleInstanceModeCheckbox.Checked;

			_config.SkipLagFrame = FrameAdvSkipLagCheckbox.Checked;
			_config.RunLuaDuringTurbo = LuaDuringTurboCheckbox.Checked;
			_config.SkipWaterboxIntegrityChecks = cbSkipWaterboxIntegrityChecks.Checked;
			_config.NoMixedInputHokeyOverride = NoMixedKeyPriorityCheckBox.Checked;
			_config.GCAdapterSupportEnabled = cbEnableGCAdapterSupport.Checked;

			Close();
			DialogResult = DialogResult.OK;
		}

		private void CancelBtn_Click(object sender, EventArgs e)
		{
			Close();
			DialogResult = DialogResult.Cancel;
		}

		private void AcceptBackgroundInputCheckbox_CheckedChanged(object sender, EventArgs e)
		{
			AcceptBackgroundInputControllerOnlyCheckBox.Enabled = AcceptBackgroundInputCheckbox.Checked;
		}

	}
}
