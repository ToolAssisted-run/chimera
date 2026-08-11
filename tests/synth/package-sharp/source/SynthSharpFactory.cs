using System.Collections.Generic;
using System.Linq;

using BizHawk.Emulation.Common;

namespace MiniHawk.Cores.SynthSharp
{
	/// <summary>Empty settings placeholder: the Synth machine has no configuration.</summary>
	public sealed class SynthSharpSettings { }

	/// <summary>Empty sync-settings placeholder: nothing about the Synth machine affects sync.</summary>
	public sealed class SynthSharpSyncSettings { }

	/// <summary>miniHawk core factory for the pure-C# flavor Synth core.</summary>
	public sealed class SynthSharpFactory : ICoreFactory
	{
		public string CoreName => "SynthSharp";

		public IReadOnlyList<string> SystemIds { get; } = [ "Synth" ];

		public Type CoreType => typeof(SynthSharp);

		public Type SettingsType => typeof(SynthSharpSettings);

		public Type SyncSettingsType => typeof(SynthSharpSyncSettings);

		public IEmulator Create(CoreCreationContext ctx)
		{
			var rom = ctx.Roms.FirstOrDefault()
				?? throw new InvalidOperationException($"{CoreName} needs a rom to load");
			return new SynthSharp(rom.FileData);
		}
	}
}
