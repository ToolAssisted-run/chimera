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
			this.SparseRadio = new System.Windows.Forms.RadioButton();
			this.SparsestRadio = new System.Windows.Forms.RadioButton();
			this.OffRadio = new System.Windows.Forms.RadioButton();
			this.toolTip1 = new System.Windows.Forms.ToolTip(this.components);
			this.GreenzoneGroupBox.SuspendLayout();
			this.SuspendLayout();
			//
			// GreenzoneGroupBox
			//
			this.GreenzoneGroupBox.Controls.Add(this.EveryFrameRadio);
			this.GreenzoneGroupBox.Controls.Add(this.SparseRadio);
			this.GreenzoneGroupBox.Controls.Add(this.SparsestRadio);
			this.GreenzoneGroupBox.Controls.Add(this.OffRadio);
			this.GreenzoneGroupBox.Dock = System.Windows.Forms.DockStyle.Fill;
			this.GreenzoneGroupBox.Location = new System.Drawing.Point(0, 0);
			this.GreenzoneGroupBox.Name = "GreenzoneGroupBox";
			this.GreenzoneGroupBox.Size = new System.Drawing.Size(198, 100);
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
			// SparseRadio
			//
			this.SparseRadio.AutoSize = true;
			this.SparseRadio.Location = new System.Drawing.Point(10, 37);
			this.SparseRadio.Name = "SparseRadio";
			this.SparseRadio.Size = new System.Drawing.Size(100, 17);
			this.SparseRadio.TabIndex = 1;
			this.SparseRadio.Text = "Every 32 frames";
			this.SparseRadio.UseVisualStyleBackColor = true;
			//
			// SparsestRadio
			//
			this.SparsestRadio.AutoSize = true;
			this.SparsestRadio.Location = new System.Drawing.Point(10, 57);
			this.SparsestRadio.Name = "SparsestRadio";
			this.SparsestRadio.Size = new System.Drawing.Size(112, 17);
			this.SparsestRadio.TabIndex = 2;
			this.SparsestRadio.Text = "Every 1000 frames";
			this.SparsestRadio.UseVisualStyleBackColor = true;
			//
			// OffRadio
			//
			this.OffRadio.AutoSize = true;
			this.OffRadio.Location = new System.Drawing.Point(10, 77);
			this.OffRadio.Name = "OffRadio";
			this.OffRadio.Size = new System.Drawing.Size(39, 17);
			this.OffRadio.TabIndex = 3;
			this.OffRadio.Text = "Off";
			this.OffRadio.UseVisualStyleBackColor = true;
			//
			// GreenzoneBox
			//
			this.AutoScaleDimensions = new System.Drawing.SizeF(6F, 13F);
			this.AutoScaleMode = System.Windows.Forms.AutoScaleMode.Font;
			this.Controls.Add(this.GreenzoneGroupBox);
			this.Name = "GreenzoneBox";
			this.Size = new System.Drawing.Size(198, 100);
			this.GreenzoneGroupBox.ResumeLayout(false);
			this.GreenzoneGroupBox.PerformLayout();
			this.ResumeLayout(false);

		}

		#endregion

		private System.Windows.Forms.GroupBox GreenzoneGroupBox;
		private System.Windows.Forms.RadioButton EveryFrameRadio;
		private System.Windows.Forms.RadioButton SparseRadio;
		private System.Windows.Forms.RadioButton SparsestRadio;
		private System.Windows.Forms.RadioButton OffRadio;
		private System.Windows.Forms.ToolTip toolTip1;
	}
}
