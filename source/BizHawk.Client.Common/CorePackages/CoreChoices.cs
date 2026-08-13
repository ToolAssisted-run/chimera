#nullable enable

using System.Collections.Generic;
using System.Linq;

using BizHawk.Emulation.Common;

namespace BizHawk.Client.Common
{
	/// <summary>One core that can run the loaded system, and whether it is the one running.</summary>
	public sealed class CoreChoice
	{
		public string CoreName { get; init; } = "";

		public bool IsCurrent { get; init; }
	}

	/// <summary>
	/// Which cores a system can be run with. Nothing stops two packages from claiming the same
	/// system - a faster core and a more accurate one, say - and when that happens the user has to
	/// be able to say which one a rom opens with, or the answer is whatever happened to register
	/// first. What they pick becomes that system's default (<see cref="Config.DefaultCores"/>),
	/// which <c>RomLoader</c> reads on the next load. Nothing writes an entry the user did not ask
	/// for: a system with one core never appears there.
	/// </summary>
	public static class CoreChoices
	{
		/// <summary>Systems some loaded package can emulate, in a stable display order.</summary>
		public static IReadOnlyList<string> SystemsWithCores()
			=> CoreRegistry.Instance.AllFactories
				.SelectMany(static f => f.SystemIds)
				.Distinct()
				.OrderBy(static id => id, System.StringComparer.OrdinalIgnoreCase)
				.ToList();

		/// <summary>
		/// The core a rom for <paramref name="systemId"/> would open with right now: the system's
		/// default if one was ever chosen, else whichever core registered first, which is what
		/// <c>RomLoader</c> falls back to. Null if no core claims the system.
		/// </summary>
		public static string? EffectiveCoreName(Config config, string systemId)
		{
			if (config.DefaultCores.TryGetValue(systemId, out var preferred)
				&& CoreRegistry.Instance.GetFactories(systemId).Any(f => f.CoreName == preferred))
			{
				return preferred;
			}
			return CoreRegistry.Instance.GetFactories(systemId).FirstOrDefault()?.CoreName;
		}

		/// <summary>Cores registered for <paramref name="systemId"/>, in a stable display order.</summary>
		public static IReadOnlyList<CoreChoice> For(string systemId, string? currentCoreName)
			=> For(CoreRegistry.Instance.GetFactories(systemId), currentCoreName);

		public static IReadOnlyList<CoreChoice> For(IEnumerable<ICoreFactory> factories, string? currentCoreName)
			=> factories
				.Select(static f => f.CoreName)
				.Distinct()
				.OrderBy(static name => name, System.StringComparer.OrdinalIgnoreCase)
				.Select(name => new CoreChoice { CoreName = name, IsCurrent = name == currentCoreName })
				.ToList();

		/// <summary>
		/// Makes <paramref name="coreName"/> the core <paramref name="systemId"/>'s roms open with.
		/// Returns false if that was already the case, so the caller can skip the reload.
		/// </summary>
		public static bool MakeDefault(Config config, string systemId, string coreName)
		{
			if (config.DefaultCores.TryGetValue(systemId, out var existing) && existing == coreName) return false;
			config.DefaultCores[systemId] = coreName;
			return true;
		}
	}
}
