using System.Runtime.InteropServices;

namespace Chimera.Common
{
	public static class LibChimeraHash
	{
		private const CallingConvention cc = CallingConvention.Cdecl;

		[UnmanagedFunctionPointer(cc)]
		public delegate uint CalcCRC(uint current, IntPtr buffer, int len);

		[DllImport("libchimerahash", CallingConvention = cc)]
		public static extern IntPtr ChimeraCalcCrcFunc();

		[DllImport("libchimerahash", CallingConvention = cc)]
		public static extern bool ChimeraSupportsShaInstructions();

		[DllImport("libchimerahash", CallingConvention = cc)]
		public static extern void ChimeraCalcSha1(IntPtr state, byte[] data, int len);
	}
}
