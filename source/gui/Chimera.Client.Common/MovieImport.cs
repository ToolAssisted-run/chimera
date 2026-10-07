#nullable enable

using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Text;

using Newtonsoft.Json;
using Newtonsoft.Json.Linq;

namespace Chimera.Client.Common
{
	/// <summary>
	/// What the frontend asks a core's importer (engine.h, ce_import_movie): the
	/// package, the movie, the files to mount beside it under their own names,
	/// and the import options as settings. Written to a file for the
	/// <c>--import-movie</c> child process and read back there.
	/// </summary>
	public sealed class MovieImportRequest
	{
		public string Package { get; set; } = "";

		public string Movie { get; set; } = "";

		public List<MountedFile> Files { get; set; } = new();

		/// <summary>The import options, as the settings JSON's extra keys; null for none.</summary>
		public Dictionary<string, object>? Settings { get; set; }

		public sealed class MountedFile
		{
			public string Name { get; set; } = "";

			public string Path { get; set; } = "";
		}

		[JsonIgnore]
		public string? SettingsJson => Settings is { Count: not 0 } ? JsonConvert.SerializeObject(Settings) : null;

		public void Write(string path) => File.WriteAllText(path, JsonConvert.SerializeObject(this), new UTF8Encoding(false));

		public static MovieImportRequest Read(string path)
			=> JsonConvert.DeserializeObject<MovieImportRequest>(File.ReadAllText(path))
				?? throw new JsonException("the import request is empty");
	}

	/// <summary>
	/// The core's answer: a refusal (<see cref="Error"/>, a sentence for the
	/// user), or what the movie dictates - the settings (the machine included),
	/// the firmware and files it was made with, notes for the user,
	/// and the movie as Chimera's input log. Nothing in it is applied here: the
	/// project is built from it by the frontend's own project creation.
	/// </summary>
	public sealed class MovieImportAnswer
	{
		public string? Error { get; private set; }

		public string Format { get; private set; } = "";

		public long Frames { get; private set; }

		/// <summary>The machine the movie was made on, as the core's machine setting names it ("" when it did not say).</summary>
		public string Game { get; private set; } = "";

		public Dictionary<string, object> Settings { get; private set; } = new();

		public List<(string Id, string Sha1)> Firmware { get; private set; } = new();

		/// <summary>In load order - the order the project takes them in.</summary>
		public List<(string Name, string Sha1, string Slot)> Files { get; private set; } = new();

		public List<string> Notes { get; private set; } = new();

		public string Input { get; private set; } = "";

		/// <summary>Reads the core's JSON. A blank answer is the core having no importer.</summary>
		public static MovieImportAnswer Parse(string json)
		{
			if (string.IsNullOrWhiteSpace(json)) return new() { Error = "this core cannot import movies" };
			var root = JObject.Parse(json);
			MovieImportAnswer a = new();
			if (root["error"]?.Value<string>() is { } error)
			{
				a.Error = error;
				return a;
			}
			a.Format = root["format"]?.Value<string>() ?? "";
			a.Frames = root["frames"]?.Value<long>() ?? 0;
			a.Game = root["game"]?.Value<string>() ?? "";
			if (root["settings"] is JObject settings)
			{
				foreach (var (key, value) in settings)
				{
					if (value is JValue { Value: { } v }) a.Settings[key] = v;
				}
			}
			foreach (var fw in root["firmware"] as JArray ?? new JArray())
			{
				var id = fw["id"]?.Value<string>();
				if (!string.IsNullOrEmpty(id)) a.Firmware.Add((id!, fw["sha1"]?.Value<string>() ?? ""));
			}
			foreach (var f in root["files"] as JArray ?? new JArray())
			{
				var name = f["name"]?.Value<string>();
				if (!string.IsNullOrEmpty(name)) a.Files.Add((name!, f["sha1"]?.Value<string>() ?? "", f["slot"]?.Value<string>() ?? ""));
			}
			a.Notes = (root["notes"] as JArray ?? new JArray()).Select(static n => n.Value<string>() ?? "").Where(static n => n.Length is not 0).ToList();
			a.Input = root["input"]?.Value<string>() ?? "";
			if (a.Input.Length is 0) a.Error = "the core answered without the movie's input";
			return a;
		}
	}
}
