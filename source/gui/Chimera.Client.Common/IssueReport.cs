#nullable enable

using System;
using System.Collections.Generic;
using System.Globalization;
using System.IO;
using System.Linq;
using System.Text;

using Chimera.Common;
using Chimera.Emulation.Common.Waterbox;

namespace Chimera.Client.Common
{
	/// <summary>
	/// What Help &gt; Report an Issue shows (issue #243): the address of the form a
	/// report is written on, the facts that form asks for first, and the files it
	/// asks to have attached. They are gathered here so that they are pasted and
	/// not typed from memory. Nothing is sent anywhere: the text goes to the
	/// clipboard, and the person files the report in their own browser.
	/// </summary>
	public static class IssueReport
	{
		/// <summary>The page that offers the report forms (bug, core request, feature).</summary>
		public static readonly string FormUrl = VersionInfo.HomePage + "/issues/new/choose";

		/// <summary>A file of the open project: its name and the kind of file it was given as.</summary>
		public sealed record ProjectFile(string Slot, string Name);

		/// <summary>A setting whose value is not the core's default.</summary>
		public sealed record ChangedSetting(string Name, string Value, string Default);

		/// <summary>What is known when the window opens. Everything may be missing.</summary>
		public sealed class Facts
		{
			/// <summary>This build, as <c>VersionInfo.GetBuildName</c> names it.</summary>
			public string Build { get; init; } = "";

			/// <summary>The running core, or null when none is.</summary>
			public string? CoreName { get; init; }

			/// <summary>Its version as the Core Manager lists it.</summary>
			public string? CoreVersion { get; init; }

			/// <summary>The operating system, in its own words.</summary>
			public string Os { get; init; } = "";

			/// <summary>What the graphics driver calls itself, or empty when no core drew on a graphics card.</summary>
			public string Gpu { get; init; } = "";

			/// <summary>The open project's files, or null when no project is open.</summary>
			public IReadOnlyList<ProjectFile>? Files { get; init; }

			/// <summary>The running core's settings that are not at their default, or null when no core runs.</summary>
			public IReadOnlyList<ChangedSetting>? Settings { get; init; }
		}

		/// <summary>
		/// The first five lines of the bug report form, filled in. The labels are the
		/// form's own (.github/ISSUE_TEMPLATE/bug_report.md), so the text is pasted
		/// over those lines. What Chimera cannot know is left as a note in the form's
		/// own style, which the finished report does not show.
		/// </summary>
		public static string Text(Facts facts)
		{
			StringBuilder text = new();
			text.Append(VersionReport.Text(facts.Build, facts.CoreName, facts.CoreVersion));
			if (string.IsNullOrWhiteSpace(facts.CoreName)) text.Append("\n**Core and version:** <!-- no core was running -->");

			var os = string.IsNullOrWhiteSpace(facts.Os) ? "<!-- type your operating system here -->" : facts.Os.Trim();
			var gpu = string.IsNullOrWhiteSpace(facts.Gpu)
				? "<!-- type your graphics card here -->"
				: "graphics driver " + facts.Gpu.Trim();
			text.Append("\n**OS and GPU:** ").Append(os).Append(", ").Append(gpu);

			text.Append("\n**Game:**");
			if (facts.Files is null) text.Append(" <!-- no project was open -->");
			else if (facts.Files.Count is 0) text.Append(" the project has no files of its own");
			else foreach (var file in facts.Files) text.Append("\n- ").Append(file.Slot).Append(": ").Append(file.Name);

			text.Append("\n**Renderer and non-default settings:**");
			if (facts.Settings is null) text.Append(" <!-- no core was running -->");
			else if (facts.Settings.Count is 0) text.Append(" every setting is at its default");
			else foreach (var setting in facts.Settings)
			{
				text.Append("\n- ").Append(setting.Name).Append(": ").Append(setting.Value)
					.Append(" (default: ").Append(setting.Default).Append(')');
			}

			return text.ToString();
		}

		/// <summary>
		/// The settings whose value differs from what the core declares as the
		/// default, in the order the core declares them and under the names its
		/// settings page shows.
		/// </summary>
		public static IReadOnlyList<ChangedSetting> ChangedSettings(WaterboxSettingsBase? settings)
		{
			List<ChangedSetting> changed = new();
			if (settings?.Declarations is not { } declarations) return changed;
			foreach (var declaration in declarations)
			{
				if (string.IsNullOrWhiteSpace(declaration.Name)) continue;
				if (settings.Values is null || !settings.Values.TryGetValue(declaration.Key, out var raw)) continue;
				var value = Shown(declaration.Coerce(raw));
				var standard = Shown(declaration.DefaultValue);
				if (string.Equals(value, standard, StringComparison.Ordinal)) continue;
				changed.Add(new(declaration.DisplayName ?? declaration.Key, value, standard));
			}
			return changed;
		}

		/// <summary>A value as a settings file writes it: no thousands separators, a dot for decimals.</summary>
		private static string Shown(object? value) => value switch
		{
			null => "",
			bool b => b ? "true" : "false",
			string s => s.Length is 0 ? "(empty)" : s,
			IFormattable f => f.ToString(null, CultureInfo.InvariantCulture),
			_ => value.ToString() ?? "",
		};

		/// <summary>
		/// The files the report form asks for that exist on this computer: the
		/// saved project, the newest crash note with the memory dump beside it,
		/// and the sandbox's log. A file that is not there is left out.
		/// </summary>
		/// <param name="projectPath">the open project's file, or null when it was never saved</param>
		/// <param name="crashFolder">the data directory's Crashes folder</param>
		/// <param name="logFolders">where the sandbox's log (minibox-diag.log) may be</param>
		public static IReadOnlyList<string> FilesToAttach(string? projectPath, string crashFolder, IEnumerable<string> logFolders)
		{
			List<string> files = new();
			try
			{
				if (!string.IsNullOrWhiteSpace(projectPath) && File.Exists(projectPath)) files.Add(projectPath!);
				if (Directory.Exists(crashFolder))
				{
					var note = new DirectoryInfo(crashFolder).GetFiles("*.txt")
						.OrderByDescending(static f => f.LastWriteTimeUtc).FirstOrDefault();
					if (note is not null)
					{
						files.Add(note.FullName);
						var dump = Path.ChangeExtension(note.FullName, ".dmp");
						if (File.Exists(dump)) files.Add(dump);
					}
				}
				foreach (var folder in logFolders)
				{
					if (string.IsNullOrWhiteSpace(folder)) continue;
					var log = Path.Combine(folder, "minibox-diag.log");
					if (!File.Exists(log) || files.Contains(log)) continue;
					files.Add(log);
					break;
				}
			}
			catch (Exception e) when (e is IOException or UnauthorizedAccessException or ArgumentException)
			{
				// a folder that cannot be read has nothing to offer; the list is a help, not a requirement
			}
			return files;
		}
	}
}
