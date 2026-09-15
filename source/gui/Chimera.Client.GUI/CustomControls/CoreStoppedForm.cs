using System.Drawing;
using System.Windows.Forms;

namespace Chimera.Client.GUI
{
	/// <summary>
	/// What to do about a core whose machine died mid-frame (CoreStoppedException). The process is
	/// fine and the machine is paused; loading a state brings it back. Every choice keeps the
	/// inputs, branches and markers - the recovery journal has them whatever happens - so what is
	/// being chosen is only where to go from here.
	/// </summary>
	public sealed class CoreStoppedForm : FormBase
	{
		public enum Choice
		{
			/// <summary>The window was closed: stay paused, decide later.</summary>
			None,
			BackToSafePoint,
			RestartFromFrameZero,
			SaveInputsAndClose,
			CloseWithoutSaving,
		}

		public Choice Chosen { get; private set; } = Choice.None;

		protected override string WindowTitleStatic => "The core stopped";

		/// <param name="reason">what the sandbox said, in one line</param>
		/// <param name="stoppedAt">the frame the machine was on when it died</param>
		/// <param name="safePoint">the newest stored greenzone frame at or before it, or -1</param>
		/// <param name="canRestart">the greenzone still holds frame 0</param>
		/// <param name="canSave">a project is open to save</param>
		public CoreStoppedForm(string reason, int stoppedAt, int safePoint, bool canRestart, bool canSave)
		{
			FormBorderStyle = FormBorderStyle.FixedDialog;
			StartPosition = FormStartPosition.CenterParent;
			MaximizeBox = false;
			MinimizeBox = false;
			ShowInTaskbar = false;
			AutoSize = true;
			AutoSizeMode = AutoSizeMode.GrowAndShrink;
			Padding = new Padding(12);

			var layout = new TableLayoutPanel
			{
				AutoSize = true,
				AutoSizeMode = AutoSizeMode.GrowAndShrink,
				ColumnCount = 1,
				Dock = DockStyle.Fill,
			};
			layout.Controls.Add(new Label
			{
				AutoSize = true,
				MaximumSize = new Size(560, 0),
				Font = new Font(Font, FontStyle.Bold),
				Text = $"The core stopped at frame {stoppedAt}, and emulation is paused.",
				Margin = new Padding(0, 0, 0, 6),
			});
			layout.Controls.Add(new TextBox
			{
				Name = "Reason",
				ReadOnly = true,
				Multiline = true,
				WordWrap = true,
				ScrollBars = ScrollBars.Vertical,
				Width = 560,
				Height = 76,
				Text = reason,
				TabStop = false,
			});
			layout.Controls.Add(new Label
			{
				AutoSize = true,
				MaximumSize = new Size(560, 0),
				Text = "Your inputs, branches and markers are safe whichever you choose.",
				Margin = new Padding(0, 6, 0, 8),
			});
			AddChoice(layout, "BackToSafePoint",
				safePoint >= 0
					? $"Go back to frame {safePoint}, the latest safe point"
					: "Go back to the latest safe point (the greenzone holds none)",
				Choice.BackToSafePoint, safePoint >= 0);
			AddChoice(layout, "RestartFromFrameZero", "Restart the machine and run again from frame 0",
				Choice.RestartFromFrameZero, canRestart);
			AddChoice(layout, "SaveInputsAndClose", "Save the project's inputs without the greenzone, and close Chimera",
				Choice.SaveInputsAndClose, canSave);
			AddChoice(layout, "CloseWithoutSaving", "Close Chimera without saving...",
				Choice.CloseWithoutSaving, true);
			Controls.Add(layout);
		}

		private void AddChoice(TableLayoutPanel layout, string name, string text, Choice choice, bool enabled)
		{
			var button = new Button
			{
				Name = name,
				Text = text,
				AutoSize = true,
				Enabled = enabled,
				Dock = DockStyle.Fill,
				TextAlign = ContentAlignment.MiddleLeft,
				Padding = new Padding(6, 3, 6, 3),
			};
			button.Click += (_, _) =>
			{
				Chosen = choice;
				DialogResult = DialogResult.OK;
				Close();
			};
			layout.Controls.Add(button);
		}
	}
}
