using System.Collections.Generic;
using System.Linq;
using System.Windows.Forms;

using BizHawk.Client.Common;
using BizHawk.Client.EmuHawk;

namespace BizHawk.Tests.Client.EmuHawk
{
	/// <summary>
	/// Drives the Open Core window without a person: build it over a known list,
	/// press its buttons, and check that what it shows - and what it opens - matches.
	///
	/// What this can prove is behaviour - rows, contents, the effect of a click.
	/// What it cannot prove is that the window looks right; that still needs eyes
	/// on a screenshot. Keeping the two apart is why the rows are computed by
	/// <see cref="CorePackageList"/> and this class only checks the wiring.
	/// </summary>
	[TestClass]
	public class OpenCoreFormTests
	{
		private static DiscoveredCorePackage Pkg(string name, string path, string sha1 = null, string error = null, string version = "")
			=> new() { Name = name, Path = path, Sha1 = sha1, Error = error, Systems = [ "NES" ], Version = version };

		/// <summary>The version a package declares is what a movie will cite, so the window shows it.</summary>
		[TestMethod]
		public void TheVersionAPackageDeclaresIsShown()
		{
			using var form = MakeForm(
			[
				Pkg("published", "/cores/p.zip", sha1: "aa", version: "9f3c1d2e4b5a"),
				Pkg("home-built", "/cores/h.zip", sha1: "bb", version: "9f3c1d2e4b5a-dirty+local"),
			]);
			Assert.AreEqual("home-built", ListOf(form).Items[0].Text);
			Assert.AreEqual("9f3c1d2e4b5a-dirty+local", ListOf(form).Items[0].SubItems[2].Text);
			Assert.AreEqual("9f3c1d2e4b5a", ListOf(form).Items[1].SubItems[2].Text);
		}

		private static ListView ListOf(Form form)
			=> form.Controls.OfType<ListView>().Single();

		/// <summary>
		/// Shows the form so its controls have real window handles: without them,
		/// selection state is not tracked and half of what this class checks would
		/// silently do nothing. Xvfb gives us a display to show it on.
		/// </summary>
		private static OpenCoreForm Realise(OpenCoreForm form)
		{
			form.Show();
			return form;
		}

		/// <summary>a form over a fixed set of packages, none of them loaded</summary>
		private static OpenCoreForm MakeForm(
			IReadOnlyList<DiscoveredCorePackage> discovered,
			IReadOnlyList<CoreRegistry.LoadedCorePackage> loaded = null,
			Action rescan = null,
			Action addPackage = null,
			Func<DiscoveredCorePackage, bool> open = null)
			=> Realise(new OpenCoreForm(
				() => CorePackageList.Build(discovered, loaded ?? [ ], [ ]),
				rescan ?? (() => { }),
				addPackage ?? (() => { }),
				[ "/tmp/Cores" ],
				open ?? (_ => true)));

		[TestMethod]
		public void RowsShowEveryPackageWithItsStatus()
		{
			var loadedPkg = Pkg("quickerNES", "/cores/q.zip", sha1: "0123456789abcdef0123456789abcdef01234567");
			using var form = MakeForm(
				[ loadedPkg, Pkg("Broken", "/cores/b.zip", sha1: "bb", error: "waterbox.config is empty or invalid") ],
				loaded: [ new CoreRegistry.LoadedCorePackage { Name = "quickerNES", Path = "/cores/q.zip", Sha1 = "0123456789abcdef0123456789abcdef01234567" } ]);

			var list = ListOf(form);
			Assert.AreEqual(2, list.Items.Count);
			var broken = list.Items[0];
			var quicker = list.Items[1];
			Assert.AreEqual("Broken", broken.Text);
			Assert.AreEqual("quickerNES", quicker.Text);
			Assert.AreEqual("NES", quicker.SubItems[1].Text);
			Assert.AreEqual("loaded", quicker.SubItems[3].Text);
			Assert.AreEqual("q.zip", quicker.SubItems[4].Text);
			Assert.AreEqual("01234567", quicker.SubItems[5].Text, "the SHA1 column shows the identity prefix");
			StringAssert.Contains(broken.SubItems[3].Text, "waterbox.config");
		}

		[TestMethod]
		public void ADirectoryPackageIsMarkedAsAFolder()
		{
			using var form = MakeForm([ Pkg("DevBuild", "/home/you/quickerNES/waterbox/bin") ]);
			StringAssert.Contains(ListOf(form).Items[0].SubItems[4].Text, "(folder)");
			Assert.AreEqual("-", ListOf(form).Items[0].SubItems[5].Text, "a directory has no hash to show");
		}

		/// <summary>Opening is what the window is for: the chosen package goes to the owner, and the window closes.</summary>
		[TestMethod]
		public void OpenLoadsTheSelectedPackageAndCloses()
		{
			List<DiscoveredCorePackage> opened = new();
			using var form = MakeForm(
				[ Pkg("quickerNES", "/cores/q.zip", sha1: "aa"), Pkg("synth", "/cores/s.zip", sha1: "bb") ],
				open: pkg => { opened.Add(pkg); return true; });

			ListOf(form).Items[1].Selected = true; // rows are alphabetical: quickerNES, synth
			form.Controls.OfType<Button>().Single(static b => b.Text is "Open").PerformClick();

			Assert.AreEqual(1, opened.Count);
			Assert.AreEqual("/cores/s.zip", opened[0].Path);
			Assert.AreEqual(DialogResult.OK, form.DialogResult);
		}

