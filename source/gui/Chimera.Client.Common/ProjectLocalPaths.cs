#nullable enable

using System.Collections.Generic;
using System.IO;
using System.Linq;

using Chimera.Emulation.Common.Engine;

using Newtonsoft.Json;
using Newtonsoft.Json.Linq;

namespace Chimera.Client.Common
{
	/// <summary>
	/// Where this machine keeps a project's files, remembered beside it.
	///
	/// A .chimeraProject is meant to be handed to someone else: it names files
	/// by name and hash and never by path, because a path is true of one machine
	/// and nowhere else (docs/project.md). But the machine that MADE the project
	/// knows where those files are, and asking it again every time you open your
	/// own work is friction for nothing.
	///
	/// So the paths live in a sibling, <c>&lt;project&gt;.chimeraLocal</c>: same
	/// name, different file, never distributed and never read as authority. It is
	/// a hint. Every path it offers is checked - the file must still be there and
	/// still hash to what the project records - and anything that fails simply
	/// falls through to the usual resolution, which is what happens on a machine
	/// that has no sidecar at all. Deleting it costs nothing but the asking.
	/// </summary>
	public sealed class ProjectLocalPaths
	{
		public const string Extension = "chimeraLocal";

		/// <summary>canonical file name (as the project records it) -> where it was last read from</summary>
		private readonly Dictionary<string, string> _files = new();

		/// <summary>firmware id -> where the file that satisfied it was last read from</summary>
		private readonly Dictionary<string, string> _firmware = new();

		public IReadOnlyDictionary<string, string> Files => _files;

		public IReadOnlyDictionary<string, string> Firmware => _firmware;

		/// <summary>The sidecar that belongs to a project file.</summary>
		public static string PathFor(string projectPath)
			=> Path.ChangeExtension(projectPath, Extension);

		/// <summary>Reads the sidecar beside a project; an absent or unreadable one is simply empty.</summary>
		public static ProjectLocalPaths Read(string projectPath)
		{
			ProjectLocalPaths local = new();
			var path = PathFor(projectPath);
			if (!File.Exists(path)) return local;
			try
			{
				var root = JObject.Parse(File.ReadAllText(path));
				Fill(local._files, root["files"] as JObject);
				Fill(local._firmware, root["firmware"] as JObject);
			}
			catch (Exception ex) when (ex is JsonException or IOException or UnauthorizedAccessException)
			{
				// a hint that cannot be read is a hint nobody has: resolution proceeds
				return new ProjectLocalPaths();
			}
			return local;
		}

		private static void Fill(Dictionary<string, string> into, JObject? from)
		{
			if (from is null) return;
			foreach (var (key, value) in from)
			{
				var path = value?.Value<string>();
				if (!string.IsNullOrWhiteSpace(path)) into[key] = path!;
			}
		}

		/// <summary>
		/// Where THIS session found the open project's firmware, by id.
		///
		/// A project that has never been written has no sidecar to write into:
		/// the wizard's choices and the boot's lookups live only in the config,
		/// and a config that is replaced (a fresh install copied over an old one)
		/// or a file that is moved takes them with it - which is a saved project
		/// that will not open, with its firmware still on the machine that made
		/// it (issue #40). The first save is the first chance to write them
		/// beside the project, so the boot leaves them here and every save merges
		/// them in. They are hints and nothing more: the next load checks each
		/// one by hash before it mounts anything, so an entry left over from a
		/// project that is no longer open costs the asking and no more.
		/// </summary>
		private static readonly Dictionary<string, string> SessionFirmware = new();

		/// <summary>Forgotten when another project boots, so a sidecar records this project's answers.</summary>
		public static void ForgetSessionFirmware()
		{
			lock (SessionFirmware) SessionFirmware.Clear();
		}

		public void RememberFirmware(string id, string path)
		{
			if (string.IsNullOrWhiteSpace(id) || string.IsNullOrWhiteSpace(path)) return;
			var full = Path.GetFullPath(path);
			_firmware[id] = full;
			lock (SessionFirmware) SessionFirmware[id] = full;
		}

		/// <summary>
		/// Offers the remembered locations to a project's unresolved files. A file
		/// that has moved, or whose bytes no longer match what the project records,
		/// is left unresolved for the resolution dialog to ask about - the hint is
		/// never allowed to mount the wrong bytes quietly.
		/// </summary>
		/// <returns>how many files the sidecar resolved</returns>
		public int ApplyTo(EngineProject project)
		{
			var resolved = 0;
			for (var i = 0; i < project.FileCount; i++)
			{
				if (project.FileStatus(i) is not 1) continue; // already found beside the project
				if (!_files.TryGetValue(project.FileName(i), out var path)) continue;
				if (!File.Exists(path)) continue;
				try
				{
					project.FileResolve(i, path);
				}
				catch (InvalidOperationException)
				{
					continue; // unreadable now; the dialog will ask
				}
				if (project.FileStatus(i) is 0)
				{
					resolved++;
				}
				else
				{
					// the path still exists but holds something else now: that is a
					// question for the user, not an answer from a hint
					project.FileUnresolve(i);
				}
			}
			return resolved;
		}

		/// <summary>
		/// Writes down where the session actually read each file from, beside the
		/// project. Nothing here is needed to open the project anywhere else, which
		/// is the whole point of keeping it out of the project file.
		/// </summary>
		public void Save(string projectPath, EngineProject project)
		{
			for (var i = 0; i < project.FileCount; i++)
			{
				var source = project.FileSourcePath(i);
				if (source.Length is 0) continue;
				_files[project.FileName(i)] = Path.GetFullPath(source);
			}
			// what the session found, for a project whose sidecar this is the
			// first of; anything this instance was told itself wins
			lock (SessionFirmware)
			{
				foreach (var (id, source) in SessionFirmware)
				{
					if (!_firmware.ContainsKey(id)) _firmware[id] = source;
				}
			}
			if (_files.Count is 0 && _firmware.Count is 0) return;

			JObject root = new()
			{
				["files"] = new JObject(_files.OrderBy(static kvp => kvp.Key, StringComparer.Ordinal)
					.Select(static kvp => new JProperty(kvp.Key, kvp.Value))),
				["firmware"] = new JObject(_firmware.OrderBy(static kvp => kvp.Key, StringComparer.Ordinal)
					.Select(static kvp => new JProperty(kvp.Key, kvp.Value))),
			};
			try
			{
				File.WriteAllText(PathFor(projectPath), root.ToString(Formatting.Indented));
			}
			catch (Exception ex) when (ex is IOException or UnauthorizedAccessException)
			{
				// a convenience that cannot be written is not worth an error: the
				// project itself saved fine, and next time it will ask
			}
		}
	}
}
