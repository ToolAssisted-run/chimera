using System.Collections.Generic;
using System.Linq;
using System.Windows.Forms;

using Chimera.Client.Common;
using Chimera.Client.GUI;
using Chimera.Emulation.Common;

namespace Chimera.Tests.Client.GUI
{
	/// <summary>
	/// Drives Config > Firmware over surveyed groups. The verdicts are decided
	/// (and tested) in <c>FirmwareSurvey</c>; what is left here is that the window
	/// shows them, folds a long list of releases to the ones on hand, and that
	/// choosing and clearing a file reach the owner.
	/// </summary>
	[TestClass]
	public class FirmwareSurveyFormTests
	{
		private static FirmwareSurveyRow Row(string core, string id, string label, CoreFirmwareState state, string sha1 = null, string path = null)
			=> new()
			{
				CoreName = core,
				Decl = new() { Id = id, Display = "Console BIOS", Label = label, Size = 4096, Sha1 = sha1 },
				State = state,
				Path = path,
				Where = path is null ? FirmwareWhere.Nowhere : FirmwareWhere.Chosen,
			};

		private static FirmwareSurveyGroup Ps2(int releases, int onHand)
		{
			List<FirmwareSurveyRow> rows = new();
			for (var i = 0; i < releases; i++)
			{
				var found = i < onHand;
				rows.Add(Row("PCSX2", "bios.bin", $"release {i}", found ? CoreFirmwareState.Good : CoreFirmwareState.Missing,
					sha1: new string((char)('A' + i % 6), 40), path: found ? $"/dumps/r{i}.bin" : null));
			}
			return new() { CoreName = "PCSX2", Rows = rows };
		}

		private sealed class Harness : IDisposable
		{
			internal readonly List<(FirmwareSurveyRow Row, string Path)> Remembered = new();
			internal string NextPick;
			internal IReadOnlyList<FirmwareSurveyGroup> Groups;
			internal readonly FirmwareSurveyForm Form;

			internal Harness(params FirmwareSurveyGroup[] groups)
			{
				Groups = groups;
				Form = new(() => Groups, (row, path) => Remembered.Add((row, path)), _ => NextPick);
				Form.Show();
			}

			public void Dispose() => Form.Dispose();
		}

		private static ListView ListOf(Form form) => form.Controls.OfType<ListView>().Single();

		[TestMethod]
		public void EveryCoreIsAGroupAndEveryDeclarationARow()
		{
			using Harness h = new(
				new FirmwareSurveyGroup { CoreName = "xemu", Rows = [ Row("xemu", "mcpx", null, CoreFirmwareState.Good, path: "/fw/mcpx_1.0.bin"), Row("xemu", "hdd", null, CoreFirmwareState.Missing) ] },
				new FirmwareSurveyGroup { CoreName = "Stella", Rows = [ ] });
			var list = ListOf(h.Form);
			Assert.AreEqual(2, list.Groups.Count);
			StringAssert.StartsWith(list.Groups[0].Header, "xemu");
			Assert.AreEqual(2, h.Form.DisplayedRows.Count);
			Assert.AreEqual(3, list.Items.Count, "the core that needs nothing still says so");
		}

		[TestMethod]
		public void ALongListOfReleasesFoldsToTheOnesOnHand()
		{
			using Harness h = new(Ps2(releases: 73, onHand: 2));
			Assert.AreEqual(2, h.Form.DisplayedRows.Count, "only the dumps on hand are rows");
			var list = ListOf(h.Form);
			Assert.AreEqual(3, list.Items.Count, "plus the line saying how many more there are");
			StringAssert.Contains(list.Items[2].SubItems[1].Text, "71 more");

			var showAll = h.Form.Controls.OfType<CheckBox>().Single();
			showAll.Checked = true;
			Assert.AreEqual(73, h.Form.DisplayedRows.Count);
		}

		[TestMethod]
		public void AShortListIsNeverFolded()
		{
			using Harness h = new(Ps2(releases: 3, onHand: 0));
			Assert.AreEqual(3, h.Form.DisplayedRows.Count);
		}

		[TestMethod]
		public void SetFileAndClearReachTheOwner()
		{
			using Harness h = new(Ps2(releases: 2, onHand: 1));
			var list = ListOf(h.Form);
			list.Items[1].Selected = true;
			Assert.AreEqual("release 1", h.Form.SelectedRow.Decl.Label);

			h.NextPick = "/somewhere/else.bin";
			h.Form.Controls.OfType<Button>().Single(static b => b.Text == "Set File...").PerformClick();
			Assert.AreEqual(1, h.Remembered.Count);
			Assert.AreEqual("release 1", h.Remembered[0].Row.Decl.Label);
			Assert.AreEqual("/somewhere/else.bin", h.Remembered[0].Path);

			list.Items[0].Selected = true;
			h.Form.Controls.OfType<Button>().Single(static b => b.Text == "Clear").PerformClick();
			Assert.AreEqual(2, h.Remembered.Count);
			Assert.IsNull(h.Remembered[1].Path, "a clear forgets");
			Assert.AreEqual("release 0", h.Remembered[1].Row.Decl.Label);
		}

		[TestMethod]
		public void APickThatIsCancelledChangesNothing()
		{
			using Harness h = new(Ps2(releases: 1, onHand: 0));
			ListOf(h.Form).Items[0].Selected = true;
			h.NextPick = null;
			h.Form.Controls.OfType<Button>().Single(static b => b.Text == "Set File...").PerformClick();
			Assert.AreEqual(0, h.Remembered.Count);
		}
	}
}
