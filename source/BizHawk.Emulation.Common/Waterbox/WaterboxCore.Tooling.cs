using System.Collections.Generic;
using System.Runtime.InteropServices;
using System.Text;

namespace BizHawk.Emulation.Common.Waterbox
{
	/// <summary>
	/// The optional half of the guest ABI: CORE-MANAGED TOOLING. A core may export
	/// any subset of four independent groups, and the adapter exposes exactly the
	/// services the groups it finds can back:
	///
	/// <list type="bullet">
	/// <item>surfaces  -> <see cref="ICoreSurfaces"/> (core-rendered viewer windows)</item>
	/// <item>registers -> <see cref="IDebuggable"/> (the generic debugger's register box)</item>
	/// <item>buses     -> extra <see cref="IMemoryDomains"/> entries (peek/poke address spaces)</item>
	/// <item>trace     -> <see cref="ITraceable"/> (the trace logger)</item>
	/// </list>
	///
	/// Nothing here is core-specific: the frontend never learns what a nametable or
	/// a 6502 is. Absent exports simply mean the tool is unavailable for that core,
	/// which the service provider communicates by not registering the service.
	/// </summary>
	public sealed partial class WaterboxCore : IDebuggable, ITraceable, ICoreSurfaces
	{
		[UnmanagedFunctionPointer(CallingConvention.Cdecl)] private delegate IntPtr NameFn(int i);
		[UnmanagedFunctionPointer(CallingConvention.Cdecl)] private delegate int IntIntFn(int i);
		[UnmanagedFunctionPointer(CallingConvention.Cdecl)] private delegate long LongIntFn(int i);
		[UnmanagedFunctionPointer(CallingConvention.Cdecl)] private delegate long LongFn();
		[UnmanagedFunctionPointer(CallingConvention.Cdecl)] private delegate IntPtr PtrIntFn(int i);
		[UnmanagedFunctionPointer(CallingConvention.Cdecl)] private delegate void VoidIntFn(int i);
		[UnmanagedFunctionPointer(CallingConvention.Cdecl)] private delegate void SetRegFn(int i, long value);
		[UnmanagedFunctionPointer(CallingConvention.Cdecl)] private delegate int PeekFn(int bus, int addr);
		[UnmanagedFunctionPointer(CallingConvention.Cdecl)] private delegate void PokeFn(int bus, int addr, int value);
		[UnmanagedFunctionPointer(CallingConvention.Cdecl)] private delegate void VoidFn();

		// Each group's "is this core capable" flag IS the nullness of its mandatory
		// delegate: null means the export wasn't there.

		// surfaces
		private PtrIntFn? _renderSurface;
		private string[] _surfaceNames = [ ];
		private int[] _surfaceWidths = [ ];
		private int[] _surfaceHeights = [ ];
		private int[][] _surfaceBuffs = [ ];

		// registers
		private LongIntFn? _regValue;
		private SetRegFn? _regSet;
		private LongFn? _executedCycles;
		private string[] _regNames = [ ];
		private byte[] _regBits = [ ];

		// trace
		private VoidIntFn? _traceSetEnabled;
		private IntFn? _traceLineCount;
		private GetPtrFn? _traceBuffer;
		private IntFn? _traceOverflow;
		private IntFn? _traceUsedBytes;
		private VoidFn? _traceClear;
		private ITraceSink? _traceSink;
		private bool _traceOverflowed;

		/// <summary>
		/// Probes for each optional group after Init (the guest must be sealed and
		/// running, since names and counts may depend on the rom and settings), and
		/// unregisters the services no group could back. The services are implemented
		/// unconditionally on this type, so <see cref="BasicServiceProvider"/> would
		/// otherwise advertise all of them for every core.
		/// </summary>
		private void InitTooling(BasicServiceProvider services, List<MemoryDomain> domains)
		{
			InitSurfaces();
			InitRegisters();
			InitBuses(domains);
			InitTrace();

			if (_renderSurface == null) services.Unregister<ICoreSurfaces>();
			if (_regValue == null) services.Unregister<IDebuggable>();
			if (_traceSetEnabled == null) services.Unregister<ITraceable>();
		}

		private void InitSurfaces()
		{
			var count = TryProc<IntFn>("GetSurfaceCount");
			var name = TryProc<NameFn>("GetSurfaceName");
			var width = TryProc<IntIntFn>("GetSurfaceWidth");
			var height = TryProc<IntIntFn>("GetSurfaceHeight");
			var render = TryProc<PtrIntFn>("RenderSurface");
			if (count == null || name == null || width == null || height == null || render == null) return;

			int n = count();
			if (n <= 0) return;

			_surfaceNames = new string[n];
			_surfaceWidths = new int[n];
			_surfaceHeights = new int[n];
			_surfaceBuffs = new int[n][];
			for (int i = 0; i < n; i++)
			{
				_surfaceNames[i] = Marshal.PtrToStringAnsi(name(i)) ?? $"Surface {i}";
				_surfaceWidths[i] = width(i);
				_surfaceHeights[i] = height(i);
				_surfaceBuffs[i] = new int[_surfaceWidths[i] * _surfaceHeights[i]];
			}
			_renderSurface = render;
		}

