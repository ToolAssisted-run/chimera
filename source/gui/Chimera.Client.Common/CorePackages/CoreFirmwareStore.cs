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

		/// <summary>
		/// The key one particular dump is remembered under. A core may declare one
		/// id many times - PCSX2 lists seventy-three releases of its bios under
		/// "bios.bin" - and a person may own several; the plain key can only say
		/// which one is in use. A pinned declaration's file is remembered under its
		/// hash as well, so the wizard, an opening project and the firmware survey
		/// can all find every dump a person ever pointed at. An unpinned
		/// declaration has no hash to remember by and keeps the plain key.
		/// </summary>
		public static string KeyFor(string coreName, CoreFirmwareDecl decl)
			=> string.IsNullOrEmpty(decl.Sha1) ? KeyFor(coreName, decl.Id) : $"{coreName}/{decl.Id}#{decl.Sha1.ToUpperInvariant()}";

		/// <summary>The remembered path for THIS declaration: its own dump's first, then whatever is in use for its id.</summary>
		public static string? GetPath(Config config, string coreName, CoreFirmwareDecl decl)
		{
			if (!string.IsNullOrEmpty(decl.Sha1)
				&& config.CoreFirmware.TryGetValue(KeyFor(coreName, decl), out var own) && !string.IsNullOrWhiteSpace(own))
			{
				return own;
			}
			return GetPath(config, coreName, decl.Id);
		}

		/// <summary>Remembers (or, for null, forgets) the file chosen for one declaration, under its own dump's key.</summary>
		public static void Remember(Config config, string coreName, CoreFirmwareDecl decl, string? path)
		{
			var key = KeyFor(coreName, decl);
			if (string.IsNullOrWhiteSpace(path)) config.CoreFirmware.Remove(key);
			else config.CoreFirmware[key] = path!;
		}

		/// <summary>Every path remembered for a core, whichever key it sits under. Hints, all of them: hashed before they are believed.</summary>
		public static IReadOnlyList<string> RememberedPaths(Config config, string coreName)
			=> config.CoreFirmware
				.Where(kv => CoreOf(kv.Key).Equals(coreName, StringComparison.OrdinalIgnoreCase) && !string.IsNullOrWhiteSpace(kv.Value))
				.Select(static kv => kv.Value)
				.Distinct(StringComparer.OrdinalIgnoreCase)
				.ToList();

		// What is remembered for a core is kept whether or not the core is
		// installed right now: a package that is removed and put back finds its
		// dumps where they were, and a person is never made to point at a
		// PlayStation 3 update twice. Only the SURVEY forgets an absent core -
		// its rows come from the packages present - never the config.

		private static string CoreOf(string key)
		{
			var slash = key.IndexOf('/');
			return slash < 0 ? key : key[..slash];
		}

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

		/// <summary>
		/// What the Firmware folder can answer for a declaration nothing else
		/// has: the file with the hash it pins, or - when it pins none, as an
		/// Xbox disk image or a console's own identity dump cannot - the file
		/// bearing the name the core says it goes by.
		///
		/// Until this, "put the file in the Firmware folder and open the project
		/// again" was advice the frontend did not take: only the wizard and a
		/// project's own hash pins ever looked in that folder, so a saved project
		/// whose remembered path had moved refused with the file sitting exactly
		/// where the message asked for it (issue #40).
		/// </summary>
		private static string? InFirmwareFolder(Config config, CoreFirmwareDecl decl)
		{
			string dir;
			try
			{
				dir = config.PathEntries.FirmwareAbsolutePath();
			}
			catch (Exception ex) when (ex is IOException or ArgumentException)
			{
				return null;
			}
			if (string.IsNullOrEmpty(dir) || !Directory.Exists(dir)) return null;
			if (string.IsNullOrEmpty(decl.Sha1))
			{
				// nothing to ask by but the name, and a name is compared the way
				// the platform that wrote it would: a dump called Complex_4627.bin
				// is the file a core declares as complex_4627.bin
				if (string.IsNullOrEmpty(decl.Name)) return null;
				try
				{
					return Directory.EnumerateFiles(dir, "*", SearchOption.TopDirectoryOnly)
						.FirstOrDefault(f => Path.GetFileName(f).Equals(decl.Name, StringComparison.OrdinalIgnoreCase));
				}
				catch (Exception ex) when (ex is IOException or UnauthorizedAccessException)
				{
					return null;
				}
			}
			return FirmwareLocator.FindFor(decl, FirmwareLocator.BuildIndex([ dir ]))?.Path;
		}

		public static CoreFirmwareEntry Describe(Config config, string coreName, CoreFirmwareDecl decl)
			=> Describe(config, coreName, decl, null);

		/// <summary>
		/// The same, answering the Firmware folder from an index the caller built
		/// once: a survey of seventy-three declarations must not hash the folder
		/// seventy-three times.
		/// </summary>
		public static CoreFirmwareEntry Describe(Config config, string coreName, CoreFirmwareDecl decl, IReadOnlyList<FirmwareLocator.IndexedFile>? index)
		{
			var path = GetPath(config, coreName, decl);
			// a choice that has moved is no longer an answer; the folder may hold one
			if (path is null || !File.Exists(path))
			{
				path = (index is null ? InFirmwareFolder(config, decl) : FirmwareLocator.FindEither(decl, index)?.Path) ?? path;
			}
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
		/// <summary>
		/// Firmware named on the command line, by id: what THIS run must use,
		/// ahead of anything the config remembers. A precompile session is a child
		/// process, and it cannot be told through the config - the parent would
		/// have to have written the file first, and the wizard may hold a path
		/// that was never written there (a core can call a firmware optional that
		/// the game in hand cannot boot without). So it is told outright, and the
		/// answer lives for the process, which is exactly one run.
		/// </summary>
		public static IReadOnlyDictionary<string, string> CommandLineFirmware { get; set; }
			= new Dictionary<string, string>(StringComparer.Ordinal);

		/// <summary>Parses "&lt;id&gt;=&lt;path&gt;" pairs; a pair without an "=" is skipped.</summary>
		public static IReadOnlyDictionary<string, string> ParseFirmwareArgs(IEnumerable<string> pairs)
		{
			var map = new Dictionary<string, string>(StringComparer.Ordinal);
			foreach (var pair in pairs ?? [ ])
			{
				if (string.IsNullOrWhiteSpace(pair)) continue;
				var eq = pair.IndexOf('=');
				if (eq <= 0 || eq == pair.Length - 1) continue;
				map[pair[..eq]] = pair[(eq + 1)..];
			}
			return map;
		}

		public static Func<CoreFirmwareDecl, byte[]?> ProviderFor(Config config, string coreName)
			=> decl =>
			{
				// said on the command line: use it, and say so if it cannot be read
				// rather than quietly falling back to a different machine
				if (decl is not null && CommandLineFirmware.TryGetValue(decl.Id, out var named))
				{
					try
					{
						return File.ReadAllBytes(named);
					}
					catch (IOException e)
					{
						Console.Error.WriteLine($"firmware {decl.Id}: cannot read {named}: {e.Message}");
						return null;
					}
				}
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
			=> EngineFirmware.RecordLine(InUse(config, registry, coreName)
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
		/// What the loaded machine is actually running on: the declarations the load
		/// called for, not every one the package lists.
		///
		/// A package may declare a hundred BIOS dumps and use the one the project
		/// chose. Judging the chosen file against the other ninety-nine calls a
		/// perfectly good dump unrecognised, once per declaration, which is what
		/// this exists to stop. A package whose declarations are unconditional gets
		/// the same answer either way.
		/// </summary>
		public static IReadOnlyList<CoreFirmwareEntry> InUse(Config config, CoreRegistry registry, string coreName)
		{
			var user = registry.AllFactories
				.OfType<ICoreFirmwareUser>()
				.FirstOrDefault(u => string.Equals(((ICoreFactory) u).CoreName, coreName, StringComparison.OrdinalIgnoreCase));
			if (user is null) return [ ];
			return InUse(config, coreName, user.Firmware, user.FirmwareInUse);
		}

		/// <summary>
		/// The same decision without a registry, so it can be tested: what a load
		/// actually used, or - before anything has loaded, when nothing is in use
		/// yet - everything the package declares.
		/// </summary>
		public static IReadOnlyList<CoreFirmwareEntry> InUse(
			Config config,
			string coreName,
			IReadOnlyList<CoreFirmwareDecl> declared,
			IReadOnlyList<CoreFirmwareDecl> inUse)
			=> (inUse.Count is 0 ? declared : inUse)
				.Select(decl => Describe(config, coreName, decl))
				.ToList();

		/// <summary>
		/// The files this core is running with that it does not recognise - a custom
		/// replacement, or a dump no declaration lists. They shape the machine and are
		/// recorded in movies like any firmware, so the user is told rather than left
		/// to wonder why a movie will not sync elsewhere.
		/// </summary>
		public static IReadOnlyList<CoreFirmwareEntry> NonStandard(Config config, CoreRegistry registry, string coreName)
			=> InUse(config, registry, coreName)
				.Where(static e => e.Usable && !e.IsStandard)
				.ToList();

		public static string Sha1Of(byte[] bytes) => ChimeraEngine.Sha1Hex(bytes);
	}
}
