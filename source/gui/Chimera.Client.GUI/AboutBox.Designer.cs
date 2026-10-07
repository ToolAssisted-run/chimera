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
			this.LogoBox = new System.Windows.Forms.PictureBox();
			this.NameLabel = new Chimera.WinForms.Controls.LocLabelEx();
			this.CommitLink = new System.Windows.Forms.LinkLabel();
			this.RepoLink = new System.Windows.Forms.LinkLabel();
			this.SiteLink = new System.Windows.Forms.LinkLabel();
			this.OK = new System.Windows.Forms.Button();
			((System.ComponentModel.ISupportInitialize)(this.LogoBox)).BeginInit();
			this.SuspendLayout();
			// 
			// LogoBox
			// 
			this.LogoBox.Location = new System.Drawing.Point(128, 14);
			this.LogoBox.Name = "LogoBox";
			this.LogoBox.Size = new System.Drawing.Size(64, 64);
			this.LogoBox.SizeMode = System.Windows.Forms.PictureBoxSizeMode.Zoom;
			this.LogoBox.TabIndex = 1;
			this.LogoBox.TabStop = false;
			// 
			// NameLabel
			// 
			this.NameLabel.Font = new System.Drawing.Font("Microsoft Sans Serif", 12F, System.Drawing.FontStyle.Bold, System.Drawing.GraphicsUnit.Point, ((byte)(0)));
			this.NameLabel.Location = new System.Drawing.Point(124, 88);
			this.NameLabel.Name = "NameLabel";
			this.NameLabel.Text = "Chimera";
			// 
			// CommitLink
			// 
			this.CommitLink.AutoSize = true;
			this.CommitLink.Location = new System.Drawing.Point(60, 120);
			this.CommitLink.Name = "CommitLink";
			this.CommitLink.Size = new System.Drawing.Size(200, 17);
			this.CommitLink.TabIndex = 2;
			this.CommitLink.TabStop = true;
			this.CommitLink.Text = "Commit XXXXXXXXX (date and time)";
			this.CommitLink.UseCompatibleTextRendering = true;
			this.CommitLink.LinkClicked += new System.Windows.Forms.LinkLabelLinkClickedEventHandler(this.Link_LinkClicked);
			// 
			// RepoLink
			// 
			this.RepoLink.AutoSize = true;
			this.RepoLink.Location = new System.Drawing.Point(60, 144);
			this.RepoLink.Name = "RepoLink";
			this.RepoLink.Size = new System.Drawing.Size(200, 13);
			this.RepoLink.TabIndex = 3;
			this.RepoLink.TabStop = true;
			this.RepoLink.Text = "github.com";
			this.RepoLink.LinkClicked += new System.Windows.Forms.LinkLabelLinkClickedEventHandler(this.Link_LinkClicked);
			// 
			// SiteLink
			// 
			this.SiteLink.AutoSize = true;
			this.SiteLink.Location = new System.Drawing.Point(60, 166);
			this.SiteLink.Name = "SiteLink";
			this.SiteLink.Size = new System.Drawing.Size(200, 13);
			this.SiteLink.TabIndex = 4;
			this.SiteLink.TabStop = true;
			this.SiteLink.Text = "toolAssisted.run";
			this.SiteLink.LinkClicked += new System.Windows.Forms.LinkLabelLinkClickedEventHandler(this.Link_LinkClicked);
			// 
			// OK
			// 
			this.OK.DialogResult = System.Windows.Forms.DialogResult.Cancel;
			this.OK.Location = new System.Drawing.Point(122, 198);
			this.OK.Name = "OK";
			this.OK.Size = new System.Drawing.Size(75, 23);
			this.OK.TabIndex = 0;
			this.OK.Text = "&OK";
			this.OK.UseVisualStyleBackColor = true;
			this.OK.Click += new System.EventHandler(this.OK_Click);
			// 
			// AboutBox
			// 
			this.AcceptButton = this.OK;
			this.AutoScaleDimensions = new System.Drawing.SizeF(6F, 13F);
			this.AutoScaleMode = System.Windows.Forms.AutoScaleMode.Font;
			this.CancelButton = this.OK;
			this.ClientSize = new System.Drawing.Size(320, 234);
			this.Controls.Add(this.LogoBox);
			this.Controls.Add(this.NameLabel);
			this.Controls.Add(this.CommitLink);
			this.Controls.Add(this.RepoLink);
			this.Controls.Add(this.SiteLink);
			this.Controls.Add(this.OK);
			this.FormBorderStyle = System.Windows.Forms.FormBorderStyle.FixedDialog;
			this.MaximizeBox = false;
			this.MinimizeBox = false;
			this.Name = "AboutBox";
			this.StartPosition = System.Windows.Forms.FormStartPosition.CenterParent;
			this.Text = "About Chimera";
			this.Load += new System.EventHandler(this.AboutBox_Load);
			((System.ComponentModel.ISupportInitialize)(this.LogoBox)).EndInit();
			this.ResumeLayout(false);
			this.PerformLayout();

		}

		#endregion

		private System.Windows.Forms.PictureBox LogoBox;
		private Chimera.WinForms.Controls.LocLabelEx NameLabel;
		private System.Windows.Forms.LinkLabel CommitLink;
		private System.Windows.Forms.LinkLabel RepoLink;
		private System.Windows.Forms.LinkLabel SiteLink;
		private System.Windows.Forms.Button OK;
	}
}
