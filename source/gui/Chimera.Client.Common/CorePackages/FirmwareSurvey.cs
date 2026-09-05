#nullable enable

using System.Collections.Generic;
using System.IO;
using System.Linq;

using Newtonsoft.Json.Linq;

using Chimera.Emulation.Common;
using Chimera.Emulation.Common.Engine;
using Chimera.Emulation.Common.Waterbox;

namespace Chimera.Client.Common
{
	/// <summary>Where the file answering a survey row was found.</summary>
	public enum FirmwareWhere
	{
		Nowhere,
		/// <summary>In the Firmware folder, under any name.</summary>
		FirmwareFolder,
		/// <summary>Where a person pointed, in this window or in an earlier project.</summary>
		Chosen,
	}

	/// <summary>One declaration of one installed core, and what the person has for it.</summary>
	public sealed class FirmwareSurveyRow
	{
		public string CoreName { get; init; } = "";
		public CoreFirmwareDecl Decl { get; init; } = new();
		public string? Path { get; init; }
		public string? Sha1 { get; init; }
		public CoreFirmwareState State { get; init; }
		public FirmwareWhere Where { get; init; }

		/// <summary>"bios = ps2-0220e-20060210", "with a file in cd", or "" when unconditional.</summary>
		public string Condition { get; init; } = "";

		public bool OnHand => State is CoreFirmwareState.Good or CoreFirmwareState.Unrecognised or CoreFirmwareState.Custom;

		public string StatusText => State switch
		{
			CoreFirmwareState.Good => string.IsNullOrEmpty(Decl.Sha1) ? "found by name" : "found, hash matches",
			CoreFirmwareState.Unrecognised => "right size, a dump the core has never seen - used anyway",
			CoreFirmwareState.Custom => $"not the {Decl.Size} bytes the core expects - used anyway",
			CoreFirmwareState.Unreadable => "chosen file is gone",
			_ => "not found",
		};

		public string WhereText => Where switch
		{
			FirmwareWhere.FirmwareFolder => "Firmware folder",
			FirmwareWhere.Chosen => Path is null ? "chosen" : $"chosen: {Path}",
			_ => "",
		};
	}

	/// <summary>Every declaration of one installed core, with the verdict for the core as a whole.</summary>
	public sealed class FirmwareSurveyGroup
	{
		public string CoreName { get; init; } = "";
		public string PackagePath { get; init; } = "";
		public IReadOnlyList<FirmwareSurveyRow> Rows { get; init; } = [ ];

		/// <summary>
		/// The line beside the core's name. For an id declared many times - many
		/// releases of one bios - any one dump on hand means a project that
		/// chooses that release opens without asking, and the line counts them.
		/// </summary>
		public string Summary
		{
			get
			{
				if (Rows.Count is 0) return "needs no firmware";
				var byId = Rows.GroupBy(static r => r.Decl.Id).ToList();
				var onHand = Rows.Count(static r => r.OnHand);
				var unpinned = Rows.All(static r => string.IsNullOrEmpty(r.Decl.Sha1));
				if (byId.Count is 1 && Rows.Count > 1)
				{
					return $"{Rows[0].Decl.DisplayName} · {onHand} of {Rows.Count} dumps on hand · any one satisfies a project that chooses it";
				}
				var idsSatisfied = byId.Count(g => g.Any(static r => r.OnHand));
				var files = byId.Count is 1 ? "1 file" : $"{byId.Count} files";
				var found = byId.Count is 1 ? (idsSatisfied is 1 ? "on hand" : "not on hand") : $"{idsSatisfied} on hand";
				var how = unpinned ? " · not pinned by hash: recognised by the name the core gives them" : "";
				return $"{files} · {found}{how}";
			}
		}

		public bool Complete => Rows.GroupBy(static r => r.Decl.Id).All(g => g.Any(static r => r.OnHand));
	}

	/// <summary>
	/// Asks every installed core what firmware it declares, and says what the person
	/// has for each - the Config > Firmware window's contents.
	///
	/// Just in time, and nothing kept: the rows are built from the packages present
	/// when asked and go away with the window. The frontend has no list of firmware
	/// of its own; it cannot, since it does not know what cores exist. Delete a core
	/// package and its rows are gone on the next survey. What the person chose for
	/// it stays in the config, keyed by the core's name, for the day it is put back.
	/// </summary>
	public static class FirmwareSurvey
	{
		/// <summary>What one package declares, with the raw entries the conditions live in. Empty for a package that cannot be read.</summary>
		public static (IReadOnlyList<CoreFirmwareDecl> Decls, JArray Raw) DeclarationsOf(string packagePath)
		{
			try
			{
				using var package = EnginePackage.Open(packagePath);
				var json = package?.EntryText(WaterboxCoreFactory.ConfigFileName);
				if (string.IsNullOrWhiteSpace(json)) return ([ ], new JArray());
				var cfg = WaterboxConfig.FromJson(json!);
				if (cfg is null) return ([ ], new JArray());
				JArray raw;
				try
				{
					raw = JArray.Parse(cfg.RawFirmwareJson);
				}
				catch (Newtonsoft.Json.JsonException)
				{
					raw = new JArray();
				}
				return (cfg.Firmware ?? [ ], raw);
			}
			catch (Exception ex) when (ex is IOException or InvalidOperationException or UnauthorizedAccessException)
			{
				return ([ ], new JArray());
			}
		}

