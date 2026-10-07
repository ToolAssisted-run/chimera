using System.Collections.Generic;
using System.Linq;
using System.Windows.Forms;

using Chimera.Client.GUI;
using Chimera.Emulation.Common.Waterbox;

namespace Chimera.Tests.Client.GUI
{
	/// <summary>
	/// A core describes its settings in paragraphs, and the property grid's own
	/// description pane holds two lines of one and then gives up - the rest was
	/// only readable as a tooltip drawn wider than the screen (issue #41). The
	/// wizard puts a scrollable box there instead, filled from the selected row.
	/// </summary>
	[TestClass]
	public class WizardDescriptionPaneTests
	{
		private const string LongDescription =
			"Size the state pool to this machine and this system when a movie loads: a quarter of"
			+ " the RAM that is actually free (never less than 1GB, never more than 8GB), and no"
			+ " more than the states can use - a small system stops reserving space it cannot fill,"
			+ " and a big one gets what the machine can spare.";

		private static NewProjectWizard MakeForm()
		{
			NewProjectWizard form = new([ ], static _ => [ ]);
			form.Show();
			form.UseSettingsDecls(
			[
				new WaterboxConfig.SettingDecl
				{
					Name = "memoryLimit",
					Display = "Memory Limit",
					Type = "int",
					Default = 3072,
					Description = LongDescription,
				},
			]);
			return form;
		}

		private static IEnumerable<Control> Descendants(Control root)
		{
			foreach (Control child in root.Controls)
			{
				yield return child;
				foreach (var deeper in Descendants(child)) yield return deeper;
			}
		}

		private static TextBox DescriptionBox(Control root)
			=> Descendants(root).OfType<TextBox>().First(static t => t.ReadOnly && t.Multiline);

		[TestMethod]
		public void TheDescriptionIsShownWhole()
		{
			using var form = MakeForm();
			var box = DescriptionBox(form);
			StringAssert.Contains(box.Text, "never more than 8GB");
			StringAssert.Contains(box.Text, "the machine can spare.");
			StringAssert.Contains(box.Text, "Memory Limit");
		}

		[TestMethod]
		public void TheBoxScrollsAndIsNotTypedInto()
		{
			using var form = MakeForm();
			var box = DescriptionBox(form);
			Assert.AreEqual(ScrollBars.Vertical, box.ScrollBars, "a paragraph needs a scrollbar");
			Assert.IsTrue(box.ReadOnly, "a description is read, not edited");
			Assert.IsTrue(box.WordWrap, "a paragraph wraps rather than running off the side");
			Assert.IsFalse(box.TabStop, "tabbing through the page should not stop in the description");
		}

		[TestMethod]
		public void TheGridsOwnPaneIsOff()
		{
			using var form = MakeForm();
			var grid = Descendants(form).OfType<PropertyGrid>().First();
			Assert.IsFalse(grid.HelpVisible, "two panes saying the same thing is one too many");
		}
	}
}
