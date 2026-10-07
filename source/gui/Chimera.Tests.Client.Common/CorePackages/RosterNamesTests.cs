#nullable enable

using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;

using Chimera.Client.Common;
using Chimera.Emulation.Common.Waterbox;

namespace Chimera.Tests.Client.Common.CorePackages
{
	/// <summary>
	/// Every system a roster core runs reads as a name, not an id (#172): the
	/// Core Manager once listed ares as "WS, WSC, ZXS, MYV, CV ...".
	///
	/// The names used to come from a table of this frontend's, keyed by system.
	/// There is no such table now: a system is called what the core that runs it
	/// calls it, and the roster row carries the core's word, because the row is
	/// the only thing a core nobody has installed yet can speak through. What is
	/// checked here is the roster itself, and that it still says what the
	/// packages say.
	/// </summary>
	[TestClass]
	public class RosterNamesTests
	{
		private static IReadOnlyList<RosterCore> Roster()
		{
			var dir = new DirectoryInfo(AppDomain.CurrentDomain.BaseDirectory);
			while (dir is not null && !File.Exists(Path.Combine(dir.FullName, CoreRoster.FileName))) dir = dir.Parent;
			if (dir is null) Assert.Inconclusive($"{CoreRoster.FileName} is not above the test binaries");
			return CoreRoster.Parse(File.ReadAllText(Path.Combine(dir!.FullName, CoreRoster.FileName)));
		}

		[TestMethod]
		public void EveryRosterSystemHasAName()
		{
			var unnamed = Roster()
				.SelectMany(static core => core.SystemList.Select(system => (Core: core.Id, System: system)))
				.Where(static x => string.IsNullOrWhiteSpace(x.System.Name) || string.IsNullOrWhiteSpace(x.System.Id))
				.Select(static x => $"{x.Core}: {x.System.Id}")
				.ToList();
			Assert.AreEqual(0, unnamed.Count, $"no name for: {string.Join(", ", unnamed)}");
		}

		/// <summary>
		/// Two cores that run the same system say the same name for it. Nothing
		/// forces them to - each core speaks for itself - but the roster is one
		/// list read by one person, and "Nintendo Entertainment System" beside
		/// "Famicom / NES" for the same id reads as two machines.
		/// </summary>
		[TestMethod]
		public void OneSystemHasOneNameAcrossTheRoster()
		{
			var split = Roster()
				.SelectMany(static core => core.SystemList.Select(system => (Core: core.Id, system.Id, system.Name)))
				.GroupBy(static x => x.Id)
				.Where(static g => g.Select(static x => x.Name).Distinct().Count() > 1)
				.Select(static g => $"{g.Key}: {string.Join(" / ", g.Select(static x => $"\"{x.Name}\" ({x.Core})"))}")
				.ToList();
			Assert.AreEqual(0, split.Count, string.Join("; ", split));
		}

		/// <summary>
		/// A roster row's names are a copy of what the core's package says, made
		/// when the core joined. Where a package of that core is here to ask
		/// (build/Cores; in CI, every core's newest), the copy must still agree:
		/// a row that drifts names a machine the core no longer calls that.
		/// </summary>
		[TestMethod]
		public void TheRosterSaysWhatThePackagesSay()
		{
			if (InstalledPackages.Files.Count is 0) Assert.Inconclusive("no core packages in build/Cores (see tools/fetch-cores.sh)");
			var roster = Roster();
			List<string> drift = new();
			var asked = 0;
			foreach (var package in InstalledPackages.Files)
			{
				var text = InstalledPackages.ConfigOf(package);
				var cfg = text is null ? null : WaterboxConfig.FromJson(text);
				// a package from before cores named their systems has nothing to compare
				if (cfg?.SystemNames is not { Count: > 0 } declared) continue;
				var row = roster.FirstOrDefault(c => string.Equals(c.Name, cfg.CoreName, StringComparison.Ordinal));
				if (row is null) continue;
				foreach (var system in row.SystemList)
				{
					if (!declared.TryGetValue(system.Id, out var said)) continue;
					asked++;
					if (said != system.Name) drift.Add($"{row.Id} {system.Id}: the roster says \"{system.Name}\", {InstalledPackages.NameOf(package)} says \"{said}\"");
				}
			}
			if (asked is 0) Assert.Inconclusive("no installed package names its systems yet");
			Assert.AreEqual(0, drift.Count, string.Join("; ", drift));
		}

		/// <summary>A row that gave only an id is still shown, as that id.</summary>
		[TestMethod]
		public void AnIdNobodyNamedStillReads()
		{
			RosterCore row = new() { Id = "x", Name = "X", Repo = "someone/x", Systems = [ "XYZ9" ] };
			CoreManagerRow shown = new() { Core = row };
			Assert.AreEqual("XYZ9", shown.SystemNameOf("XYZ9"));
			Assert.AreEqual("XYZ9", shown.SystemsSpelled);
		}
	}
}
