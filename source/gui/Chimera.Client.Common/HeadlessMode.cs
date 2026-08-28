#nullable enable

namespace Chimera.Client.Common
{
	/// <summary>
	/// State of the <c>--headless</c> CLI option, for unattended automation runs
	/// (e.g. the witness harness) where nobody can answer a dialog. Any attempt
	/// to show a modal dialog logs its content to the console and terminates the
	/// process with <see cref="EXIT_CODE_DIALOG"/> instead of blocking invisibly
	/// on a display no one is watching; warning dialogs whose flow would continue
	/// are logged and skipped instead (see call sites in <c>Program</c>).
	/// </summary>
	public static class HeadlessMode
	{
		/// <summary>distinct from regular exit codes so harnesses can tell "a dialog would have blocked here" from other failures</summary>
		public const int EXIT_CODE_DIALOG = 64;

		public static bool Enabled { get; set; }

		/// <summary>logs a dialog that cannot be meaningfully answered without a user, then terminates the process</summary>
		public static void FatalDialog(string? caption, string? text)
		{
			Console.Error.WriteLine($"[headless] a modal dialog would have blocked; exiting {EXIT_CODE_DIALOG}");
			Console.Error.WriteLine($"[headless] caption: {caption}");
			Console.Error.WriteLine($"[headless] text: {text}");
			Environment.Exit(EXIT_CODE_DIALOG);
		}

		/// <summary>logs a dialog whose surrounding flow continues normally (warnings, fallback notices)</summary>
		public static void LogSuppressedWarning(string text)
			=> Console.Error.WriteLine($"[headless] suppressed warning dialog: {text}");
	}
}