		/// <summary>
		/// The index every row is answered from: the Firmware folder plus every path
		/// remembered for the packages present, hashed once. Built by the caller so
		/// a rescan is a rebuild and nothing else.
		/// </summary>
		public static IReadOnlyList<FirmwareLocator.IndexedFile> BuildIndex(Config config, string? firmwareFolder, IEnumerable<string> coreNames)
			=> FirmwareLocator.BuildIndex(
				string.IsNullOrEmpty(firmwareFolder) ? [ ] : [ firmwareFolder! ],
				coreNames.SelectMany(core => CoreFirmwareStore.RememberedPaths(config, core)));

		/// <param name="config">where chosen paths are remembered, for these packages and for any that are not installed today</param>
		/// <param name="packages">the packages present right now</param>
		/// <param name="declarationsOf">what a package declares - the real reader, or a test's</param>
		/// <param name="firmwareFolder">the Firmware folder, for saying "found there" rather than "chosen"</param>
		/// <param name="index">the folder and every remembered path, hashed once (<see cref="BuildIndex"/>)</param>
		public static IReadOnlyList<FirmwareSurveyGroup> Build(
			Config config,
			IReadOnlyList<DiscoveredCorePackage> packages,
			Func<DiscoveredCorePackage, (IReadOnlyList<CoreFirmwareDecl> Decls, JArray Raw)> declarationsOf,
			string? firmwareFolder,
			IReadOnlyList<FirmwareLocator.IndexedFile> index)
		{
			var present = packages.Where(static p => p.Error is null).ToList();

			List<FirmwareSurveyGroup> groups = new();
			foreach (var package in present.OrderBy(static p => p.Name, StringComparer.OrdinalIgnoreCase))
			{
				var (decls, raw) = declarationsOf(package);
				List<FirmwareSurveyRow> rows = new();
				for (var i = 0; i < decls.Count; i++)
				{
					var decl = decls[i];
					var entry = CoreFirmwareStore.Describe(config, package.Name, decl, index);
					rows.Add(new FirmwareSurveyRow
					{
						CoreName = package.Name,
						Decl = decl,
						Path = entry.Path,
						Sha1 = entry.Sha1,
						State = entry.State,
						Where = entry.Path is null ? FirmwareWhere.Nowhere
							: IsUnder(firmwareFolder, entry.Path) ? FirmwareWhere.FirmwareFolder
							: FirmwareWhere.Chosen,
						Condition = i < raw.Count && raw[i] is JObject o && o["requiredWhen"] is { } when ? ConditionText(when) : "",
					});
				}
				groups.Add(new FirmwareSurveyGroup { CoreName = package.Name, PackagePath = package.Path, Rows = rows });
			}
			return groups;
		}

		private static bool IsUnder(string? folder, string path)
		{
			if (string.IsNullOrEmpty(folder)) return false;
			try
			{
				var dir = System.IO.Path.GetFullPath(folder!).TrimEnd(System.IO.Path.DirectorySeparatorChar, System.IO.Path.AltDirectorySeparatorChar);
				var file = System.IO.Path.GetFullPath(path);
				return string.Equals(System.IO.Path.GetDirectoryName(file), dir, StringComparison.OrdinalIgnoreCase);
			}
			catch (Exception ex) when (ex is IOException or ArgumentException)
			{
				return false;
			}
		}

		/// <summary>
		/// The condition language of docs/project.md, said in words: the survey has
		/// no project to evaluate it against, so it shows what the wizard will ask.
		/// </summary>
		public static string ConditionText(JToken when)
		{
			if (when is not JObject o) return "";
			if (o["setting"] is { } setting)
			{
				if (o["is"] is { } value) return $"{setting} = {Literal(value)}";
				if (o["in"] is JArray options) return $"{setting} in {string.Join(", ", options.Select(Literal))}";
				return setting.ToString();
			}
			if (o["slot"] is { } slot)
			{
				return o["extension"] is { } ext ? $"a .{ext} file in {slot}" : $"a file in {slot}";
			}
			if (o["all"] is JArray all) return string.Join(" and ", all.Select(ConditionText).Where(static s => s.Length is not 0).Select(Group));
			if (o["any"] is JArray any) return string.Join(" or ", any.Select(ConditionText).Where(static s => s.Length is not 0).Select(Group));
			if (o["not"] is { } not) return $"not {Group(ConditionText(not))}";
			return "";
		}

		private static string Group(string s) => s.Contains(" and ") || s.Contains(" or ") ? $"({s})" : s;

		private static string Literal(JToken token)
			=> token.Type is JTokenType.String ? token.ToString() : token.ToString(Newtonsoft.Json.Formatting.None);
	}
}
