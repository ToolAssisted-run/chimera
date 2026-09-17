using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Security.Cryptography;

using Newtonsoft.Json;

namespace Chimera.Client.Common
{
	/// <summary>One compiled object a core keeps for a game: the name it gave it, and its hash.</summary>
	public sealed class CoreCacheFile
	{
		[JsonProperty("name")]
		public string Name { get; set; }

		[JsonProperty("sha1")]
		public string Sha1 { get; set; }
	}

	/// <summary>
	/// What a core compiled for one game, remembered beside the objects
	/// themselves (docs/compile-cache.md). The objects are regenerable from the
	/// rom and the package, so this file is not precious; what it buys is
	/// knowing WHICH objects a game needs before running it, which is what lets
	/// the wizard show a list and refuse to create a project with a hole in it.
	/// </summary>
	public sealed class CoreCacheManifest
	{
		[JsonProperty("rom")]
		public string RomName { get; set; }

		[JsonProperty("romSha1")]
		public string RomSha1 { get; set; }

		/// <summary>
		/// Which core compiled this, and which build of its package.
		///
		/// The directory used to say this - one per core and package version -
		/// and a game's code now lives under the game's hash alone, so the
		/// answer had to move in here. It is not decoration: objects compiled by
		/// a different build of the package are different code, and reading them
		/// would be reading somebody else's answer. <see cref="CompiledBy"/> is
		/// what refuses that.
		/// </summary>
		[JsonProperty("core")]
		public string CoreName { get; set; }

		[JsonProperty("coreVersion")]
		public string CoreVersion { get; set; }

		/// <summary>When the sessions produced it, so a person can see what is old.</summary>
		[JsonProperty("compiled")]
		public DateTime? Compiled { get; set; }

		[JsonProperty("files")]
		public List<CoreCacheFile> Files { get; set; } = [ ];

		/// <summary>The manifest sits inside the game's own directory, so removing that directory removes it too.</summary>
		private static string PathFor(string dir)
			=> dir is null ? null : Path.Combine(dir, "manifest.json");

		/// <summary>
		/// Whether this was compiled by the core and package build now running.
		/// A manifest that predates the core+version fields cannot say, and is
		/// treated as not matching: compiling again is cheap beside trusting
		/// objects nothing vouches for.
		/// </summary>
		public bool CompiledBy(string coreName, string coreVersion)
			=> !string.IsNullOrEmpty(CoreName)
				&& string.Equals(CoreName, coreName, StringComparison.OrdinalIgnoreCase)
				&& string.Equals(CoreVersion ?? "", coreVersion ?? "", StringComparison.Ordinal);

		public static CoreCacheManifest Load(string dir)
		{
			var path = PathFor(dir);
			if (path is null || !File.Exists(path)) return null;
			try
			{
				return JsonConvert.DeserializeObject<CoreCacheManifest>(File.ReadAllText(path));
			}
			catch (Exception)
			{
				return null; // an unreadable manifest is a missing one: compile again
			}
		}

		public void Save(string dir)
		{
			var path = PathFor(dir);
			if (path is null) return;
			Directory.CreateDirectory(Path.GetDirectoryName(path)!);
			File.WriteAllText(path, JsonConvert.SerializeObject(this, Formatting.Indented));
		}

		/// <summary>The file's hash as it is on disk now, or null when it is not there.</summary>
		public static string HashOf(string dir, string name)
		{
			var path = dir is null ? null : Path.Combine(dir, name.Replace('/', Path.DirectorySeparatorChar));
			if (path is null || !File.Exists(path)) return null;
			using var stream = File.OpenRead(path);
			using var sha1 = SHA1.Create();
			return BitConverter.ToString(sha1.ComputeHash(stream)).Replace("-", "");
		}

		/// <summary>Which of the listed objects are missing or no longer what they were.</summary>
		public IReadOnlyList<CoreCacheFile> Unsatisfied(string dir)
			=> Files.Where(f => !string.Equals(HashOf(dir, f.Name), f.Sha1, StringComparison.OrdinalIgnoreCase)).ToList();

		public bool Satisfied(string dir) => Files.Count is not 0 && Unsatisfied(dir).Count is 0;
	}
}
