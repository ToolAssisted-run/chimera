#nullable enable

using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;

namespace Chimera.Client.Common
{
	/// <summary>What a directory somebody chose for Chimera's data turns out to be.</summary>
	public enum DataDirectoryChoice
	{
		/// <summary>Empty, or not there yet: the data goes straight into it.</summary>
		Empty,

		/// <summary>Already holds a Chimera data directory - an earlier move, or another install's. It can be adopted as it is.</summary>
		HoldsChimeraData,

		/// <summary>It is where the data already is.</summary>
		Same,

		/// <summary>Inside the current data directory, or around it: a move would be into or out of itself.</summary>
		Nested,

		/// <summary>It cannot be created or written to.</summary>
		Unwritable,
	}

	/// <summary>
	/// Moving everything Chimera keeps per user - project caches, unpacked cores, precompiled
	/// code, the core store, recovery journals, crash notes, the lock book - somewhere other than
	/// the system drive (issue #52; "everything" is user-decided, 2026-09-17).
	///
	/// It was always possible through <c>CHIMERA_DATA_HOME</c>, which nobody who needs this will
	/// find. The setting is the same thing with a window on it, and the variable still wins where
	/// it is set, because a portable install or a test that sets it means it.
	///
	/// NOTHING HERE RUNS WHILE THE DATA IS IN USE. A project holds its state history open, the
	/// package list holds paths into the core store, Windows holds the crash folder's path. So a
	/// change is recorded and carried out by the next start, before any of that exists
	/// (<see cref="ApplyPending"/>), and this class never has to reason about who has what open.
	///
	/// A move is a copy that is checked and only then a delete: until the last byte is in the new
	/// place the old place is whole, and a failure at any point leaves things as they were and
	/// removes what was copied. Within one volume it is a rename and takes no time at all.
	/// </summary>
	public static class DataDirectory
	{
		/// <summary>What a Chimera data directory is recognised by: the names it has at its top.</summary>
		private static readonly string[] Marks = [ "Projects", "Cores", "UnpackedCores", "Cache", "Recovery", "Crashes", "cache-locks.json" ];

		/// <summary>What a folder picked by hand is given when it already holds other things.</summary>
		public const string SubdirectoryName = "Chimera";

		/// <summary>
		/// Where the data would actually go for a folder somebody picked. A folder with other
		/// things in it - a drive's root, a Documents folder - is not taken over: the data gets a
		/// <c>Chimera</c> directory of its own inside it, so that "everything in the data
		/// directory is Chimera's" stays true and a later move can take all of it.
		/// </summary>
		public static string TargetFor(string picked)
		{
			var full = Path.GetFullPath(picked);
			if (!Directory.Exists(full) || HoldsChimeraData(full)) return full;
			return Directory.EnumerateFileSystemEntries(full).Any() ? Path.Combine(full, SubdirectoryName) : full;
		}

		public static bool HoldsChimeraData(string dir)
			=> Directory.Exists(dir) && Marks.Any(mark => Directory.Exists(Path.Combine(dir, mark)) || File.Exists(Path.Combine(dir, mark)));

		public static DataDirectoryChoice Inspect(string current, string target)
		{
			var from = WithSeparator(Path.GetFullPath(current));
			var to = WithSeparator(Path.GetFullPath(target));
			var comparison = Chimera.Common.OSTailoredCode.IsUnixHost ? StringComparison.Ordinal : StringComparison.OrdinalIgnoreCase;
			if (string.Equals(from, to, comparison)) return DataDirectoryChoice.Same;
			if (to.StartsWith(from, comparison) || from.StartsWith(to, comparison)) return DataDirectoryChoice.Nested;
			if (!Writable(target)) return DataDirectoryChoice.Unwritable;
			return HoldsChimeraData(target) ? DataDirectoryChoice.HoldsChimeraData : DataDirectoryChoice.Empty;
		}

		private static string WithSeparator(string path)
			=> path.TrimEnd(Path.DirectorySeparatorChar, Path.AltDirectorySeparatorChar) + Path.DirectorySeparatorChar;

		/// <summary>Whether a file can be made there. Creates the directory, which is what choosing it means.</summary>
		public static bool Writable(string dir)
		{
			try
			{
				Directory.CreateDirectory(dir);
				var probe = Path.Combine(dir, ".chimera-write-test-" + Path.GetRandomFileName());
				File.WriteAllBytes(probe, [ ]);
				File.Delete(probe);
				return true;
			}
			catch (Exception ex) when (ex is IOException or UnauthorizedAccessException or NotSupportedException or ArgumentException)
			{
				return false;
			}
		}

		/// <summary>Bytes under a directory; what cannot be read counts as nothing.</summary>
		public static long SizeOf(string dir)
		{
			long total = 0;
			try
			{
				if (!Directory.Exists(dir)) return 0;
				foreach (var file in Directory.EnumerateFiles(dir, "*", SearchOption.AllDirectories))
				{
					try { total += new FileInfo(file).Length; }
					catch (Exception ex) when (ex is IOException or UnauthorizedAccessException) { }
				}
			}
			catch (Exception ex) when (ex is IOException or UnauthorizedAccessException) { }
			return total;
		}