		private void InitRegisters()
		{
			var count = TryProc<IntFn>("GetRegisterCount");
			var name = TryProc<NameFn>("GetRegisterName");
			var value = TryProc<LongIntFn>("GetRegisterValue");
			if (count == null || name == null || value == null) return;

			int n = count();
			if (n <= 0) return;

			// Width is per-register and only the core knows it (an 8-bit accumulator
			// next to a 16-bit PC). Optional: cores that don't say get 32 bits, which
			// only affects how many hex digits the debugger shows.
			var bits = TryProc<IntIntFn>("GetRegisterBits");
			_regNames = new string[n];
			_regBits = new byte[n];
			for (int i = 0; i < n; i++)
			{
				_regNames[i] = Marshal.PtrToStringAnsi(name(i)) ?? $"R{i}";
				int b = bits?.Invoke(i) ?? 32;
				_regBits[i] = (byte)(b is > 0 and <= 64 ? b : 32);
			}
			_regValue = value;
			_regSet = TryProc<SetRegFn>("SetRegisterValue");
			_executedCycles = TryProc<LongFn>("GetExecutedCycles");
		}

		/// <summary>
		/// Buses are address SPACES rather than blocks of memory - the guest resolves
		/// each access through its own mapper/mirroring logic - so they can't be
		/// pointer-mapped like the memory domains. They join the domain list as
		/// callback-backed domains, which is what makes them show up in the hex editor
		/// and the watch tools with no further plumbing.
		/// </summary>
		private void InitBuses(List<MemoryDomain> domains)
		{
			var count = TryProc<IntFn>("GetBusCount");
			var name = TryProc<NameFn>("GetBusName");
			var peek = TryProc<PeekFn>("PeekBus");
			if (count == null || name == null || peek == null) return;

			var poke = TryProc<PokeFn>("PokeBus");
			var size = TryProc<LongIntFn>("GetBusSize");
			var busWritable = TryProc<IntIntFn>("GetBusWritable");
			int n = count();
			for (int i = 0; i < n; i++)
			{
				int bus = i; // capture per iteration
				var busName = Marshal.PtrToStringAnsi(name(i)) ?? $"Bus {i}";
				long busSize = size?.Invoke(i) ?? 0x10000; // 64K default: the common case
				// A core with a poke may still have read-only buses; claiming writable
				// for those would give the hex editor a silently discarded edit.
				bool writable = poke != null && (busWritable?.Invoke(i) ?? 1) != 0;
				domains.Add(new MemoryDomainDelegate(
					busName,
					busSize,
					MemoryDomain.Endian.Little,
					// guarded: a domain can outlive the core in a tool that kept a reference
					addr => _obj == IntPtr.Zero ? (byte)0 : (byte)peek(bus, (int)addr),
					!writable ? null : (addr, val) => { if (_obj != IntPtr.Zero) poke(bus, (int)addr, val); },
					1));
			}
		}

		private void InitTrace()
		{
			var setEnabled = TryProc<VoidIntFn>("TraceSetEnabled");
			var lineCount = TryProc<IntFn>("TraceGetLineCount");
			var buffer = TryProc<GetPtrFn>("TraceGetBuffer");
			if (setEnabled == null || lineCount == null || buffer == null) return;

			_traceLineCount = lineCount;
			_traceBuffer = buffer;
			_traceOverflow = TryProc<IntFn>("TraceGetOverflow");
			_traceUsedBytes = TryProc<IntFn>("TraceGetUsedBytes");
			_traceClear = TryProc<VoidFn>("TraceClear");
			_traceHeader = Marshal.PtrToStringAnsi(TryProc<GetPtrFn>("TraceGetHeader")?.Invoke() ?? IntPtr.Zero)
				?? "Instructions";
			_traceSetEnabled = setEnabled;
			_traceSetEnabled(0); // tracing costs the guest time; off until a sink asks
		}

		// ---------------- ICoreSurfaces ----------------

		public IReadOnlyList<string> SurfaceNames => _surfaceNames;

		public void GetSurfaceSize(int index, out int width, out int height)
		{
			width = _surfaceWidths[index];
			height = _surfaceHeights[index];
		}

		public int[] RenderSurface(int index)
		{
			CheckDisposed();
			var render = _renderSurface ?? throw new NotImplementedException($"{_cfg.CoreName} exposes no surfaces");
			var buff = _surfaceBuffs[index];
			var p = render(index);
			if (p != IntPtr.Zero) Marshal.Copy(p, buff, 0, buff.Length);
			return buff;
		}

