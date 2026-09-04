using Chimera.Emulation.Common;
using Chimera.Emulation.Common.Waterbox;

namespace Chimera.Tests.Emulation.Common
{
	/// <summary>
	/// A package says what rate it mixes at, and the adapter resamples it to the
	/// 44100 the sound path assumes. A 48 kHz core that could not say so was
	/// played a semitone and a half low (issue #37).
	/// </summary>
	[TestClass]
	public class WaterboxAudioRateTests
	{
		private static WaterboxConfig Cfg(string audio)
			=> WaterboxConfig.FromJson($$"""
				{
				  "coreName": "rated",
				  "systemId": "SYS",
				  "video": { "width": 320, "height": 224 },
				  "audio": {{audio}},
				  "input": { "buttons": [] }
				}
				""");

		[TestMethod]
		public void APackageThatSaysNothingMixesAt44100()
		{
			Assert.AreEqual(44100, Cfg("""{ "samplesPerFrame": 1024 }""").Audio.Rate);
		}

		[TestMethod]
		public void APackageDeclaresItsRate()
		{
			Assert.AreEqual(48000, Cfg("""{ "samplesPerFrame": 2048, "channels": 2, "rate": 48000 }""").Audio.Rate);
		}

		[TestMethod]
		public void FortyEightThousandBecomesFortyFourThousandOneHundred()
		{
			// The resampler holds a few hundred samples of filter history back, a
			// constant delay rather than a rate error - so the measure is steady
			// state: once running, a second of 48 kHz in comes out as a second of
			// 44.1 kHz, to within the per-frame rounding.
			var total = 0;
			using SDLResampler resampler = new(48000, 44100, (buf, n) => total += n);
			var frame = new short[4800 * 2];
			void Second()
			{
				for (var i = 0; i < 10; i++)
				{
					resampler.EnqueueSamples(frame, 4800);
					resampler.Flush();
				}
			}
			Second();
			var afterOne = total;
			Second();
			var steady = total - afterOne;
			Assert.IsTrue(steady is >= 44050 and <= 44150, $"the second second gave {steady} samples, not 44100");
			Assert.IsTrue(afterOne is >= 43000 and <= 44100, $"the first second gave {afterOne}: more than a little latency");
		}
	}
}
