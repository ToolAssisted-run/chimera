using System.Collections.Generic;
using System.Linq;
using System.Windows.Forms;

using Chimera.Client.Common;
using Chimera.Client.GUI;

namespace Chimera.Tests.Client.GUI
{
	/// <summary>
	/// The order of a slot's files is part of the machine - the first is in the drive at boot and
	/// the rest follow in swap order - so it can be changed without removing everything and
	/// starting again (user, 2026-09-17: a PC-98 game can want its second disk booted).
	/// </summary>
	[TestClass]
	public class WizardFileOrderTests
	{
		private const string Declaration = """
			{
			  "slots": [
			    { "id": "floppy", "title": "Floppy disks", "min": 0, "max": -1, "formats": ["fdi"] },
			    { "id": "hdd", "title": "Hard disk", "min": 0, "max": 1, "formats": ["hdd"] }
			  ]
			}
			""";

		private static NewProjectWizard MakeForm()
		{
			NewProjectWizard form = new([ ], static _ => [ ]);
			form.Show();
			form.UseDeclaration(ProjectSlotDeclaration.Parse(Declaration));
			foreach (var disk in new[] { "A", "B", "C" }) form.AddFileToSlot("floppy", $"/games/Disk {disk}.fdi");
			return form;
		}

		private static string[] Order(NewProjectWizard form)
			=> form.FilesInSlot("floppy").Select(static p => System.IO.Path.GetFileNameWithoutExtension(p)).ToArray();

		[TestMethod]
		public void AFileMovesUpAndDownAndTheRestKeepTheirOrder()
		{
			using var form = MakeForm();
			CollectionAssert.AreEqual(new[] { "Disk A", "Disk B", "Disk C" }, Order(form));
			form.MoveFileInSlot("floppy", 1, -1);
			CollectionAssert.AreEqual(new[] { "Disk B", "Disk A", "Disk C" }, Order(form), "the second disk is now the one that boots");
			form.MoveFileInSlot("floppy", 0, +1);
			form.MoveFileInSlot("floppy", 1, +1);
			CollectionAssert.AreEqual(new[] { "Disk A", "Disk C", "Disk B" }, Order(form));
		}

		[TestMethod]
		public void MovingPastEitherEndOrFromNowhereChangesNothing()
		{
			using var form = MakeForm();
			form.MoveFileInSlot("floppy", 0, -1);
			form.MoveFileInSlot("floppy", 2, +1);
			form.MoveFileInSlot("floppy", -1, +1);
			form.MoveFileInSlot("floppy", 7, -1);
			form.MoveFileInSlot("no such slot", 0, +1);
			CollectionAssert.AreEqual(new[] { "Disk A", "Disk B", "Disk C" }, Order(form));
		}

		private static IEnumerable<Button> Arrows(Control root)
			=> root.Controls.Cast<Control>().SelectMany(static c => Arrows(c).Concat(c is Button { Text: "▲" or "▼" } b ? new[] { b } : [ ]));

		[TestMethod]
		public void OnlyASlotThatTakesSeveralFilesHasTheButtonsAndTheyFollowTheSelection()
		{
			using var form = MakeForm();
			var arrows = Arrows(form).ToList();
			Assert.AreEqual(2, arrows.Count, "the floppy list has an Up and a Down; the single hard disk slot has neither");
			var up = arrows.Single(static b => b.Text == "▲");
			var down = arrows.Single(static b => b.Text == "▼");
			Assert.IsFalse(up.Enabled || down.Enabled, "nothing is selected, so there is nothing to move");

			var list = up.Parent.Controls.OfType<ListBox>().Single();
			list.SelectedIndex = 0;
			Assert.IsFalse(up.Enabled, "the first cannot go up");
			Assert.IsTrue(down.Enabled);

			down.PerformClick();
			CollectionAssert.AreEqual(new[] { "Disk B", "Disk A", "Disk C" }, Order(form));
			Assert.AreEqual(1, list.SelectedIndex, "the moved file stays selected, so the button can be pressed again");
			Assert.IsTrue(up.Enabled && down.Enabled);
			down.PerformClick();
			Assert.IsFalse(down.Enabled, "the last cannot go down");
		}
	}
}
