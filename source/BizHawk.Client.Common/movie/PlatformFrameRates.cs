namespace BizHawk.Client.Common
{
	/// <summary>
	/// Nominal frame-rate fallback used only for display purposes (movie length
	/// listings, estimated session time) when no core is loaded to report its true
	/// rate. miniHawk carries no per-system rate table — exact rates are the loaded
	/// core's business (<see cref="BizHawk.Emulation.Common.IVideoProvider"/> vsync
	/// numbers); this is a rough estimate only.
	/// </summary>
	public static class PlatformFrameRates
	{
		public static double GetFrameRate(string systemId, bool pal)
			=> pal ? 50.0 : 60.0;
	}
}
