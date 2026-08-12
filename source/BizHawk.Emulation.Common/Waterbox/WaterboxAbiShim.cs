using System.Collections.Generic;
using System.Runtime.InteropServices;

using BizHawk.Common;

namespace BizHawk.Emulation.Common.Waterbox
{
	/// <summary>
	/// Bridges the host's calling convention to the guest's.
	///
	/// A waterbox guest is ALWAYS sysv64 - it is a Linux ELF, whatever the host
	/// runs on. On Linux that matches the host and there is nothing to do. On
	/// Windows the CLR calls with win64, so arguments would go out in RCX/RDX/R8/R9
	/// while the guest reads them from RDI/RSI/RDX/RCX: the guest would run on
	/// garbage and fault on the first pointer it touched. That is not a subtle
	/// desync, it is an instant crash the moment any guest code runs.
	///
	/// miniBox exports "depart" trampolines - win64 entry points that call a sysv64
	/// target taken from RAX - one per argument count. This wraps a guest entry
	/// point in a stub that loads the target into RAX and jumps to the right one:
	///
	///     48 B8 &lt;target&gt;   mov rax, imm64
	///     49 BB &lt;departN&gt;   mov r11, imm64
	///     41 FF E3          jmp r11
	///
	/// (jumping through r11 rather than a rel32, since the trampoline may be far
	/// away; r11 is caller-saved in both conventions and unused for arguments.)
	/// </summary>
	public sealed class WaterboxAbiShim : IDisposable
	{
		private const int MAX_ARGS = 6; // depart0..depart6, matching miniBox
		private const int STUB_SIZE = 32;
		private const int STUB_COUNT = 256;

		private readonly bool _passthrough;
		private readonly IntPtr[] _departs = new IntPtr[MAX_ARGS + 1];
		private readonly Dictionary<(IntPtr Target, int ArgCount), IntPtr> _stubs = new();
		private IntPtr _stubPage;
		private int _stubsUsed;

		public WaterboxAbiShim(IImportResolver hostLib)
		{
			// Unix: host and guest are both sysv64, so every wrap is the identity.
			_passthrough = OSTailoredCode.IsUnixHost;
			if (_passthrough) return;

			for (int i = 0; i <= MAX_ARGS; i++)
			{
				_departs[i] = hostLib.GetProcAddrOrThrow($"depart{i}");
			}

			_stubPage = VirtualAlloc(IntPtr.Zero, (UIntPtr)(STUB_SIZE * STUB_COUNT),
				MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
			if (_stubPage == IntPtr.Zero) throw new InvalidOperationException("could not allocate waterbox ABI stubs");
		}

		/// <summary>
		/// Returns a pointer callable with the HOST convention that lands on
		/// <paramref name="guestEntry"/> with the guest's. Identity on Unix.
		/// </summary>
		public IntPtr Wrap(IntPtr guestEntry, int argCount)
		{
			if (_passthrough || guestEntry == IntPtr.Zero) return guestEntry;
			if (argCount is < 0 or > MAX_ARGS)
			{
				throw new ArgumentOutOfRangeException(nameof(argCount), $"waterbox guest calls take at most {MAX_ARGS} arguments");
			}

			if (_stubs.TryGetValue((guestEntry, argCount), out var existing)) return existing;
			if (_stubsUsed == STUB_COUNT) throw new InvalidOperationException("out of waterbox ABI stubs");

			var stub = _stubPage + (_stubsUsed++ * STUB_SIZE);
			var code = new byte[13];
			code[0] = 0x48; code[1] = 0xB8;                                  // mov rax, imm64
			BitConverter.GetBytes((long)guestEntry).CopyTo(code, 2);
			code[10] = 0x49; code[11] = 0xBB;                                // mov r11, imm64
			var tail = new byte[11];
			BitConverter.GetBytes((long)_departs[argCount]).CopyTo(tail, 0);
			tail[8] = 0x41; tail[9] = 0xFF; tail[10] = 0xE3;                 // jmp r11
			Marshal.Copy(code, 0, stub, 12);
			Marshal.Copy(tail, 0, stub + 12, 11);

			_stubs[(guestEntry, argCount)] = stub;
			return stub;
		}

		public void Dispose()
		{
			if (_stubPage != IntPtr.Zero)
			{
				VirtualFree(_stubPage, UIntPtr.Zero, MEM_RELEASE);
				_stubPage = IntPtr.Zero;
			}
		}

		private const uint MEM_COMMIT = 0x1000, MEM_RESERVE = 0x2000, MEM_RELEASE = 0x8000;
		private const uint PAGE_EXECUTE_READWRITE = 0x40;

		[DllImport("kernel32.dll", SetLastError = true)]
		private static extern IntPtr VirtualAlloc(IntPtr addr, UIntPtr size, uint type, uint protect);

		[DllImport("kernel32.dll", SetLastError = true)]
		private static extern bool VirtualFree(IntPtr addr, UIntPtr size, uint type);
	}
}
