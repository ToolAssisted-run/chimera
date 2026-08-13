using System.Collections.Generic;
using System.Linq;
using System.Windows.Forms;

using BizHawk.Client.Common;
using BizHawk.Client.EmuHawk;

namespace BizHawk.Tests.Client.EmuHawk
{
	/// <summary>
	/// Drives the Core Packages window without a person: build it over a known
	/// list, tick and untick rows, and check that what it shows and what it writes
	/// to the config agree.
	///
	/// What this can prove is behaviour - rows, states, the effect of a click.
	/// What it cannot prove is that the window looks right; that still needs eyes
	/// on a screenshot. Keeping the two apart is why the states live in
	/// <see cref="CorePackageList"/> and this class only checks the wiring.
	/// </summary>
	[TestClass]
	public class CorePackagesFormTests
	{
		private static DiscoveredCorePackage Pkg(string name, string path, string sha1 = null, string error = null)
			=> new() { Name = name, Path = path, Sha1 = sha1, Error = error, Systems = [ "NES" ] };

		private static ListView ListOf(Form form)
			=> form.Controls.OfType<ListView>().Single();

		/// <summary>
		/// Shows the form so its controls have real window handles: without them,
		/// selection state is not tracked and half of what this class checks would
		/// silently do nothing. Xvfb gives us a display to show it on.
		/// </summary>
		private static CorePackagesForm Realise(CorePackagesForm form)
		{
			form.Show();
			return form;
		}

		/// <summary>a form over a fixed set of packages, none of them loaded</summary>
		private static CorePackagesForm MakeForm(
			Config config,
			IReadOnlyList<DiscoveredCorePackage> discovered,
			IReadOnlyList<CoreRegistry.LoadedCorePackage> loaded = null,
			Action rescan = null,
			Action addPackage = null)
			=> Realise(new CorePackagesForm(
				config,
				() => CorePackageList.Build(discovered, loaded ?? [ ], [ ], config),
				rescan ?? (() => { }),
				addPackage ?? (() => { }),
				[ "/tmp/Cores" ]));

		[TestMethod]
		public void RowsShowEveryPackageWithItsStatus()
		{
			Config config = new();
			var loadedPkg = Pkg("quickerNES", "/cores/q.zip", sha1: "0123456789abcdef0123456789abcdef01234567");
			using var form = MakeForm(
				config,
				[ loadedPkg, Pkg("Broken", "/cores/b.zip", sha1: "bb", error: "waterbox.config is empty or invalid") ],
				loaded: [ new CoreRegistry.LoadedCorePackage { Name = "quickerNES", Path = "/cores/q.zip", Sha1 = "0123456789abcdef0123456789abcdef01234567" } ]);

			var list = ListOf(form);
			Assert.AreEqual(2, list.Items.Count);
			var broken = list.Items[0];
			var quicker = list.Items[1];
			Assert.AreEqual("Broken", broken.Text);
			Assert.AreEqual("quickerNES", quicker.Text);
			Assert.AreEqual("NES", quicker.SubItems[1].Text);
			Assert.AreEqual("loaded", quicker.SubItems[2].Text);
			Assert.AreEqual("q.zip", quicker.SubItems[3].Text);
			Assert.AreEqual("01234567", quicker.SubItems[4].Text, "the SHA1 column shows the identity prefix");
			StringAssert.Contains(broken.SubItems[2].Text, "waterbox.config");
			Assert.IsTrue(quicker.Checked);
			Assert.IsFalse(broken.Checked, "an unreadable package is not something the user can switch on");
		}

		[TestMethod]
		public void TickingARowWritesTheChoiceToTheConfigAndRedrawsTheRow()
		{
			Config config = new();
			var pkg = Pkg("quickerNES", "/cores/q.zip", sha1: "aa");
			using var form = MakeForm(config, [ pkg ]);
			var list = ListOf(form);
			Assert.AreEqual("loads on restart", list.Items[0].SubItems[2].Text);

			list.Items[0].Checked = false; // exactly what a click does

			CollectionAssert.Contains(config.DisabledCorePackages, "aa");
			Assert.AreEqual("disabled", ListOf(form).Items[0].SubItems[2].Text, "the row must reflect the new state immediately");

			ListOf(form).Items[0].Checked = true;
			Assert.AreEqual(0, config.DisabledCorePackages.Count);
			Assert.AreEqual("loads on restart", ListOf(form).Items[0].SubItems[2].Text);
		}

