using System;
using System.IO;
using System.Linq;

using Chimera.Client.Common;
using Chimera.Common;

namespace Chimera.Tests.Client.Common
{
	/// <summary>
	/// What a crash no handler sees leaves behind (docs/project.md, "Crash notes"): the note the
	/// crash module writes from WerFault.exe is read back into what failed, where and in whose code,
	/// matched to the session it ended, and never piles up.
	/// </summary>
	[TestClass]
	public class CrashCaptureTests
	{
		private const string DriverFastFail = "Chimera crash note\n"
			+ "time=2026-09-13 13:38:03\n"
			+ "process=4242\n"
			+ "code=0xc0000409\n"
			+ "kind=fast fail (the process ended itself; no handler runs)\n"
			+ "fastfail=7 FATAL_APP_EXIT\n"
			+ "address=0x7ffb1108eb9d\n"
			+ "module=nvoglv64.dll\n"
			+ "offset=0x108eb9d\n"
			+ "fatal=1\n"
			+ "frame=21\n"
			+ "report=611c0df5-873c-4c26-bfe6-407cc10bb9a0\n"
			+ "\n"
			+ "session:\n"
			+ "  project=C:\\TAS\\nss102.chimeraProject\n"
			+ "  system=Flash\n"
			+ "\n"
			+ "stack (faulting thread):\n"
			+ "  #00 nvoglv64.dll+0x108eb9d\n"
			+ "  #01 KERNEL32.DLL+0x1259d  BaseThreadInitThunk+0x1d\n"
			+ "\n"
			+ "dump=written\n";

		private string _dir = "";

		[TestInitialize]
		public void Setup()
		{
			_dir = Path.Combine(Path.GetTempPath(), "chimera-crash-" + Path.GetRandomFileName());
			Directory.CreateDirectory(_dir);
		}

		[TestCleanup]
		public void Teardown()
		{
			try { Directory.Delete(_dir, recursive: true); } catch { }
		}

		private string WriteNote(string stem, string text, bool dump = true)
		{
			var path = Path.Combine(_dir, stem + ".txt");
			File.WriteAllText(path, text);
			if (dump) File.WriteAllText(Path.ChangeExtension(path, ".dmp"), "MDMP");
			return path;
		}

		[TestMethod]
		public void ANoteSaysWhatFailedWhereAndInWhoseCode()
		{
			var path = WriteNote("2026-09-13 13.38.03 pid4242", DriverFastFail);
			var note = CrashCapture.Read(path);

			Assert.IsNotNull(note);
			Assert.AreEqual(4242, note!.ProcessId);
			Assert.AreEqual("fast fail in nvoglv64.dll+0x108eb9d (the NVIDIA graphics driver) at frame 21", note.Summary);
			StringAssert.Contains(note.Session, "project=C:\\TAS\\nss102.chimeraProject");
			StringAssert.StartsWith(note.Stack, "#00 nvoglv64.dll+0x108eb9d");
			Assert.AreEqual("written", note.Field("dump"), "a field after the stack is still a field");
			Assert.AreEqual(Path.ChangeExtension(path, ".dmp"), note.DumpPath);
		}

		[TestMethod]
		public void ANoteWithoutAFrameOrAKnownModuleStillReads()
		{
			var note = CrashCapture.Parse("Chimera crash note\r\nprocess=7\r\ncode=0xc0000005\r\nkind=access violation\r\nmodule=chimera.dll\r\noffset=0x10\r\nframe=0\r\n", "x.txt");

			Assert.IsNotNull(note);
			Assert.AreEqual("access violation in chimera.dll+0x10", note!.Summary);
			Assert.IsNull(note.Time);
			Assert.IsNull(note.DumpPath);
		}

		[TestMethod]
		public void AnythingElseIsNotANote()
		{
			Assert.IsNull(CrashCapture.Parse("time=2026-09-13 13:38:03\nprocess=1\n", "x.txt"));
			Assert.IsNull(CrashCapture.Parse("", "x.txt"));
		}

