#nullable disable

using System.Collections;
using System.Collections.Generic;
using System.Runtime.InteropServices;

using Chimera.Emulation.Common.Engine;

namespace Chimera.Emulation.Common.Waterbox
{
	/// <summary>
	/// Memory callbacks for a waterbox core, over the engine's memory hook.
	/// <para>
	/// The bookkeeping - which delegate wants which address, and what a
	/// callback's return value means - stays here, because it is frontend
	/// state. The ADDRESS COMPARISON does not: every registration is pushed
	/// down to the guest, which owns a table of the addresses it is watching
	/// and only calls out when the machine actually touches one. Nothing in
	/// this class runs on an access that matches nothing, which is the whole
	/// point (ToolAssisted-run/chimera#113: BizHawk called managed code on
	/// EVERY instruction while an execute callback existed).
	/// </para>
	/// <para>
	/// The matching itself is <see cref="MemoryCallbackSystem"/>'s - it is
	/// already the shape the Lua API expects, down to what happens when two
	/// callbacks at one address both return a replacement. This type adds only
	/// the push-down, after every operation that can change what is watched:
	/// a registration is rare and an access is not, so re-sending the whole
	/// set is the cheap side of the trade.
	/// </para>
	/// </summary>
	public sealed class WaterboxMemoryCallbacks : IMemoryCallbackSystem, IDisposable
	{
		/// <summary>the engine's flags (engine.h CE_MEMHOOK_*), which are not the frontend's</summary>
		private const int EngineRead = 1;
		private const int EngineWrite = 2;
		private const int EngineExec = 4;

		[UnmanagedFunctionPointer(CallingConvention.Cdecl)]
		private delegate long SinkFn(int scope, uint addr, uint value, uint flags, IntPtr user);

		// Kept alive for as long as the engine holds the pointer: a collected
		// delegate would leave the guest calling into a hole mid-frame.
		private static readonly SinkFn _sinkDelegate = Sink;
		private static IntPtr _sinkPtr;

		private readonly EngineSession _session;
		private readonly MemoryCallbackSystem _inner;
		private readonly string[] _scopes;
		private GCHandle _self;
		private bool _disposed;

		public WaterboxMemoryCallbacks(EngineSession session)
		{
			_session = session;
			var scopes = new string[session.MemHookScopeCount];
			for (int i = 0; i < scopes.Length; i++) scopes[i] = session.MemHookScopeName(i);
			_scopes = scopes;
			_inner = new MemoryCallbackSystem(scopes);
			ExecuteCallbacksAvailable = session.MemHookExecutes;

			_self = GCHandle.Alloc(this, GCHandleType.Normal);
			if (_sinkPtr == IntPtr.Zero) _sinkPtr = Marshal.GetFunctionPointerForDelegate(_sinkDelegate);
			EngineSession.MemHookSetSink(_sinkPtr, GCHandle.ToIntPtr(_self));
			_session.MemHookClear();
		}

		/// <summary>
		/// The guest found a match. This runs INSIDE the frame, on the thread
		/// advancing it, with the machine stopped where the access happened -
		/// so the callback can read the memory the access was about. Nothing
		/// may escape back into native code: an exception unwinding through
		/// the sandbox would take the machine with it.
		/// </summary>
		private static long Sink(int scope, uint addr, uint value, uint flags, IntPtr user)
		{
			try
			{
				if (user == IntPtr.Zero) return -1;
				if (GCHandle.FromIntPtr(user).Target is not WaterboxMemoryCallbacks self) return -1;
				if (self._disposed) return -1;
				string scopeName = scope >= 0 && scope < self._scopes.Length ? self._scopes[scope] : "System Bus";
				uint result = self._inner.CallMemoryCallbacks(addr, value, ToFrontendFlags(flags), scopeName);
				return result == value ? -1 : result;
			}
			catch (Exception)
			{
				// A script that threw has already been shown against itself by
				// the Lua sandbox; the machine carries on with the value it had.
				return -1;
			}
		}

		private static uint ToFrontendFlags(uint engineFlags)
		{
			uint f = (uint)MemoryCallbackFlags.SizeByte;
			if ((engineFlags & EngineRead) is not 0) f |= (uint)MemoryCallbackFlags.AccessRead;
			if ((engineFlags & EngineWrite) is not 0) f |= (uint)MemoryCallbackFlags.AccessWrite;
			if ((engineFlags & EngineExec) is not 0) f |= (uint)MemoryCallbackFlags.AccessExecute;
			return f;
		}

		/// <summary>
		/// Sends the guest the whole watch set. Called after every change: the
		/// guest's table accumulates, and only the frontend knows whether the
		/// callback just dropped was the last one wanting that address.
		/// </summary>
		private void Push()
		{
			if (_disposed || _session.Disposed) return;
			_session.MemHookClear();
			foreach (var cb in _inner)
			{
				int scope = IndexOfScope(cb.Scope);
				if (scope < 0) continue;
				int flags = cb.Type switch
				{
					MemoryCallbackType.Read => EngineRead,
					MemoryCallbackType.Write => EngineWrite,
					MemoryCallbackType.Execute => EngineExec,
					_ => 0,
				};
				if (flags is 0) continue;
				// no address = every address in the scope; the guest has a
				// wildcard of its own so the table is not 64K entries of yes
				_session.MemHookWatch(scope, cb.Address.HasValue ? cb.Address.Value : -1, flags);
			}
		}

		private int IndexOfScope(string scope)
		{
			for (int i = 0; i < _scopes.Length; i++)
			{
				if (_scopes[i] == scope) return i;
			}
			return -1;
		}

		// ---------------- IMemoryCallbackSystem ----------------

		public bool ExecuteCallbacksAvailable { get; }

		public string[] AvailableScopes => _scopes;

		public bool HasReads => _inner.HasReads;

		public bool HasWrites => _inner.HasWrites;

		public bool HasExecutes => _inner.HasExecutes;

		public bool HasReadsForScope(string scope) => _inner.HasReadsForScope(scope);

		public bool HasWritesForScope(string scope) => _inner.HasWritesForScope(scope);

		public bool HasExecutesForScope(string scope) => _inner.HasExecutesForScope(scope);

		public void Add(IMemoryCallback callback)
		{
			if (callback.Type is MemoryCallbackType.Execute && !ExecuteCallbacksAvailable)
			{
				throw new NotImplementedException("this core cannot see its own instruction fetches");
			}
			_inner.Add(callback);
			Push();
		}

		public uint CallMemoryCallbacks(uint addr, uint value, uint flags, string scope)
			=> _inner.CallMemoryCallbacks(addr, value, flags, scope);

		public void Remove(MemoryCallbackDelegate action)
		{
			_inner.Remove(action);
			Push();
		}

		public void RemoveAll(IEnumerable<MemoryCallbackDelegate> actions)
		{
			_inner.RemoveAll(actions);
			Push();
		}

		public void Clear()
		{
			_inner.Clear();
			Push();
		}

		public IEnumerator<IMemoryCallback> GetEnumerator() => _inner.GetEnumerator();

		IEnumerator IEnumerable.GetEnumerator() => _inner.GetEnumerator();

		public void Dispose()
		{
			if (_disposed) return;
			_disposed = true;
			// The sink is one per process; only stop it if this instance is
			// the one that still owns it.
			EngineSession.MemHookSetSink(IntPtr.Zero, IntPtr.Zero);
			if (_self.IsAllocated) _self.Free();
		}
	}
}
