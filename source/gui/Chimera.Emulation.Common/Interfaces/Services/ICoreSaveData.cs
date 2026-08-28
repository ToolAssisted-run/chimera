namespace Chimera.Emulation.Common
{
	/// <summary>
	/// Save data the CORE keeps and the USER takes out (docs/save-data.md).
	///
	/// A core with save data (memory card files, a disk image) keeps it inside
	/// the guest machine, where savestates and rewind already handle it; this
	/// service is only the way OUT - the core's own enumeration of
	/// (relative path, bytes), which the frontend writes verbatim on the user's
	/// explicit request and never interprets. There is no change detection, no
	/// autosave, and no import side here: exported files come back as game
	/// inputs, mounted hash-bound and cited by the movie header.
	///
	/// The service is registered exactly when the core exports the savedata
	/// guest ABI group; a core whose persistent state is plain machine memory
	/// exports nothing and the Export Save Data menu item never appears.
	/// </summary>
	public interface ICoreSaveData : IEmulatorService
	{
		/// <summary>
		/// Snapshots the core's exportable files and returns how many there are.
		/// The list is dynamic (a game creates files while it runs), so take a
		/// fresh snapshot per export; the accessors below refer to the last one.
		/// Zero is a valid answer - a memory card with nothing on it yet.
		/// </summary>
		int SaveDataSnapshot();

		/// <summary>The file's relative '/'-separated path - the zip entry name.</summary>
		string SaveDataName(int index);

		long SaveDataSize(int index);

		/// <summary>
		/// Copies bytes [offset, offset+buffer.Length) of file <paramref name="index"/>
		/// into <paramref name="buffer"/>; returns the count copied (clamped at the
		/// file's end). Ranged so a huge file streams in chunks.
		/// </summary>
		int SaveDataRead(int index, long offset, byte[] buffer);
	}
}
