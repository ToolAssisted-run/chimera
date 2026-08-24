using System.Globalization;
using System.IO;
using System.Windows.Forms;

using Chimera.Bizware.Graphics;
using Chimera.Client.Common;
using Chimera.Client.Common.Filters;
using Chimera.Common;
using Chimera.Common.NumberExtensions;
using Chimera.Emulation.Common;
using Chimera.WinForms.Controls;

namespace Chimera.Client.EmuHawk
{
	public partial class DisplayConfig : Form, IDialogParent
	{
		private readonly Config _config;

		private readonly RadioButtonGroupTracker _snowRadioTracker;

		private readonly SzNUDEx nudSnowBias = new()
		{
			DecimalPlaces = 2,
			Increment = 0.25m,
			Maximum = 2.0m,
			Minimum = -2.0m,
			Size = new(48, 23),
		};

		private readonly SzNUDEx nudSnowIntensity = new()
		{
			DecimalPlaces = 1,
			Increment = 0.1m,
			Maximum = 1.0m,
			Minimum = 0.1m,
			Size = new(48, 23),
		};

		private readonly SzTextBoxEx txtSWTOverride;

		private readonly TransparentTrackBar tbSnowFramerate = new() { Maximum = 20, Minimum = 1, Size = new(160, 45) };

		public IDialogController DialogController { get; }

