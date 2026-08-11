using System.Collections.Generic;
using System.Linq;

using BizHawk.Emulation.Common;

namespace MiniHawk.Cores.Synth
{
	/// <summary>Empty settings placeholder: the Synth machine has no configuration.</summary>
	public sealed class SynthNativeSettings { }

	/// <summary>Empty sync-settings placeholder: nothing about the Synth machine affects sync.</summary>
	public sealed class SynthNativeSyncSettings { }

	/// <summary>miniHawk core factory for the native-flavor Synth core.</summary>
	public sealed class SynthNativeFactory : ICoreFactory
	{
		public string CoreName => "SynthNative";

		public IReadOnlyList<string> SystemIds { get; } = [ "Synth" ];

		public Type CoreType => typeof(SynthNative);

		public Type SettingsType => typeof(SynthNativeSettings);

		public Type SyncSettingsType => typeof(SynthNativeSyncSettings);

		public IEmulator Create(CoreCreationContext ctx)
		{
			var rom = ctx.Roms.FirstOrDefault()
				?? throw new InvalidOperationException($"{CoreName} needs a rom to load");
			return new SynthNative(rom.FileData);
		}
	}
}
