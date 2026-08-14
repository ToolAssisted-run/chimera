#nullable enable

using System.Runtime.InteropServices;

namespace BizHawk.Emulation.Common.Waterbox
{
	/// <summary>
	/// The save-file channel: what a machine keeps when it is switched off - a cart's
	/// battery-backed RAM, a disk the game wrote to, a memory card.
	///
	/// It is deliberately NOT a memory domain the host copies out. Only the core knows
	/// what belongs in a save file: for the disk system it is the difference between
	/// the disk now and the disk as it was inserted, which exists nowhere as a block of
	/// memory. So the core serializes it, into a buffer it owns, and the host moves
	/// bytes it does not interpret.
	///
	/// Optional, like the tooling groups: a core with nothing to save exports none of
	/// this and <see cref="ISaveRam"/> is unregistered, which is what greys out the
	/// frontend's Save RAM menu.
	/// </summary>
	public sealed partial class WaterboxCore : ISaveRam
	{
		[UnmanagedFunctionPointer(CallingConvention.Cdecl)] private delegate IntPtr GetSaveRamBufferFn(int size);

		private IntFn? _saveRamSize;
		private GetPtrFn? _saveRamGet;
		private GetSaveRamBufferFn? _saveRamBuffer;
		private IntIntFn? _saveRamPut;

		/// <summary>
		/// True once the machine has run a frame - the point after which its save file
		/// may differ from the one loaded. Cheaper and steadier than asking the core to
		/// track dirtiness, and the frontend only uses this to skip pointless writes.
		/// </summary>
		private bool _saveRamMaybeDirty;

		private void InitSaveRam(BasicServiceProvider services)
		{
			_saveRamSize = TryProc<IntFn>("GetSaveRamSize");
			_saveRamGet = TryProc<GetPtrFn>("GetSaveRam");
			_saveRamBuffer = TryProc<GetSaveRamBufferFn>("GetSaveRamBuffer");
			_saveRamPut = TryProc<IntIntFn>("PutSaveRam");

			// A core may export the group and still have nothing to save for THIS rom (the
			// same NES core runs battery carts, plain carts and disks), and a service that
			// is registered but always returns null makes the frontend write empty files.
			if (_saveRamSize == null || _saveRamGet == null || _saveRamSize() == 0)
			{
				services.Unregister<ISaveRam>();
			}
		}

		public byte[]? CloneSaveRam(bool clearDirty = true)
		{
			if (_saveRamSize == null || _saveRamGet == null) return null;
			CheckDisposed();
			var size = _saveRamSize();
			if (size <= 0) return null;
			var ptr = _saveRamGet();
			if (ptr == IntPtr.Zero) return null;
			var ret = new byte[size];
			Marshal.Copy(ptr, ret, 0, size);
			if (clearDirty) _saveRamMaybeDirty = false;
			return ret;
		}

		public void StoreSaveRam(byte[] data)
		{
			if (_saveRamBuffer == null || _saveRamPut == null) return;
			CheckDisposed();
			var ptr = _saveRamBuffer(data.Length);
			if (ptr == IntPtr.Zero) throw new InvalidOperationException($"{_cfg.CoreName}: no room for a {data.Length} byte save file");
			Marshal.Copy(data, 0, ptr, data.Length);
			// The core is the judge of whether the file fits the machine - a disk save with
			// the wrong number of sides, say - and says so by refusing it. Loading half a
			// save silently would be worse than not loading it.
			if (_saveRamPut(data.Length) == 0)
			{
				throw new InvalidOperationException($"{_cfg.CoreName}: this save file does not belong to this game");
			}
			_saveRamMaybeDirty = false;
		}

		public bool SaveRamModified => _saveRamMaybeDirty;
	}
}