		public DisplayConfig(Config config, IDialogController dialogController, IGL gl)
		{
			_config = config;
			DialogController = dialogController;
			var snowSettings = _config.GetCoreSettings<NullEmulator, SnowyNullVideo.Settings>() ?? new();

			InitializeComponent();
			flpStaticWindowTitles.Controls.Remove(cbStaticWindowTitles);
			txtSWTOverride = new() { Size = new(80, 23), Text = _config.MainFormStaticWindowTitleOverrideEffective };
			flpStaticWindowTitles.Controls.InsertBefore(
				lblStaticWindowTitles,
				insert: new SingleRowFLP
				{
					Controls =
					{
						cbStaticWindowTitles,
						new Label { Size = new(32, 1) }, // whitespace
						new LabelEx { Text = "Window title suffix (static and not):" },
						txtSWTOverride,
					},
				});
			LocSzGroupBoxEx grpSnow = new() { Location = new(6, 200), Size = new(371, 160), Text = "Snowy NullHawk" };
			_snowRadioTracker = grpSnow.Tracker;
			RadioButtonEx rbSnowAlways = new(_snowRadioTracker)
			{
				Checked = config.SnowyNullHawk is SnowyNullVideo.TriggerCriterion.Always,
				Name = nameof(rbSnowAlways),
				Tag = SnowyNullVideo.TriggerCriterion.Always,
				Text = "Always",
			};
			RadioButtonEx rbSnowForChristmas = new(_snowRadioTracker)
			{
				Checked = config.SnowyNullHawk is SnowyNullVideo.TriggerCriterion.WeekOfChristmas,
				Name = nameof(rbSnowForChristmas),
				Tag = SnowyNullVideo.TriggerCriterion.WeekOfChristmas,
				Text = "During Christmas (Dec. 20th through 26th)",
			};
			RadioButtonEx rbSnowNever = new(_snowRadioTracker)
			{
				Checked = config.SnowyNullHawk is SnowyNullVideo.TriggerCriterion.Never,
				Name = nameof(rbSnowNever),
				Tag = SnowyNullVideo.TriggerCriterion.Never,
				Text = "Never",
			};
			LabelEx lblFramerate = new();
			tbSnowFramerate.ValueChanged += (changedSender, _) =>
			{
				var val = ((TrackBar) changedSender).Value;
				lblFramerate.Text = $"Framerate: {60.0 / val:F1} Hz";
			};
			tbSnowFramerate.Value = snowSettings.FramerateScalar;
			nudSnowIntensity.Value = new(snowSettings.Intensity);
			nudSnowBias.Value = new(snowSettings.Bias);
			grpSnow.Controls.Add(new LocSzSingleColumnFLP
			{
				Controls =
				{
					new LabelEx { Text = "When no rom loaded, draw \"snow\" (white noise):" },
					new SingleRowFLP { Controls = { rbSnowAlways, rbSnowNever } },
					rbSnowForChristmas,
					new SingleRowFLP
					{
						Controls =
						{
							new LabelEx { Text = "Brightness multiplier:" },
							nudSnowIntensity,
							new LabelEx { Text = "RNG bias:" },
							nudSnowBias,
						},
					},
					new SingleRowFLP { Controls = { lblFramerate, tbSnowFramerate } },
				},
				Location = new(5, 15),
				Size = new(320, 144),
			});
			tpMisc.Controls.Remove(flpStaticWindowTitles);
			tpMisc.Controls.Remove(groupBox5);
			tpMisc.Controls.Add(new SingleColumnFLP
			{
				Controls =
				{
					groupBox5,
					flpStaticWindowTitles,
					grpSnow,
				},
			});

			rbFinalFilterNone.Checked = _config.DispFinalFilter == 0;
			rbFinalFilterBilinear.Checked = _config.DispFinalFilter != 0;

			checkLetterbox.Checked = _config.DispFixAspectRatio;
			checkPadInteger.Checked = _config.DispFixScaleInteger;
			cbFullscreenHacks.Checked = _config.DispFullscreenHacks;
			cbAutoPrescale.Checked = _config.DispAutoPrescale;
			cbScaleOSD.Checked = _config.ScaleOSDWithSystemScale;

			if (_config.DispSpeedupFeatures == 2) rbDisplayFull.Checked = true;
			if (_config.DispSpeedupFeatures == 1) rbDisplayMinimal.Checked = true;
			if (_config.DispSpeedupFeatures == 0) rbDisplayAbsoluteZero.Checked = true;

			cbStaticWindowTitles.Checked = _config.UseStaticWindowTitles;

			cbStatusBarWindowed.Checked = _config.DispChromeStatusBarWindowed;
			cbCaptionWindowed.Checked = _config.DispChromeCaptionWindowed;
			cbMenuWindowed.Checked = _config.DispChromeMenuWindowed;
			cbMainFormSaveWindowPosition.Checked = _config.SaveWindowPosition;
			cbMainFormStayOnTop.Checked = _config.MainFormStayOnTop;
			cbMainFormMouseCaptureForcesTopmost.Checked = _config.MainFormMouseCaptureForcesTopmost;
			if (OSTailoredCode.IsUnixHost)
			{
				cbMainFormStayOnTop.Enabled = false;
				cbMainFormStayOnTop.Visible = false;
				cbMainFormMouseCaptureForcesTopmost.Enabled = false;
				cbMainFormMouseCaptureForcesTopmost.Visible = false;
			}
			cbStatusBarFullscreen.Checked = _config.DispChromeStatusBarFullscreen;
			cbMenuFullscreen.Checked = _config.DispChromeMenuFullscreen;
			trackbarFrameSizeWindowed.Value = _config.DispChromeFrameWindowed;
			cbFSAutohideMouse.Checked = _config.DispChromeFullscreenAutohideMouse;
			SyncTrackBar();

			cbAllowDoubleclickFullscreen.Checked = _config.DispChromeAllowDoubleClickFullscreen;

			nudPrescale.Value = _config.DispPrescale;

			if (_config.DispManagerAR == EDispManagerAR.None)
				rbUseRaw.Checked = true;
			else if (_config.DispManagerAR == EDispManagerAR.System)
				rbUseSystem.Checked = true;
			else if (_config.DispManagerAR == EDispManagerAR.CustomSize)
				rbUseCustom.Checked = true;
			else if (_config.DispManagerAR == EDispManagerAR.CustomRatio)
				rbUseCustomRatio.Checked = true;

			if(_config.DispCustomUserARWidth != -1)
				txtCustomARWidth.Text = _config.DispCustomUserARWidth.ToString();
			if (_config.DispCustomUserARHeight != -1)
				txtCustomARHeight.Text = _config.DispCustomUserARHeight.ToString();
			if (_config.DispCustomUserArx != -1)
				txtCustomARX.Text = _config.DispCustomUserArx.ToString(NumberFormatInfo.InvariantInfo);
			if (_config.DispCustomUserAry != -1)
				txtCustomARY.Text = _config.DispCustomUserAry.ToString(NumberFormatInfo.InvariantInfo);

			txtCropLeft.Text = _config.DispCropLeft.ToString();
			txtCropTop.Text = _config.DispCropTop.ToString();
			txtCropRight.Text = _config.DispCropRight.ToString();
			txtCropBottom.Text = _config.DispCropBottom.ToString();

			RefreshAspectRatioOptions();
		}

