namespace BizHawk.Client.EmuHawk
{
	partial class EmuHawkOptions
	{
		/// <summary>
		/// Required designer variable.
		/// </summary>
		private System.ComponentModel.IContainer components = null;

		/// <summary>
		/// Clean up any resources being used.
		/// </summary>
		/// <param name="disposing">true if managed resources should be disposed; otherwise, false.</param>
		protected override void Dispose(bool disposing)
		{
			if (disposing && (components != null))
			{
				components.Dispose();
			}
			base.Dispose(disposing);
		}

		#region Windows Form Designer generated code

		/// <summary>
		/// Required method for Designer support - do not modify
		/// the contents of this method with the code editor.
		/// </summary>
		private void InitializeComponent()
		{
			this.components = new System.ComponentModel.Container();
			this.OkBtn = new System.Windows.Forms.Button();
			this.CancelBtn = new System.Windows.Forms.Button();
			this.tabControl1 = new System.Windows.Forms.TabControl();
			this.tabPage1 = new System.Windows.Forms.TabPage();
			this.cbMergeLAndRModifierKeys = new System.Windows.Forms.CheckBox();
			this.cbEnableGCAdapterSupport = new System.Windows.Forms.CheckBox();
			this.HandleAlternateKeyboardLayoutsCheckBox = new System.Windows.Forms.CheckBox();
			this.NeverAskSaveCheckbox = new System.Windows.Forms.CheckBox();
			this.label2 = new BizHawk.WinForms.Controls.LocLabelEx();
			this.AcceptBackgroundInputCheckbox = new System.Windows.Forms.CheckBox();
			this.AcceptBackgroundInputControllerOnlyCheckBox = new System.Windows.Forms.CheckBox();
			this.label1 = new BizHawk.WinForms.Controls.LocLabelEx();
			this.RunInBackgroundCheckbox = new System.Windows.Forms.CheckBox();
			this.EnableContextMenuCheckbox = new System.Windows.Forms.CheckBox();
			this.PauseWhenMenuActivatedCheckbox = new System.Windows.Forms.CheckBox();
			this.groupBox1 = new System.Windows.Forms.GroupBox();
			this.locLabelEx1 = new BizHawk.WinForms.Controls.LocLabelEx();
			this.StartPausedCheckbox = new System.Windows.Forms.CheckBox();
			this.label14 = new BizHawk.WinForms.Controls.LocLabelEx();
			this.StartFullScreenCheckbox = new System.Windows.Forms.CheckBox();
			this.label3 = new BizHawk.WinForms.Controls.LocLabelEx();
			this.SingleInstanceModeCheckbox = new System.Windows.Forms.CheckBox();
			this.tabPage3 = new System.Windows.Forms.TabPage();
			this.NoMixedKeyPriorityCheckBox = new System.Windows.Forms.CheckBox();
			this.label6 = new BizHawk.WinForms.Controls.LocLabelEx();
			this.cbSkipWaterboxIntegrityChecks = new System.Windows.Forms.CheckBox();
			this.label5 = new BizHawk.WinForms.Controls.LocLabelEx();
			this.LuaDuringTurboCheckbox = new System.Windows.Forms.CheckBox();
			this.label12 = new BizHawk.WinForms.Controls.LocLabelEx();
			this.label13 = new BizHawk.WinForms.Controls.LocLabelEx();
			this.FrameAdvSkipLagCheckbox = new System.Windows.Forms.CheckBox();
			this.toolTip1 = new System.Windows.Forms.ToolTip(this.components);
			this.tabControl1.SuspendLayout();
			this.tabPage1.SuspendLayout();
			this.groupBox1.SuspendLayout();
			this.tabPage3.SuspendLayout();
			this.SuspendLayout();
			// 
			// OkBtn
			// 
			this.OkBtn.Anchor = ((System.Windows.Forms.AnchorStyles)((System.Windows.Forms.AnchorStyles.Bottom | System.Windows.Forms.AnchorStyles.Right)));
			this.OkBtn.Location = new System.Drawing.Point(280, 394);
			this.OkBtn.Name = "OkBtn";
			this.OkBtn.Size = new System.Drawing.Size(60, 23);
			this.OkBtn.TabIndex = 0;
			this.OkBtn.Text = "&OK";
			this.OkBtn.UseVisualStyleBackColor = true;
			this.OkBtn.Click += new System.EventHandler(this.OkBtn_Click);
			// 
			// CancelBtn
			// 
			this.CancelBtn.Anchor = ((System.Windows.Forms.AnchorStyles)((System.Windows.Forms.AnchorStyles.Bottom | System.Windows.Forms.AnchorStyles.Right)));
			this.CancelBtn.DialogResult = System.Windows.Forms.DialogResult.Cancel;
			this.CancelBtn.Location = new System.Drawing.Point(346, 394);
			this.CancelBtn.Name = "CancelBtn";
			this.CancelBtn.Size = new System.Drawing.Size(60, 23);
			this.CancelBtn.TabIndex = 1;
			this.CancelBtn.Text = "&Cancel";
			this.CancelBtn.UseVisualStyleBackColor = true;
			this.CancelBtn.Click += new System.EventHandler(this.CancelBtn_Click);
			// 
			// tabControl1
			// 
			this.tabControl1.Anchor = ((System.Windows.Forms.AnchorStyles)((((System.Windows.Forms.AnchorStyles.Top | System.Windows.Forms.AnchorStyles.Bottom) 
            | System.Windows.Forms.AnchorStyles.Left) 
            | System.Windows.Forms.AnchorStyles.Right)));
			this.tabControl1.Controls.Add(this.tabPage1);
			this.tabControl1.Controls.Add(this.tabPage3);
			this.tabControl1.Location = new System.Drawing.Point(9, 12);
			this.tabControl1.Name = "tabControl1";
			this.tabControl1.SelectedIndex = 0;
			this.tabControl1.Size = new System.Drawing.Size(400, 377);
			this.tabControl1.TabIndex = 2;
			// 
			// tabPage1
			// 
			this.tabPage1.Controls.Add(this.cbEnableGCAdapterSupport);
			this.tabPage1.Controls.Add(this.cbMergeLAndRModifierKeys);
			this.tabPage1.Controls.Add(this.HandleAlternateKeyboardLayoutsCheckBox);
			this.tabPage1.Controls.Add(this.NeverAskSaveCheckbox);
			this.tabPage1.Controls.Add(this.label2);
			this.tabPage1.Controls.Add(this.AcceptBackgroundInputCheckbox);
			this.tabPage1.Controls.Add(this.AcceptBackgroundInputControllerOnlyCheckBox);
			this.tabPage1.Controls.Add(this.label1);
			this.tabPage1.Controls.Add(this.RunInBackgroundCheckbox);
			this.tabPage1.Controls.Add(this.EnableContextMenuCheckbox);
			this.tabPage1.Controls.Add(this.PauseWhenMenuActivatedCheckbox);
			this.tabPage1.Controls.Add(this.groupBox1);
			this.tabPage1.Location = new System.Drawing.Point(4, 22);
			this.tabPage1.Name = "tabPage1";
			this.tabPage1.Padding = new System.Windows.Forms.Padding(3);
			this.tabPage1.Size = new System.Drawing.Size(392, 328);
			this.tabPage1.TabIndex = 0;
			this.tabPage1.Text = "General";
			this.tabPage1.UseVisualStyleBackColor = true;
			// 
			// cbEnableGCAdapterSupport
			// 
			this.cbEnableGCAdapterSupport.AutoSize = true;
			this.cbEnableGCAdapterSupport.Location = new System.Drawing.Point(7, 191);
			this.cbEnableGCAdapterSupport.Name = "cbEnableGCAdapterSupport";
			this.cbEnableGCAdapterSupport.Size = new System.Drawing.Size(320, 17);
			this.cbEnableGCAdapterSupport.TabIndex = 29;
			this.cbEnableGCAdapterSupport.Text = "Enable Wii U/Switch GameCube Adapter Support";
			this.cbEnableGCAdapterSupport.UseVisualStyleBackColor = true;
			// 
			// cbMergeLAndRModifierKeys
			// 
			this.cbMergeLAndRModifierKeys.AutoSize = true;
			this.cbMergeLAndRModifierKeys.Location = new System.Drawing.Point(7, 168);
			this.cbMergeLAndRModifierKeys.Name = "cbMergeLAndRModifierKeys";
			this.cbMergeLAndRModifierKeys.Size = new System.Drawing.Size(320, 17);
			this.cbMergeLAndRModifierKeys.TabIndex = 29;
			this.cbMergeLAndRModifierKeys.Text = "Merge L+R modifier keys e.g. Shift instead of LShift and RShift";
			this.cbMergeLAndRModifierKeys.UseVisualStyleBackColor = true;
			// 
			// HandleAlternateKeyboardLayoutsCheckBox
			// 
			this.HandleAlternateKeyboardLayoutsCheckBox.AutoSize = true;
			this.HandleAlternateKeyboardLayoutsCheckBox.Location = new System.Drawing.Point(7, 145);
			this.HandleAlternateKeyboardLayoutsCheckBox.Name = "HandleAlternateKeyboardLayoutsCheckBox";
			this.HandleAlternateKeyboardLayoutsCheckBox.Size = new System.Drawing.Size(320, 17);
			this.HandleAlternateKeyboardLayoutsCheckBox.TabIndex = 26;
			this.HandleAlternateKeyboardLayoutsCheckBox.Text = "Handle alternate keyboard layouts (e.g. Dvorak) [experimental]";
			this.HandleAlternateKeyboardLayoutsCheckBox.UseVisualStyleBackColor = true;
			// 
			// NeverAskSaveCheckbox
			// 
			this.NeverAskSaveCheckbox.AutoSize = true;
			this.NeverAskSaveCheckbox.Location = new System.Drawing.Point(6, 29);
			this.NeverAskSaveCheckbox.Name = "NeverAskSaveCheckbox";
			this.NeverAskSaveCheckbox.Size = new System.Drawing.Size(390, 17);
			this.NeverAskSaveCheckbox.TabIndex = 20;
			this.NeverAskSaveCheckbox.Text = "When EmuHawk is closing, skip \"unsaved changes\" prompts and discard all.";
			this.NeverAskSaveCheckbox.UseVisualStyleBackColor = true;
			// 
			// label2
			// 
			this.label2.Location = new System.Drawing.Point(26, 112);
			this.label2.MaximumSize = new System.Drawing.Size(380, 0);
			this.label2.Name = "label2";
			this.label2.Text = "\"Eavesdrops\" on keyboard and gamepad input while other programs are focused.";
			// 
			// AcceptBackgroundInputCheckbox
			// 
			this.AcceptBackgroundInputCheckbox.AutoSize = true;
			this.AcceptBackgroundInputCheckbox.Location = new System.Drawing.Point(6, 92);
			this.AcceptBackgroundInputCheckbox.Name = "AcceptBackgroundInputCheckbox";
			this.AcceptBackgroundInputCheckbox.Size = new System.Drawing.Size(146, 17);
			this.AcceptBackgroundInputCheckbox.TabIndex = 23;
			this.AcceptBackgroundInputCheckbox.Text = "Accept background input";
			this.AcceptBackgroundInputCheckbox.UseVisualStyleBackColor = true;
			this.AcceptBackgroundInputCheckbox.CheckedChanged += new System.EventHandler(this.AcceptBackgroundInputCheckbox_CheckedChanged);
			// 
			// AcceptBackgroundInputControllerOnlyCheckBox
			// 
			this.AcceptBackgroundInputControllerOnlyCheckBox.AutoSize = true;
			this.AcceptBackgroundInputControllerOnlyCheckBox.Enabled = false;
			this.AcceptBackgroundInputControllerOnlyCheckBox.Location = new System.Drawing.Point(156, 92);
			this.AcceptBackgroundInputControllerOnlyCheckBox.Name = "AcceptBackgroundInputControllerOnlyCheckBox";
			this.AcceptBackgroundInputControllerOnlyCheckBox.Size = new System.Drawing.Size(117, 17);
			this.AcceptBackgroundInputControllerOnlyCheckBox.TabIndex = 24;
			this.AcceptBackgroundInputControllerOnlyCheckBox.Text = "From controller only";
			this.AcceptBackgroundInputControllerOnlyCheckBox.UseVisualStyleBackColor = true;
			// 
			// label1
			// 
			this.label1.Location = new System.Drawing.Point(26, 72);
			this.label1.Name = "label1";
			this.label1.Text = "When this is set, the client will continue to run when it loses focus.";
			// 
			// RunInBackgroundCheckbox
			// 
			this.RunInBackgroundCheckbox.AutoSize = true;
			this.RunInBackgroundCheckbox.Location = new System.Drawing.Point(6, 52);
			this.RunInBackgroundCheckbox.Name = "RunInBackgroundCheckbox";
			this.RunInBackgroundCheckbox.Size = new System.Drawing.Size(117, 17);
			this.RunInBackgroundCheckbox.TabIndex = 21;
			this.RunInBackgroundCheckbox.Text = "Run in background";
			this.RunInBackgroundCheckbox.UseVisualStyleBackColor = true;
			// 
			// EnableContextMenuCheckbox
			// 
			this.EnableContextMenuCheckbox.AutoSize = true;
			this.EnableContextMenuCheckbox.Location = new System.Drawing.Point(196, 6);
			this.EnableContextMenuCheckbox.Name = "EnableContextMenuCheckbox";
			this.EnableContextMenuCheckbox.Size = new System.Drawing.Size(128, 17);
			this.EnableContextMenuCheckbox.TabIndex = 18;
			this.EnableContextMenuCheckbox.Text = "Enable Context Menu";
			this.EnableContextMenuCheckbox.UseVisualStyleBackColor = true;
			// 
			// PauseWhenMenuActivatedCheckbox
			// 
			this.PauseWhenMenuActivatedCheckbox.AutoSize = true;
			this.PauseWhenMenuActivatedCheckbox.Location = new System.Drawing.Point(6, 6);
			this.PauseWhenMenuActivatedCheckbox.Name = "PauseWhenMenuActivatedCheckbox";
			this.PauseWhenMenuActivatedCheckbox.Size = new System.Drawing.Size(161, 17);
			this.PauseWhenMenuActivatedCheckbox.TabIndex = 17;
			this.PauseWhenMenuActivatedCheckbox.Text = "Pause when menu activated";
			this.PauseWhenMenuActivatedCheckbox.UseVisualStyleBackColor = true;
			// 
			// groupBox1
			// 
			this.groupBox1.Controls.Add(this.locLabelEx1);
			this.groupBox1.Controls.Add(this.StartPausedCheckbox);
			this.groupBox1.Controls.Add(this.label14);
			this.groupBox1.Controls.Add(this.StartFullScreenCheckbox);
			this.groupBox1.Controls.Add(this.label3);
			this.groupBox1.Controls.Add(this.SingleInstanceModeCheckbox);
			this.groupBox1.Location = new System.Drawing.Point(6, 214);
			this.groupBox1.Name = "groupBox1";
			this.groupBox1.Size = new System.Drawing.Size(369, 133);
			this.groupBox1.TabIndex = 15;
			this.groupBox1.TabStop = false;
			this.groupBox1.Text = "Startup Options";
			// 
			// locLabelEx1
			// 
			this.locLabelEx1.Location = new System.Drawing.Point(26, 112);
			this.locLabelEx1.Name = "locLabelEx1";
			this.locLabelEx1.Text = "Note: Only a tiny subset of commandline args work (incl. rom path)";
			// 
			// StartPausedCheckbox
			// 
			this.StartPausedCheckbox.AutoSize = true;
			this.StartPausedCheckbox.Location = new System.Drawing.Point(6, 19);
			this.StartPausedCheckbox.Name = "StartPausedCheckbox";
			this.StartPausedCheckbox.Size = new System.Drawing.Size(86, 17);
			this.StartPausedCheckbox.TabIndex = 2;
			this.StartPausedCheckbox.Text = "Start paused";
			this.StartPausedCheckbox.UseVisualStyleBackColor = true;
			// 
			// label14
			// 
			this.label14.Location = new System.Drawing.Point(26, 99);
			this.label14.Name = "label14";
			this.label14.Text = "Note: Requires closing and reopening EmuHawk to take effect.";
			// 
			// StartFullScreenCheckbox
			// 
			this.StartFullScreenCheckbox.AutoSize = true;
			this.StartFullScreenCheckbox.Location = new System.Drawing.Point(6, 42);
			this.StartFullScreenCheckbox.Name = "StartFullScreenCheckbox";
			this.StartFullScreenCheckbox.Size = new System.Drawing.Size(110, 17);
			this.StartFullScreenCheckbox.TabIndex = 3;
			this.StartFullScreenCheckbox.Text = "Start in Fullscreen";
			this.StartFullScreenCheckbox.UseVisualStyleBackColor = true;
			// 
			// label3
			// 
			this.label3.Location = new System.Drawing.Point(26, 85);
			this.label3.Name = "label3";
			this.label3.Text = "Enable to force only one instance of EmuHawk at a time.";
			// 
			// SingleInstanceModeCheckbox
			// 
			this.SingleInstanceModeCheckbox.AutoSize = true;
			this.SingleInstanceModeCheckbox.Location = new System.Drawing.Point(6, 65);
			this.SingleInstanceModeCheckbox.Name = "SingleInstanceModeCheckbox";
			this.SingleInstanceModeCheckbox.Size = new System.Drawing.Size(127, 17);
			this.SingleInstanceModeCheckbox.TabIndex = 10;
			this.SingleInstanceModeCheckbox.Text = "Single instance mode";
			this.SingleInstanceModeCheckbox.UseVisualStyleBackColor = true;
			// 
			// tabPage3
			// 
			this.tabPage3.Controls.Add(this.NoMixedKeyPriorityCheckBox);
			this.tabPage3.Controls.Add(this.label6);
			this.tabPage3.Controls.Add(this.cbSkipWaterboxIntegrityChecks);
			this.tabPage3.Controls.Add(this.label5);
			this.tabPage3.Controls.Add(this.LuaDuringTurboCheckbox);
			this.tabPage3.Controls.Add(this.label12);
			this.tabPage3.Controls.Add(this.label13);
			this.tabPage3.Controls.Add(this.FrameAdvSkipLagCheckbox);
			this.tabPage3.Location = new System.Drawing.Point(4, 22);
			this.tabPage3.Name = "tabPage3";
			this.tabPage3.Size = new System.Drawing.Size(392, 322);
			this.tabPage3.TabIndex = 2;
			this.tabPage3.Text = "Advanced";
			this.tabPage3.UseVisualStyleBackColor = true;
			// 
			// NoMixedKeyPriorityCheckBox
			// 
			this.NoMixedKeyPriorityCheckBox.AutoSize = true;
			this.NoMixedKeyPriorityCheckBox.Location = new System.Drawing.Point(6, 294);
			this.NoMixedKeyPriorityCheckBox.Name = "NoMixedKeyPriorityCheckBox";
			this.NoMixedKeyPriorityCheckBox.Size = new System.Drawing.Size(288, 17);
			this.NoMixedKeyPriorityCheckBox.TabIndex = 25;
			this.NoMixedKeyPriorityCheckBox.Text = "Key Priority Toggle - Remove Mixed Key Priority Options";
			this.NoMixedKeyPriorityCheckBox.UseVisualStyleBackColor = true;
			// 
			// label6
			// 
			this.label6.Location = new System.Drawing.Point(27, 243);
			this.label6.Name = "label6";
			this.label6.Text = "Skips some integrity check exceptions in waterbox cores.\r\nUseful for romhackers. " +
    "Reboot core after changing.\r\nDon\'t bother reporting bugs if checked.\r\n";
			// 
			// cbSkipWaterboxIntegrityChecks
			// 
			this.cbSkipWaterboxIntegrityChecks.AutoSize = true;
			this.cbSkipWaterboxIntegrityChecks.Location = new System.Drawing.Point(6, 223);
			this.cbSkipWaterboxIntegrityChecks.Name = "cbSkipWaterboxIntegrityChecks";
			this.cbSkipWaterboxIntegrityChecks.Size = new System.Drawing.Size(170, 17);
			this.cbSkipWaterboxIntegrityChecks.TabIndex = 18;
			this.cbSkipWaterboxIntegrityChecks.Text = "Skip waterbox integrity checks";
			this.cbSkipWaterboxIntegrityChecks.UseVisualStyleBackColor = true;
			// 
			// label5
			// 
			this.label5.Location = new System.Drawing.Point(27, 194);
			this.label5.Name = "label5";
			this.label5.Text = "Will prevent many Out Of Memory crashes during long movies.\r\nYou must restart the" +
    " program after changing this.";
			// 
			// LuaDuringTurboCheckbox
			// 
			this.LuaDuringTurboCheckbox.AutoSize = true;
			this.LuaDuringTurboCheckbox.Location = new System.Drawing.Point(6, 151);
			this.LuaDuringTurboCheckbox.Name = "LuaDuringTurboCheckbox";
			this.LuaDuringTurboCheckbox.Size = new System.Drawing.Size(166, 17);
			this.LuaDuringTurboCheckbox.TabIndex = 15;
			this.LuaDuringTurboCheckbox.Text = "Run lua scripts when turboing";
			this.LuaDuringTurboCheckbox.UseVisualStyleBackColor = true;
			// 
			// label12
			// 
			this.label12.Location = new System.Drawing.Point(27, 135);
			this.label12.Name = "label12";
			this.label12.Text = "frames in which no input was polled (lag frames)";
			// 
			// label13
			// 
			this.label13.Location = new System.Drawing.Point(27, 122);
			this.label13.Name = "label13";
			this.label13.Text = "When enabled, the frame advance button will skip over";
			// 
			// FrameAdvSkipLagCheckbox
			// 
			this.FrameAdvSkipLagCheckbox.AutoSize = true;
			this.FrameAdvSkipLagCheckbox.Location = new System.Drawing.Point(6, 102);
			this.FrameAdvSkipLagCheckbox.Name = "FrameAdvSkipLagCheckbox";
			this.FrameAdvSkipLagCheckbox.Size = new System.Drawing.Size(241, 17);
			this.FrameAdvSkipLagCheckbox.TabIndex = 12;
			this.FrameAdvSkipLagCheckbox.Text = "Frame advance button skips non-input frames";
			this.FrameAdvSkipLagCheckbox.UseVisualStyleBackColor = true;
			// 
			// EmuHawkOptions
			// 
			this.AcceptButton = this.OkBtn;
			this.AutoScaleDimensions = new System.Drawing.SizeF(6F, 13F);
			this.AutoScaleMode = System.Windows.Forms.AutoScaleMode.Font;
			this.CancelButton = this.CancelBtn;
			this.ClientSize = new System.Drawing.Size(418, 424);
			this.Controls.Add(this.tabControl1);
			this.Controls.Add(this.CancelBtn);
			this.Controls.Add(this.OkBtn);
			this.Name = "EmuHawkOptions";
			this.ShowIcon = false;
			this.StartPosition = System.Windows.Forms.FormStartPosition.CenterParent;
			this.Text = "Customization Options";
			this.Load += new System.EventHandler(this.GuiOptions_Load);
			this.tabControl1.ResumeLayout(false);
			this.tabPage1.ResumeLayout(false);
			this.tabPage1.PerformLayout();
			this.groupBox1.ResumeLayout(false);
			this.groupBox1.PerformLayout();
			this.tabPage3.ResumeLayout(false);
			this.tabPage3.PerformLayout();
			this.ResumeLayout(false);

		}

