using System.Collections.Generic;
using System.Linq;

using BizHawk.Emulation.Common;

namespace MiniHawk.Cores.SynthBox
{
	/// <summary>Empty settings placeholder: the Synth machine has no configuration.</summary>
	public sealed class SynthBoxSettings { }

	/// <summary>Empty sync-settings placeholder: nothing about the Synth machine affects sync.</summary>
	public sealed class SynthBoxSyncSettings { }

	/// <summary>miniHawk core factory for the waterboxed-flavor Synth core.</summary>
	public sealed class SynthBoxFactory : ICoreFactory
	{
		public string CoreName => "SynthBox";

		public IReadOnlyList<string> SystemIds { get; } = [ "Synth" ];

		public Type CoreType => typeof(SynthBox);

		public Type SettingsType => typeof(SynthBoxSettings);

		public Type SyncSettingsType => typeof(SynthBoxSyncSettings);

		public IEmulator Create(CoreCreationContext ctx)
		{
			var rom = ctx.Roms.FirstOrDefault()
				?? throw new InvalidOperationException($"{CoreName} needs a rom to load");
			return new SynthBox(rom.FileData);
		}
	}
}