		// ---------------- IDebuggable ----------------

		public IDictionary<string, RegisterValue> GetCpuFlagsAndRegisters()
		{
			CheckDisposed();
			var read = _regValue ?? throw new NotImplementedException($"{_cfg.CoreName} exposes no registers");
			var dict = new Dictionary<string, RegisterValue>(_regNames.Length);
			for (int i = 0; i < _regNames.Length; i++)
			{
				dict[_regNames[i]] = new RegisterValue((ulong)read(i), _regBits[i]);
			}
			return dict;
		}

		public void SetCpuRegister(string register, int value)
		{
			if (_regSet == null) throw new NotImplementedException($"{_cfg.CoreName} does not support writing registers");
			CheckDisposed();
			for (int i = 0; i < _regNames.Length; i++)
			{
				if (_regNames[i] == register)
				{
					_regSet(i, value);
					return;
				}
			}
			throw new InvalidOperationException($"no such register: {register}");
		}

		// Breakpoints and stepping would need the guest to give up control mid-frame,
		// which the waterbox ABI (one FrameAdvance call, no re-entry) doesn't allow.
		public IMemoryCallbackSystem MemoryCallbacks
			=> throw new NotImplementedException("waterbox cores do not support memory callbacks");

		public bool CanStep(StepType type) => false;

		public void Step(StepType type) => throw new NotImplementedException();

		public long TotalExecutedCycles
			=> _executedCycles?.Invoke() ?? throw new NotImplementedException($"{_cfg.CoreName} does not report a cycle count");

		// ---------------- ITraceable ----------------

		private string _traceHeader = "Instructions";

		public string Header => _traceHeader;

		/// <summary>
		/// Attaching a sink turns tracing ON inside the guest. The guest appends lines
		/// to a buffer of its own and we drain it once per frame (see
		/// <see cref="DrainTrace"/>) - a callback per instruction would cross the
		/// sandbox boundary millions of times a second.
		/// </summary>
		public ITraceSink? Sink
		{
			get => _traceSink;
			set
			{
				bool toggling = (_traceSink != null) != (value != null);
				_traceSink = value;
				if (toggling) _traceSetEnabled?.Invoke(value != null ? 1 : 0);
			}
		}

		/// <summary>
		/// A savestate is guest memory, and the guest's "tracing on" flag and trace
		/// hook pointer are guest memory too, so loading a state can silently turn
		/// tracing off (or back on) under an attached sink. Re-assert it after a load,
		/// and discard whatever lines the restored buffer happens to hold - they were
		/// traced before the load and would appear out of order.
		/// </summary>
		private void RestoreTraceState()
		{
			if (_traceSetEnabled == null) return;
			_traceSetEnabled(_traceSink != null ? 1 : 0);
			_traceClear?.Invoke();
		}

		private void DrainTrace()
		{
			var sink = _traceSink;
			if (sink == null || _traceLineCount == null || _traceBuffer == null) return;

			int lines = _traceLineCount();
			if (lines > 0)
			{
				// One bulk copy of the whole used region beats a marshalled string per
				// line when a frame can produce tens of thousands of them. Cores that
				// don't report the byte count fall back to reading line by line.
				int used = _traceUsedBytes?.Invoke() ?? -1;
				if (used > 0)
				{
					var bytes = new byte[used];
					Marshal.Copy(_traceBuffer(), bytes, 0, used);
					int start = 0;
					for (int i = 0; i < used; i++)
					{
						if (bytes[i] != 0) continue;
						PutTraceLine(sink, Encoding.ASCII.GetString(bytes, start, i - start));
						start = i + 1;
					}
				}
				else
				{
					var p = _traceBuffer();
					for (int i = 0; i < lines; i++)
					{
						var line = Marshal.PtrToStringAnsi(p);
						if (string.IsNullOrEmpty(line)) break;
						PutTraceLine(sink, line);
						p += line.Length + 1;
					}
				}
			}

			// Report a truncated frame once rather than every frame it recurs.
			if (_traceOverflow?.Invoke() != 0 && !_traceOverflowed)
			{
				_traceOverflowed = true;
				sink.Put(new TraceInfo($"[{_cfg.CoreName}: trace buffer full, lines dropped]", ""));
			}

			_traceClear?.Invoke();
		}

		/// <summary>
		/// The guest writes one NUL-terminated line per instruction, optionally split
		/// by a tab into the disassembly and the register state - the two columns the
		/// trace logger shows. Cores that don't split get one wide column.
		/// </summary>
		private static void PutTraceLine(ITraceSink sink, string line)
		{
			int tab = line.IndexOf('\t');
			sink.Put(tab < 0
				? new TraceInfo(line, "")
				: new TraceInfo(line.Substring(0, tab), line.Substring(tab + 1)));
		}
	}
}