		[TestMethod]
		public void TheNoteIsTheProcesssOwnNotAnEarlierOneWithTheSameId()
		{
			WriteNote("2026-09-13 13.38.03 pid4242", DriverFastFail);
			var before = new DateTime(2026, 9, 13, 13, 30, 0, DateTimeKind.Local).ToUniversalTime();
			var after = new DateTime(2026, 9, 13, 13, 40, 0, DateTimeKind.Local).ToUniversalTime();

			Assert.IsNotNull(CrashCapture.NoteFor(4242, before, _dir), "the process that started before the crash");
			Assert.IsNull(CrashCapture.NoteFor(4242, after, _dir), "a later process that was given the same id");
			Assert.IsNull(CrashCapture.NoteFor(4243, before, _dir), "another process");
			Assert.IsNull(CrashCapture.NoteFor(4242, before, Path.Combine(_dir, "none")), "no folder");
		}

		[TestMethod]
		public void OnlyTheNewestNotesAreKeptAndTheirDumpsGoWithThem()
		{
			for (var day = 10; day <= 14; day++) WriteNote($"2026-09-{day} 12.00.00 pid{day}", DriverFastFail);
			File.WriteAllText(Path.Combine(_dir, "readme.md"), "not a note");

			CrashCapture.Prune(_dir, keep: 2);

			var left = Directory.GetFiles(_dir).Select(Path.GetFileName).OrderBy(n => n, StringComparer.Ordinal).ToArray();
			CollectionAssert.AreEqual(new[]
			{
				"2026-09-13 12.00.00 pid13.dmp",
				"2026-09-13 12.00.00 pid13.txt",
				"2026-09-14 12.00.00 pid14.dmp",
				"2026-09-14 12.00.00 pid14.txt",
				"readme.md",
			}, left);
		}

		[TestMethod]
		public void TheBlockIsLaidOutAsTheModuleReadsIt()
		{
			// source/crash/chimera_crash.c: magic, version, folder[520] (UTF-16), frame, session length, session
			Assert.AreEqual(1048, CrashCapture.FrameOffset);
			Assert.AreEqual(1056, CrashCapture.SessionLengthOffset);
			Assert.AreEqual(1060, CrashCapture.SessionOffset);
			Assert.AreEqual(17440, CrashCapture.BlockSize);
		}

		[TestMethod]
		public void OffWindowsNothingIsArmedAndItSaysWhy()
		{
			if (!OSTailoredCode.IsUnixHost) Assert.Inconclusive("Windows arms for real; covered by the crash run");
			Assert.IsNotNull(CrashCapture.Arm(_dir));
			Assert.IsFalse(CrashCapture.Armed);
			CrashCapture.NoteFrame(5);
			CrashCapture.DescribeSession("nothing to write to, and nothing thrown");
		}

		[TestMethod]
		public void AFolderLeftByACrashNamesTheProcessThatLeftIt()
		{
			var dir = Path.Combine(_dir, "recovery");
			Directory.CreateDirectory(dir);
			File.WriteAllText(Path.Combine(dir, "snapshot.chimeraProject"), "{}");
			var started = new DateTime(2026, 9, 13, 11, 0, 0, DateTimeKind.Utc);
			ProjectRecovery.WriteSession(dir, new ProjectRecovery.SessionRecord
			{
				ProcessId = int.MaxValue - 7, // not running anywhere
				ProcessStartedUtcTicks = started.Ticks,
				ProjectPath = "p.chimeraProject",
			});

			var leftover = ProjectRecovery.FindUnfinishedIn(dir);

			Assert.IsNotNull(leftover);
			Assert.AreEqual(int.MaxValue - 7, leftover!.ProcessId);
			Assert.AreEqual(started, leftover.ProcessStartedUtc);
		}
	}
}
