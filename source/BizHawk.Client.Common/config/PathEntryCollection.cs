using System.Collections.Generic;
using System.IO;
using System.Linq;

using BizHawk.Common.CollectionExtensions;
using BizHawk.Common.PathExtensions;
using BizHawk.Emulation.Common;

using Newtonsoft.Json;

namespace BizHawk.Client.Common
{
	public class PathEntryCollection
	{
		public static readonly string GLOBAL = string.Join("_", "Global", VSystemID.Raw.NULL);

		private static PathEntry BaseEntryFor(string sysID, string path)
			=> new(sysID, "Base", path);

		private static PathEntry CheatsEntryFor(string sysID)
			=> new(sysID, "Cheats", Path.Combine(".", "Cheats"));

		private static IEnumerable<PathEntry> CommonEntriesFor(string sysID, string basePath, bool omitSaveRAM = false)
			=> [
				BaseEntryFor(sysID, basePath),
				ROMEntryFor(sysID),
				SavestatesEntryFor(sysID),
				..(omitSaveRAM ? [ ] : new[] { SaveRAMEntryFor(sysID) }),
				ScreenshotsEntryFor(sysID),
				CheatsEntryFor(sysID),
			];

		public static string GetDisplayNameFor(string sysID)
			=> sysID == GLOBAL ? "Global" : sysID; // no per-system display-name table in miniHawk

		public static bool InGroup(string sysID, string group)
			=> sysID == group || group.Split('_').Contains(sysID);

		private static PathEntry PalettesEntryFor(string sysID)
			=> new(sysID, "Palettes", Path.Combine(".", "Palettes"));

		private static PathEntry ROMEntryFor(string sysID, string path = ".")
			=> new(sysID, "ROM", path);

		private static PathEntry SaveRAMEntryFor(string sysID)
			=> new(sysID, "Save RAM", Path.Combine(".", "SaveRAM"));

		private static PathEntry SavestatesEntryFor(string sysID)
			=> new(sysID, "Savestates", Path.Combine(".", "State"));

		private static PathEntry ScreenshotsEntryFor(string sysID)
			=> new(sysID, "Screenshots", Path.Combine(".", "Screenshots"));

		private static PathEntry UserEntryFor(string sysID)
			=> new(sysID, "User", Path.Combine(".", "User"));

		public List<PathEntry> Paths { get; }

		[JsonConstructor]
		public PathEntryCollection(List<PathEntry> paths)
		{
			Paths = paths;
		}

		public PathEntryCollection() : this(new List<PathEntry>(Defaults.Value)) {}

		public bool UseRecentForRoms { get; set; }
		public string LastRomPath { get; set; } = ".";

		public PathEntry this[string system, string type]
			=> Paths.Find(p => p.IsSystem(system) && p.Type == type) ?? TryGetDebugPath(system, type);

		private PathEntry TryGetDebugPath(string system, string type)
		{
			if (Paths.Exists(p => p.IsSystem(system)))
			{
				// we have the system, but not the type.  don't attempt to add an unknown type
				return null;
			}

			// no entries for this system yet: generate the stock set on demand.
			// (miniHawk ships no per-system path table; systems come from core packages)
			Paths.AddRange(CommonEntriesFor(system, basePath: Path.Combine(".", system.RemoveInvalidFileSystemChars())));

			return this[system, type];
		}

		public void ResolveWithDefaults()
		{
			// Add missing GLOBAL entries
			foreach (var defaultPath in Defaults.Value)
			{
				if (!Paths.Exists(p => p.System == defaultPath.System && p.Type == defaultPath.Type)) Paths.Add(defaultPath);
			}

			// Remove GLOBAL-scope entries that no longer exist in defaults.
			// Per-system entries are kept as-is: they're either user customizations or
			// were generated on demand for a core-package system.
			Paths.RemoveAll(pathEntry =>
				pathEntry.System == GLOBAL
				&& !Defaults.Value.Any(p => p.System == pathEntry.System && p.Type == pathEntry.Type));
		}

		[JsonIgnore]
		internal string TempFilesFragment => this[GLOBAL, "Temp Files"].Path;

		public static readonly Lazy<IReadOnlyList<PathEntry>> Defaults = new(() => new[]
		{
			new[] {
				BaseEntryFor(GLOBAL, "."),
				ROMEntryFor(GLOBAL),
				new(GLOBAL, "Movies", Path.Combine(".", "Movies")),
				new(GLOBAL, "Movie backups", Path.Combine(".", "Movies", "backup")),
				new(GLOBAL, "A/V Dumps", "."),
				new(GLOBAL, "Tools", Path.Combine(".", "Tools")),
				new(GLOBAL, "Lua", Path.Combine(".", "Lua")),
				new(GLOBAL, "Watch (.wch)", Path.Combine(".", ".")),
				new(GLOBAL, "Debug Logs", Path.Combine(".", "")),
				new(GLOBAL, "Macros", Path.Combine(".", "Movies", "Macros")),
				new(GLOBAL, "Multi-Disk Bundles", Path.Combine(".", "")),
				new(GLOBAL, "External Tools", Path.Combine(".", "ExternalTools")),
				new(GLOBAL, "Temp Files", ""),
			},
		}.SelectMany(a => a).ToArray());
}
}
