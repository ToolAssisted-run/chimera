#nullable enable

using System;
using System.Collections.Generic;
using System.Globalization;
using System.IO;
using System.Linq;
using System.Runtime.InteropServices;
using System.Text;

using Chimera.Common;

using Microsoft.Win32;

namespace Chimera.Client.Common
{
	/// <summary>
	/// What a crash that no handler sees leaves behind (docs/project.md, "Crash
	/// notes"). A graphics driver that fast-fails ends the process before any
	/// code of Chimera's can run, so the note is written from outside it: Windows
	/// Error Reporting loads <c>chimera_crash.dll</c> into WerFault.exe, and that
	/// module reads the block this class keeps in Chimera's memory - where to
	/// write, the frame, a few lines about the session - and writes a note and a
	/// minidump into <see cref="Root"/>. Nothing here is needed for the work to
	/// survive (the recovery journal is); it is what says why it had to.
	/// </summary>
	public static class CrashCapture
	{
		public const string ModuleFileName = "chimera_crash.dll";

		/// <summary>
		/// Where Windows allows a module to be a helper for a process that asks. A
		/// per-user list, so no administrator is needed; values are module paths.
		/// </summary>
		internal const string HelperModulesKey = @"Software\Microsoft\Windows\Windows Error Reporting\RuntimeExceptionHelperModules";

		// The block, laid out exactly as source/crash/chimera_crash.c reads it.
		internal const uint Magic = 0x52434843; // "CHCR"
		internal const uint Version = 2;
		internal const int FolderChars = 520;
		internal const int FrameOffset = 8 + FolderChars * 2;
		internal const int GlRecorderOffset = FrameOffset + 8;
		internal const int GlRecorderBytesOffset = GlRecorderOffset + 8;
		internal const int SessionLengthOffset = GlRecorderBytesOffset + 4;
		internal const int SessionOffset = SessionLengthOffset + 4;
		internal const int SessionCapacity = 16380;
		internal const int BlockSize = SessionOffset + SessionCapacity;

		/// <summary>Notes (with their dumps) older than the newest this many are removed when a session starts.</summary>
		internal const int NotesKept = 20;

		private static readonly object SessionGate = new();

		/// <summary>Never freed: it is read when the process is already gone.</summary>
		private static IntPtr _block;

		public static string Root => Path.Combine(ProjectCache.DataHome, "Crashes");

		public static bool Armed => _block != IntPtr.Zero;

		[DllImport("kernel32.dll", CharSet = CharSet.Unicode, ExactSpelling = true)]
		private static extern int WerRegisterRuntimeExceptionModule(string pwszOutOfProcessCallbackDll, IntPtr pContext);

		/// <summary>
		/// Asks Windows to run the crash module if this process dies. Returns null
		/// when armed, or why not - never throws: a crash note is worth having, not
		/// worth failing a start over.
		/// </summary>
		public static string? Arm(string moduleDirectory)
		{
			if (OSTailoredCode.IsUnixHost) return "crash notes are written by Windows Error Reporting";
			if (_block != IntPtr.Zero) return null;
			try
			{
				var module = Path.GetFullPath(Path.Combine(moduleDirectory, ModuleFileName));
				if (!File.Exists(module)) return $"{module} is missing";
				var root = Path.GetFullPath(Root);
				if (root.Length >= FolderChars) return $"the crash folder's path is too long: {root}";
				Directory.CreateDirectory(root);
				using (var key = Registry.CurrentUser.CreateSubKey(HelperModulesKey))
				{
					if (key is null) return $"HKCU\\{HelperModulesKey} could not be opened";
					ForgetMissingModules(key);
					if (key.GetValue(module) is not int) key.SetValue(module, 0, RegistryValueKind.DWord);
				}

				var block = Marshal.AllocHGlobal(BlockSize);
				Marshal.Copy(new byte[BlockSize], 0, block, BlockSize);
				Marshal.WriteInt32(block, 0, unchecked((int)Magic));
				Marshal.WriteInt32(block, 4, unchecked((int)Version));
				var folder = Encoding.Unicode.GetBytes(root);
				Marshal.Copy(folder, 0, block + 8, folder.Length);
				var hr = WerRegisterRuntimeExceptionModule(module, block);
				if (hr < 0)
				{
					Marshal.FreeHGlobal(block);
					return $"Windows refused the crash module (0x{hr:X8})";
				}
				_block = block;
				Prune(root, NotesKept);
				return null;
			}
			catch (Exception ex) when (ex is IOException or UnauthorizedAccessException or System.Security.SecurityException
				or ArgumentException or NotSupportedException or EntryPointNotFoundException)
			{
				return ex.Message;
			}
		}

