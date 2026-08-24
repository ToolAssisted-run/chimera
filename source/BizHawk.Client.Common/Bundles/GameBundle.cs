#nullable enable

using System.Collections.Generic;
using System.IO;

using BizHawk.Emulation.Common.Engine;

namespace BizHawk.Client.Common
{
	/// <summary>
	/// A game that is more than one file: the rom, plus whatever a core keeps alongside
	/// it - the cartridge's battery memory, the disk the game wrote on.
	///
	/// A bundle is a CATALOGUE, not a container. It names files that sit beside it and
	/// pins each by SHA1; it never holds their bytes. The format, the naming rules and
	/// the bundle's identity live in the engine (see docs/engine-migration.md); this
	/// class is the frontend's living model of one, plus the filesystem around it.
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
		public const string Extension = ".gameBundle";

		/// <summary>What to call this in the window title and recent list. Falls back to the rom's name.</summary>
		public string? Name { get; set; }

		public BundlePart? Rom { get; set; }

		public List<BundleAttachment> Attach { get; } = new();

		/// <summary>Where the bundle was read from; every file it names is relative to this directory.</summary>
		public string? Directory { get; set; }

		public string? Path { get; set; }

		public class BundlePart
		{
			/// <summary>File name, relative to the bundle. Never absolute, never outside the bundle's folder.</summary>
			public string File { get; set; } = "";

			/// <summary>SHA1 of the file's bytes (uppercase hex), or null in a hand-written bundle that pins nothing.</summary>
			public string? Sha1 { get; set; }
		}

		public sealed class BundleAttachment : BundlePart
		{
			/// <summary>The core this belongs to, by the name movies record.</summary>
			public string Core { get; set; } = "";

			/// <summary>The id the core declared for its persistent data ("sram", "disk").</summary>
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
				using var engine = ToEngine();
				return engine.ContentId;
			}
		}

		public static bool IsBundlePath(string path)
			=> string.Equals(System.IO.Path.GetExtension(path), Extension, StringComparison.OrdinalIgnoreCase);

		/// <exception cref="InvalidOperationException">the file is not a readable bundle</exception>
		public static GameBundle Load(string path)
		{
			// the engine refuses anything unacceptable: unreadable JSON, a newer
			// format version, no rom, a part naming a path a bundle may not name
			using var parsed = EngineBundle.Parse(File.ReadAllText(path), System.IO.Path.GetFileName(path));
			GameBundle bundle = new()
			{
				Name = parsed.Name,
				Rom = new() { File = parsed.RomFile, Sha1 = parsed.RomSha1 },
				Path = path,
				Directory = System.IO.Path.GetDirectoryName(System.IO.Path.GetFullPath(path)),
			};
			for (long i = 0; i < parsed.AttachCount; i++)
			{
				var (core, id, file, sha1) = parsed.AttachAt(i);
				bundle.Attach.Add(new() { Core = core, Id = id, File = file, Sha1 = sha1 });
			}
			return bundle;
		}

		public void Save(string path)
		{
			Path = path;
			Directory = System.IO.Path.GetDirectoryName(System.IO.Path.GetFullPath(path));
			using var engine = ToEngine();
			File.WriteAllText(path, engine.Serialize());
		}

		private EngineBundle ToEngine()
		{
			EngineBundle engine = new() { Name = Name };
			engine.SetRom(Rom?.File ?? "", Rom?.Sha1);
			foreach (var a in Attach) engine.AddAttach(a.Core, a.Id, a.File, a.Sha1);
			return engine;
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
			switch (EngineBundle.CheckPath(part.File))
			{
				case 0: break;
				case 1: throw new InvalidOperationException("a bundle entry names no file");
				case 2: throw new InvalidOperationException($"\"{part.File}\": a bundle may only name files beside it, not absolute paths");
				default: throw new InvalidOperationException($"\"{part.File}\": a bundle may only name files beside it, not outside its own folder");
			}
			return System.IO.Path.GetFullPath(System.IO.Path.Combine(Directory ?? ".", part.File));
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

		public static string Sha1Of(byte[] data) => ChimeraEngine.Sha1Hex(data);

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
