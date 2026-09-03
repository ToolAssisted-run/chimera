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

		[JsonProperty("files")]
		public List<CoreCacheFile> Files { get; set; } = [ ];

		private static string PathFor(string dir, string romSha1)
			=> dir is null || string.IsNullOrEmpty(romSha1) ? null : Path.Combine(dir, "games", romSha1 + ".json");

		public static CoreCacheManifest Load(string dir, string romSha1)
		{
			var path = PathFor(dir, romSha1);
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

		public void Save(string dir, string romSha1)
		{
			var path = PathFor(dir, romSha1);
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