		/// <summary>
		/// Registry values left by a Chimera that is no longer where it was (a
		/// moved install, a test copy that was deleted). Only this module's name
		/// is touched; other programs' helpers are theirs.
		/// </summary>
		private static void ForgetMissingModules(RegistryKey key)
		{
			foreach (var name in key.GetValueNames())
			{
				if (!string.Equals(Path.GetFileName(name), ModuleFileName, StringComparison.OrdinalIgnoreCase)) continue;
				if (!File.Exists(name)) key.DeleteValue(name, throwOnMissingValue: false);
			}
		}

		/// <summary>The frame a crash note names. Cheap enough for every frame.</summary>
		public static void NoteFrame(long frame)
		{
			if (_block != IntPtr.Zero) Marshal.WriteInt64(_block, FrameOffset, frame);
		}

		/// <summary>
		/// Where the GPU bridge keeps its flight recorder (the engine's
		/// ce_gl_flight_recorder): the crash module reads the last GL calls out of
		/// it. A fixed block in libchimera, so noting it once is enough.
		/// </summary>
		public static void NoteGlRecorder(IntPtr address, uint bytes)
		{
			if (_block == IntPtr.Zero) return;
			Marshal.WriteInt32(_block, GlRecorderBytesOffset, 0);
			Marshal.WriteInt64(_block, GlRecorderOffset, address.ToInt64());
			Marshal.WriteInt32(_block, GlRecorderBytesOffset, address == IntPtr.Zero ? 0 : unchecked((int)bytes));
		}

		/// <summary>
		/// The lines a crash note carries under "session:" - the project, the core
		/// and its build, what was being done. Replaces what was there.
		/// </summary>
		public static void DescribeSession(string text)
		{
			if (_block == IntPtr.Zero) return;
			var bytes = Encoding.UTF8.GetBytes(text);
			var length = Math.Min(bytes.Length, SessionCapacity);
			lock (SessionGate)
			{
				// the length says 0 while the bytes change, so a crash between the two reads nothing half written
				Marshal.WriteInt32(_block, SessionLengthOffset, 0);
				Marshal.Copy(bytes, 0, _block + SessionOffset, length);
				Marshal.WriteInt32(_block, SessionLengthOffset, length);
			}
		}

		/// <summary>One note, as the crash module wrote it.</summary>
		public sealed class Note
		{
			public string Path { get; init; } = "";

			/// <summary>The minidump beside the note, when there is one.</summary>
			public string? DumpPath { get; init; }

			public IReadOnlyDictionary<string, string> Fields { get; init; } = new Dictionary<string, string>();

			public string Session { get; init; } = "";

			public string Stack { get; init; } = "";

			/// <summary>Local time, as the note says it.</summary>
			public DateTime? Time { get; init; }

			public int ProcessId { get; init; }

			public string Field(string name) => Fields.TryGetValue(name, out var value) ? value : "";

			/// <summary>One line a person can read: what failed and where.</summary>
			public string Summary
			{
				get
				{
					var kind = Field("kind");
					var cut = kind.IndexOf(" (", StringComparison.Ordinal);
					if (cut > 0) kind = kind.Substring(0, cut);
					if (kind.Length is 0) kind = $"exception {Field("code")}";
					var module = Field("module");
					var where = module.Length is 0 ? "" : $" in {module}+{Field("offset")}";
					var who = WhoseModule(module);
					if (who is not null) where += $" ({who})";
					var frame = Field("frame");
					var at = frame.Length is 0 || frame == "0" ? "" : $" at frame {frame}";
					return $"{kind}{where}{at}";
				}
			}
		}

