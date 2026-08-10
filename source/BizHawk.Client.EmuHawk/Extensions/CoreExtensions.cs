using System.Drawing;

using BizHawk.Client.Common;
using BizHawk.Emulation.Common;

namespace BizHawk.Client.EmuHawk.CoreExtensions
{
	public static class CoreExtensions
	{
		public static TimeSpan EstimatedRealTimeSincePowerOn(this IEmulator core)
		{
			if (core.HasCycleTiming())
			{
				var cycleCore = core.AsCycleTiming();
				return TimeSpan.FromSeconds(cycleCore.CycleCount / cycleCore.ClockRate);
			}
			var frameCount = unchecked((ulong) core.Frame);
			var frameRate = core switch
			{
				NullEmulator => NullVideo.DefaultVsyncNum / unchecked((double) NullVideo.DefaultVsyncDen),
				_ => PlatformFrameRates.GetFrameRate(
					core.SystemId,
					pal: core.HasRegions() && core.AsRegionable().Region is DisplayType.PAL),
			};
			return TimeSpan.FromSeconds(frameCount / frameRate);
		}

		public static Bitmap Icon(this IEmulator core)
			=> core.Attributes() is not PortedCoreAttribute
				? Properties.Resources.CorpHawkSmall
				: null; // core-specific icons would have to come from the core package; not currently part of the contract

		public static string GetSystemDisplayName(this IEmulator emulator) => emulator switch
		{
			NullEmulator => string.Empty,
			_ => EmulatorExtensions.SystemIDToDisplayName(emulator.SystemId),
		};
	}
}

