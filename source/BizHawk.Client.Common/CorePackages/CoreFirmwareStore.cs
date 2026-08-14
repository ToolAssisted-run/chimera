#nullable enable

using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Security.Cryptography;

using BizHawk.Emulation.Common;

namespace BizHawk.Client.Common
{
	/// <summary>How a provided file compares to what the core asked for.</summary>
	public enum CoreFirmwareState
	{
		/// <summary>Nothing chosen for it yet.</summary>
		Missing,

		/// <summary>Chosen, but the file is gone or unreadable now.</summary>
		Unreadable,

		/// <summary>Readable, but not the size the core declared - almost certainly the wrong file.</summary>
		WrongSize,

		/// <summary>Right size, but no known-good hash matches. Used anyway; a good dump may simply be one the core has never seen.</summary>
		Unrecognised,

		/// <summary>Matches one of the hashes the core listed.</summary>
		Good,
	}

	/// <summary>One firmware declaration plus what the user has (or has not) provided for it.</summary>
	public sealed class CoreFirmwareEntry
	{
		/// <summary>The core that wants it - the same name movies record.</summary>
		public string CoreName { get; init; } = "";

		public CoreFirmwareDecl Decl { get; init; } = new();

		/// <summary>Where the user pointed, or null if nowhere.</summary>
		public string? Path { get; init; }

		public CoreFirmwareState State { get; init; }

		/// <summary>The SHA1 of the file the user provided (uppercase hex), or null if there is none or it would not read.</summary>
		public string? Sha1 { get; init; }

		/// <summary>The SHA1s the core says are right, in declaration order. Empty when it pins none.</summary>
		public IReadOnlyList<string> ExpectedSha1 => Decl.Sha1 ?? (IReadOnlyList<string>) [ ];

		/// <summary>Short form for a column; the full value goes in the detail line.</summary>
		public static string Short(string? sha1) => sha1 is null ? "" : sha1.ToUpperInvariant()[..8];

		public string StatusText => State switch
		{
			CoreFirmwareState.Missing => Decl.Required ? "not provided" : "not provided (optional)",
			CoreFirmwareState.Unreadable => "file is gone",
			CoreFirmwareState.WrongSize => $"wrong size (expected {Decl.Size} bytes)",
			CoreFirmwareState.Unrecognised => "unrecognised dump - will be used anyway",
			CoreFirmwareState.Good => "ok",
			_ => "?",
		};

		/// <summary>True if this file is good enough to hand to the core.</summary>
		public bool Usable => State is CoreFirmwareState.Unrecognised or CoreFirmwareState.Good;
	}

	/// <summary>
	/// Where the user's firmware files are, and whether they are the right ones.
	///
	/// The frontend knows nothing about any particular firmware: every entry here
	/// exists because a loaded core package declared it (see
	/// <see cref="CoreFirmwareDecl"/>). This class is the whole of the frontend's
	/// side - remember a path per (core, id), read it back, say how it compares to
	/// the declaration. Deliberately free of UI so it can be tested without one.
	/// </summary>
	public static class CoreFirmwareStore
	{
		/// <summary>The config key a choice is remembered under. Keyed by core name, not by package hash: rebuilding a core package must not make the user find their BIOS again.</summary>
		public static string KeyFor(string coreName, string id) => $"{coreName}/{id}";

		public static string? GetPath(Config config, string coreName, string id)
			=> config.CoreFirmware.TryGetValue(KeyFor(coreName, id), out var path) && !string.IsNullOrWhiteSpace(path)
				? path
				: null;

		public static void SetPath(Config config, string coreName, string id, string? path)
		{
			var key = KeyFor(coreName, id);
			if (string.IsNullOrWhiteSpace(path)) config.CoreFirmware.Remove(key);
			else config.CoreFirmware[key] = path!;
		}

		/// <summary>Every declaration of every loaded package, with its current state - the firmware window's contents.</summary>
		public static IReadOnlyList<CoreFirmwareEntry> Enumerate(Config config, CoreRegistry registry)
			=> registry.AllFactories
				.OfType<ICoreFirmwareUser>()
				.SelectMany(user => user.Firmware.Select(decl => Describe(config, ((ICoreFactory) user).CoreName, decl)))
				.ToList();

		/// <summary>True if any loaded package expects firmware at all (the firmware window is pointless otherwise).</summary>
		public static bool AnyExpected(CoreRegistry registry)
			=> registry.AllFactories.OfType<ICoreFirmwareUser>().Any(static u => u.Firmware.Count != 0);

		public static CoreFirmwareEntry Describe(Config config, string coreName, CoreFirmwareDecl decl)
		{
			var path = GetPath(config, coreName, decl.Id);
			if (path is null) return new() { CoreName = coreName, Decl = decl, State = CoreFirmwareState.Missing };

			byte[] bytes;
			try
			{
				bytes = File.ReadAllBytes(path);
			}
			catch
			{
				return new() { CoreName = coreName, Decl = decl, Path = path, State = CoreFirmwareState.Unreadable };
			}
			var sha1 = Sha1Of(bytes); // computed even for a file that will be refused: the user wants to see WHAT they pointed at
			if (decl.Size != 0 && bytes.Length != decl.Size)
			{
				return new() { CoreName = coreName, Decl = decl, Path = path, Sha1 = sha1, State = CoreFirmwareState.WrongSize };
			}
			var known = decl.Sha1 is not null
				&& decl.Sha1.Exists(h => string.Equals(h, sha1, StringComparison.OrdinalIgnoreCase));
			return new()
			{
				CoreName = coreName,
				Decl = decl,
				Path = path,
				Sha1 = sha1,
				State = known || decl.Sha1 is null or { Count: 0 } ? CoreFirmwareState.Good : CoreFirmwareState.Unrecognised,
			};
		}

		/// <summary>
		/// The provider handed to a core at load: the file's bytes, or null when there is
		/// nothing usable. A wrong-sized file counts as nothing - handing it over would
		/// only turn a clear "provide this" into a mystery crash inside the sandbox.
		/// </summary>
		public static Func<CoreFirmwareDecl, byte[]?> ProviderFor(Config config, string coreName)
			=> decl =>
			{
				var entry = Describe(config, coreName, decl);
				if (!entry.Usable || entry.Path is null) return null;
				try
				{
					return File.ReadAllBytes(entry.Path);
				}
				catch
				{
					return null;
				}
			};

		public static string Sha1Of(byte[] bytes)
		{
			using var sha1 = SHA1.Create();
			return BitConverter.ToString(sha1.ComputeHash(bytes)).Replace("-", "");
		}
	}
}