		/// <summary>Names a module a person may recognise. Null for any other.</summary>
		internal static string? WhoseModule(string module)
		{
			var m = module.ToLowerInvariant();
			if (m.StartsWith("nvoglv", StringComparison.Ordinal) || m.StartsWith("nvwgf", StringComparison.Ordinal)
				|| m.StartsWith("nvd3dum", StringComparison.Ordinal) || m.StartsWith("nvlddmkm", StringComparison.Ordinal))
				return "the NVIDIA graphics driver";
			if (m.StartsWith("atiogl", StringComparison.Ordinal) || m.StartsWith("atig6", StringComparison.Ordinal)
				|| m.StartsWith("amdxx", StringComparison.Ordinal) || m.StartsWith("aticfx", StringComparison.Ordinal)
				|| m.StartsWith("amdvlk", StringComparison.Ordinal))
				return "the AMD graphics driver";
			if (m.StartsWith("ig", StringComparison.Ordinal) && (m.Contains("icd") || m.Contains("xel") || m.Contains("d3d") || m.Contains("gl")))
				return "the Intel graphics driver";
			if (m.StartsWith("vulkan-1", StringComparison.Ordinal) || m == "opengl32.dll") return "the graphics system";
			return null;
		}

		public static Note? Read(string path)
		{
			try
			{
				return Parse(File.ReadAllText(path, Encoding.UTF8), path);
			}
			catch (Exception ex) when (ex is IOException or UnauthorizedAccessException)
			{
				return null;
			}
		}

		internal static Note? Parse(string text, string path)
		{
			var lines = text.Replace("\r\n", "\n").Split('\n');
			if (lines.Length is 0 || lines[0] != "Chimera crash note") return null;
			var fields = new Dictionary<string, string>(StringComparer.Ordinal);
			var session = new StringBuilder();
			var stack = new StringBuilder();
			StringBuilder? section = null;
			for (var i = 1; i < lines.Length; i++)
			{
				var line = lines[i];
				if (line.Length is 0) continue;
				if (line == "session:")
				{
					section = session;
					continue;
				}
				if (line.StartsWith("stack", StringComparison.Ordinal) && line.EndsWith(":", StringComparison.Ordinal))
				{
					section = stack;
					continue;
				}
				if (section is not null && line.StartsWith("  ", StringComparison.Ordinal))
				{
					section.Append(line.Substring(2)).Append('\n');
					continue;
				}
				section = null;
				var eq = line.IndexOf('=');
				if (eq > 0) fields[line.Substring(0, eq)] = line.Substring(eq + 1);
			}
			DateTime? time = fields.TryGetValue("time", out var t)
				&& DateTime.TryParseExact(t, "yyyy-MM-dd HH:mm:ss", CultureInfo.InvariantCulture, DateTimeStyles.AssumeLocal, out var parsed)
					? parsed
					: null;
			var dump = System.IO.Path.ChangeExtension(path, ".dmp");
			return new Note
			{
				Path = path,
				DumpPath = File.Exists(dump) ? dump : null,
				Fields = fields,
				Session = session.ToString(),
				Stack = stack.ToString(),
				Time = time,
				ProcessId = fields.TryGetValue("process", out var p) && int.TryParse(p, NumberStyles.None, CultureInfo.InvariantCulture, out var pid) ? pid : 0,
			};
		}

		/// <summary>Every note in <paramref name="directory"/>, newest first.</summary>
		public static IReadOnlyList<Note> NotesIn(string directory)
		{
			if (!Directory.Exists(directory)) return Array.Empty<Note>();
			return Directory.GetFiles(directory, "*.txt")
				.OrderByDescending(f => System.IO.Path.GetFileName(f), StringComparer.Ordinal)
				.Select(Read)
				.Where(n => n is not null)
				.ToList()!;
		}

		/// <summary>
		/// The note left by the process <paramref name="processId"/> that started
		/// at <paramref name="startedUtc"/>, if it crashed - a process id alone is
		/// reused, so the note must also be no older than the process.
		/// </summary>
		public static Note? NoteFor(int processId, DateTime startedUtc, string? directory = null)
		{
			var started = startedUtc.ToLocalTime().AddSeconds(-2); // the note's time has whole seconds
			return NotesIn(directory ?? Root).FirstOrDefault(n => n.ProcessId == processId && n.Time is { } time && time >= started);
		}

		/// <summary>Keeps the newest <paramref name="keep"/> notes; older notes go with their dumps.</summary>
		internal static void Prune(string directory, int keep)
		{
			try
			{
				var notes = Directory.GetFiles(directory, "*.txt")
					.OrderByDescending(f => System.IO.Path.GetFileName(f), StringComparer.Ordinal)
					.Skip(keep);
				foreach (var note in notes)
				{
					File.Delete(System.IO.Path.ChangeExtension(note, ".dmp"));
					File.Delete(note);
				}
			}
			catch (Exception ex) when (ex is IOException or UnauthorizedAccessException)
			{
				// the next start tries again
			}
		}
	}
}
