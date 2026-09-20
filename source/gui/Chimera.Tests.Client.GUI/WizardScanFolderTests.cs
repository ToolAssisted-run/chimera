using System.Collections.Generic;
using System.IO;

using Chimera.Client.GUI;

namespace Chimera.Tests.Client.GUI
{
	/// <summary>
	/// The sub-folders choice belongs to the act of choosing a folder, so it travels with the picker:
	/// the page hands its current answer IN and takes back whatever the person settled on.
	///
	/// WHAT THESE TESTS CAN SEE, and what they cannot:
	/// they drive the wizard with a stub picker, so they prove the wizard offers the right value, honours
	/// what comes back, and remembers it. They prove NOTHING about the Windows dialog itself - whether it
	/// draws the tick box, and whether unticking it is read back - because that is COM against the Windows
	/// shell and these run on Mono. tests/ui/windows/folder-picker-checkbox.sh asks that question on a real
	/// Windows; see docs/folder-picker.md.
	/// </summary>
	[TestClass]
	public class WizardScanFolderTests
	{
		private static string _dir = "";

		[ClassInitialize]
		public static void MakePlayground(TestContext _)
		{
			_dir = Path.Combine(Path.GetTempPath(), $"chimera-wizard-scan-{System.Guid.NewGuid():N}");
			Directory.CreateDirectory(_dir);
		}

		[ClassCleanup(ClassCleanupBehavior.EndOfClass)]
		public static void RemovePlayground() => Directory.Delete(_dir, recursive: true);

		/// <summary>a folder whose only firmware is one level down, so the choice decides the outcome</summary>
		private static string MakeBuriedFirmware(string name)
		{
			var root = Path.Combine(_dir, name);
			var below = Path.Combine(root, "dumps");
			Directory.CreateDirectory(below);
			File.WriteAllText(Path.Combine(root, "readme.txt"), "nothing to find here");
			File.WriteAllText(Path.Combine(below, "disksys.rom"), "FDS BIOS BYTES");
			return root;
		}

		/// <summary>the wizard's firmware page, wanting the FDS bios, with a picker under the test's thumb</summary>
		private static NewProjectWizard MakeForm(PickScanFolder picker)
		{
			NewProjectWizard form = new([ ], static _ => [ ], pickFirmwareFolder: picker);
			form.Show();
			var fixtureDir = Path.Combine(_dir, Path.GetRandomFileName());
			Directory.CreateDirectory(fixtureDir);
			var (cfg, _) = ProjectFormsShots.MakeFirmwareFixture(fixtureDir);
			// no index: nothing is found until the scan finds it
			form.UseFirmwareNeeds(cfg, [ ("bios", 2) ], [ ]);
			return form;
		}

		[TestMethod]
		public void TheScanOffersTheAnswerItHoldsAndActsOnWhatComesBack()
		{
			var folder = MakeBuriedFirmware("offered");
			List<bool> offered = new();
			var answer = true;
			using var form = MakeForm((ref bool includeSubfolders) =>
			{
				offered.Add(includeSubfolders);
				includeSubfolders = answer;
				return folder;
			});

			// ticked is what the page starts on and what it offers
			answer = false;
			form.ScanFirmwareFolderForTest();
			CollectionAssert.AreEqual(new[] { true }, offered, "the page offers the answer it holds, which starts ticked");
			Assert.IsFalse(form.FirmwareSatisfied("bios"),
				"unticked, the scan must not descend - and the only dump is one level down");
			Assert.IsFalse(form.FirmwareScanSubfolders, "the page took the picker's answer back");

			// and the answer it took back is the one it offers next time
			answer = true;
			form.ScanFirmwareFolderForTest();
			CollectionAssert.AreEqual(new[] { true, false }, offered, "the second scan offers what the first settled on");
			Assert.IsTrue(form.FirmwareSatisfied("bios"), "ticked, the scan reaches the dump below the folder");
			Assert.IsTrue(form.FirmwareScanSubfolders);
		}

		[TestMethod]
		public void ACancelledPickerScansNothingAndChangesNothing()
		{
			var seen = 0;
			using var form = MakeForm((ref bool includeSubfolders) =>
			{
				seen++;
				includeSubfolders = false; // a picker that was cancelled must not be believed
				return null;
			});

			form.ScanFirmwareFolderForTest();
			Assert.AreEqual(1, seen);
			Assert.IsFalse(form.FirmwareSatisfied("bios"), "no folder, no scan");
			Assert.IsTrue(form.FirmwareScanSubfolders,
				"a null return means the flag means nothing; the page keeps the answer it had");
		}

		/// <summary>
		/// One control for one setting. The page carries a box of its own exactly where the folder picker
		/// cannot carry one - so on Mono it is on the page, on Windows it is in the dialog, never both.
		///
		/// BOTH halves are asked here, on whichever platform this runs, by telling the picker what to say
		/// about itself. Asking only <see cref="FolderBrowserEx.CanShowCheckBox"/> as it really is would
		/// check one half and leave the other - the one that hides the page's box - untested on the only
		/// platform the suite runs on, which is exactly how a rule with two sides ships half-broken.
		/// </summary>
		[TestMethod]
		public void ExactlyOneControlOffersTheChoice()
		{
			try
			{
				FolderBrowserEx.PretendCanShowCheckBox = true;
				using var pickerCarriesIt = MakeForm((ref bool _) => null);
				Assert.IsFalse(pickerCarriesIt.FirmwareScanSubfoldersVisible,
					"the dialog asks, so the page must not ask as well");
				Assert.IsTrue(pickerCarriesIt.FirmwareScanSubfolders,
					"hidden, but still the page's memory of the answer, and still ticked to start with");

				FolderBrowserEx.PretendCanShowCheckBox = false;
				using var pageCarriesIt = MakeForm((ref bool _) => null);
				Assert.IsTrue(pageCarriesIt.FirmwareScanSubfoldersVisible,
					"a picker with nowhere to put the option leaves the page to offer it");

				// and a wizard with no Scan Folder button at all shows neither
				using NewProjectWizard noPicker = new([ ], static _ => [ ]);
				noPicker.Show();
				Assert.IsFalse(noPicker.FirmwareScanSubfoldersVisible, "no Scan Folder button, no option beside it");
			}
			finally
			{
				FolderBrowserEx.PretendCanShowCheckBox = null;
			}
		}
	}
}
