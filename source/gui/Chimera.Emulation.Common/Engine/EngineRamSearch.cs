using System.Runtime.InteropServices;

using Chimera.Common;

namespace Chimera.Emulation.Common.Engine
{
	/// <summary>One row of a RAM Search, as the engine lists it.</summary>
	public readonly struct RamSearchRow
	{
		public readonly long Address;
		public readonly uint Current;
		public readonly uint Previous;
		public readonly int ChangeCount;

		public RamSearchRow(long address, uint current, uint previous, int changeCount)
		{
			Address = address;
			Current = current;
			Previous = previous;
			ChangeCount = changeCount;
		}
	}

	/// <summary>
	/// A RAM Search held by the engine (engine.h, ce_ramsearch_*). Nothing here
	/// decides anything: it hands the engine the domain's memory - the pointer
	/// when the domain has one, a read function when it has not - and passes
	/// calls through. The only failure is the host running out of memory, which
	/// is thrown as <see cref="OutOfMemoryException"/> for the tool to report.
	/// </summary>
	public sealed class EngineRamSearch : IDisposable
	{
		[UnmanagedFunctionPointer(CallingConvention.Cdecl)]
		private delegate long ReadFn(IntPtr user, long offset, IntPtr buf, long len);

		private static LibChimera E => ChimeraEngine.Instance;

		private IntPtr _handle;
		private readonly MemoryDomain _domain;
		private readonly ReadFn? _read; // must outlive the handle
		private byte[] _chunk = Array.Empty<byte>();

		public EngineRamSearch(MemoryDomain domain)
		{
			_domain = domain;
			if (domain is MemoryDomainIntPtr direct && direct.Data != IntPtr.Zero)
			{
				_handle = E.ce_ramsearch_create(direct.Data, IntPtr.Zero, IntPtr.Zero, domain.Size);
			}
			else
			{
				_read = Read;
				_handle = E.ce_ramsearch_create(IntPtr.Zero, Marshal.GetFunctionPointerForDelegate(_read), IntPtr.Zero, domain.Size);
			}
			if (_handle == IntPtr.Zero) throw new OutOfMemoryException();
		}

		private long Read(IntPtr user, long offset, IntPtr buf, long len)
		{
			try
			{
				if (len <= 0 || offset < 0 || offset + len > _domain.Size) return 0;
				if (_chunk.Length != len) _chunk = new byte[len];
				using (_domain.EnterExit()) _domain.BulkPeekByte(offset.RangeToExclusive(offset + len), _chunk);
				Marshal.Copy(_chunk, 0, buf, (int)len);
				return len;
			}
			catch
			{
				return 0; // nothing may unwind into the engine; unread memory reads as zero
			}
		}

		private static void Check(int result)
		{
			if (result != 0) throw new OutOfMemoryException("RAM Search ran out of memory.");
		}

		public void Start(int size, bool misaligned, bool bigEndian, bool detailed)
			=> Check(E.ce_ramsearch_start(_handle, size, misaligned ? 1 : 0, bigEndian ? 1 : 0, detailed ? 1 : 0));

		public long Count => _handle == IntPtr.Zero ? 0 : E.ce_ramsearch_count(_handle);

		public bool TryGetRow(long index, out RamSearchRow row)
		{
			ulong address = 0;
			uint current = 0, previous = 0, changes = 0;
			if (_handle == IntPtr.Zero || E.ce_ramsearch_row(_handle, index, ref address, ref current, ref previous, ref changes) is 0)
			{
				row = default;
				return false;
			}
			row = new RamSearchRow((long)address, current, previous, (int)changes);
			return true;
		}

		public long IndexOf(long address) => address < 0 ? -1 : E.ce_ramsearch_index_of(_handle, (ulong)address);

		public long Search(int compare, int op, int display, uint value, uint differentBy, int previousType)
		{
			var removed = E.ce_ramsearch_search(_handle, compare, op, display, value, differentBy, previousType);
			if (removed < 0) throw new OutOfMemoryException("RAM Search ran out of memory.");
			return removed;
		}

		public bool WouldRemove(long index, int compare, int op, int display, uint value, uint differentBy)
			=> E.ce_ramsearch_would_remove(_handle, index, compare, op, display, value, differentBy) is not 0;

		public void Update(int previousType) => Check(E.ce_ramsearch_update(_handle, previousType));
		public void SetPreviousToCurrent() => Check(E.ce_ramsearch_set_previous_to_current(_handle));
		public void ClearChangeCounts() => E.ce_ramsearch_clear_change_counts(_handle);
		public void SetBigEndian(bool bigEndian) => E.ce_ramsearch_set_big_endian(_handle, bigEndian ? 1 : 0);
		public void SetPreviousType(int previousType) => Check(E.ce_ramsearch_set_previous_type(_handle, previousType));
		public void RemoveIndices(long[] indices) => Check(E.ce_ramsearch_remove_indices(_handle, indices, indices.LongLength));
		public void RemoveAddresses(ulong[] addresses, bool recordUndo) => Check(E.ce_ramsearch_remove_addresses(_handle, addresses, addresses.LongLength, recordUndo ? 1 : 0));
		public void AddAddresses(ulong[] addresses, bool append) => Check(E.ce_ramsearch_add_addresses(_handle, addresses, addresses.LongLength, append ? 1 : 0));
		public void ConvertTo(int size) => Check(E.ce_ramsearch_convert_to(_handle, size));
		public void Sort(int column, bool reverse, int display) => Check(E.ce_ramsearch_sort(_handle, column, reverse ? 1 : 0, display));
		public long OutOfRangeCount => E.ce_ramsearch_out_of_range_count(_handle);
		public void RemoveOutOfRange() => Check(E.ce_ramsearch_remove_out_of_range(_handle));

		public void SetUndoEnabled(bool on) => E.ce_ramsearch_set_undo_enabled(_handle, on ? 1 : 0);
		public bool CanUndo => _handle != IntPtr.Zero && E.ce_ramsearch_can_undo(_handle) is not 0;
		public bool CanRedo => _handle != IntPtr.Zero && E.ce_ramsearch_can_redo(_handle) is not 0;
		public void ClearHistory() => E.ce_ramsearch_clear_history(_handle);
		public long Undo() => E.ce_ramsearch_undo(_handle);
		public long Redo() => E.ce_ramsearch_redo(_handle);

		public void Dispose()
		{
			var handle = _handle;
			_handle = IntPtr.Zero;
			if (handle != IntPtr.Zero) E.ce_ramsearch_destroy(handle);
			GC.SuppressFinalize(this);
		}

		~EngineRamSearch()
		{
			if (_handle != IntPtr.Zero) E.ce_ramsearch_destroy(_handle);
		}
	}
}
