#nullable enable

using System.Windows.Forms;

using Chimera.Client.Common;

namespace Chimera.Client.EmuHawk
{
	public static class DialogControllerWinFormsExtensions
	{
		/// <summary>every modal dialog in the app funnels through the extensions below; in --headless mode showing one is fatal (log + exit) rather than an invisible block</summary>
		private static void HeadlessGuard(string kind, string? detail)
		{
			if (HeadlessMode.Enabled) HeadlessMode.FatalDialog(kind, detail);
		}

		public static IWin32Window AsWinFormsHandle(this IDialogParent dialogParent) => (IWin32Window) dialogParent;

		public static DialogResult ShowDialogAsChild(this IDialogParent dialogParent, CommonDialog dialog)
		{
			HeadlessGuard(dialog.GetType().Name, null);
			return dialog.ShowDialog(dialogParent.AsWinFormsHandle());
		}

		public static DialogResult ShowDialogAsChild(this IDialogParent dialogParent, Form dialog)
		{
			HeadlessGuard(dialog.GetType().Name, dialog.Text);
			return dialog.ShowDialog(dialogParent.AsWinFormsHandle());
		}

		public static DialogResult ShowDialogWithTempMute(this IDialogParent dialogParent, CommonDialog dialog)
		{
			HeadlessGuard(dialog.GetType().Name, null);
			return dialogParent.DialogController.DoWithTempMute(() => dialog.ShowDialog(dialogParent.AsWinFormsHandle()));
		}

		public static DialogResult ShowDialogWithTempMute(this IDialogParent dialogParent, Form dialog)
		{
			HeadlessGuard(dialog.GetType().Name, dialog.Text);
			return dialogParent.DialogController.DoWithTempMute(() => dialog.ShowDialog(dialogParent.AsWinFormsHandle()));
		}

		public static DialogResult ShowDialogWithTempMute(this IDialogParent dialogParent, FolderBrowserEx dialog)
		{
			HeadlessGuard(nameof(FolderBrowserEx), dialog.Description);
			return dialogParent.DialogController.DoWithTempMute(() => dialog.ShowDialog(dialogParent.AsWinFormsHandle()));
		}

		public static DialogResult ShowMessageBox(
			this IDialogController mainForm,
			IDialogParent? owner,
			string text,
			string? caption,
			MessageBoxButtons buttons,
			EMsgBoxIcon? icon)
		{
			HeadlessGuard(caption ?? "(no caption)", text);
			return MessageBox.Show(
					owner?.AsWinFormsHandle(),
					text,
					caption ?? string.Empty,
					buttons,
					icon switch
					{
						null => MessageBoxIcon.None,
						EMsgBoxIcon.None => MessageBoxIcon.None,
						EMsgBoxIcon.Error => MessageBoxIcon.Error,
						EMsgBoxIcon.Question => MessageBoxIcon.Question,
						EMsgBoxIcon.Warning => MessageBoxIcon.Warning,
						EMsgBoxIcon.Info => MessageBoxIcon.Information,
						_ => throw new InvalidOperationException(),
					});
		}
	}
}