		/// <summary>
		/// Moves everything in <paramref name="from"/> into <paramref name="to"/>. Returns null when
		/// it is done, or why it was not - and then <paramref name="from"/> is as it was and nothing
		/// of it is left in <paramref name="to"/>.
		/// </summary>
		/// <param name="progress">bytes done, bytes in all; only a move between volumes has anything to report</param>
		/// <param name="rename">how one entry is renamed into place; replaced by tests to stand in for a second volume</param>
		public static string? Move(string from, string to, Action<long, long>? progress = null, Func<bool>? cancelled = null,
			Action<string, string>? rename = null)
		{
			rename ??= static (a, b) =>
			{
				if (Directory.Exists(a)) Directory.Move(a, b);
				else File.Move(a, b);
			};
			if (!Directory.Exists(from)) return null; // nothing was ever kept: there is nothing to move
			List<(string From, string To, bool Copied)> done = new();
			try
			{
				Directory.CreateDirectory(to);
				var entries = Directory.EnumerateFileSystemEntries(from).ToList();
				foreach (var entry in entries)
				{
					if (File.Exists(Path.Combine(to, Path.GetFileName(entry))) || Directory.Exists(Path.Combine(to, Path.GetFileName(entry))))
					{
						throw new IOException($"{Path.Combine(to, Path.GetFileName(entry))} is already there");
					}
				}
				var total = SizeOf(from);
				long moved = 0;
				foreach (var entry in entries)
				{
					if (cancelled?.Invoke() is true) throw new OperationCanceledException();
					var dest = Path.Combine(to, Path.GetFileName(entry));
					var size = Directory.Exists(entry) ? SizeOf(entry) : new FileInfo(entry).Length;
					try
					{
						rename(entry, dest); // the same volume: no bytes move
						done.Add((entry, dest, Copied: false));
						moved += size;
						progress?.Invoke(moved, total);
					}
					catch (IOException)
					{
						// another volume: copy, check, and only then is the original let go
						done.Add((entry, dest, Copied: true));
						var before = moved;
						Copy(entry, dest, n => progress?.Invoke(before + n, total), cancelled);
						if (SizeOf(dest) != size && Directory.Exists(entry)) throw new IOException($"{dest} is not the size of {entry}");
						moved += size;
					}
				}
				// everything is in the new place: now, and not before, the copied originals go.
				// A failure here leaves a stray copy behind and loses nothing.
				foreach (var (source, _, copied) in done.Where(static d => d.Copied))
				{
					try
					{
						if (Directory.Exists(source)) Directory.Delete(source, recursive: true);
						else File.Delete(source);
					}
					catch (Exception ex) when (ex is IOException or UnauthorizedAccessException)
					{
						Console.WriteLine($"[data] {source} was copied and could not be removed: {ex.Message}");
					}
				}
				try { if (!Directory.EnumerateFileSystemEntries(from).Any()) Directory.Delete(from); }
				catch (Exception ex) when (ex is IOException or UnauthorizedAccessException) { }
				return null;
			}
			catch (Exception ex) when (ex is IOException or UnauthorizedAccessException or OperationCanceledException or NotSupportedException)
			{
				// put back what was renamed, remove what was copied: the old place was never less than whole
				foreach (var (source, dest, copied) in Enumerable.Reverse(done))
				{
					try
					{
						if (copied)
						{
							if (Directory.Exists(dest)) Directory.Delete(dest, recursive: true);
							else if (File.Exists(dest)) File.Delete(dest);
						}
						else
						{
							rename(dest, source);
						}
					}
					catch (Exception undo) when (undo is IOException or UnauthorizedAccessException)
					{
						Console.WriteLine($"[data] could not undo {dest}: {undo.Message}");
					}
				}
				return ex is OperationCanceledException ? "cancelled" : ex.Message;
			}
		}

		private static void Copy(string from, string to, Action<long> copiedSoFar, Func<bool>? cancelled)
		{
			long copied = 0;
			if (File.Exists(from))
			{
				File.Copy(from, to);
				copiedSoFar(new FileInfo(to).Length);
				return;
			}
			Directory.CreateDirectory(to);
			foreach (var dir in Directory.EnumerateDirectories(from, "*", SearchOption.AllDirectories))
			{
				Directory.CreateDirectory(Path.Combine(to, dir.Substring(from.Length).TrimStart(Path.DirectorySeparatorChar, Path.AltDirectorySeparatorChar)));
			}
			foreach (var file in Directory.EnumerateFiles(from, "*", SearchOption.AllDirectories))
			{
				if (cancelled?.Invoke() is true) throw new OperationCanceledException();
				var dest = Path.Combine(to, file.Substring(from.Length).TrimStart(Path.DirectorySeparatorChar, Path.AltDirectorySeparatorChar));
				File.Copy(file, dest);
				File.SetLastWriteTimeUtc(dest, File.GetLastWriteTimeUtc(file)); // the cache manager reads ages from these
				copied += new FileInfo(dest).Length;
				copiedSoFar(copied);
			}
		}

		/// <summary>
		/// A change somebody asked for, to be carried out by the next start. Returns what to tell
		/// them, or null when there was nothing pending or it simply worked.
		/// </summary>
		/// <param name="move">carries the data over; false adopts or starts fresh at the new place and leaves the old alone</param>
		public static string? ApplyPending(Config config, Func<string, string, string?> move)
		{
			var pending = config.DataDirectoryPending;
			if (pending is null) return null;
			var carry = config.DataDirectoryPendingMove;
			config.DataDirectoryPending = null;
			config.DataDirectoryPendingMove = false;

			var from = ProjectCache.DataHomeFor(config.DataDirectory);
			var to = ProjectCache.DataHomeFor(pending);
			if (Inspect(from, to) is DataDirectoryChoice.Same) { config.DataDirectory = pending; return null; }
			if (Inspect(from, to) is DataDirectoryChoice.Nested or DataDirectoryChoice.Unwritable)
			{
				return $"The data directory was not changed: {to} cannot be used. It is still {from}.";
			}
			if (carry && move(from, to) is { } why)
			{
				return $"The data directory was not moved ({why}). It is still {from}, and nothing in it was changed.";
			}
			config.DataDirectory = pending;
			return null;
		}
	}
}
