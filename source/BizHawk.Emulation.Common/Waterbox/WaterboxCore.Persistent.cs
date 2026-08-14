#nullable enable

using System.Runtime.InteropServices;

namespace BizHawk.Emulation.Common.Waterbox
{
	/// <summary>
	/// The persistent-data channel: what a machine keeps when it is switched off - a
	/// cart's battery-backed RAM, a disk the game wrote to, a memory card.
	///
	/// It is deliberately NOT a memory domain the host copies out. Only the core knows
	/// what belongs in a save file: for the disk system it is the difference between
	/// the disk now and the disk as it was inserted, which exists nowhere as a block of
	/// memory. So the core serializes it, into a buffer it owns, and the host moves
	/// bytes it does not interpret.
	///
	/// Optional, like the tooling groups: a core with nothing to keep exports none of
	/// this and <see cref="ICorePersistentData"/> is unregistered, which is what leaves
	/// it out of the core's own menu.
	/// </summary>
	public sealed partial class WaterboxCore : ICorePersistentData
	{
		[UnmanagedFunctionPointer(CallingConvention.Cdecl)] private delegate IntPtr GetPersistentBufferFn(int size);

		private IntFn? _persistSize;
		private GetPtrFn? _persistGet;
		private GetPersistentBufferFn? _persistBuffer;
		private IntIntFn? _persistPut;

		/// <summary>
		/// True once the machine has run a frame - the point after which what it keeps may
		/// differ from what was loaded. Cheaper and steadier than asking the core to track
		/// dirtiness, and the frontend only uses this to skip pointless writes.
		/// </summary>
		private bool _persistMaybeDirty;

		public string PersistentDataName { get; private set; } = "Persistent Data";

		public string PersistentDataId { get; private set; } = "data";

		private void InitPersistentData(BasicServiceProvider services)
		{
			_persistSize = TryProc<IntFn>("GetPersistentSize");
			_persistGet = TryProc<GetPtrFn>("GetPersistent");
			_persistBuffer = TryProc<GetPersistentBufferFn>("GetPersistentBuffer");
			_persistPut = TryProc<IntIntFn>("PutPersistent");

			// A core may export the group and still keep nothing for THIS rom (the same NES
			// core runs battery carts, plain carts and disks), and a service that is
			// registered but always returns null makes the frontend write empty files.
			if (_persistSize == null || _persistGet == null || _persistSize() == 0)
			{
				services.Unregister<ICorePersistentData>();
				return;
			}

			// what the core calls it, and what a bundle files it under; both are the core's
			// vocabulary, and the frontend only ever repeats them
			PersistentDataName = Marshal.PtrToStringAnsi(TryProc<GetPtrFn>("GetPersistentName")?.Invoke() ?? IntPtr.Zero) ?? PersistentDataName;
            PersistentDataId = Marshal.PtrToStringAnsi(TryProc<GetPtrFn>("GetPersistentId")?.Invoke() ?? IntPtr.Zero) ?? PersistentDataId;
		}

		public byte[]? ClonePersistentData(bool clearDirty = true)
		{
			if (_persistSize == null || _persistGet == null) return null;
			CheckDisposed();
			var size = _persistSize();
			if (size <= 0) return null;
			var ptr = _persistGet();
			if (ptr == IntPtr.Zero) return null;
			var ret = new byte[size];
			Marshal.Copy(ptr, ret, 0, size);
			if (clearDirty) _persistMaybeDirty = false;
			return ret;
		}

		public void StorePersistentData(byte[] data)
		{
			if (_persistBuffer == null || _persistPut == null) return;
			CheckDisposed();
			var ptr = _persistBuffer(data.Length);
			if (ptr == IntPtr.Zero) throw new InvalidOperationException($"{_cfg.CoreName}: no room for {data.Length} bytes");
			Marshal.Copy(data, 0, ptr, data.Length);
			// The core is the judge of whether the file fits the machine - a disk save with
			// the wrong number of sides, say - and says so by refusing it. Loading half a
			// save silently would be worse than not loading it.
			if (_persistPut(data.Length) == 0)
			{
				throw new InvalidOperationException($"{_cfg.CoreName}: this file does not belong to this game");
			}
			_persistMaybeDirty = false;
		}

		public bool PersistentDataModified => _persistMaybeDirty;
	}
}
