#nullable enable

using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;

using Chimera.Emulation.Common;
using Chimera.Emulation.Common.Engine;

namespace Chimera.Client.Common
{
	/// <summary>How a provided file compares to what the core asked for.</summary>
	public enum CoreFirmwareState
	{
		/// <summary>Nothing chosen for it yet.</summary>
		Missing,

		/// <summary>Chosen, but the file is gone or unreadable now.</summary>
		Unreadable,

		/// <summary>
		/// Readable, but not the size the core declared. Either a deliberate replacement
		/// (a custom font, a modified BIOS) or the wrong file picked by mistake - the
		/// frontend cannot tell those apart, so it uses the file and says so.
		/// </summary>
		Custom,

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
		public IReadOnlyList<string> ExpectedSha1 => string.IsNullOrEmpty(Decl.Sha1) ? [ ] : [ Decl.Sha1 ];

		/// <summary>Short form for a column; the full value goes in the detail line.</summary>
		public static string Short(string? sha1) => sha1 is null ? "" : sha1.ToUpperInvariant()[..8];

		public string StatusText => State switch
		{
			CoreFirmwareState.Missing => Decl.Required ? "not provided" : "not provided (optional)",
			CoreFirmwareState.Unreadable => "file is gone",
			CoreFirmwareState.Custom => $"custom file, not the {Decl.Size} bytes the core expects - used anyway",
			CoreFirmwareState.Unrecognised => "unrecognised dump - used anyway",
			CoreFirmwareState.Good => "ok",
			_ => "?",
		};

		/// <summary>
		/// True if this file is handed to the core. Every readable file is: what a core
		/// declares is what it EXPECTS, not what the user is allowed to supply, and
		/// replacing a declared file is a legitimate thing to want (a custom system font,
		/// a modified BIOS). The core runs sandboxed and validates its own inputs, so a
		/// wrong file fails inside the machine rather than endangering the frontend, and
		/// <see cref="IsStandard"/> is what warns the user before it gets that far.
		/// </summary>
		public bool Usable => State is CoreFirmwareState.Custom or CoreFirmwareState.Unrecognised or CoreFirmwareState.Good;

		/// <summary>
		/// True if this is a file the core says it knows. False for anything the user
		/// substituted, or a dump no declaration lists - which still runs, but is worth
		/// telling the user about, since it makes a machine nobody else can reproduce
		/// without that same file.
		/// </summary>
		public bool IsStandard => State is CoreFirmwareState.Good;

		/// <summary>One line naming what is non-standard about this file. Empty when there is nothing to say.</summary>
		public string WarningText => State switch
		{
			CoreFirmwareState.Custom => $"{CoreName}: {Decl.DisplayName} is a custom file ({Short(Sha1)}), not what the core expects",
			CoreFirmwareState.Unrecognised => $"{CoreName}: {Decl.DisplayName} is an unrecognised dump ({Short(Sha1)})",
			_ => "",
		};
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
			// the verdict is the engine's (see docs/engine-migration.md)
			var state = EngineFirmware.Classify(decl.Size, string.IsNullOrEmpty(decl.Sha1) ? [ ] : [ decl.Sha1 ], bytes.Length, sha1) switch
			{
				EngineFirmware.Verdict.WrongSize => CoreFirmwareState.Custom,
				EngineFirmware.Verdict.Unrecognised => CoreFirmwareState.Unrecognised,
				_ => CoreFirmwareState.Good,
			};
			return new() { CoreName = coreName, Decl = decl, Path = path, Sha1 = sha1, State = state };
		}

		/// <summary>
		/// The provider handed to a core at load: the file's bytes, or null when the user
		/// pointed at nothing, or at a file that no longer reads. Anything readable goes
		/// over, whatever its size or hash - see <see cref="CoreFirmwareEntry.Usable"/>
		/// for why, and <see cref="NonStandard"/> for the warning that goes with it.
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

		/// <summary>
		/// What firmware a core is running with, as "&lt;id&gt;=&lt;sha1&gt;" pairs in a fixed
		/// order - the line a movie records. A different BIOS is a different machine, so a
		/// movie that does not carry this is not reproducible; and it has to be canonical,
		/// or replay would report a difference that is only an ordering.
		/// </summary>
		public static string RecordFor(Config config, CoreRegistry registry, string coreName)
			=> EngineFirmware.RecordLine(ForCore(config, registry, coreName)
				// what the core was actually given, not merely what the user pointed at:
				// a file the frontend never handed over did not shape this machine
				.Where(static e => e.Usable && e.Sha1 is not null)
				.Select(static e => (e.Decl.Id, e.Sha1!)));

		/// <summary>Every declaration of one core, with its current state.</summary>
		public static IReadOnlyList<CoreFirmwareEntry> ForCore(Config config, CoreRegistry registry, string coreName)
			=> Enumerate(config, registry)
				.Where(e => string.Equals(e.CoreName, coreName, StringComparison.OrdinalIgnoreCase))
				.ToList();

		/// <summary>
		/// The files this core is running with that it does not recognise - a custom
		/// replacement, or a dump no declaration lists. They shape the machine and are
		/// recorded in movies like any firmware, so the user is told rather than left
		/// to wonder why a movie will not sync elsewhere.
		/// </summary>
		public static IReadOnlyList<CoreFirmwareEntry> NonStandard(Config config, CoreRegistry registry, string coreName)
			=> ForCore(config, registry, coreName)
				.Where(static e => e.Usable && !e.IsStandard)
				.ToList();

		public static string Sha1Of(byte[] bytes) => ChimeraEngine.Sha1Hex(bytes);
	}
}
