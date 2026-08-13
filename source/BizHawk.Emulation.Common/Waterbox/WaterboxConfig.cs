using System.Collections.Generic;

using Newtonsoft.Json;

namespace BizHawk.Emulation.Common.Waterbox
{
	/// <summary>
	/// The per-core declaration in a waterbox package's <c>waterbox.config</c>
	/// (JSON). Describes the STATIC machine surface the generic adapter needs -
	/// system id, controller, video, audio, guest-heap layout - plus a placeholder
	/// for default user-tunable settings. Memory domains are NOT here: their size
	/// and count can depend on runtime settings, so the guest self-describes them
	/// at runtime (guest ABI GetMemoryDomain*).
	///
	/// Deserialized with Newtonsoft (property matching is case-insensitive, so the
	/// JSON uses camelCase and these are PascalCase).
	/// </summary>
	public sealed class WaterboxConfig
	{
		public string CoreName { get; set; }

		public string SystemId { get; set; }

		/// <summary>Who wrote the core. Shown wherever the frontend introduces it.</summary>
		public string Author { get; set; }

		/// <summary>The core's own version string (not the package format's).</summary>
		public string Version { get; set; }

		/// <summary>Where the core lives, for the about box.</summary>
		public string Url { get; set; }

		/// <summary>The mounted file name the guest reads the rom from (default "rom").</summary>
		public string RomFile { get; set; } = "rom";

		public bool Deterministic { get; set; } = true;

		/// <summary>Guest heap sizes in MiB, in order: sbrk, sealed, invis, plain, mmap.</summary>
		public uint[] MemoryLayoutMiB { get; set; }

		public VideoConfig Video { get; set; }

		public AudioConfig Audio { get; set; }

		public InputConfig Input { get; set; }

		public LagConfig Lag { get; set; }

		/// <summary>Rom file extension (with leading dot, lowercase) to system ID map - how files route to this core.</summary>
		public Dictionary<string, string> Extensions { get; set; }

		/// <summary>Default user-tunable settings for the core (values TBD).</summary>
		public Dictionary<string, object> Settings { get; set; }

		public sealed class VideoConfig
		{
			public int Width { get; set; }
			public int Height { get; set; }
			public int VirtualWidth { get; set; }
			public int VirtualHeight { get; set; }
			public int VsyncNumerator { get; set; } = 60;
			public int VsyncDenominator { get; set; } = 1;

			/// <summary>Guest export returning a Width*Height BGRA (0xFFrrggbb) frame.</summary>
			public string GetBgra { get; set; }
		}

		public sealed class AudioConfig
		{
			public int SamplesPerFrame { get; set; }
			public int Channels { get; set; } = 1;

			/// <summary>Guest export returning SamplesPerFrame*Channels interleaved int16.</summary>
			public string Get { get; set; }
		}

		public sealed class InputConfig
		{
			public string Name { get; set; }

			/// <summary>Bool button names, in bit order (button i -&gt; bit i of the 64-bit FrameAdvance mask).</summary>
			public List<string> Buttons { get; set; }

			/// <summary>
			/// Analog controls (paddles, sticks, triggers), in index order. They cannot
			/// travel in the button mask, so the adapter pushes each one to the guest
			/// with the optional <c>SetAxis(index, value)</c> export before every frame.
			/// A core that declares axes must export it.
			/// </summary>
			public List<AxisConfig> Axes { get; set; }
		}

		public sealed class AxisConfig
		{
			public string Name { get; set; }
			public int Min { get; set; }
			public int Max { get; set; }

			/// <summary>Value when the control is untouched (centre for a stick, rest position for a paddle).</summary>
			public int Neutral { get; set; }
		}

		public sealed class LagConfig
		{
			/// <summary>Guest export returning nonzero if input was polled this frame (lag = !this). Optional.</summary>
			public string InputWasRead { get; set; }
		}

		public static WaterboxConfig FromJson(string json)
			=> JsonConvert.DeserializeObject<WaterboxConfig>(json);
	}
}
