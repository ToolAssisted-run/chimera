using System.Collections.Generic;
using System.Linq;
using System.Windows.Forms;

using Chimera.Client.Common;
using Chimera.Client.GUI;
using Chimera.Emulation.Common;

namespace Chimera.Tests.Client.GUI
{
	/// <summary>
	/// Drives the Firmware window over a fixed set of declarations. The verdicts
	/// are decided (and tested) in <c>CoreFirmwareStore</c>, so what is left here
	/// is that the window shows them and that its two buttons reach the owner.
	/// </summary>
	[TestClass]
	public class CoreFirmwareFormTests
	{
		private static CoreFirmwareEntry Entry(
			string core,
			string id,
			CoreFirmwareState state,
			string path = null,
			bool required = true,
			string sha1 = null,
			params string[] expected)
			=> new()
			{
				CoreName = core,
				Decl = new()
				{
					Id = id,
					Display = id is "bios" ? "FDS BIOS" : id,
					Size = 8192,
					Required = required,
					Sha1 = expected.Length is 0 ? null : expected[0],
				},
				Path = path,
				Sha1 = sha1,
				State = state,
			};

		// column order: [mark] Core, Firmware, Expected SHA1, Actual SHA1, Status, File
		private const int COL_CORE = 1;
		private const int COL_FIRMWARE = 2;
		private const int COL_EXPECTED = 3;
		private const int COL_ACTUAL = 4;
		private const int COL_STATUS = 5;
		private const int COL_FILE = 6;

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
			Assert.AreEqual("QuickerNesHawk", list.Items[0].SubItems[COL_CORE].Text);
			Assert.AreEqual("FDS BIOS", list.Items[0].SubItems[COL_FIRMWARE].Text);
			Assert.AreEqual("ok", list.Items[0].SubItems[COL_STATUS].Text);
			Assert.AreEqual("disksys.rom", list.Items[0].SubItems[COL_FILE].Text, "the File column shows the name, the detail line the path");
			Assert.AreEqual("not provided", list.Items[1].SubItems[COL_STATUS].Text);
			Assert.AreEqual("", list.Items[1].SubItems[COL_FILE].Text);
		}

		/// <summary>An optional file that is absent is not a problem, and must not be dressed as one.</summary>
		[TestMethod]
		public void OptionalIsSaidToBeOptional()
		{
			using var form = MakeForm([ Entry("Core", "expansion", CoreFirmwareState.Missing, required: false) ]);
			StringAssert.Contains(ListOf(form).Items[0].SubItems[COL_STATUS].Text, "optional");
		}

		[TestMethod]
		public void ACustomFileSaysWhatTheCoreExpected()
		{
			using var form = MakeForm([ Entry("Core", "bios", CoreFirmwareState.Custom, "/roms/wrong.rom") ]);
			var status = ListOf(form).Items[0].SubItems[COL_STATUS].Text;
			StringAssert.Contains(status, "8192");
			StringAssert.Contains(status, "used anyway", "a substituted file is used, and the row must not imply otherwise");
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

		/// <summary>
		/// The point of the window: what the core says is right, what you actually have,
		/// and a mark that answers "am I done here" without reading either.
		/// </summary>
		[TestMethod]
		public void ExpectedAndActualHashesAreBothShown()
		{
			const string GOOD = "57FE1BDEE955BB48D357E463CCBF129496930B62";
			const string OTHER = "E4E41472C454F928E53EB10E0509BF7D1146ECC1";
			const string MINE = "0123456789ABCDEF0123456789ABCDEF01234567";
			using var form = MakeForm(
			[
				Entry("Core", "bios", CoreFirmwareState.Good, "/f/a.rom", sha1: GOOD, expected: new[] { GOOD, OTHER }),
				Entry("Core", "boot", CoreFirmwareState.Unrecognised, "/f/b.rom", sha1: MINE, expected: new[] { GOOD }),
				Entry("Core", "char", CoreFirmwareState.Missing, expected: new[] { GOOD }),
			]);

			var list = ListOf(form);
			Assert.AreEqual("57FE1BDE", list.Items[0].SubItems[COL_EXPECTED].Text);
			Assert.AreEqual("57FE1BDE", list.Items[0].SubItems[COL_ACTUAL].Text, "a match reads the same on both sides");
			Assert.AreEqual("0123456789ABCDEF"[..8], list.Items[1].SubItems[COL_ACTUAL].Text);
			Assert.AreNotEqual(list.Items[1].SubItems[COL_EXPECTED].Text, list.Items[1].SubItems[COL_ACTUAL].Text);
			Assert.AreEqual("", list.Items[2].SubItems[COL_ACTUAL].Text, "nothing provided means nothing to hash");

			// and the marks: tick, warning, cross, empty circle
			Assert.AreEqual(0, list.Items[0].ImageIndex);
			Assert.AreEqual(1, list.Items[1].ImageIndex);
			Assert.AreEqual(3, list.Items[2].ImageIndex);
		}

		/// <summary>
		/// A file that will be used but is not what the core knows (custom, unrecognised)
		/// carries the caution mark; only a file that cannot be used at all gets the error
		/// mark. The user has to be able to tell "running, but not standard" from "broken".
		/// </summary>
		[TestMethod]
		public void ACustomFileIsMarkedCautionAndAnUnusableOneError()
		{
			using var form = MakeForm(
			[
				Entry("Core", "bios", CoreFirmwareState.Custom, "/f/small.rom", sha1: "AABBCCDDEE"),
				Entry("Core", "boot", CoreFirmwareState.Unreadable, "/f/gone.rom"),
			]);
			Assert.AreEqual(1, ListOf(form).Items[0].ImageIndex);
			Assert.AreEqual(2, ListOf(form).Items[1].ImageIndex);
			Assert.AreEqual("AABBCCDD", ListOf(form).Items[0].SubItems[COL_ACTUAL].Text, "a custom file still shows what it hashed to");
		}

		/// <summary>A declaration that pins nothing says so rather than showing an empty column.</summary>
		[TestMethod]
		public void ADeclarationWithNoHashesSaysAny()
		{
			using var form = MakeForm([ Entry("Core", "bios", CoreFirmwareState.Good, "/f/a.rom", sha1: "AABBCCDDEE") ]);
			Assert.AreEqual("(any)", ListOf(form).Items[0].SubItems[COL_EXPECTED].Text);
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
