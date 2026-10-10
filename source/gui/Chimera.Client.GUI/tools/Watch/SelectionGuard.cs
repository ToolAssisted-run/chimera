#nullable enable

using System.Windows.Forms;

namespace Chimera.Client.GUI
{
	/// <summary>
	/// Two questions a list of addresses has to ask before it acts on a
	/// keystroke or on its selection (issue 224).
	/// A menu shortcut such as Ctrl+A is taken before the control that has the
	/// keyboard sees the key, so someone typing in a value box selected every
	/// address in the list instead of the text. And an action on the selection
	/// was then an action on millions of rows: adding each to RAM Watch, or
	/// copying each, froze the window.
	/// </summary>
	public static class SelectionGuard
	{
		/// <summary>More rows than anyone picks by hand; above this an action asks first.</summary>
		public const int ManyRows = 1000;

		/// <summary>The text box that has the keyboard inside <paramref name="root"/>, if one does.</summary>
		public static TextBoxBase? TextBoxWithKeyboard(ContainerControl? root)
		{
			Control? control = root?.ActiveControl;
			// a group box, a panel or a spin box holds the one that really has it
			while (control is ContainerControl { ActiveControl: { } inner }) control = inner;
			return control as TextBoxBase;
		}

		/// <summary>More rows than selecting one by one stays quick for; above this Select All asks first.</summary>
		public const long ManyRowsToSelect = 1_000_000;

		/// <summary>What to ask before selecting all <paramref name="count"/> rows of a list; null when there is nothing to ask.</summary>
		public static string? SelectAllQuestion(long count)
			=> count > ManyRowsToSelect
				? $"The list holds {count:N0} addresses. Selecting all of them takes a while and uses a lot of memory. Select them all?"
				: null;

		/// <summary>What to ask before acting on <paramref name="count"/> rows; null when there is nothing to ask.</summary>
		public static string? Question(int count, string action)
			=> count > ManyRows
				? $"{count:N0} addresses are selected. {action} all of them?"
				: null;
	}
}
