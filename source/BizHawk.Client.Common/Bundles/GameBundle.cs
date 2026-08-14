#nullable enable

using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Security.Cryptography;
using System.Text;

using Newtonsoft.Json;

namespace BizHawk.Client.Common
{
	/// <summary>
	/// A game that is more than one file: the rom, plus whatever a core keeps alongside
	/// it - the cartridge's battery memory, the disk the game wrote on.
	///
	/// A bundle is a CATALOGUE, not a container. It names files that sit beside it and
	/// pins each by SHA1; it never holds their bytes. So a bundle stays a few hundred
	/// bytes, editing a save does not mean rebuilding anything, and the file you point
	/// the emulator at is still the file you would have pointed it at.
	///
	/// The hashes are what make it worth citing: a movie that says "recorded against
	/// bundle X" is only meaningful if X pins its parts by content. A hand-written
	/// bundle may leave them out, and then nothing is checked.
	///
	/// Attachments are addressed to a core by name and by the id THAT CORE declared for
	/// its persistent data. The frontend never learns what an "sram" is; it carries
	/// bytes to a core that asked for them, and refuses to hand a save meant for one
	/// core to another.
	/// </summary>
	public sealed class GameBundle
	{
		public const string Extension = ".bundle";

		/// <summary>Format version, so an older frontend can refuse a newer bundle rather than misread it.</summary>
		[JsonProperty("bundle")]
		public int FormatVersion { get; set; } = 1;

		/// <summary>What to call this in the window title and recent list. Falls back to the rom's name.</summary>
		[JsonProperty("name")]
		public string? Name { get; set; }

		[JsonProperty("rom")]
		public BundlePart? Rom { get; set; }

		[JsonProperty("attach")]
		public List<BundleAttachment> Attach { get; set; } = new();

		/// <summary>Where the bundle was read from; every file it names is relative to this directory.</summary>
		[JsonIgnore]
		public string? Directory { get; set; }

		[JsonIgnore]
		public string? Path { get; set; }

		public class BundlePart
		{
			/// <summary>File name, relative to the bundle. Never absolute, never outside the bundle's folder.</summary>
			[JsonProperty("file")]
			public string File { get; set; } = "";

			/// <summary>SHA1 of the file's bytes (uppercase hex), or null in a hand-written bundle that pins nothing.</summary>
			[JsonProperty("sha1", NullValueHandling = NullValueHandling.Ignore)]
			public string? Sha1 { get; set; }
		}

		public sealed class BundleAttachment : BundlePart
		{
			/// <summary>The core this belongs to, by the name movies record.</summary>
			[JsonProperty("core")]
			public string Core { get; set; } = "";

			/// <summary>The id the core declared for its persistent data ("sram", "disk").</summary>
			[JsonProperty("id")]
			public string Id { get; set; } = "";
		}

		/// <summary>
		/// The bundle's identity: a hash over what it says its parts ARE, not over the
		/// file itself, so reformatting or renaming a bundle does not change what a movie
		/// recorded. Null when the bundle pins nothing (nothing to be identical to).
		/// </summary>
		public string? ContentId
		{
			get
			{
				if (Rom?.Sha1 is null) return null;
				StringBuilder sb = new();
				sb.Append("rom:").Append(Rom.Sha1.ToUpperInvariant()).Append('\n');
				foreach (var a in Attach.OrderBy(static a => a.Core, StringComparer.Ordinal).ThenBy(static a => a.Id, StringComparer.Ordinal))
				{
					if (a.Sha1 is null) return null;
					sb.Append(a.Core).Append(':').Append(a.Id).Append(':').Append(a.Sha1.ToUpperInvariant()).Append('\n');
				}
				using SHA1 sha1 = SHA1.Create();
				return BitConverter.ToString(sha1.ComputeHash(Encoding.UTF8.GetBytes(sb.ToString()))).Replace("-", "");
			}
		}

		public static bool IsBundlePath(string path)
			=> string.Equals(System.IO.Path.GetExtension(path), Extension, StringComparison.OrdinalIgnoreCase);

		/// <exception cref="InvalidOperationException">the file is not a readable bundle</exception>
		public static GameBundle Load(string path)
		{
			GameBundle? bundle;
			try
			{
				bundle = JsonConvert.DeserializeObject<GameBundle>(File.ReadAllText(path));
			}
			catch (JsonException e)
			{
				throw new InvalidOperationException($"{System.IO.Path.GetFileName(path)} is not a readable bundle: {e.Message}");
			}
			if (bundle is null) throw new InvalidOperationException($"{System.IO.Path.GetFileName(path)} is empty");
			if (bundle.FormatVersion > 1) throw new InvalidOperationException($"{System.IO.Path.GetFileName(path)} is a version {bundle.FormatVersion} bundle; this build understands version 1");
			if (string.IsNullOrWhiteSpace(bundle.Rom?.File)) throw new InvalidOperationException($"{System.IO.Path.GetFileName(path)} names no rom");
			bundle.Attach ??= new();
			bundle.Path = path;
			bundle.Directory = System.IO.Path.GetDirectoryName(System.IO.Path.GetFullPath(path));
			foreach (var part in bundle.AllParts()) _ = bundle.ResolveFile(part); // reject bad paths at load, not at use
			return bundle;
		}

