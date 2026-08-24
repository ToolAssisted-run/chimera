#nullable enable

namespace BizHawk.Emulation.Common.Waterbox
{
	/// <summary>
	/// The persistent-data channel: what a machine keeps when it is switched off - a
	/// cart's battery-backed RAM, a disk the game wrote to, a memory card.
	///
	/// It is deliberately NOT a memory domain the host copies out. Only the core knows
	/// what belongs in a save file: for the disk system it is the difference between
	/// the disk now and the disk as it was inserted, which exists nowhere as a block of
	/// memory. So the core serializes it, into a buffer it owns, and the engine's
	/// session moves bytes it does not interpret.
	///
	/// Optional, like the tooling groups: a core with nothing to keep (for THIS rom)
	/// answers unavailable and <see cref="ICorePersistentData"/> is unregistered,
	/// which is what leaves it out of the core's own menu.
	/// </summary>
	public sealed partial class WaterboxCore : ICorePersistentData
	{
		/// <summary>
		/// True once the machine has run a frame - the point after which what it keeps may
		/// differ from what was loaded. Cheaper and steadier than asking the core to track
		/// dirtiness, and the frontend only uses this to skip pointless writes.
		/// </summary>
		private bool _persistMaybeDirty;

		public string PersistentDataName { get; private set; } = "Persistent Data";

		public string PersistentDataId { get; private set; } = "data";

		private void InitPersistentData(BasicServiceProvider services)
		{
			if (!_session.PersistAvailable)
			{
				services.Unregister<ICorePersistentData>();
				return;
			}

			// what the core calls it, and what a bundle files it under; both are the core's
			// vocabulary, and the frontend only ever repeats them
			PersistentDataName = _session.PersistName;
			PersistentDataId = _session.PersistId;
		}

		public byte[]? ClonePersistentData(bool clearDirty = true)
		{
			if (!_session.PersistAvailable) return null;
			CheckDisposed();
			var data = _session.PersistGet();
			if (data is null) return null;
			if (clearDirty) _persistMaybeDirty = false;
			return data;
		}

		public void StorePersistentData(byte[] data)
		{
			if (!_session.PersistAvailable) return;
			CheckDisposed();
			switch (_session.PersistPut(data))
			{
				case 0:
					_persistMaybeDirty = false;
					return;
				case 2:
					throw new InvalidOperationException($"{_cfg.CoreName}: no room for {data.Length} bytes");
				default:
					// The core is the judge of whether the file fits the machine - a disk save with
					// the wrong number of sides, say - and says so by refusing it. Loading half a
					// save silently would be worse than not loading it.
					throw new InvalidOperationException($"{_cfg.CoreName}: this file does not belong to this game");
			}
		}

		public bool PersistentDataModified => _persistMaybeDirty;
	}
}
