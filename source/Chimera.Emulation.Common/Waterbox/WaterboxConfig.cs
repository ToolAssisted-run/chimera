using System.Collections.Generic;
using System.Globalization;

using Newtonsoft.Json;

namespace Chimera.Emulation.Common.Waterbox
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

		/// <summary>
		/// Files the core needs that it may not ship - a disk-system BIOS, say. Each is
		/// mounted for the guest under its declared id, alongside the rom and the
		/// settings; the guest opens it by that name during Init.
		/// </summary>
		public List<CoreFirmwareDecl> Firmware { get; set; }

		/// <summary>
		/// The user-tunable settings this core offers, declared by the package. The
		/// frontend renders them from this and nothing else - it has no per-core
		/// settings dialogs to render them with.
		/// </summary>
		public List<SettingDecl> Settings { get; set; }

		/// <summary>
		/// One user-tunable setting. Enough for the frontend to draw a labelled,
		/// typed, documented row without knowing what the setting means.
		/// </summary>
		public sealed class SettingDecl
		{
			/// <summary>Key the guest reads it under, in the mounted settings JSON.</summary>
			public string Name { get; set; }

			/// <summary>Label for the settings grid. Falls back to <see cref="Name"/>.</summary>
			public string Display { get; set; }

			/// <summary>The sentence shown when the row is selected.</summary>
			public string Description { get; set; }

			/// <summary>"bool", "int" or "enum" (the default is inferred from the other fields).</summary>
			public string Type { get; set; }

			public object Default { get; set; }

			/// <summary>Allowed values, for an enum setting.</summary>
			public List<string> Options { get; set; }

			public int? Min { get; set; }

			public int? Max { get; set; }


			public string DisplayName => string.IsNullOrWhiteSpace(Display) ? Name : Display;

			/// <summary>
			/// The .NET type the settings grid should edit this as. Enums arrive as
			/// strings (the guest reads them by name), so an enum is edited as a string
			/// from a fixed list rather than as a synthesized Enum type.
			/// </summary>
			public Type ClrType => EffectiveType switch
			{
				"bool" => typeof(bool),
				"int" => typeof(int),
				"float" => typeof(double),
				_ => typeof(string),
			};

			/// <summary>The declared type, or one inferred from the options/default when omitted.</summary>
			public string EffectiveType
			{
				get
				{
					if (!string.IsNullOrWhiteSpace(Type)) return Type.ToLowerInvariant();
					if (Options is { Count: > 0 }) return "enum";
					return Default switch
					{
						bool => "bool",
						sbyte or byte or short or ushort or int or uint or long or ulong => "int",
						float or double or decimal => "float",
						_ => "string",
					};
				}
			}

			/// <summary>The default, coerced to <see cref="ClrType"/>.</summary>
			public object DefaultValue => Coerce(Default);

			/// <summary>
			/// Brings a value (from JSON, so possibly a boxed long or a string) to the
			/// type this setting is edited as, clamping ints to any declared range.
			/// </summary>
			public object Coerce(object value)
			{
				switch (EffectiveType)
				{
					case "bool":
						return value switch { null => false, bool b => b, _ => bool.TryParse(value.ToString(), out var pb) && pb };
					case "int":
					{
						var n = value switch
						{
							null => 0,
							int i => i,
							_ => int.TryParse(Convert.ToString(value, CultureInfo.InvariantCulture), NumberStyles.Integer, CultureInfo.InvariantCulture, out var pi) ? pi : 0,
						};
						if (Min is int min && n < min) n = min;
						if (Max is int max && n > max) n = max;
						return n;
					}
					case "float":
					{
						return value switch
						{
							null => 0.0,
							double d => d,
							float f => (double)f,
							_ => double.TryParse(Convert.ToString(value, CultureInfo.InvariantCulture), NumberStyles.Float, CultureInfo.InvariantCulture, out var pd) ? pd : 0.0,
						};
					}
					default:
					{
						var s = value?.ToString() ?? "";
						// an unknown option would leave the grid showing a value the core
						// will not accept, so fall back to the first legal one
						if (Options is { Count: > 0 } && !Options.Contains(s)) return Options.Contains(Default?.ToString()) ? Default.ToString() : Options[0];
						return s;
					}
				}
			}
		}

		public sealed class VideoConfig
		{
			public int Width { get; set; }
			public int Height { get; set; }
			public int VirtualWidth { get; set; }
			public int VirtualHeight { get; set; }
			/// <summary>
			/// Frame rate as a fraction. Only a fallback: a core that knows its own rate - because
			/// the region is a user setting, say - exports <c>GetVsyncNumerator</c> and
			/// <c>GetVsyncDenominator</c>, which the adapter asks after Init and prefers.
			/// </summary>
			public int VsyncNumerator { get; set; } = 60;

			public int VsyncDenominator { get; set; } = 1;

			/// <summary>Guest export returning a Width*Height BGRA (0xFFrrggbb) frame.</summary>
			public string GetBgra { get; set; }
		}

		public sealed class AudioConfig
		{
			/// <summary>
			/// Samples per frame, per channel. A core that produces a varying number (any blip-style
			/// resampler does) exports <c>GetAudioSampleCount</c> and this becomes the capacity: the
			/// adapter takes what the core reports, clamped to this.
			/// </summary>
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

		/// <summary>
		/// The "firmware" array exactly as the package wrote it, for the engine's
		/// decision tree (ce_firmware_evaluate) - conditions and all, no round
		/// trip through the typed declarations.
		/// </summary>
		[JsonIgnore]
		public string RawFirmwareJson { get; private set; } = "[]";

		/// <summary>
		/// The "settings" array exactly as the package wrote it, for the engine's
		/// exposure gate (ce_settings_evaluate) - conditions and all.
		/// </summary>
		[JsonIgnore]
		public string RawSettingsJson { get; private set; } = "[]";

		public static WaterboxConfig FromJson(string json)
		{
			var cfg = JsonConvert.DeserializeObject<WaterboxConfig>(json);
			if (cfg is not null)
			{
				try
				{
					var root = Newtonsoft.Json.Linq.JObject.Parse(json);
					if (root["firmware"] is Newtonsoft.Json.Linq.JArray fw) cfg.RawFirmwareJson = fw.ToString(Formatting.None);
					if (root["settings"] is Newtonsoft.Json.Linq.JArray st) cfg.RawSettingsJson = st.ToString(Formatting.None);
				}
				catch (JsonException)
				{
					// the typed parse above already decided the config is usable
				}
			}
			return cfg;
		}
	}
}