		#endregion

		private System.Windows.Forms.Button OkBtn;
		private System.Windows.Forms.Button CancelBtn;
		private System.Windows.Forms.TabControl tabControl1;
		private System.Windows.Forms.TabPage tabPage1;
		private System.Windows.Forms.TabPage tabPage3;
		private System.Windows.Forms.ToolTip toolTip1;
		private System.Windows.Forms.CheckBox FrameAdvSkipLagCheckbox;
		private BizHawk.WinForms.Controls.LocLabelEx label12;
		private BizHawk.WinForms.Controls.LocLabelEx label13;
		private System.Windows.Forms.CheckBox LuaDuringTurboCheckbox;
		private BizHawk.WinForms.Controls.LocLabelEx label5;
		private BizHawk.WinForms.Controls.LocLabelEx label6;
		private System.Windows.Forms.CheckBox cbSkipWaterboxIntegrityChecks;
		private System.Windows.Forms.CheckBox HandleAlternateKeyboardLayoutsCheckBox;
		private System.Windows.Forms.CheckBox NeverAskSaveCheckbox;
		private BizHawk.WinForms.Controls.LocLabelEx label2;
		private System.Windows.Forms.CheckBox AcceptBackgroundInputCheckbox;
		private System.Windows.Forms.CheckBox AcceptBackgroundInputControllerOnlyCheckBox;
		private BizHawk.WinForms.Controls.LocLabelEx label1;
		private System.Windows.Forms.CheckBox RunInBackgroundCheckbox;
		private System.Windows.Forms.CheckBox EnableContextMenuCheckbox;
		private System.Windows.Forms.CheckBox PauseWhenMenuActivatedCheckbox;
		private System.Windows.Forms.GroupBox groupBox1;
		private System.Windows.Forms.CheckBox StartPausedCheckbox;
		private BizHawk.WinForms.Controls.LocLabelEx label14;
		private System.Windows.Forms.CheckBox StartFullScreenCheckbox;
		private BizHawk.WinForms.Controls.LocLabelEx label3;
		private System.Windows.Forms.CheckBox SingleInstanceModeCheckbox;
		private System.Windows.Forms.CheckBox NoMixedKeyPriorityCheckBox;
		private WinForms.Controls.LocLabelEx locLabelEx1;
		private System.Windows.Forms.CheckBox cbMergeLAndRModifierKeys;
		private System.Windows.Forms.CheckBox cbEnableGCAdapterSupport;
	}
}