		private void BtnOk_Click(object sender, EventArgs e)
		{
			if (rbFinalFilterNone.Checked)
				_config.DispFinalFilter = 0;
			if (rbFinalFilterBilinear.Checked)
				_config.DispFinalFilter = 1;

			_config.DispPrescale = (int)nudPrescale.Value;

			_config.DispFixAspectRatio = checkLetterbox.Checked;
			_config.DispFixScaleInteger = checkPadInteger.Checked;
			_config.DispFullscreenHacks = cbFullscreenHacks.Checked;
			_config.DispAutoPrescale = cbAutoPrescale.Checked;
			_config.ScaleOSDWithSystemScale = cbScaleOSD.Checked;

			_config.DispChromeStatusBarWindowed = cbStatusBarWindowed.Checked;
			_config.DispChromeCaptionWindowed = cbCaptionWindowed.Checked;
			_config.DispChromeMenuWindowed = cbMenuWindowed.Checked;
			_config.SaveWindowPosition = cbMainFormSaveWindowPosition.Checked;
			_config.MainFormStayOnTop = cbMainFormStayOnTop.Checked;
			Owner.TopMost = _config.MainFormStayOnTop;
			_config.MainFormMouseCaptureForcesTopmost = cbMainFormMouseCaptureForcesTopmost.Checked;
			_config.DispChromeStatusBarFullscreen = cbStatusBarFullscreen.Checked;
			_config.DispChromeMenuFullscreen = cbMenuFullscreen.Checked;
			_config.DispChromeFrameWindowed = trackbarFrameSizeWindowed.Value;
			_config.DispChromeFullscreenAutohideMouse = cbFSAutohideMouse.Checked;
			_config.DispChromeAllowDoubleClickFullscreen = cbAllowDoubleclickFullscreen.Checked;

			if (rbDisplayFull.Checked) _config.DispSpeedupFeatures = 2;
			if (rbDisplayMinimal.Checked) _config.DispSpeedupFeatures = 1;
			if (rbDisplayAbsoluteZero.Checked) _config.DispSpeedupFeatures = 0;

			_config.UseStaticWindowTitles = cbStaticWindowTitles.Checked;

			_config.SnowyNullHawk = _snowRadioTracker.GetSelectionTagAs<SnowyNullVideo.TriggerCriterion>()
				?? SnowyNullVideo.TriggerCriterion.WeekOfChristmas;
			_config.MainFormStaticWindowTitleOverride = string.IsNullOrWhiteSpace(txtSWTOverride.Text)
				|| txtSWTOverride.Text.Equals(VersionInfo.CustomBuildString, StringComparison.Ordinal)
					? string.Empty
					: txtSWTOverride.Text;
			_config.PutCoreSettings(
				new SnowyNullVideo.Settings(
					Bias: nudSnowBias.Value.ConvertToF32(),
					FramerateScalar: tbSnowFramerate.Value,
					Intensity: nudSnowIntensity.Value.ConvertToF32()),
				typeof(NullEmulator));

			if (rbUseRaw.Checked)
				_config.DispManagerAR = EDispManagerAR.None;
			else if (rbUseSystem.Checked)
				_config.DispManagerAR = EDispManagerAR.System;
			else if (rbUseCustom.Checked)
				_config.DispManagerAR = EDispManagerAR.CustomSize;
			else if (rbUseCustomRatio.Checked)
				_config.DispManagerAR = EDispManagerAR.CustomRatio;

			if (!string.IsNullOrWhiteSpace(txtCustomARWidth.Text))
			{
				if (int.TryParse(txtCustomARWidth.Text, out int dispCustomUserARWidth))
				{
					_config.DispCustomUserARWidth = dispCustomUserARWidth;
				}
			}
			else
			{
				_config.DispCustomUserARWidth = -1;
			}

			if (!string.IsNullOrWhiteSpace(txtCustomARHeight.Text))
			{
				if (int.TryParse(txtCustomARHeight.Text, out int dispCustomUserARHeight))
				{
					_config.DispCustomUserARHeight = dispCustomUserARHeight;
				}
			}
			else
			{
				_config.DispCustomUserARHeight = -1;
			}

			if (!string.IsNullOrWhiteSpace(txtCustomARX.Text))
			{
				if (float.TryParse(txtCustomARX.Text, out float dispCustomUserArx))
				{
					_config.DispCustomUserArx = dispCustomUserArx;
				}
			}
			else
			{
				_config.DispCustomUserArx = -1;
			}

			if (!string.IsNullOrWhiteSpace(txtCustomARY.Text))
			{
				if (float.TryParse(txtCustomARY.Text, out float dispCustomUserAry))
				{
					_config.DispCustomUserAry = dispCustomUserAry;
				}
			}
			else
			{
				_config.DispCustomUserAry = -1;
			}

			if (int.TryParse(txtCropLeft.Text, out int dispCropLeft))
			{
				_config.DispCropLeft = dispCropLeft;
			}

			if (int.TryParse(txtCropTop.Text, out int dispCropTop))
			{
				_config.DispCropTop = dispCropTop;
			}

			if (int.TryParse(txtCropRight.Text, out int dispCropRight))
			{
				_config.DispCropRight = dispCropRight;
			}

			if (int.TryParse(txtCropBottom.Text, out int dispCropBottom))
			{
				_config.DispCropBottom = dispCropBottom;
			}

			DialogResult = DialogResult.OK;
			Close();
		}