		/// <summary>A package that will not load leaves the window up, showing why.</summary>
		[TestMethod]
		public void APackageThatWillNotOpenKeepsTheWindowOpen()
		{
			using var form = MakeForm([ Pkg("quickerNES", "/cores/q.zip", sha1: "aa") ], open: _ => false);
			ListOf(form).Items[0].Selected = true;
			form.Controls.OfType<Button>().Single(static b => b.Text is "Open").PerformClick();
			Assert.AreNotEqual(DialogResult.OK, form.DialogResult);
		}

		/// <summary>There is nothing to open for a package that could not even be read.</summary>
		[TestMethod]
		public void ABrokenPackageCannotBeOpened()
		{
			var opened = 0;
			using var form = MakeForm(
				[ Pkg("Broken", "/cores/b.zip", sha1: "bb", error: "waterbox.config is empty or invalid") ],
				open: _ => { opened++; return true; });
			ListOf(form).Items[0].Selected = true;
			var button = form.Controls.OfType<Button>().Single(static b => b.Text is "Open");
			Assert.IsFalse(button.Enabled);
			button.PerformClick();
			Assert.AreEqual(0, opened);
		}

		/// <summary>A package already in the session says so rather than offering to load it twice.</summary>
		[TestMethod]
		public void AnAlreadyLoadedPackageSaysSo()
		{
			var pkg = Pkg("quickerNES", "/cores/q.zip", sha1: "aa");
			using var form = MakeForm(
				[ pkg ],
				loaded: [ new CoreRegistry.LoadedCorePackage { Name = "quickerNES", Path = "/cores/q.zip", Sha1 = "aa" } ]);
			ListOf(form).Items[0].Selected = true;
			Assert.AreEqual("Loaded", form.Controls.OfType<Button>().Single(static b => b.Text is "Loaded").Text);
		}

		[TestMethod]
		public void RescanRefreshesTheRows()
		{
			List<DiscoveredCorePackage> discovered = [ Pkg("First", "/cores/1.zip", sha1: "11") ];
			var rescanned = 0;
			using var form = Realise(new OpenCoreForm(
				() => CorePackageList.Build(discovered, [ ], [ ]),
				() => { rescanned++; discovered.Add(Pkg("Second", "/cores/2.zip", sha1: "22")); },
				() => { },
				[ "/tmp/Cores" ],
				_ => true));
			Assert.AreEqual(1, ListOf(form).Items.Count);

			form.Controls.OfType<Button>().Single(static b => b.Text == "Rescan").PerformClick();

			Assert.AreEqual(1, rescanned);
			Assert.AreEqual(2, ListOf(form).Items.Count, "a package that appeared during the rescan must show up");
		}

		[TestMethod]
		public void AddPackageRefreshesTheRowsToo()
		{
			List<DiscoveredCorePackage> discovered = [ ];
			using var form = Realise(new OpenCoreForm(
				() => CorePackageList.Build(discovered, [ ], [ ]),
				() => { },
				() => discovered.Add(Pkg("Chosen", "/elsewhere/c.zip", sha1: "cc")),
				[ "/tmp/Cores" ],
				_ => true));

			form.Controls.OfType<Button>().Single(static b => b.Text == "Add Package...").PerformClick();

			Assert.AreEqual(1, ListOf(form).Items.Count);
			Assert.AreEqual("Chosen", ListOf(form).Items[0].Text);
		}

		[TestMethod]
		public void SelectingARowShowsItsPathAndTheExtensionsItClaims()
		{
			DiscoveredCorePackage pkg = new()
			{
				Name = "quickerNES",
				Path = "/cores/q.zip",
				Sha1 = "aa",
				Systems = [ "NES" ],
				Extensions = new Dictionary<string, string> { [".nes"] = "NES", [".fds"] = "NES" },
			};
			using var form = MakeForm([ pkg ]);
			ListOf(form).Items[0].Selected = true;

			var detail = form.Controls.OfType<Label>().Single(static l => l.Text.Contains("/cores/q.zip"));
			StringAssert.Contains(detail.Text, ".fds");
			StringAssert.Contains(detail.Text, ".nes");
		}

		[TestMethod]
		public void SelectingABrokenRowShowsTheWholeReasonNotTheTruncatedColumn()
		{
			const string reason = "package \"quickerNES\" declares native libquickernes.so but it is missing";
			using var form = MakeForm([ Pkg("quickerNES", "/cores/q.zip", sha1: "aa", error: reason) ]);
			ListOf(form).Items[0].Selected = true;

			Assert.IsTrue(
				form.Controls.OfType<Label>().Any(l => l.Text.Contains(reason)),
				"the Status column clips a long message, so the detail line has to carry it");
		}

		[TestMethod]
		public void SelectionSurvivesARepopulate()
		{
			using var form = MakeForm([ Pkg("A", "/cores/a.zip", sha1: "aa"), Pkg("B", "/cores/b.zip", sha1: "bb") ]);
			ListOf(form).Items[1].Selected = true;
			Assert.AreEqual("B", form.SelectedEntry.Name);

			form.Populate();

			Assert.AreEqual("B", form.SelectedEntry?.Name, "losing the selection on a refresh would clear the detail line under the user");
		}

		[TestMethod]
		public void AnEmptyCoresFolderShowsAnEmptyListRatherThanFailing()
		{
			using var form = MakeForm([ ]);
			Assert.AreEqual(0, ListOf(form).Items.Count);
			Assert.AreEqual(0, form.Entries.Count);
			Assert.IsNull(form.SelectedEntry);
		}
	}
}
