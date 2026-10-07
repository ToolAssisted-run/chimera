namespace Chimera.Client.GUI
{
	partial class GreenzoneBox
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

		#region Component Designer generated code

		/// <summary>
		/// Required method for Designer support - do not modify
		/// the contents of this method with the code editor.
		/// </summary>
		private void InitializeComponent()
		{
			this.components = new System.ComponentModel.Container();
			this.GreenzoneGroupBox = new System.Windows.Forms.GroupBox();
			this.EveryFrameRadio = new System.Windows.Forms.RadioButton();
			this.EveryNRadio = new System.Windows.Forms.RadioButton();
			this.PeriodNum = new System.Windows.Forms.NumericUpDown();
			this.FramesLabel = new System.Windows.Forms.Label();
			this.OffRadio = new System.Windows.Forms.RadioButton();
			this.toolTip1 = new System.Windows.Forms.ToolTip(this.components);
			this.GreenzoneGroupBox.SuspendLayout();
			((System.ComponentModel.ISupportInitialize)(this.PeriodNum)).BeginInit();
			this.SuspendLayout();
			// 
			// GreenzoneGroupBox
			// 
			this.GreenzoneGroupBox.Controls.Add(this.EveryFrameRadio);
			this.GreenzoneGroupBox.Controls.Add(this.EveryNRadio);
			this.GreenzoneGroupBox.Controls.Add(this.PeriodNum);
			this.GreenzoneGroupBox.Controls.Add(this.FramesLabel);
			this.GreenzoneGroupBox.Controls.Add(this.OffRadio);
			this.GreenzoneGroupBox.Dock = System.Windows.Forms.DockStyle.Fill;
			this.GreenzoneGroupBox.Location = new System.Drawing.Point(0, 0);
			this.GreenzoneGroupBox.Name = "GreenzoneGroupBox";
			this.GreenzoneGroupBox.Size = new System.Drawing.Size(198, 80);
			this.GreenzoneGroupBox.TabIndex = 0;
			this.GreenzoneGroupBox.TabStop = false;
			this.GreenzoneGroupBox.Text = "Greenzone";
			// 
			// EveryFrameRadio
			// 
			this.EveryFrameRadio.AutoSize = true;
			this.EveryFrameRadio.Checked = true;
			this.EveryFrameRadio.Location = new System.Drawing.Point(10, 17);
			this.EveryFrameRadio.Name = "EveryFrameRadio";
			this.EveryFrameRadio.Size = new System.Drawing.Size(81, 17);
			this.EveryFrameRadio.TabIndex = 0;
			this.EveryFrameRadio.TabStop = true;
			this.EveryFrameRadio.Text = "Every frame";
			this.EveryFrameRadio.UseVisualStyleBackColor = true;
			// 
			// EveryNRadio
			// 
			this.EveryNRadio.AutoSize = true;
			this.EveryNRadio.Location = new System.Drawing.Point(10, 38);
			this.EveryNRadio.Name = "EveryNRadio";
			this.EveryNRadio.Size = new System.Drawing.Size(52, 17);
			this.EveryNRadio.TabIndex = 1;
			this.EveryNRadio.Text = "Every";
			this.EveryNRadio.UseVisualStyleBackColor = true;
			// 
			// PeriodNum
			// 
			this.PeriodNum.Location = new System.Drawing.Point(64, 36);
			this.PeriodNum.Maximum = new decimal(new int[] {
            999,
            0,
            0,
            0});
			this.PeriodNum.Minimum = new decimal(new int[] {
            2,
            0,
            0,
            0});
			this.PeriodNum.Name = "PeriodNum";
			this.PeriodNum.Size = new System.Drawing.Size(44, 20);
			this.PeriodNum.TabIndex = 2;
			this.PeriodNum.Value = new decimal(new int[] {
            2,
            0,
            0,
            0});
			this.PeriodNum.ValueChanged += new System.EventHandler(this.PeriodNum_ValueChanged);
			// 
			// FramesLabel
			// 
			this.FramesLabel.AutoSize = true;
			this.FramesLabel.Location = new System.Drawing.Point(111, 39);
			this.FramesLabel.Name = "FramesLabel";
			this.FramesLabel.Size = new System.Drawing.Size(38, 13);
			this.FramesLabel.TabIndex = 3;
			this.FramesLabel.Text = "frames";
			// 
			// OffRadio
			// 
			this.OffRadio.AutoSize = true;
			this.OffRadio.Location = new System.Drawing.Point(10, 59);
			this.OffRadio.Name = "OffRadio";
			this.OffRadio.Size = new System.Drawing.Size(39, 17);
			this.OffRadio.TabIndex = 4;
			this.OffRadio.Text = "Off";
			this.OffRadio.UseVisualStyleBackColor = true;
			// 
			// GreenzoneBox
			// 
			this.AutoScaleDimensions = new System.Drawing.SizeF(6F, 13F);
			this.AutoScaleMode = System.Windows.Forms.AutoScaleMode.Font;
			this.Controls.Add(this.GreenzoneGroupBox);
			this.Name = "GreenzoneBox";
			this.Size = new System.Drawing.Size(198, 80);
			this.GreenzoneGroupBox.ResumeLayout(false);
			this.GreenzoneGroupBox.PerformLayout();
			((System.ComponentModel.ISupportInitialize)(this.PeriodNum)).EndInit();
			this.ResumeLayout(false);

		}

		#endregion

		private System.Windows.Forms.GroupBox GreenzoneGroupBox;
		private System.Windows.Forms.RadioButton EveryFrameRadio;
		private System.Windows.Forms.RadioButton EveryNRadio;
		private System.Windows.Forms.NumericUpDown PeriodNum;
		private System.Windows.Forms.Label FramesLabel;
		private System.Windows.Forms.RadioButton OffRadio;
		private System.Windows.Forms.ToolTip toolTip1;
	}
}