		private void CheckLetterbox_CheckedChanged(object sender, EventArgs e)
		{
			RefreshAspectRatioOptions();
		}

		private void CheckPadInteger_CheckedChanged(object sender, EventArgs e)
		{
			RefreshAspectRatioOptions();
		}

		private void RbUseRaw_CheckedChanged(object sender, EventArgs e)
		{
			RefreshAspectRatioOptions();
		}

		private void RbUseSystem_CheckedChanged(object sender, EventArgs e)
		{
			RefreshAspectRatioOptions();
		}

		private void RefreshAspectRatioOptions()
		{
			grpARSelection.Enabled = checkLetterbox.Checked;
			checkPadInteger.Enabled = checkLetterbox.Checked;
		}

		private void TrackBarFrameSizeWindowed_ValueChanged(object sender, EventArgs e)
		{
			SyncTrackBar();
		}

		private void SyncTrackBar()
		{
			if (trackbarFrameSizeWindowed.Value == 0)
			{
				lblFrameTypeWindowed.Text = "None";
			}

			if (trackbarFrameSizeWindowed.Value == 1)
			{
				lblFrameTypeWindowed.Text = "Thin";
			}

			if (trackbarFrameSizeWindowed.Value == 2)
			{
				lblFrameTypeWindowed.Text = "Thick";
			}
		}

		private void LinkLabel1_LinkClicked(object sender, LinkLabelLinkClickedEventArgs e)
		{
			Util.OpenUrlExternal("https://tasvideos.org/Bizhawk/DisplayConfig");
		}

		private void BtnDefaults_Click(object sender, EventArgs e)
		{
			nudPrescale.Value = 1;
			cbAutoPrescale.Checked = true;
			rbFinalFilterBilinear.Checked = true;
			checkLetterbox.Checked = true;
			rbUseSystem.Checked = true;
			txtCropLeft.Text = "0";
			txtCropTop.Text = "0";
			txtCropRight.Text = "0";
			txtCropBottom.Text = "0";
		}
	}
}