		public void Save(string path)
		{
			Path = path;
			Directory = System.IO.Path.GetDirectoryName(System.IO.Path.GetFullPath(path));
			File.WriteAllText(path, JsonConvert.SerializeObject(this, Formatting.Indented));
		}

		public IEnumerable<BundlePart> AllParts()
		{
			if (Rom is not null) yield return Rom;
			foreach (var a in Attach) yield return a;
		}

		/// <summary>
		/// The absolute path of a part. A bundle may only name files beside it (or under
		/// it): a catalogue that reaches somewhere else stops being something you can
		/// hand to anyone, and an absolute path in a bundle is a path on ONE machine.
		/// </summary>
		/// <exception cref="InvalidOperationException">the part names a path a bundle may not name</exception>
		public string ResolveFile(BundlePart part)
		{
			var name = part.File;
			if (string.IsNullOrWhiteSpace(name)) throw new InvalidOperationException("a bundle entry names no file");
			if (System.IO.Path.IsPathRooted(name) || name.Contains(':'))
			{
				throw new InvalidOperationException($"\"{name}\": a bundle may only name files beside it, not absolute paths");
			}
			var full = System.IO.Path.GetFullPath(System.IO.Path.Combine(Directory ?? ".", name));
			var root = System.IO.Path.GetFullPath(Directory ?? ".");
			if (!full.StartsWith(root + System.IO.Path.DirectorySeparatorChar, StringComparison.Ordinal) && full != root)
			{
				throw new InvalidOperationException($"\"{name}\": a bundle may only name files beside it, not outside its own folder");
			}
			return full;
		}

		/// <summary>
		/// Reads a part, checking it against the hash the bundle pinned it to.
		/// </summary>
		/// <exception cref="InvalidOperationException">the file is missing, or is not the file the bundle names</exception>
		public byte[] ReadFile(BundlePart part)
		{
			var path = ResolveFile(part);
			if (!File.Exists(path)) throw new InvalidOperationException($"{System.IO.Path.GetFileName(path)} is missing from this bundle's folder");
			var data = File.ReadAllBytes(path);
			if (part.Sha1 is not null)
			{
				var actual = Sha1Of(data);
				if (!string.Equals(actual, part.Sha1, StringComparison.OrdinalIgnoreCase))
				{
					throw new InvalidOperationException(
						$"{System.IO.Path.GetFileName(path)} is not the file this bundle names (bundle says {part.Sha1.ToUpperInvariant()[..8]}, file is {actual[..8]})");
				}
			}
			return data;
		}

		/// <summary>Rewrites an attachment's file and re-pins it, which is what closing a game writes back.</summary>
		public void WriteAttachment(BundleAttachment attachment, byte[] data)
		{
			File.WriteAllBytes(ResolveFile(attachment), data);
			attachment.Sha1 = Sha1Of(data);
		}

		public BundleAttachment? FindAttachment(string coreName, string id)
			=> Attach.Find(a => string.Equals(a.Core, coreName, StringComparison.OrdinalIgnoreCase)
				&& string.Equals(a.Id, id, StringComparison.OrdinalIgnoreCase));

		public static string Sha1Of(byte[] data)
		{
			using SHA1 sha1 = SHA1.Create();
			return BitConverter.ToString(sha1.ComputeHash(data)).Replace("-", "");
		}

		/// <summary>
		/// Builds a bundle for a rom and one core's persistent data, both already sitting
		/// in <paramref name="bundlePath"/>'s folder. This is what "compose a bundle" does
		/// when you want a movie to start from a save.
		/// </summary>
		public static GameBundle Compose(string bundlePath, string romPath, string coreName, string id, string attachmentPath, string? name = null)
		{
			GameBundle bundle = new()
			{
				Name = name,
				Rom = new() { File = System.IO.Path.GetFileName(romPath), Sha1 = Sha1Of(File.ReadAllBytes(romPath)) },
				Path = bundlePath,
				Directory = System.IO.Path.GetDirectoryName(System.IO.Path.GetFullPath(bundlePath)),
			};
			bundle.Attach.Add(new()
			{
				Core = coreName,
				Id = id,
				File = System.IO.Path.GetFileName(attachmentPath),
				Sha1 = Sha1Of(File.ReadAllBytes(attachmentPath)),
			});
			return bundle;
		}
	}
}
