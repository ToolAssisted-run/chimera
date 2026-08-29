using Chimera.WinForms.Controls;

namespace Chimera.Client.GUI
{
	partial class TAStudio
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
			this.TASMenu = new Chimera.WinForms.Controls.MenuStripEx();
			this.toolStripSeparator3 = new Chimera.WinForms.Controls.ToolStripSeparatorEx();
			this.toolStripSeparator1 = new Chimera.WinForms.Controls.ToolStripSeparatorEx();
			this.saveSelectionToMacroToolStripMenuItem = new Chimera.WinForms.Controls.ToolStripMenuItemEx();
			this.placeMacroAtSelectionToolStripMenuItem = new Chimera.WinForms.Controls.ToolStripMenuItemEx();
			this.recentMacrosToolStripMenuItem = new Chimera.WinForms.Controls.ToolStripMenuItemEx();
			this.toolStripSeparator22 = new Chimera.WinForms.Controls.ToolStripSeparatorEx();
			this.EditSubMenu = new Chimera.WinForms.Controls.ToolStripMenuItemEx();
			this.UndoMenuItem = new Chimera.WinForms.Controls.ToolStripMenuItemEx();
			this.RedoMenuItem = new Chimera.WinForms.Controls.ToolStripMenuItemEx();
			this.showUndoHistoryToolStripMenuItem = new Chimera.WinForms.Controls.ToolStripMenuItemEx();
			this.SelectionUndoMenuItem = new Chimera.WinForms.Controls.ToolStripMenuItemEx();
			this.SelectionRedoMenuItem = new Chimera.WinForms.Controls.ToolStripMenuItemEx();
			this.toolStripSeparator5 = new Chimera.WinForms.Controls.ToolStripSeparatorEx();
			this.DeselectMenuItem = new Chimera.WinForms.Controls.ToolStripMenuItemEx();
			this.SelectBetweenMarkersMenuItem = new Chimera.WinForms.Controls.ToolStripMenuItemEx();
			this.SelectAllMenuItem = new Chimera.WinForms.Controls.ToolStripMenuItemEx();
			this.ReselectClipboardMenuItem = new Chimera.WinForms.Controls.ToolStripMenuItemEx();
			this.GoToFrameMenuItem = new Chimera.WinForms.Controls.ToolStripMenuItemEx();
			this.toolStripSeparator7 = new Chimera.WinForms.Controls.ToolStripSeparatorEx();
			this.CopyMenuItem = new Chimera.WinForms.Controls.ToolStripMenuItemEx();
			this.PasteMenuItem = new Chimera.WinForms.Controls.ToolStripMenuItemEx();
			this.PasteInsertMenuItem = new Chimera.WinForms.Controls.ToolStripMenuItemEx();
			this.CutMenuItem = new Chimera.WinForms.Controls.ToolStripMenuItemEx();
			this.toolStripSeparator8 = new Chimera.WinForms.Controls.ToolStripSeparatorEx();
			this.ClearFramesMenuItem = new Chimera.WinForms.Controls.ToolStripMenuItemEx();
			this.DeleteFramesMenuItem = new Chimera.WinForms.Controls.ToolStripMenuItemEx();
			this.InsertFrameMenuItem = new Chimera.WinForms.Controls.ToolStripMenuItemEx();
			this.InsertNumFramesMenuItem = new Chimera.WinForms.Controls.ToolStripMenuItemEx();
			this.CloneFramesMenuItem = new Chimera.WinForms.Controls.ToolStripMenuItemEx();
			this.CloneFramesXTimesMenuItem = new Chimera.WinForms.Controls.ToolStripMenuItemEx();
			this.toolStripSeparator6 = new Chimera.WinForms.Controls.ToolStripSeparatorEx();
			this.TruncateMenuItem = new Chimera.WinForms.Controls.ToolStripMenuItemEx();
			this.ClearGreenzoneMenuItem = new Chimera.WinForms.Controls.ToolStripMenuItemEx();
			this.GreenzoneICheckSeparator = new Chimera.WinForms.Controls.ToolStripSeparatorEx();
			this.StateHistoryIntegrityCheckMenuItem = new Chimera.WinForms.Controls.ToolStripMenuItemEx();
			this.MetaSubMenu = new Chimera.WinForms.Controls.ToolStripMenuItemEx();
			this.HeaderMenuItem = new Chimera.WinForms.Controls.ToolStripMenuItemEx();
			this.CommentsMenuItem = new Chimera.WinForms.Controls.ToolStripMenuItemEx();
			this.SubtitlesMenuItem = new Chimera.WinForms.Controls.ToolStripMenuItemEx();
			this.SettingsSubMenu = new Chimera.WinForms.Controls.ToolStripMenuItemEx();
			this.TAStudioSettingsToolStripMenuItem = new System.Windows.Forms.ToolStripMenuItem();
			this.MoveWithMainWindowMenuItem = new System.Windows.Forms.ToolStripMenuItem();
			this.ColumnsSubMenu = new Chimera.WinForms.Controls.ToolStripMenuItemEx();
			this.toolStripSeparator19 = new Chimera.WinForms.Controls.ToolStripSeparatorEx();
			this.HelpSubMenu = new Chimera.WinForms.Controls.ToolStripMenuItemEx();
			this.TASEditorManualOnlineMenuItem = new Chimera.WinForms.Controls.ToolStripMenuItemEx();
			this.aboutToolStripMenuItem = new Chimera.WinForms.Controls.ToolStripMenuItemEx();
			this.toolStripSeparator10 = new Chimera.WinForms.Controls.ToolStripSeparatorEx();
			this.EnableTooltipsMenuItem = new Chimera.WinForms.Controls.ToolStripMenuItemEx();
			this.TasStatusStrip = new Chimera.WinForms.Controls.StatusStripEx();
			this.MessageStatusLabel = new System.Windows.Forms.ToolStripStatusLabel();
			this.ProgressBar = new System.Windows.Forms.ToolStripProgressBar();
			this.toolStripStatusLabel2 = new System.Windows.Forms.ToolStripStatusLabel();
			this.SplicerStatusLabel = new System.Windows.Forms.ToolStripStatusLabel();
			this.TasPlaybackBox = new Chimera.Client.GUI.PlaybackBox();
			this.MarkerControl = new Chimera.Client.GUI.MarkerControl();
			this.RightClickMenu = new System.Windows.Forms.ContextMenuStrip(this.components);
			this.SetMarkersContextMenuItem = new Chimera.WinForms.Controls.ToolStripMenuItemEx();
			this.SetMarkerWithTextContextMenuItem = new Chimera.WinForms.Controls.ToolStripMenuItemEx();
			this.RemoveMarkersContextMenuItem = new Chimera.WinForms.Controls.ToolStripMenuItemEx();
			this.toolStripSeparator15 = new Chimera.WinForms.Controls.ToolStripSeparatorEx();
			this.DeselectContextMenuItem = new Chimera.WinForms.Controls.ToolStripMenuItemEx();
			this.SelectBetweenMarkersContextMenuItem = new Chimera.WinForms.Controls.ToolStripMenuItemEx();
			this.toolStripSeparator16 = new Chimera.WinForms.Controls.ToolStripSeparatorEx();
			this.UngreenzoneContextMenuItem = new Chimera.WinForms.Controls.ToolStripMenuItemEx();
			this.CancelSeekContextMenuItem = new Chimera.WinForms.Controls.ToolStripMenuItemEx();
			this.toolStripSeparator17 = new Chimera.WinForms.Controls.ToolStripSeparatorEx();
			this.copyToolStripMenuItem = new Chimera.WinForms.Controls.ToolStripMenuItemEx();
			this.pasteToolStripMenuItem = new Chimera.WinForms.Controls.ToolStripMenuItemEx();
			this.pasteInsertToolStripMenuItem = new Chimera.WinForms.Controls.ToolStripMenuItemEx();
			this.cutToolStripMenuItem = new Chimera.WinForms.Controls.ToolStripMenuItemEx();
			this.separateToolStripMenuItem = new Chimera.WinForms.Controls.ToolStripSeparatorEx();
			this.ClearContextMenuItem = new Chimera.WinForms.Controls.ToolStripMenuItemEx();
			this.DeleteFramesContextMenuItem = new Chimera.WinForms.Controls.ToolStripMenuItemEx();
			this.InsertFrameContextMenuItem = new Chimera.WinForms.Controls.ToolStripMenuItemEx();
			this.InsertNumFramesContextMenuItem = new Chimera.WinForms.Controls.ToolStripMenuItemEx();
			this.CloneContextMenuItem = new Chimera.WinForms.Controls.ToolStripMenuItemEx();
			this.CloneXTimesContextMenuItem = new Chimera.WinForms.Controls.ToolStripMenuItemEx();
			this.toolStripSeparator18 = new Chimera.WinForms.Controls.ToolStripSeparatorEx();
			this.TruncateContextMenuItem = new Chimera.WinForms.Controls.ToolStripMenuItemEx();
			this.BranchContextMenuItem = new Chimera.WinForms.Controls.ToolStripMenuItemEx();
			this.BookMarkControl = new Chimera.Client.GUI.BookmarksBranchesBox();
			this.BranchesMarkersSplit = new System.Windows.Forms.SplitContainer();
			this.MainVertialSplit = new System.Windows.Forms.SplitContainer();
			this.toolTip1 = new System.Windows.Forms.ToolTip(this.components);
			this.ColumnRightClickMenu = new System.Windows.Forms.ContextMenuStrip(this.components);
			this.AutoHoldContextMenuItem = new System.Windows.Forms.ToolStripMenuItem();
			this.HideColumnContextMenuItem = new System.Windows.Forms.ToolStripMenuItem();
			this.ShowColumnsContextMenuItem = new System.Windows.Forms.ToolStripMenuItem();
			this.NewInputRollContextMenuItem = new System.Windows.Forms.ToolStripMenuItem();
			this.DeleteInputRollContextMenuItem = new System.Windows.Forms.ToolStripMenuItem();
			this.TASMenu.SuspendLayout();
			this.TasStatusStrip.SuspendLayout();
			this.RightClickMenu.SuspendLayout();
			((System.ComponentModel.ISupportInitialize)(this.BranchesMarkersSplit)).BeginInit();
			this.BranchesMarkersSplit.Panel1.SuspendLayout();
			this.BranchesMarkersSplit.Panel2.SuspendLayout();
			this.BranchesMarkersSplit.SuspendLayout();
			((System.ComponentModel.ISupportInitialize)(this.MainVertialSplit)).BeginInit();
			this.MainVertialSplit.Panel2.SuspendLayout();
			this.MainVertialSplit.SuspendLayout();
			this.ColumnRightClickMenu.SuspendLayout();
			this.SuspendLayout();
			// 
			// TASMenu
			// 
			this.TASMenu.Items.AddRange(new System.Windows.Forms.ToolStripItem[] {
            this.EditSubMenu,
            this.MetaSubMenu,
            this.SettingsSubMenu,
            this.ColumnsSubMenu,
            this.HelpSubMenu});
			this.TASMenu.TabIndex = 0;
			// 
			// saveSelectionToMacroToolStripMenuItem
			// 
			this.saveSelectionToMacroToolStripMenuItem.Text = "Save Selection to Macro";
			this.saveSelectionToMacroToolStripMenuItem.Click += new System.EventHandler(this.SaveSelectionToMacroMenuItem_Click);
			// 
			// placeMacroAtSelectionToolStripMenuItem
			// 
			this.placeMacroAtSelectionToolStripMenuItem.Text = "Place Macro at Selection";
			this.placeMacroAtSelectionToolStripMenuItem.Click += new System.EventHandler(this.PlaceMacroAtSelectionMenuItem_Click);
			// 
			// recentMacrosToolStripMenuItem
			// 
			this.recentMacrosToolStripMenuItem.DropDownItems.AddRange(new System.Windows.Forms.ToolStripItem[] {
            this.toolStripSeparator22});
			this.recentMacrosToolStripMenuItem.Text = "Recent Macros";
			this.recentMacrosToolStripMenuItem.DropDownOpened += new System.EventHandler(this.RecentMacrosMenuItem_DropDownOpened);
			// 
			// EditSubMenu
			// 
			this.EditSubMenu.DropDownItems.AddRange(new System.Windows.Forms.ToolStripItem[] {
            this.UndoMenuItem,
            this.RedoMenuItem,
            this.showUndoHistoryToolStripMenuItem,
            this.SelectionUndoMenuItem,
            this.SelectionRedoMenuItem,
            this.toolStripSeparator5,
            this.DeselectMenuItem,
            this.SelectBetweenMarkersMenuItem,
            this.SelectAllMenuItem,
            this.ReselectClipboardMenuItem,
            this.GoToFrameMenuItem,
            this.toolStripSeparator7,
            this.CopyMenuItem,
            this.PasteMenuItem,
            this.PasteInsertMenuItem,
            this.CutMenuItem,
            this.toolStripSeparator8,
            this.ClearFramesMenuItem,
            this.DeleteFramesMenuItem,
            this.InsertFrameMenuItem,
            this.InsertNumFramesMenuItem,
            this.CloneFramesMenuItem,
            this.CloneFramesXTimesMenuItem,
            this.toolStripSeparator6,
            this.TruncateMenuItem,
            this.ClearGreenzoneMenuItem,
            this.GreenzoneICheckSeparator,
            this.StateHistoryIntegrityCheckMenuItem,
            this.toolStripSeparator1,
            this.saveSelectionToMacroToolStripMenuItem,
            this.placeMacroAtSelectionToolStripMenuItem,
            this.recentMacrosToolStripMenuItem});
			this.EditSubMenu.Text = "&Edit";
			this.EditSubMenu.DropDownClosed += new System.EventHandler(this.EditSubMenu_DropDownClosed);
			this.EditSubMenu.DropDownOpened += new System.EventHandler(this.EditSubMenu_DropDownOpened);
			// 
			// UndoMenuItem
			// 
			this.UndoMenuItem.Text = "&Undo";
			this.UndoMenuItem.Click += new System.EventHandler(this.UndoMenuItem_Click);
			// 
			// RedoMenuItem
			// 
			this.RedoMenuItem.Enabled = false;
			this.RedoMenuItem.Text = "&Redo";
			this.RedoMenuItem.Click += new System.EventHandler(this.RedoMenuItem_Click);
			// 
			// showUndoHistoryToolStripMenuItem
			// 
			this.showUndoHistoryToolStripMenuItem.Text = "Show Undo History";
			this.showUndoHistoryToolStripMenuItem.Click += new System.EventHandler(this.ShowUndoHistoryMenuItem_Click);
			// 
			// SelectionUndoMenuItem
			// 
			this.SelectionUndoMenuItem.Enabled = false;
			this.SelectionUndoMenuItem.Text = "Selection Undo";
			// 
			// SelectionRedoMenuItem
			// 
			this.SelectionRedoMenuItem.Enabled = false;
			this.SelectionRedoMenuItem.Text = "Selection Redo";
			// 
			// DeselectMenuItem
			// 
			this.DeselectMenuItem.Text = "Deselect";
			this.DeselectMenuItem.Click += new System.EventHandler(this.DeselectMenuItem_Click);
			// 
			// SelectBetweenMarkersMenuItem
			// 
			this.SelectBetweenMarkersMenuItem.Text = "Select between Markers";
			this.SelectBetweenMarkersMenuItem.Click += new System.EventHandler(this.SelectBetweenMarkersMenuItem_Click);
			// 
			// SelectAllMenuItem
			// 
			this.SelectAllMenuItem.ShortcutKeyDisplayString = "";
			this.SelectAllMenuItem.Text = "Select &All";
			this.SelectAllMenuItem.Click += new System.EventHandler(this.SelectAllMenuItem_Click);
			// 
			// ReselectClipboardMenuItem
			// 
			this.ReselectClipboardMenuItem.Text = "Reselect Clipboard";
			this.ReselectClipboardMenuItem.Click += new System.EventHandler(this.ReselectClipboardMenuItem_Click);
			// 
			// ReselectClipboardMenuItem
			// 
			this.GoToFrameMenuItem.Text = "Go to Frame...";
			this.GoToFrameMenuItem.Click += new System.EventHandler(this.GoToFrameMenuItem_Click);
			// 
			// CopyMenuItem
			// 
			this.CopyMenuItem.ShortcutKeys = ((System.Windows.Forms.Keys)((System.Windows.Forms.Keys.Control | System.Windows.Forms.Keys.C)));
			this.CopyMenuItem.Text = "Copy";
			this.CopyMenuItem.Click += new System.EventHandler(this.CopyMenuItem_Click);
			// 
			// PasteMenuItem
			// 
			this.PasteMenuItem.ShortcutKeys = ((System.Windows.Forms.Keys)((System.Windows.Forms.Keys.Control | System.Windows.Forms.Keys.V)));
			this.PasteMenuItem.Text = "&Paste";
			this.PasteMenuItem.Click += new System.EventHandler(this.PasteMenuItem_Click);
			// 
			// PasteInsertMenuItem
			// 
			this.PasteInsertMenuItem.Text = "&Paste Insert";
			this.PasteInsertMenuItem.Click += new System.EventHandler(this.PasteInsertMenuItem_Click);
			// 
			// CutMenuItem
			// 
			this.CutMenuItem.ShortcutKeys = ((System.Windows.Forms.Keys)((System.Windows.Forms.Keys.Control | System.Windows.Forms.Keys.X)));
			this.CutMenuItem.Text = "&Cut";
			this.CutMenuItem.Click += new System.EventHandler(this.CutMenuItem_Click);
			// 
			// ClearFramesMenuItem
			// 
			this.ClearFramesMenuItem.ShortcutKeyDisplayString = "";
			this.ClearFramesMenuItem.Text = "Clear";
			this.ClearFramesMenuItem.Click += new System.EventHandler(this.ClearFramesMenuItem_Click);
			// 
			// DeleteFramesMenuItem
			// 
			this.DeleteFramesMenuItem.Text = "&Delete";
			this.DeleteFramesMenuItem.Click += new System.EventHandler(this.DeleteFramesMenuItem_Click);
			// 
			// InsertFrameMenuItem
			// 
			this.InsertFrameMenuItem.Text = "&Insert";
			this.InsertFrameMenuItem.Click += new System.EventHandler(this.InsertFrameMenuItem_Click);
			// 
			// InsertNumFramesMenuItem
			// 
			this.InsertNumFramesMenuItem.ShortcutKeyDisplayString = "";
			this.InsertNumFramesMenuItem.Text = "Insert # of Frames";
			this.InsertNumFramesMenuItem.Click += new System.EventHandler(this.InsertNumFramesMenuItem_Click);
			// 
			// CloneFramesMenuItem
			// 
			this.CloneFramesMenuItem.Text = "&Clone";
			this.CloneFramesMenuItem.Click += new System.EventHandler(this.CloneFramesMenuItem_Click);
			// 
			// CloneFramesXTimesMenuItem
			// 
			this.CloneFramesXTimesMenuItem.Text = "Clone # Times";
			this.CloneFramesXTimesMenuItem.Click += new System.EventHandler(this.CloneFramesXTimesMenuItem_Click);
			// 
			// TruncateMenuItem
			// 
			this.TruncateMenuItem.Text = "&Truncate Movie";
			this.TruncateMenuItem.Click += new System.EventHandler(this.TruncateMenuItem_Click);
			// 
			// ClearGreenzoneMenuItem
			// 
			this.ClearGreenzoneMenuItem.Text = "&Clear Savestate History";
			this.ClearGreenzoneMenuItem.Click += new System.EventHandler(this.ClearGreenzoneMenuItem_Click);
			// 
			// StateHistoryIntegrityCheckMenuItem
			// 
			this.StateHistoryIntegrityCheckMenuItem.ShortcutKeys = ((System.Windows.Forms.Keys)(((System.Windows.Forms.Keys.Control | System.Windows.Forms.Keys.Shift) 
            | System.Windows.Forms.Keys.I)));
			this.StateHistoryIntegrityCheckMenuItem.Text = "State History Integrity Check";
			this.StateHistoryIntegrityCheckMenuItem.Click += new System.EventHandler(this.StateHistoryIntegrityCheckMenuItem_Click);
			// 
			// MetaSubMenu
			// 
			this.MetaSubMenu.DropDownItems.AddRange(new System.Windows.Forms.ToolStripItem[] {
            this.HeaderMenuItem,
            this.CommentsMenuItem,
            this.SubtitlesMenuItem});
			this.MetaSubMenu.Text = "&Metadata";
			// 
			// HeaderMenuItem
			// 
			this.HeaderMenuItem.Text = "&Header...";
			this.HeaderMenuItem.Click += new System.EventHandler(this.HeaderMenuItem_Click);
			// 
			// CommentsMenuItem
			// 
			this.CommentsMenuItem.Text = "&Comments...";
			this.CommentsMenuItem.Click += new System.EventHandler(this.CommentsMenuItem_Click);
			// 
			// SubtitlesMenuItem
			// 
			this.SubtitlesMenuItem.Text = "&Subtitles...";
			this.SubtitlesMenuItem.Click += new System.EventHandler(this.SubtitlesMenuItem_Click);
			// 
			// SettingsSubMenu
			// 
			this.SettingsSubMenu.DropDownItems.AddRange(new System.Windows.Forms.ToolStripItem[] {
            this.MoveWithMainWindowMenuItem,
            this.TAStudioSettingsToolStripMenuItem});
			this.SettingsSubMenu.Text = "&Settings";
			this.SettingsSubMenu.DropDownOpened += new System.EventHandler(this.SettingsSubMenu_DropDownOpened);
			// 
			// MoveWithMainWindowMenuItem
			// 
			this.MoveWithMainWindowMenuItem.Name = "MoveWithMainWindowMenuItem";
			this.MoveWithMainWindowMenuItem.Size = new System.Drawing.Size(156, 22);
			this.MoveWithMainWindowMenuItem.Text = "&Move with the main window";
			this.MoveWithMainWindowMenuItem.ToolTipText = "Drag the main window and this one comes along, keeping its place beside it. Drag this one and it moves alone. Hold Shift to move the main window without this one.";
			this.MoveWithMainWindowMenuItem.Click += new System.EventHandler(this.MoveWithMainWindowMenuItem_Click);
			// 
			// TAStudioSettingsToolStripMenuItem
			// 
			this.TAStudioSettingsToolStripMenuItem.Name = "TAStudioSettingsToolStripMenuItem";
			this.TAStudioSettingsToolStripMenuItem.Size = new System.Drawing.Size(156, 22);
			this.TAStudioSettingsToolStripMenuItem.Text = "Open settings...";
			this.TAStudioSettingsToolStripMenuItem.Click += new System.EventHandler(this.TAStudioSettingsToolStripMenuItem_Click);
			// 
			// ColumnsSubMenu
			// 
			this.ColumnsSubMenu.Text = "&Columns";
			// 
			// HelpSubMenu
			// 
			this.HelpSubMenu.DropDownItems.AddRange(new System.Windows.Forms.ToolStripItem[] {
            this.TASEditorManualOnlineMenuItem,
            this.aboutToolStripMenuItem,
            this.toolStripSeparator10,
            this.EnableTooltipsMenuItem});
			this.HelpSubMenu.Text = "&Help";
			// 
			// TASEditorManualOnlineMenuItem
			// 
			this.TASEditorManualOnlineMenuItem.Text = "TAS Editor Manual Online...";
			this.TASEditorManualOnlineMenuItem.Click += new System.EventHandler(this.TASEditorManualOnlineMenuItem_Click);
			// 
			// aboutToolStripMenuItem
			// 
			this.aboutToolStripMenuItem.Enabled = false;
			this.aboutToolStripMenuItem.Text = "&About";
			// 
			// EnableTooltipsMenuItem
			// 
			this.EnableTooltipsMenuItem.Enabled = false;
			this.EnableTooltipsMenuItem.Text = "&Enable Tooltips";
			// 
			// TasStatusStrip
			// 
			this.TasStatusStrip.Items.AddRange(new System.Windows.Forms.ToolStripItem[] {
            this.MessageStatusLabel,
            this.ProgressBar,
            this.toolStripStatusLabel2,
            this.SplicerStatusLabel});
			this.TasStatusStrip.Location = new System.Drawing.Point(0, 554);
			this.TasStatusStrip.Name = "TasStatusStrip";
			this.TasStatusStrip.TabIndex = 4;
			// 
			// MessageStatusLabel
			// 
			this.MessageStatusLabel.Name = "MessageStatusLabel";
			this.MessageStatusLabel.Size = new System.Drawing.Size(103, 17);
			this.MessageStatusLabel.Text = "TAStudio engaged";
			// 
			// ProgressBar
			// 
			this.ProgressBar.Name = "ProgressBar";
			this.ProgressBar.Size = new System.Drawing.Size(100, 16);
			// 
			// toolStripStatusLabel2
			// 
			this.toolStripStatusLabel2.Name = "toolStripStatusLabel2";
			this.toolStripStatusLabel2.Size = new System.Drawing.Size(269, 17);
			this.toolStripStatusLabel2.Spring = true;
			// 
			// SplicerStatusLabel
			// 
			this.SplicerStatusLabel.Name = "SplicerStatusLabel";
			this.SplicerStatusLabel.Padding = new System.Windows.Forms.Padding(20, 0, 0, 0);
			this.SplicerStatusLabel.Size = new System.Drawing.Size(20, 17);
			// 
			// TasPlaybackBox
			// 
			this.TasPlaybackBox.Dock = System.Windows.Forms.DockStyle.Top;
			this.TasPlaybackBox.Location = new System.Drawing.Point(0, 0);
			this.TasPlaybackBox.Margin = new System.Windows.Forms.Padding(3, 3, 3, 44);
			this.TasPlaybackBox.Name = "TasPlaybackBox";
			this.TasPlaybackBox.Size = new System.Drawing.Size(200, 108);
			this.TasPlaybackBox.TabIndex = 5;
			this.TasPlaybackBox.Tastudio = null;
			// 
			// MarkerControl
			// 
			this.MarkerControl.Dock = System.Windows.Forms.DockStyle.Fill;
			this.MarkerControl.Location = new System.Drawing.Point(0, 0);
			this.MarkerControl.Name = "MarkerControl";
			this.MarkerControl.Size = new System.Drawing.Size(200, 227);
			this.MarkerControl.TabIndex = 6;
			this.MarkerControl.Tastudio = null;
			// 
			// RightClickMenu
			// 
			this.RightClickMenu.Items.AddRange(new System.Windows.Forms.ToolStripItem[] {
            this.SetMarkersContextMenuItem,
            this.SetMarkerWithTextContextMenuItem,
            this.RemoveMarkersContextMenuItem,
            this.toolStripSeparator15,
            this.DeselectContextMenuItem,
            this.SelectBetweenMarkersContextMenuItem,
            this.toolStripSeparator16,
            this.UngreenzoneContextMenuItem,
            this.CancelSeekContextMenuItem,
            this.toolStripSeparator17,
            this.copyToolStripMenuItem,
            this.pasteToolStripMenuItem,
            this.pasteInsertToolStripMenuItem,
            this.cutToolStripMenuItem,
            this.separateToolStripMenuItem,
            this.ClearContextMenuItem,
            this.DeleteFramesContextMenuItem,
            this.InsertFrameContextMenuItem,
            this.InsertNumFramesContextMenuItem,
            this.CloneContextMenuItem,
            this.CloneXTimesContextMenuItem,
            this.toolStripSeparator18,
            this.TruncateContextMenuItem,
            this.BranchContextMenuItem});
			this.RightClickMenu.Name = "RightClickMenu";
			this.RightClickMenu.Size = new System.Drawing.Size(253, 502);
			this.RightClickMenu.Opened += new System.EventHandler(this.RightClickMenu_Opened);
			// 
			// SetMarkersContextMenuItem
			// 
			this.SetMarkersContextMenuItem.Text = "Set Markers";
			this.SetMarkersContextMenuItem.Click += new System.EventHandler(this.SetMarkersMenuItem_Click);
			// 
			// SetMarkerWithTextContextMenuItem
			// 
			this.SetMarkerWithTextContextMenuItem.Text = "Set Marker with Text";
			this.SetMarkerWithTextContextMenuItem.Click += new System.EventHandler(this.SetMarkerWithTextMenuItem_Click);
			// 
			// RemoveMarkersContextMenuItem
			// 
			this.RemoveMarkersContextMenuItem.Text = "Remove Markers";
			this.RemoveMarkersContextMenuItem.Click += new System.EventHandler(this.RemoveMarkersMenuItem_Click);
			// 
			// DeselectContextMenuItem
			// 
			this.DeselectContextMenuItem.Text = "Deselect";
			this.DeselectContextMenuItem.Click += new System.EventHandler(this.DeselectMenuItem_Click);
			// 
			// SelectBetweenMarkersContextMenuItem
			// 
			this.SelectBetweenMarkersContextMenuItem.Text = "Select between Markers";
			this.SelectBetweenMarkersContextMenuItem.Click += new System.EventHandler(this.SelectBetweenMarkersMenuItem_Click);
			// 
			// UngreenzoneContextMenuItem
			// 
			this.UngreenzoneContextMenuItem.Text = "Clear Greenzone";
			this.UngreenzoneContextMenuItem.Click += new System.EventHandler(this.ClearGreenzoneMenuItem_Click);
			// 
			// CancelSeekContextMenuItem
			// 
			this.CancelSeekContextMenuItem.Text = "Cancel Seek";
			this.CancelSeekContextMenuItem.Click += new System.EventHandler(this.CancelSeekContextMenuItem_Click);
			// 
			// copyToolStripMenuItem
			// 
			this.copyToolStripMenuItem.ShortcutKeyDisplayString = "Ctrl+C";
			this.copyToolStripMenuItem.Text = "Copy";
			this.copyToolStripMenuItem.Click += new System.EventHandler(this.CopyMenuItem_Click);
			// 
			// pasteToolStripMenuItem
			// 
			this.pasteToolStripMenuItem.ShortcutKeyDisplayString = "Ctrl+V";
			this.pasteToolStripMenuItem.Text = "Paste";
			this.pasteToolStripMenuItem.Click += new System.EventHandler(this.PasteMenuItem_Click);
			// 
			// pasteInsertToolStripMenuItem
			// 
			this.pasteInsertToolStripMenuItem.ShortcutKeyDisplayString = "Ctrl+Shift+V";
			this.pasteInsertToolStripMenuItem.Text = "Paste Insert";
			this.pasteInsertToolStripMenuItem.Click += new System.EventHandler(this.PasteInsertMenuItem_Click);
			// 
			// cutToolStripMenuItem
			// 
			this.cutToolStripMenuItem.ShortcutKeyDisplayString = "Ctrl+X";
			this.cutToolStripMenuItem.Text = "Cut";
			this.cutToolStripMenuItem.Click += new System.EventHandler(this.CutMenuItem_Click);
			// 
			// ClearContextMenuItem
			// 
			this.ClearContextMenuItem.Text = "Clear";
			this.ClearContextMenuItem.Click += new System.EventHandler(this.ClearFramesMenuItem_Click);
			// 
			// DeleteFramesContextMenuItem
			// 
			this.DeleteFramesContextMenuItem.Text = "Delete";
			this.DeleteFramesContextMenuItem.Click += new System.EventHandler(this.DeleteFramesMenuItem_Click);
			// 
			// InsertFrameContextMenuItem
			// 
			this.InsertFrameContextMenuItem.Text = "Insert";
			this.InsertFrameContextMenuItem.Click += new System.EventHandler(this.InsertFrameMenuItem_Click);
			// 
			// InsertNumFramesContextMenuItem
			// 
			this.InsertNumFramesContextMenuItem.Text = "Insert # of Frames";
			this.InsertNumFramesContextMenuItem.Click += new System.EventHandler(this.InsertNumFramesMenuItem_Click);
			// 
			// CloneContextMenuItem
			// 
			this.CloneContextMenuItem.Text = "Clone";
			this.CloneContextMenuItem.Click += new System.EventHandler(this.CloneFramesMenuItem_Click);
			// 
			// CloneXTimesContextMenuItem
			// 
			this.CloneXTimesContextMenuItem.Text = "Clone # Times";
			this.CloneXTimesContextMenuItem.Click += new System.EventHandler(this.CloneFramesXTimesMenuItem_Click);
			// 
			// TruncateContextMenuItem
			// 
			this.TruncateContextMenuItem.Text = "Truncate Movie";
			this.TruncateContextMenuItem.Click += new System.EventHandler(this.TruncateMenuItem_Click);
			// 
			// BranchContextMenuItem
			// 
			this.BranchContextMenuItem.Text = "&Branch";
			this.BranchContextMenuItem.Click += new System.EventHandler(this.BranchContextMenuItem_Click);
			// 
			// 
			// 
			// BookMarkControl
			// 
			this.BookMarkControl.Dock = System.Windows.Forms.DockStyle.Fill;
			this.BookMarkControl.Location = new System.Drawing.Point(0, 108);
			this.BookMarkControl.Name = "BookMarkControl";
			this.BookMarkControl.Size = new System.Drawing.Size(200, 185);
			this.BookMarkControl.TabIndex = 8;
			this.BookMarkControl.Tastudio = null;
			// 
			// BranchesMarkersSplit
			// 
			this.BranchesMarkersSplit.Anchor = ((System.Windows.Forms.AnchorStyles)((((System.Windows.Forms.AnchorStyles.Top | System.Windows.Forms.AnchorStyles.Bottom) 
            | System.Windows.Forms.AnchorStyles.Left) 
            | System.Windows.Forms.AnchorStyles.Right)));
			this.BranchesMarkersSplit.Location = new System.Drawing.Point(4, 4);
			this.BranchesMarkersSplit.Name = "BranchesMarkersSplit";
			this.BranchesMarkersSplit.Orientation = System.Windows.Forms.Orientation.Horizontal;
			// 
			// BranchesMarkersSplit.Panel1
			// 
			this.BranchesMarkersSplit.Panel1.Controls.Add(this.BookMarkControl);
			this.BranchesMarkersSplit.Panel1.Controls.Add(this.TasPlaybackBox);
			this.BranchesMarkersSplit.Panel1MinSize = 200;
			// 
			// BranchesMarkersSplit.Panel2
			// 
			this.BranchesMarkersSplit.Panel2.Controls.Add(this.MarkerControl);
			this.BranchesMarkersSplit.Size = new System.Drawing.Size(200, 524);
			this.BranchesMarkersSplit.SplitterDistance = 293;
			this.BranchesMarkersSplit.TabIndex = 9;
			this.BranchesMarkersSplit.SplitterMoved += new System.Windows.Forms.SplitterEventHandler(this.BranchesMarkersSplit_SplitterMoved);
			// 
			// MainVertialSplit
			// 
			this.MainVertialSplit.Anchor = ((System.Windows.Forms.AnchorStyles)((((System.Windows.Forms.AnchorStyles.Top | System.Windows.Forms.AnchorStyles.Bottom) 
            | System.Windows.Forms.AnchorStyles.Left) 
            | System.Windows.Forms.AnchorStyles.Right)));
			this.MainVertialSplit.FixedPanel = System.Windows.Forms.FixedPanel.Panel2;
			this.MainVertialSplit.Location = new System.Drawing.Point(2, 23);
			this.MainVertialSplit.Name = "MainVertialSplit";
			// 
			// MainVertialSplit.Panel2
			// 
			this.MainVertialSplit.Panel2.Controls.Add(this.BranchesMarkersSplit);
			this.MainVertialSplit.Size = new System.Drawing.Size(507, 528);
			this.MainVertialSplit.SplitterDistance = 295;
			this.MainVertialSplit.TabIndex = 10;
			this.MainVertialSplit.SplitterMoved += new System.Windows.Forms.SplitterEventHandler(this.MainVerticalSplit_SplitterMoved);
			// 
			// ColumnRightClickMenu
			// 
			this.ColumnRightClickMenu.Items.AddRange(new System.Windows.Forms.ToolStripItem[] {
            this.AutoHoldContextMenuItem,
            this.HideColumnContextMenuItem,
            this.ShowColumnsContextMenuItem,
            this.NewInputRollContextMenuItem,
            this.DeleteInputRollContextMenuItem});
			this.ColumnRightClickMenu.Name = "ColumnRightClickMenu";
			this.ColumnRightClickMenu.Size = new System.Drawing.Size(181, 92);
			this.ColumnRightClickMenu.Opened += new System.EventHandler(this.ColumnRightClickMenu_Opened);
			// 
			// AutoHoldContextMenuItem
			// 
			this.AutoHoldContextMenuItem.Name = "AutoHoldContextMenuItem";
			this.AutoHoldContextMenuItem.Size = new System.Drawing.Size(180, 22);
			this.AutoHoldContextMenuItem.Text = "Auto-hold";
			this.AutoHoldContextMenuItem.Click += new System.EventHandler(this.AutoHoldContextMenuItem_Click);
			// 
			// HideColumnContextMenuItem
			// 
			this.HideColumnContextMenuItem.Name = "HideColumnContextMenuItem";
			this.HideColumnContextMenuItem.Size = new System.Drawing.Size(180, 22);
			this.HideColumnContextMenuItem.Text = "Hide column";
			this.HideColumnContextMenuItem.Click += new System.EventHandler(this.HideColumnContextMenuItem_Click);
			// 
			// ShowColumnsContextMenuItem
			// 
			this.ShowColumnsContextMenuItem.Name = "ShowColumnsContextMenuItem";
			this.ShowColumnsContextMenuItem.Size = new System.Drawing.Size(180, 22);
			this.ShowColumnsContextMenuItem.Text = "Show columns";
			// 
			// NewInputRollContextMenuItem
			// 
			this.NewInputRollContextMenuItem.Name = "NewInputRollContextMenuItem";
			this.NewInputRollContextMenuItem.Size = new System.Drawing.Size(180, 22);
			this.NewInputRollContextMenuItem.Text = "New input roll";
			this.NewInputRollContextMenuItem.Click += new System.EventHandler(this.NewInputRollContextMenuItem_Click);
			// 
			// DeleteInputRollContextMenuItem
			// 
			this.DeleteInputRollContextMenuItem.Name = "DeleteInputRollContextMenuItem";
			this.DeleteInputRollContextMenuItem.Size = new System.Drawing.Size(180, 22);
			this.DeleteInputRollContextMenuItem.Text = "Delete input roll";
			this.DeleteInputRollContextMenuItem.Click += new System.EventHandler(this.DeleteInputRollContextMenuItem_Click);
			// 
			// TAStudio
			// 
			this.AllowDrop = true;
			this.AutoScaleDimensions = new System.Drawing.SizeF(6F, 13F);
			this.AutoScaleMode = System.Windows.Forms.AutoScaleMode.Font;
			this.ClientSize = new System.Drawing.Size(509, 576);
			this.Controls.Add(this.MainVertialSplit);
			this.Controls.Add(this.TasStatusStrip);
			this.Controls.Add(this.TASMenu);
			this.KeyPreview = true;
			this.MainMenuStrip = this.TASMenu;
			this.MinimumSize = new System.Drawing.Size(200, 148);
			this.Name = "TAStudio";
			this.StartPosition = System.Windows.Forms.FormStartPosition.CenterParent;
			this.Deactivate += new System.EventHandler(this.TAStudio_Deactivate);
			this.FormClosing += new System.Windows.Forms.FormClosingEventHandler(this.Tastudio_Closing);
			this.Load += new System.EventHandler(this.Tastudio_Load);
			this.DragDrop += new System.Windows.Forms.DragEventHandler(this.TAStudio_DragDrop);
			this.DragEnter += new System.Windows.Forms.DragEventHandler(this.DragEnterWrapper);
			this.Resize += new System.EventHandler(this.TAStudio_Resize);
			this.TASMenu.ResumeLayout(false);
			this.TASMenu.PerformLayout();
			this.TasStatusStrip.ResumeLayout(false);
			this.TasStatusStrip.PerformLayout();
			this.RightClickMenu.ResumeLayout(false);
			this.BranchesMarkersSplit.Panel1.ResumeLayout(false);
			this.BranchesMarkersSplit.Panel2.ResumeLayout(false);
			((System.ComponentModel.ISupportInitialize)(this.BranchesMarkersSplit)).EndInit();
			this.BranchesMarkersSplit.ResumeLayout(false);
			this.MainVertialSplit.Panel2.ResumeLayout(false);
			((System.ComponentModel.ISupportInitialize)(this.MainVertialSplit)).EndInit();
			this.MainVertialSplit.ResumeLayout(false);
			this.ColumnRightClickMenu.ResumeLayout(false);
			this.ResumeLayout(false);
			this.PerformLayout();

		}

		#endregion

		private MenuStripEx TASMenu;
		private Chimera.WinForms.Controls.ToolStripSeparatorEx toolStripSeparator1;
		private Chimera.WinForms.Controls.ToolStripMenuItemEx EditSubMenu;
		private Chimera.WinForms.Controls.ToolStripSeparatorEx toolStripSeparator3;
		private Chimera.WinForms.Controls.ToolStripMenuItemEx InsertFrameMenuItem;
		private Chimera.WinForms.Controls.ToolStripSeparatorEx toolStripSeparator7;
		private Chimera.WinForms.Controls.ToolStripMenuItemEx CloneFramesMenuItem;
		private Chimera.WinForms.Controls.ToolStripMenuItemEx CloneFramesXTimesMenuItem;
		private Chimera.WinForms.Controls.ToolStripMenuItemEx DeleteFramesMenuItem;
		private Chimera.WinForms.Controls.ToolStripMenuItemEx ClearFramesMenuItem;
		private Chimera.WinForms.Controls.ToolStripMenuItemEx InsertNumFramesMenuItem;
		private Chimera.WinForms.Controls.ToolStripMenuItemEx SelectAllMenuItem;
		private Chimera.WinForms.Controls.ToolStripSeparatorEx toolStripSeparator8;
		private Chimera.WinForms.Controls.ToolStripMenuItemEx TruncateMenuItem;
		private Chimera.WinForms.Controls.ToolStripMenuItemEx CopyMenuItem;
		private Chimera.WinForms.Controls.ToolStripMenuItemEx PasteMenuItem;
		private Chimera.WinForms.Controls.ToolStripMenuItemEx PasteInsertMenuItem;
		private Chimera.WinForms.Controls.ToolStripMenuItemEx CutMenuItem;
		private Chimera.WinForms.Controls.ToolStripMenuItemEx UndoMenuItem;
		private Chimera.WinForms.Controls.ToolStripMenuItemEx RedoMenuItem;
		private Chimera.WinForms.Controls.ToolStripMenuItemEx SelectionUndoMenuItem;
		private Chimera.WinForms.Controls.ToolStripMenuItemEx SelectionRedoMenuItem;
		private Chimera.WinForms.Controls.ToolStripSeparatorEx toolStripSeparator5;
		private Chimera.WinForms.Controls.ToolStripMenuItemEx DeselectMenuItem;
		private Chimera.WinForms.Controls.ToolStripMenuItemEx SelectBetweenMarkersMenuItem;
		private Chimera.WinForms.Controls.ToolStripMenuItemEx ReselectClipboardMenuItem;
		private Chimera.WinForms.Controls.ToolStripMenuItemEx GoToFrameMenuItem;
		private Chimera.WinForms.Controls.ToolStripSeparatorEx toolStripSeparator6;
		private Chimera.WinForms.Controls.ToolStripMenuItemEx HelpSubMenu;
		private Chimera.WinForms.Controls.ToolStripMenuItemEx EnableTooltipsMenuItem;
		private Chimera.WinForms.Controls.ToolStripSeparatorEx toolStripSeparator10;
		private Chimera.WinForms.Controls.ToolStripMenuItemEx aboutToolStripMenuItem;
		private Chimera.WinForms.Controls.ToolStripMenuItemEx SettingsSubMenu;
		private StatusStripEx TasStatusStrip;
		private System.Windows.Forms.ToolStripStatusLabel MessageStatusLabel;
		public PlaybackBox TasPlaybackBox;
		private System.Windows.Forms.ToolStripStatusLabel SplicerStatusLabel;
		private Chimera.WinForms.Controls.ToolStripMenuItemEx MetaSubMenu;
		private Chimera.WinForms.Controls.ToolStripMenuItemEx HeaderMenuItem;
		private Chimera.WinForms.Controls.ToolStripMenuItemEx CommentsMenuItem;
		private Chimera.WinForms.Controls.ToolStripMenuItemEx SubtitlesMenuItem;
		private MarkerControl MarkerControl;
		private System.Windows.Forms.ContextMenuStrip RightClickMenu;
		private Chimera.WinForms.Controls.ToolStripMenuItemEx SetMarkersContextMenuItem;
		private Chimera.WinForms.Controls.ToolStripMenuItemEx RemoveMarkersContextMenuItem;
		private Chimera.WinForms.Controls.ToolStripSeparatorEx toolStripSeparator15;
		private Chimera.WinForms.Controls.ToolStripMenuItemEx DeselectContextMenuItem;
		private Chimera.WinForms.Controls.ToolStripMenuItemEx SelectBetweenMarkersContextMenuItem;
		private Chimera.WinForms.Controls.ToolStripSeparatorEx toolStripSeparator16;
		private Chimera.WinForms.Controls.ToolStripMenuItemEx UngreenzoneContextMenuItem;
		private Chimera.WinForms.Controls.ToolStripSeparatorEx toolStripSeparator17;
		private Chimera.WinForms.Controls.ToolStripMenuItemEx ClearContextMenuItem;
		private Chimera.WinForms.Controls.ToolStripMenuItemEx DeleteFramesContextMenuItem;
		private Chimera.WinForms.Controls.ToolStripMenuItemEx InsertFrameContextMenuItem;
		private Chimera.WinForms.Controls.ToolStripMenuItemEx InsertNumFramesContextMenuItem;
		private Chimera.WinForms.Controls.ToolStripMenuItemEx CloneContextMenuItem;
		private Chimera.WinForms.Controls.ToolStripMenuItemEx CloneXTimesContextMenuItem;
		private Chimera.WinForms.Controls.ToolStripSeparatorEx toolStripSeparator18;
		private Chimera.WinForms.Controls.ToolStripMenuItemEx TruncateContextMenuItem;
		private Chimera.WinForms.Controls.ToolStripMenuItemEx ClearGreenzoneMenuItem;
		private Chimera.WinForms.Controls.ToolStripSeparatorEx GreenzoneICheckSeparator;
		private Chimera.WinForms.Controls.ToolStripMenuItemEx StateHistoryIntegrityCheckMenuItem;
		private Chimera.WinForms.Controls.ToolStripMenuItemEx ColumnsSubMenu;
		private Chimera.WinForms.Controls.ToolStripSeparatorEx toolStripSeparator19;
		private Chimera.WinForms.Controls.ToolStripMenuItemEx CancelSeekContextMenuItem;
		private System.Windows.Forms.ToolStripProgressBar ProgressBar;
		private System.Windows.Forms.ToolStripStatusLabel toolStripStatusLabel2;
		private Chimera.WinForms.Controls.ToolStripMenuItemEx copyToolStripMenuItem;
		private Chimera.WinForms.Controls.ToolStripMenuItemEx pasteToolStripMenuItem;
		private Chimera.WinForms.Controls.ToolStripSeparatorEx separateToolStripMenuItem;
		private Chimera.WinForms.Controls.ToolStripMenuItemEx pasteInsertToolStripMenuItem;
		private Chimera.WinForms.Controls.ToolStripMenuItemEx cutToolStripMenuItem;
		private Chimera.WinForms.Controls.ToolStripMenuItemEx showUndoHistoryToolStripMenuItem;
		private Chimera.WinForms.Controls.ToolStripMenuItemEx saveSelectionToMacroToolStripMenuItem;
		private Chimera.WinForms.Controls.ToolStripMenuItemEx placeMacroAtSelectionToolStripMenuItem;
		private Chimera.WinForms.Controls.ToolStripMenuItemEx recentMacrosToolStripMenuItem;
		private Chimera.WinForms.Controls.ToolStripSeparatorEx toolStripSeparator22;
		private BookmarksBranchesBox BookMarkControl;
		private Chimera.WinForms.Controls.ToolStripMenuItemEx BranchContextMenuItem;
		private System.Windows.Forms.SplitContainer BranchesMarkersSplit;
		private System.Windows.Forms.SplitContainer MainVertialSplit;
		private Chimera.WinForms.Controls.ToolStripMenuItemEx SetMarkerWithTextContextMenuItem;
		private Chimera.WinForms.Controls.ToolStripMenuItemEx TASEditorManualOnlineMenuItem;
		private System.Windows.Forms.ToolTip toolTip1;
		private System.Windows.Forms.ToolStripMenuItem MoveWithMainWindowMenuItem;
		private System.Windows.Forms.ToolStripMenuItem TAStudioSettingsToolStripMenuItem;
		private System.Windows.Forms.ContextMenuStrip ColumnRightClickMenu;
		private System.Windows.Forms.ToolStripMenuItem AutoHoldContextMenuItem;
		private System.Windows.Forms.ToolStripMenuItem HideColumnContextMenuItem;
		private System.Windows.Forms.ToolStripMenuItem ShowColumnsContextMenuItem;
		private System.Windows.Forms.ToolStripMenuItem NewInputRollContextMenuItem;
		private System.Windows.Forms.ToolStripMenuItem DeleteInputRollContextMenuItem;
	}
}