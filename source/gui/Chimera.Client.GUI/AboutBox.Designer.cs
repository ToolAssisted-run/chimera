namespace Chimera.Client.GUI
{
	partial class AboutBox
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
			this.OK = new System.Windows.Forms.Button();
			this.pictureBox1 = new System.Windows.Forms.PictureBox();
			this.linkLabel1 = new System.Windows.Forms.LinkLabel();
			this.label3 = new Chimera.WinForms.Controls.LocLabelEx();
			this.label4 = new Chimera.WinForms.Controls.LocLabelEx();
			this.VersionLabel = new Chimera.WinForms.Controls.LocLabelEx();
			this.btnCopyHash = new System.Windows.Forms.Button();
			this.linkLabel2 = new System.Windows.Forms.LinkLabel();
			this.linkLabel3 = new System.Windows.Forms.LinkLabel();
			this.DateLabel = new Chimera.WinForms.Controls.LocLabelEx();
			this.linkLabelBizHawk = new System.Windows.Forms.LinkLabel();
			((System.ComponentModel.ISupportInitialize)(this.pictureBox1)).BeginInit();
			this.SuspendLayout();
			// 
			// OK
			// 
			this.OK.Anchor = ((System.Windows.Forms.AnchorStyles)((System.Windows.Forms.AnchorStyles.Bottom | System.Windows.Forms.AnchorStyles.Right)));
			this.OK.DialogResult = System.Windows.Forms.DialogResult.Cancel;
			this.OK.Location = new System.Drawing.Point(361, 195);
			this.OK.Name = "OK";
			this.OK.Size = new System.Drawing.Size(75, 23);
			this.OK.TabIndex = 0;
			this.OK.Text = "&OK";
			this.OK.UseVisualStyleBackColor = true;
			this.OK.Click += new System.EventHandler(this.OK_Click);
			// 
			// pictureBox1
			// 
			this.pictureBox1.Location = new System.Drawing.Point(12, 10);
			this.pictureBox1.Name = "pictureBox1";
			this.pictureBox1.Size = new System.Drawing.Size(164, 164);
			this.pictureBox1.SizeMode = System.Windows.Forms.PictureBoxSizeMode.CenterImage;
			this.pictureBox1.TabIndex = 1;
			this.pictureBox1.TabStop = false;
			// 
			// linkLabel1
			// 
			this.linkLabel1.Anchor = ((System.Windows.Forms.AnchorStyles)((System.Windows.Forms.AnchorStyles.Bottom | System.Windows.Forms.AnchorStyles.Right)));
			this.linkLabel1.AutoSize = true;
			this.linkLabel1.Location = new System.Drawing.Point(245, 200);
			this.linkLabel1.Name = "linkLabel1";
			this.linkLabel1.Size = new System.Drawing.Size(102, 13);
			this.linkLabel1.TabIndex = 2;
			this.linkLabel1.TabStop = true;
			this.linkLabel1.Text = "Chimera Homepage";
			this.linkLabel1.LinkClicked += new System.Windows.Forms.LinkLabelLinkClickedEventHandler(this.linkLabel1_LinkClicked);
			// 
			// label3
			// 
			this.label3.Font = new System.Drawing.Font("Microsoft Sans Serif", 12F, System.Drawing.FontStyle.Bold, System.Drawing.GraphicsUnit.Point, ((byte)(0)));
			this.label3.Location = new System.Drawing.Point(197, 10);
			this.label3.Name = "label3";
			this.label3.Text = "Chimera";
			// 
			// label4
			// 
			this.label4.Font = new System.Drawing.Font("Microsoft Sans Serif", 9.75F, System.Drawing.FontStyle.Italic, System.Drawing.GraphicsUnit.Point, ((byte)(0)));
			this.label4.Location = new System.Drawing.Point(207, 31);
			this.label4.Name = "label4";
			this.label4.Text = "\"A modular emulation frontend\"";
			// 
			// VersionLabel
			// 
			this.VersionLabel.Location = new System.Drawing.Point(198, 75);
			this.VersionLabel.Name = "VersionLabel";
			this.VersionLabel.Text = "versioninfo goes here";
			// 
			// btnCopyHash
			// 
			this.btnCopyHash.Anchor = ((System.Windows.Forms.AnchorStyles)((System.Windows.Forms.AnchorStyles.Bottom | System.Windows.Forms.AnchorStyles.Left)));
			this.btnCopyHash.AutoSize = true;
			this.btnCopyHash.AutoSizeMode = System.Windows.Forms.AutoSizeMode.GrowAndShrink;
			this.btnCopyHash.Location = new System.Drawing.Point(12, 196);
			this.btnCopyHash.Name = "btnCopyHash";
			this.btnCopyHash.Size = new System.Drawing.Size(22, 22);
			this.btnCopyHash.TabIndex = 18;
			this.btnCopyHash.UseVisualStyleBackColor = true;
			this.btnCopyHash.Click += new System.EventHandler(this.btnCopyHash_Click);
			// 
			// linkLabel2
			// 
			this.linkLabel2.Anchor = ((System.Windows.Forms.AnchorStyles)((System.Windows.Forms.AnchorStyles.Bottom | System.Windows.Forms.AnchorStyles.Left)));
			this.linkLabel2.AutoSize = true;
			this.linkLabel2.Location = new System.Drawing.Point(40, 200);
			this.linkLabel2.Name = "linkLabel2";
			this.linkLabel2.Size = new System.Drawing.Size(100, 13);
			this.linkLabel2.TabIndex = 19;
			this.linkLabel2.TabStop = true;
			this.linkLabel2.Text = "Commit #XXXXXXX";
			this.linkLabel2.LinkClicked += new System.Windows.Forms.LinkLabelLinkClickedEventHandler(this.linkLabel2_LinkClicked);
			// 
			// linkLabel3
			// 
			this.linkLabel3.AutoSize = true;
			this.linkLabel3.Location = new System.Drawing.Point(198, 112);
			this.linkLabel3.Name = "linkLabel3";
			this.linkLabel3.Size = new System.Drawing.Size(63, 13);
			this.linkLabel3.TabIndex = 20;
			this.linkLabel3.TabStop = true;
			this.linkLabel3.Text = "Credits";
			this.linkLabel3.LinkClicked += new System.Windows.Forms.LinkLabelLinkClickedEventHandler(this.linkLabel3_LinkClicked);
			// 
			// DateLabel
			// 
			this.DateLabel.Location = new System.Drawing.Point(198, 91);
			this.DateLabel.Name = "DateLabel";
			this.DateLabel.Text = "timestamp goes here";
			// 
			// linkLabelBizHawk
			// 
			this.linkLabelBizHawk.AutoSize = true;
			this.linkLabelBizHawk.LinkArea = new System.Windows.Forms.LinkArea(21, 7);
			this.linkLabelBizHawk.Location = new System.Drawing.Point(198, 137);
			this.linkLabelBizHawk.Name = "linkLabelBizHawk";
			this.linkLabelBizHawk.Size = new System.Drawing.Size(170, 17);
			this.linkLabelBizHawk.TabIndex = 21;
			this.linkLabelBizHawk.TabStop = true;
			this.linkLabelBizHawk.Text = "A derivative fork of BizHawk";
			this.linkLabelBizHawk.UseCompatibleTextRendering = true;
			this.linkLabelBizHawk.LinkClicked += new System.Windows.Forms.LinkLabelLinkClickedEventHandler(this.linkLabelBizHawk_LinkClicked);
			// 
			// AboutBox
			// 
			this.AcceptButton = this.OK;
			this.AutoScaleDimensions = new System.Drawing.SizeF(6F, 13F);
			this.AutoScaleMode = System.Windows.Forms.AutoScaleMode.Font;
			this.CancelButton = this.OK;
			this.ClientSize = new System.Drawing.Size(448, 230);
			this.Controls.Add(this.DateLabel);
			this.Controls.Add(this.linkLabelBizHawk);
			this.Controls.Add(this.linkLabel3);
			this.Controls.Add(this.linkLabel2);
			this.Controls.Add(this.btnCopyHash);
			this.Controls.Add(this.VersionLabel);
			this.Controls.Add(this.label4);
			this.Controls.Add(this.label3);
			this.Controls.Add(this.linkLabel1);
			this.Controls.Add(this.pictureBox1);
			this.Controls.Add(this.OK);
			this.MinimumSize = new System.Drawing.Size(453, 240);
			this.Name = "AboutBox";
			this.StartPosition = System.Windows.Forms.FormStartPosition.CenterParent;
			this.Text = "About Chimera";
			this.Load += new System.EventHandler(this.AboutBox_Load);
			((System.ComponentModel.ISupportInitialize)(this.pictureBox1)).EndInit();
			this.ResumeLayout(false);
			this.PerformLayout();

		}

		#endregion

		private System.Windows.Forms.Button OK;
		private System.Windows.Forms.PictureBox pictureBox1;
		private System.Windows.Forms.LinkLabel linkLabel1;
		private Chimera.WinForms.Controls.LocLabelEx label3;
		private Chimera.WinForms.Controls.LocLabelEx label4;
		//private System.Windows.Forms.TextBox textBox1;
		private Chimera.WinForms.Controls.LocLabelEx VersionLabel;
		private System.Windows.Forms.Button btnCopyHash;
		private System.Windows.Forms.LinkLabel linkLabel2;
        private System.Windows.Forms.LinkLabel linkLabel3;
		private Chimera.WinForms.Controls.LocLabelEx DateLabel;
		private System.Windows.Forms.LinkLabel linkLabelBizHawk;
	}
}