		[TestMethod]
		public void UntickingALoadedPackageSaysItTakesARestart()
		{
			Config config = new();
			var pkg = Pkg("quickerNES", "/cores/q.zip", sha1: "aa");
			using var form = MakeForm(
				config,
				[ pkg ],
				loaded: [ new CoreRegistry.LoadedCorePackage { Name = "quickerNES", Path = "/cores/q.zip", Sha1 = "aa" } ]);

			ListOf(form).Items[0].Checked = false;

			Assert.AreEqual(CorePackageState.LoadedDisabled, form.Entries[0].State);
			StringAssert.Contains(ListOf(form).Items[0].SubItems[2].Text, "restart");
			Assert.IsTrue(
				form.Controls.OfType<Label>().Any(static l => l.Text.Contains("next launch")),
				"the window must explain why the core is still there");
		}

		[TestMethod]
		public void TickingABrokenPackageIsRefusedAndTheRowSnapsBack()
		{
			Config config = new();
			using var form = MakeForm(config, [ Pkg("Broken", "/cores/b.zip", sha1: "bb", error: "no systemId") ]);
			var list = ListOf(form);

			list.Items[0].Checked = true;

			Assert.IsFalse(list.Items[0].Checked, "there is nothing to enable — the package could not be read");
			Assert.AreEqual(0, config.DisabledCorePackages.Count, "and refusing must not write a disabled entry either");
		}

		[TestMethod]
		public void RescanRefreshesTheRows()
		{
			Config config = new();
			List<DiscoveredCorePackage> discovered = [ Pkg("First", "/cores/1.zip", sha1: "11") ];
			var rescanned = 0;
			using var form = Realise(new CorePackagesForm(
				config,
				() => CorePackageList.Build(discovered, [ ], [ ], config),
				() => { rescanned++; discovered.Add(Pkg("Second", "/cores/2.zip", sha1: "22")); },
				() => { },
				[ "/tmp/Cores" ]));
			Assert.AreEqual(1, ListOf(form).Items.Count);

			form.Controls.OfType<Button>().Single(static b => b.Text == "Rescan").PerformClick();

			Assert.AreEqual(1, rescanned);
			Assert.AreEqual(2, ListOf(form).Items.Count, "a package that appeared during the rescan must show up");
		}

		[TestMethod]
		public void SelectionSurvivesARepopulate()
		{
			Config config = new();
			using var form = MakeForm(config, [ Pkg("A", "/cores/a.zip", sha1: "aa"), Pkg("B", "/cores/b.zip", sha1: "bb") ]);
			var list = ListOf(form);
			list.Items[1].Selected = true;
			Assert.AreEqual("B", form.SelectedEntry.Name);

			list.Items[1].Checked = false; // triggers a repopulate

			Assert.AreEqual("B", form.SelectedEntry?.Name, "losing the selection on every click would make the detail line useless");
		}

		[TestMethod]
		public void TheDetailLineShowsThePathAndTheExtensionsItClaims()
		{
			Config config = new();
			DiscoveredCorePackage pkg = new()
			{
				Name = "quickerNES",
				Path = "/cores/q.zip",
				Sha1 = "aa",
				Systems = [ "NES" ],
				Extensions = new Dictionary<string, string> { [".nes"] = "NES", [".fds"] = "NES" },
			};
			using var form = MakeForm(config, [ pkg ]);
			ListOf(form).Items[0].Selected = true;

			var detail = form.Controls.OfType<Label>().Single(static l => l.Text.Contains("/cores/q.zip"));
			StringAssert.Contains(detail.Text, ".fds");
			StringAssert.Contains(detail.Text, ".nes");
		}

		[TestMethod]
		public void AnEmptyCoresFolderShowsAnEmptyListRatherThanFailing()
		{
			Config config = new();
			using var form = MakeForm(config, [ ]);
			Assert.AreEqual(0, ListOf(form).Items.Count);
			Assert.AreEqual(0, form.Entries.Count);
			Assert.IsNull(form.SelectedEntry);
		}
	}
}
