using System.Collections.Generic;
using System.Linq;
using System.Windows.Forms;

using BizHawk.Client.Common;
using BizHawk.Client.EmuHawk;
using BizHawk.Emulation.Common;

namespace BizHawk.Tests.Client.EmuHawk
{
	/// <summary>
	/// Drives the Firmware window over a fixed set of declarations. Same split as
	/// <see cref="OpenCoreFormTests"/>: the verdicts are decided (and tested)
	/// in <c>CoreFirmwareStore</c>, so what is left here is that the window shows
	/// them and that its two buttons reach the owner.
	/// </summary>
	[TestClass]
	public class CoreFirmwareFormTests
	{
		private static CoreFirmwareEntry Entry(string core, string id, CoreFirmwareState state, string path = null, bool required = true)
			=> new()
			{
				CoreName = core,
				Decl = new() { Id = id, Display = id is "bios" ? "FDS BIOS" : id, Size = 8192, Required = required },
				Path = path,
				State = state,
			};

		private static ListView ListOf(Form form) => form.Controls.OfType<ListView>().Single();

		private static CoreFirmwareForm MakeForm(
			IReadOnlyList<CoreFirmwareEntry> entries,
			Action<CoreFirmwareEntry, string> setPath = null)
		{
			var rows = entries.ToList();
			CoreFirmwareForm form = new(() => rows, (entry, path) => setPath?.Invoke(entry, path));
			form.Show(); // real handles, or selection state is not tracked
			return form;
		}

		[TestMethod]
		public void EveryDeclarationOfEveryCoreIsListedWithItsState()
		{
			using var form = MakeForm(
			[
				Entry("QuickerNesHawk", "bios", CoreFirmwareState.Good, "/roms/disksys.rom"),
				Entry("SomeCore", "charset", CoreFirmwareState.Missing),
			]);

			var list = ListOf(form);
			Assert.AreEqual(2, list.Items.Count);
			Assert.AreEqual("QuickerNesHawk", list.Items[0].Text);
			Assert.AreEqual("FDS BIOS", list.Items[0].SubItems[1].Text);
			Assert.AreEqual("ok", list.Items[0].SubItems[2].Text);
			Assert.AreEqual("disksys.rom", list.Items[0].SubItems[3].Text, "the File column shows the name, the detail line the path");
			Assert.AreEqual("not provided", list.Items[1].SubItems[2].Text);
			Assert.AreEqual("", list.Items[1].SubItems[3].Text);
		}

		/// <summary>An optional file that is absent is not a problem, and must not be dressed as one.</summary>
		[TestMethod]
		public void OptionalIsSaidToBeOptional()
		{
			using var form = MakeForm([ Entry("Core", "expansion", CoreFirmwareState.Missing, required: false) ]);
			StringAssert.Contains(ListOf(form).Items[0].SubItems[2].Text, "optional");
		}

		[TestMethod]
		public void AWrongSizedFileSaysWhatWasExpected()
		{
			using var form = MakeForm([ Entry("Core", "bios", CoreFirmwareState.WrongSize, "/roms/wrong.rom") ]);
			StringAssert.Contains(ListOf(form).Items[0].SubItems[2].Text, "8192");
		}

		/// <summary>Clearing a row hands the owner a null path - the frontend's "forget this".</summary>
		[TestMethod]
		public void ClearForgetsTheSelectedRow()
		{
			List<(string Core, string Id, string Path)> calls = new();
			using var form = MakeForm(
				[ Entry("Core", "bios", CoreFirmwareState.Good, "/roms/disksys.rom") ],
				(entry, path) => calls.Add((entry.CoreName, entry.Decl.Id, path)));

			form.Controls.OfType<Button>().Single(static b => b.Text is "Clear").PerformClick();

			Assert.AreEqual(1, calls.Count);
			Assert.AreEqual(("Core", "bios", null), calls[0]);
		}

		/// <summary>With nothing selected there is nothing to clear; the button must not throw.</summary>
		[TestMethod]
		public void ButtonsAreHarmlessWithAnEmptyList()
		{
			var called = false;
			using var form = MakeForm([ ], (_, _) => called = true);
			form.Controls.OfType<Button>().Single(static b => b.Text is "Clear").PerformClick();
			Assert.IsFalse(called);
			Assert.IsNull(form.SelectedEntry);
		}

	}
}
