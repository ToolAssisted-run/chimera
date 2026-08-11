using System.Runtime.InteropServices;

using BizHawk.BizInvoke;

namespace MiniHawk.Cores.SynthBox
{
	/// <summary>
	/// BizInvoke surface of libminiboxhost - the miniBox waterbox host
	/// (extern/miniBox/source/host/minibox.h). The wbx_* ABI is the waterbox host
	/// contract (byte-compatible with BizHawk's Rust waterboxhost), so this binding
	/// mirrors that host's managed consumer.
	///
	/// Every fallible call takes a trailing <see cref="ReturnData"/> by ref: on
	/// success ErrorMessage[0] is 0 and Data holds the result.
	/// </summary>
	public abstract class LibMiniBoxHost
	{
		/// <summary>Page-aligned guest heap sizes (mirrors mb_memory_layout_template).</summary>
		[StructLayout(LayoutKind.Sequential)]
		public struct MemoryLayoutTemplate
		{
			public UIntPtr SbrkSize;
			public UIntPtr SealedSize;
			public UIntPtr InvisSize;
			public UIntPtr PlainSize;
			public UIntPtr MmapSize;
		}

		/// <summary>mb_return: a 1024-byte error string plus a result word.</summary>
		[StructLayout(LayoutKind.Sequential)]
		public unsafe struct ReturnData
		{
			public fixed byte ErrorMessage[1024];
			public UIntPtr Data;

			public string GetError()
			{
				fixed (byte* p = ErrorMessage)
				{
					return p[0] == 0 ? null : Marshal.PtrToStringAnsi((IntPtr)p);
				}
			}

			public void ThrowIfError()
			{
				var e = GetError();
				if (e != null) throw new InvalidOperationException("miniBox host error: " + e);
			}

			public IntPtr DataOrThrow()
			{
				ThrowIfError();
				return unchecked((IntPtr)(long)(ulong)Data);
			}
		}

		/// <summary>n read (0 = EOF, &lt;0 fail); may read less than requested.</summary>
		[UnmanagedFunctionPointer(CallingConvention.Cdecl)]
		public delegate IntPtr ReadCallback(UIntPtr userdata, IntPtr data, UIntPtr size);

		/// <summary>0 ok, &lt;0 fail; must consume all requested bytes.</summary>
		[UnmanagedFunctionPointer(CallingConvention.Cdecl)]
		public delegate int WriteCallback(UIntPtr userdata, IntPtr data, UIntPtr size);

		[BizImport(CallingConvention.Cdecl)]
		public abstract void wbx_create_host(ref MemoryLayoutTemplate layout, string moduleName, ReadCallback cb, UIntPtr userdata, ref ReturnData ret);

		[BizImport(CallingConvention.Cdecl)]
		public abstract void wbx_destroy_host(IntPtr obj, ref ReturnData ret);

		[BizImport(CallingConvention.Cdecl)]
		public abstract void wbx_activate_host(IntPtr obj, ref ReturnData ret);

		[BizImport(CallingConvention.Cdecl)]
		public abstract void wbx_deactivate_host(IntPtr obj, ref ReturnData ret);

		[BizImport(CallingConvention.Cdecl)]
		public abstract void wbx_get_proc_addr(IntPtr obj, string name, ref ReturnData ret);

		[BizImport(CallingConvention.Cdecl)]
		public abstract void wbx_seal(IntPtr obj, ref ReturnData ret);

		// C's bool is one byte; pass 0/1 as a byte to avoid the 4-byte Win32 BOOL.
		[BizImport(CallingConvention.Cdecl)]
		public abstract void wbx_mount_file(IntPtr obj, string name, ReadCallback cb, UIntPtr userdata, byte writable, ref ReturnData ret);

		[BizImport(CallingConvention.Cdecl)]
		public abstract void wbx_save_state(IntPtr obj, WriteCallback cb, UIntPtr userdata, ref ReturnData ret);

		[BizImport(CallingConvention.Cdecl)]
		public abstract void wbx_load_state(IntPtr obj, ReadCallback cb, UIntPtr userdata, ref ReturnData ret);
	}
}
