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
	/// first. The choice is remembered per system in <see cref="Config.PreferredCores"/> and read
	/// back by <c>RomLoader</c> on the next load.
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
		/// The core a rom for <paramref name="systemId"/> would open with right now: the remembered
		/// preference, or - when there is none - whichever core registered first, which is what
		/// <c>RomLoader</c> falls back to. Null if no core claims the system.
		/// </summary>
		public static string? EffectiveCoreName(Config config, string systemId)
		{
			if (config.PreferredCores.TryGetValue(systemId, out var preferred)
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
		/// Remembers <paramref name="coreName"/> as the core for <paramref name="systemId"/>.
		/// Returns false if that was already the choice, so the caller can skip the reload.
		/// </summary>
		public static bool Prefer(Config config, string systemId, string coreName)
		{
			if (config.PreferredCores.TryGetValue(systemId, out var existing) && existing == coreName) return false;
			config.PreferredCores[systemId] = coreName;
			return true;
		}
	}
}
