#nullable enable

using Chimera.Emulation.Common;

namespace Chimera.Client.Common
{
	/// <summary>
	/// A game core's properties as the watch tools see them (docs/game-cores.md): an
	/// element is a watch named after it - one of the tools' own byte, word or double
	/// word watches when it fits one as it is, and otherwise a <see cref="PropertyWatch"/>
	/// the engine reads. The name is the watch's note, which is also what a freeze made
	/// from it is called - so a property frozen from RAM Watch or RAM Search is listed in
	/// the cheats by its name.
	/// </summary>
	public static class GamePropertyWatches
	{
		/// <summary>The watch that reads <paramref name="element"/> in <paramref name="domain"/>, named after it.</summary>
		public static Watch WatchOf(IGameProperties properties, GamePropertyElement element, MemoryDomain domain)
		{
			var property = element.Property;
			if (!property.FitsAPlainWatch) return new PropertyWatch(properties, element, domain);
			return Watch.GenerateWatch(
				domain,
				element.Offset,
				property.Size switch { 1 => WatchSize.Byte, 2 => WatchSize.Word, _ => WatchSize.DWord },
				property.Type is GamePropertyType.F32 ? WatchDisplayType.Float
					: property.Type is GamePropertyType.S8 or GamePropertyType.S16 or GamePropertyType.S32 ? WatchDisplayType.Signed
					: WatchDisplayType.Unsigned,
				property.BigEndian,
				note: element.Name);
		}

		/// <summary>
		/// Names a watch after the element that starts where it points, when it has no note
		/// of its own; a note somebody wrote is never replaced. Returns the watch.
		/// </summary>
		public static Watch Named(Watch watch, IGameProperties? properties)
		{
			if (properties is null || watch.IsSeparator || watch.Domain is null || !string.IsNullOrEmpty(watch.Notes)) return watch;
			if (properties.At(watch.Domain.Name, watch.Address, out var starts) is { } element && starts) watch.Notes = element.Name;
			return watch;
		}

		/// <summary>The element that starts at an address, or null: how a list of addresses names the ones that are properties.</summary>
		public static GamePropertyElement? StartingAt(IGameProperties? properties, MemoryDomain? domain, long address)
			=> properties is not null && domain is not null && properties.At(domain.Name, address, out var starts) is { } element && starts
				? element
				: null;
	}
}
