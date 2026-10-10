#nullable enable

using System.Windows.Forms;

using Chimera.Client.GUI;

namespace Chimera.Tests.Client.GUI
{
	/// <summary>
	/// Issue 224: Ctrl+A typed in RAM Search's value box selected every address
	/// in the list, and the next action then ran over millions of rows and
	/// froze the window. The two questions the form now asks first.
	/// </summary>
	[TestClass]
	public class SelectionGuardTests
	{
		[TestMethod]
		public void TheTextBoxThatHasTheKeyboardIsFoundThroughItsContainers()
		{
			using Form form = new();
			UserControl group = new();
			TextBox value = new();
			Button search = new();
			group.Controls.Add(value);
			form.Controls.Add(group);
			form.Controls.Add(search);

			// nothing has it yet
			Assert.IsNull(SelectionGuard.TextBoxWithKeyboard(form));

			// a text box inside a container: the container is the form's active
			// control, and the box is the container's
			group.ActiveControl = value;
			form.ActiveControl = group;
			Assert.AreSame(value, SelectionGuard.TextBoxWithKeyboard(form));

			// a button has it: Ctrl+A means the list
			form.ActiveControl = search;
			Assert.IsNull(SelectionGuard.TextBoxWithKeyboard(form));

			Assert.IsNull(SelectionGuard.TextBoxWithKeyboard(null));
		}

		[TestMethod]
		public void AnActionOnManyRowsAsksFirstAndOnFewDoesNot()
		{
			Assert.IsNull(SelectionGuard.Question(1, "Copy"));
			Assert.IsNull(SelectionGuard.Question(SelectionGuard.ManyRows, "Copy"));
			var question = SelectionGuard.Question(SelectionGuard.ManyRows + 1, "Add to RAM Watch");
			Assert.IsNotNull(question);
			StringAssert.Contains(question, "addresses are selected");
			StringAssert.Contains(question, "Add to RAM Watch all of them?");
		}

		[TestMethod]
		public void SelectingAllOfAHugeListAsksFirst()
		{
			Assert.IsNull(SelectionGuard.SelectAllQuestion(0));
			Assert.IsNull(SelectionGuard.SelectAllQuestion(SelectionGuard.ManyRowsToSelect));
			var question = SelectionGuard.SelectAllQuestion(16L * 1024 * 1024);
			Assert.IsNotNull(question);
			StringAssert.Contains(question, "Select them all?");
		}
	}
}
