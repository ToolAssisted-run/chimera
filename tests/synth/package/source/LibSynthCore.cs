using System.Runtime.InteropServices;

using BizHawk.BizInvoke;

namespace MiniHawk.Cores.Synth
{
	/// <summary>BizInvoke surface of libsynthcore (tests/synth/native/synthcore.c; see SPEC.md).</summary>
	public abstract class LibSynthCore
	{
		/// <returns>NULL if the rom is rejected</returns>
		[BizImport(CallingConvention.Cdecl)]
		public abstract IntPtr synth_create(byte[] rom, uint romSize);

		[BizImport(CallingConvention.Cdecl)]
		public abstract void synth_destroy(IntPtr ctx);

		/// <summary>power-on: zeroes the whole machine state</summary>
		[BizImport(CallingConvention.Cdecl)]
		public abstract void synth_reset(IntPtr ctx);

		/// <summary>advance one frame with the given pad bitmask</summary>
		[BizImport(CallingConvention.Cdecl)]
		public abstract void synth_frame(IntPtr ctx, byte pad);

		/// <returns>pointer to the 4096-byte RAM (stable for the context's lifetime)</returns>
		[BizImport(CallingConvention.Cdecl)]
		public abstract IntPtr synth_get_ram(IntPtr ctx);

		/// <returns>pointer to the 128x120 palette-index framebuffer</returns>
		[BizImport(CallingConvention.Cdecl)]
		public abstract IntPtr synth_get_framebuffer(IntPtr ctx);

		/// <returns>pointer to the last frame's 735 mono int16 samples</returns>
		[BizImport(CallingConvention.Cdecl)]
		public abstract IntPtr synth_get_audio(IntPtr ctx);

		[BizImport(CallingConvention.Cdecl)]
		public abstract uint synth_state_size();

		/// <summary>palette-resolved BGRA presentation of the framebuffer (128*120 ints)</summary>
		[BizImport(CallingConvention.Cdecl)]
		public abstract void synth_get_video_bgra(IntPtr ctx, int[] dest);

		[BizImport(CallingConvention.Cdecl)]
		public abstract void synth_serialize(IntPtr ctx, byte[] dest);

		[BizImport(CallingConvention.Cdecl)]
		public abstract void synth_deserialize(IntPtr ctx, byte[] src);

		/// <returns>1 if an INPUT instruction executed during the last frame (lag tracking)</returns>
		[BizImport(CallingConvention.Cdecl)]
		public abstract byte synth_input_was_read(IntPtr ctx);
	}
}
