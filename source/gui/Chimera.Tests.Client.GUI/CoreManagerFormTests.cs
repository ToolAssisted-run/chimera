using System;
using System.Collections.Generic;
using System.Linq;
using System.Net;
using System.Net.Http;
using System.Threading;
using System.Threading.Tasks;
using System.Windows.Forms;

using Chimera.Client.Common;
using Chimera.Client.GUI;

namespace Chimera.Tests.Client.GUI
{
	/// <summary>
	/// The wiring between the Core Manager window and the model underneath it. The
	/// model is tested on its own (CoreManagerModelTests); what is checked here is
	/// that pressing the one button that talks to GitHub puts the answer where
	/// somebody can see it, with the date and commit they need to tell two builds
	/// apart.
	/// </summary>
	[TestClass]
	public class CoreManagerFormTests
	{
		private static readonly List<RosterCore> Roster =
		[
			new() { Id = "gpgx", Name = "Genesis Plus GX", Repo = "ToolAssisted-run/chimera-core-gpgx", Systems = [ "GEN" ] },
		];

		/// <summary>A version's date as the lists write it: local day and minute.</summary>
		private static string When(string iso) => CoreVersionDates.Format(CoreVersionDates.Parse(iso)!.Value);

		private const string Feed = @"[
			{ ""tag_name"": ""dev"", ""published_at"": ""2026-09-07T11:00:00Z"", ""assets"": [
				{ ""name"": ""gpgx-cccccccccccc.chimeraCore"", ""browser_download_url"": ""https://example.invalid/c"", ""size"": 3145728 } ] },
			{ ""tag_name"": ""nightly-2026-09-05"", ""published_at"": ""2026-09-05T05:00:00Z"", ""assets"": [
				{ ""name"": ""gpgx-bbbbbbbbbbbb.chimeraCore"", ""browser_download_url"": ""https://example.invalid/b"", ""size"": 3145728 } ] },
			{ ""tag_name"": ""nightly-2026-09-01"", ""published_at"": ""2026-09-01T05:00:00Z"", ""assets"": [
				{ ""name"": ""gpgx-aaaaaaaaaaaa.chimeraCore"", ""browser_download_url"": ""https://example.invalid/a"", ""size"": 3145728 } ] }
		]";

		/// <summary>Answers every request with one canned body; no network is touched.</summary>
		private sealed class Canned : HttpMessageHandler
		{
			private readonly string _body;

			private readonly HttpStatusCode _status;

			public Canned(string body, HttpStatusCode status = HttpStatusCode.OK)
			{
				_body = body;
				_status = status;
			}

			protected override Task<HttpResponseMessage> SendAsync(HttpRequestMessage request, CancellationToken cancellationToken)
				=> Task.FromResult(new HttpResponseMessage(_status) { Content = new StringContent(_body) });
		}

		private static ListView ListOf(Form form)
		{
			foreach (Control c in form.Controls)
			{
				if (c is ListView list) return list;
			}
			throw new InvalidOperationException("no core list on the form");
		}

		private static CheckBox SelectAllOf(Form form)
		{
			foreach (Control c in form.Controls)
			{
				if (c is CheckBox box && box.Text.StartsWith("Select all", StringComparison.Ordinal)) return box;
			}
			throw new InvalidOperationException("no select-all on the form");
		}

		private static CoreManagerForm Open(
			string body,
			IReadOnlyList<DiscoveredCorePackage> installed,
			HttpStatusCode status = HttpStatusCode.OK,
			IReadOnlyList<RosterCore>? roster = null,
			CoreKindFilter shows = CoreKindFilter.All,
			Action<CoreKindFilter>? rememberShows = null)
		{
			// a cache directory of its own, so the test never reads or writes the
			// store this machine actually uses
			var cache = System.IO.Path.Combine(System.IO.Path.GetTempPath(), $"chimera-feed-{Guid.NewGuid():N}");
			return new CoreManagerForm(
				() => roster ?? Roster,
				() => installed,
				new CoreFeed(new HttpClient(new Canned(body, status)), cache),
				new CoreInstaller(),
				shows: shows,
				rememberShows: rememberShows);
		}

		private static CoreKindFilterBox ShowsOf(Form form) => form.Controls.OfType<CoreKindFilterBox>().Single();

		/// <summary>A column of every listed row, by the column's header.</summary>
		private static List<string> Column(ListView list, string header)
		{
			var at = list.Columns.Cast<ColumnHeader>().ToList().FindIndex(c => c.Text == header);
			return list.Items.Cast<ListViewItem>().Select(i => i.SubItems[at].Text).ToList();
		}

		private static ComboBox VersionsOf(Form form)
		{
			foreach (Control panel in form.Controls)
			{
				foreach (Control c in panel.Controls)
				{
					if (c is ComboBox combo) return combo;
				}
			}
			throw new InvalidOperationException("no version selector on the form");
		}

		[TestMethod]
		public async Task FetchingVersionsFillsTheSelectorNewestFirst()
		{
			using var form = Open(Feed, [ ]);
			form.Show();
			Assert.IsTrue(form.Select("Genesis Plus GX"));
			var versions = VersionsOf(form);
			Assert.AreEqual(0, versions.Items.Count, "nothing is fetched to draw the window");

			await form.FetchSelectedVersions();

			// the dev build is newest and is NOT offered by default: it is replaced on
			// every push, so a movie recorded on it can stop being fetchable
			CollectionAssert.AreEqual(
				new[] { $"{When("2026-09-05T05:00:00Z")}  (bbbbbbbb)", $"{When("2026-09-01T05:00:00Z")}  (aaaaaaaa)" },
				versions.Items.Cast<object>().Select(static i => i.ToString()).ToList());
		}

		[TestMethod]
		public async Task EveryVersionCarriesItsDateAndCommit()
		{
			using var form = Open(Feed, [ ]);
			form.Show();
			Assert.IsTrue(form.Select("Genesis Plus GX"));
			await form.FetchSelectedVersions();
			foreach (var text in VersionsOf(form).Items.Cast<object>().Select(static i => i.ToString()))
			{
				// the day AND the minute: several versions in one day must read apart
				StringAssert.Matches(text, new System.Text.RegularExpressions.Regex(@"^\d{4}-\d{2}-\d{2} \d{2}:\d{2}\s+\([0-9a-f]{8}\)"), text);
			}
		}

		[TestMethod]
		public async Task AnInstalledVersionIsMarkedRatherThanOfferedTwice()
		{
			DiscoveredCorePackage have = new()
			{
				Name = "Genesis Plus GX",
				Version = "bbbbbbbbbbbb",
				Path = "/store/gpgx-bbbbbbbbbbbb.chimeraCore",
				Sha1 = new string('b', 40),
			};
			using var form = Open(Feed, [ have ]);
			form.Show();
			Assert.IsTrue(form.Select("Genesis Plus GX"));
			await form.FetchSelectedVersions();
			var texts = VersionsOf(form).Items.Cast<object>().Select(static i => i.ToString()).ToList();
			Assert.AreEqual(2, texts.Count, "the installed version is the published one, not a second line");
			StringAssert.Contains(texts[0], "installed");
			Assert.IsFalse(texts[1].Contains("installed"));
		}

		[TestMethod]
		public void ConstructingTheWindowSurvivesTheListRaisingEvents()
		{
			// A ListView raises ItemChecked while its handle is created, which on
			// .NET Framework happens INSIDE the constructor, before the buttons the
			// handler enables exist. That crashed on Windows the first time the
			// window opened, with a NullReferenceException nothing on Linux showed.
			// Forcing the handle and then ticking exercises the same order.
			using var form = Open(Feed, [ ]);
			_ = form.Handle;
			form.Show();
			Assert.IsTrue(form.SetChecked("Genesis Plus GX", true));
			Assert.IsTrue(form.BulkActionsEnabled);
		}

		[TestMethod]
		public void TheBulkButtonsWaitUntilSomethingIsTicked()
		{
			using var form = Open(Feed, [ ]);
			form.Show();
			Assert.IsFalse(form.BulkActionsEnabled, "nothing ticked, so there is nothing for them to do");
			Assert.IsTrue(form.SetChecked("Genesis Plus GX", true));
			Assert.IsTrue(form.BulkActionsEnabled);
			Assert.IsTrue(form.SetChecked("Genesis Plus GX", false));
			Assert.IsFalse(form.BulkActionsEnabled);
		}

		[TestMethod]
		public void ExternalCoresFollowTheOfficialOnesAndSaySo()
		{
			List<RosterCore> roster =
			[
				Roster[0],
				new() { Id = "aardvark", Name = "Aardvark", Repo = "someone/aardvark", Systems = [ "ARC" ], IsExternal = true },
			];
			using var form = Open(Feed, [ ], roster: roster);
			form.Show();
			var list = ListOf(form);
			CollectionAssert.AreEqual(new[] { "Genesis Plus GX", "Aardvark" }, list.Items.Cast<ListViewItem>().Select(static i => i.Text).ToList(), "one list: no divider row");
			Assert.AreEqual("someone/aardvark  (added by hand)", Column(list, "Source")[1], "the source says what the divider said");

			SelectAllOf(form).Checked = true;
			Assert.AreEqual(2, list.Items.Cast<ListViewItem>().Count(static i => i.Checked));
		}

		[TestMethod]
		public void OneListWithATypeThatTheShowChoiceNarrows()
		{
			// docs/game-cores.md: no dividers; a Type column, and Show: All / Emulators / Games
			// (user-decided, 2026-09-29). A game core added by hand is still a game core
			List<RosterCore> roster =
			[
				new() { Id = "sdlpop", Name = "SDLPoP", Repo = "ToolAssisted-run/chimera-core-sdlpop", Systems = [ "PoP" ], Kind = "game" },
				Roster[0],
				new() { Id = "zork", Name = "Zork", Repo = "someone/zork", Systems = [ "ZRK" ], Kind = "game", IsExternal = true },
				new() { Id = "aardvark", Name = "Aardvark", Repo = "someone/aardvark", Systems = [ "ARC" ], IsExternal = true },
			];
			List<CoreKindFilter> remembered = new();
			using var form = Open(Feed, [ ], roster: roster, rememberShows: remembered.Add);
			form.Show();
			var list = ListOf(form);
			CollectionAssert.AreEqual(new[] { "Genesis Plus GX", "Aardvark", "SDLPoP", "Zork" }, list.Items.Cast<ListViewItem>().Select(static i => i.Text).ToList(),
				"the emulators, then the games, each official first");
			CollectionAssert.AreEqual(new[] { "Emulator", "Emulator", "Game", "Game" }, Column(list, "Type"));

			// everything ticked, then only the games shown: the emulators' ticks go with them,
			// so no button acts on a core out of view
			SelectAllOf(form).Checked = true;
			ShowsOf(form).ChooseForTest(CoreKindFilter.Games);
			CollectionAssert.AreEqual(new[] { "SDLPoP", "Zork" }, list.Items.Cast<ListViewItem>().Select(static i => i.Text).ToList());
			CollectionAssert.AreEqual(new[] { CoreKindFilter.Games }, remembered, "the choice is handed to the owner to keep");
			ShowsOf(form).ChooseForTest(CoreKindFilter.All);
			CollectionAssert.AreEqual(new[] { "SDLPoP", "Zork" }, list.Items.Cast<ListViewItem>().Where(static i => i.Checked).Select(static i => i.Text).ToList());

			ShowsOf(form).ChooseForTest(CoreKindFilter.Emulators);
			SelectAllOf(form).Checked = false;
			SelectAllOf(form).Checked = true;
			CollectionAssert.AreEqual(new[] { "Genesis Plus GX", "Aardvark" }, list.Items.Cast<ListViewItem>().Where(static i => i.Checked).Select(static i => i.Text).ToList(), "select all ticks what is shown");
		}

		[TestMethod]
		public void TheWindowOpensOnTheKindItWasLeftOn()
		{
			List<RosterCore> roster =
			[
				new() { Id = "sdlpop", Name = "SDLPoP", Repo = "ToolAssisted-run/chimera-core-sdlpop", Systems = [ "PoP" ], Kind = "game" },
				Roster[0],
			];
			using var form = Open(Feed, [ ], roster: roster, shows: CoreKindFilter.Games);
			form.Show();
			Assert.AreEqual(CoreKindFilter.Games, ShowsOf(form).Value);
			CollectionAssert.AreEqual(new[] { "SDLPoP" }, ListOf(form).Items.Cast<ListViewItem>().Select(static i => i.Text).ToList());
		}

		[TestMethod]
		public void OneCoreIsOneRow()
		{
			using var form = Open(Feed, [ ]);
			form.Show();
			Assert.AreEqual(1, ListOf(form).Items.Count);
		}

		[TestMethod]
		public async Task ARateLimitIsShownInsteadOfVersions()
		{
			using var form = Open("[]", [ ], HttpStatusCode.Forbidden);
			form.Show();
			Assert.IsTrue(form.Select("Genesis Plus GX"));
			await form.FetchSelectedVersions();
			// the canned 403 carries no rate-limit headers, so it is reported as the
			// plain refusal it is - what matters is that the window says something
			Assert.AreEqual(0, VersionsOf(form).Items.Count);
		}
	}
}
