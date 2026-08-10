#nullable disable

using System.Collections.Generic;
using System.Linq;

namespace BizHawk.Emulation.Common
{
	/// <summary>
	/// miniHawk carries no built-in firmware knowledge — firmware requirements are
	/// core-specific and belong to core packages. The firmware *mechanism*
	/// (FirmwareManager, CoreComm firmware provider, this database's shape) is generic
	/// and stays; a future core-contract story can let packages register records here.
	/// </summary>
	public static class FirmwareDatabase
	{
		public static IEnumerable<FirmwareFile> FirmwareFiles => FirmwareFilesByHash.Values;

		public static readonly IReadOnlyDictionary<string, FirmwareFile> FirmwareFilesByHash
			= new Dictionary<string, FirmwareFile>();

		public static readonly IReadOnlyDictionary<FirmwareOption, FirmwareFile> FirmwareFilesByOption
			= new Dictionary<FirmwareOption, FirmwareFile>();

		public static readonly IReadOnlyCollection<FirmwareOption> FirmwareOptions = [ ];

		public static readonly IReadOnlyCollection<FirmwareRecord> FirmwareRecords = [ ];

		public static readonly IReadOnlyList<FirmwarePatchOption> AllPatches = [ ];

		public static FirmwareRecord? LookupFirmwareRecord(FirmwareID id)
		{
			foreach (var fr in FirmwareRecords)
			{
				if (fr.ID == id) return fr;
			}
			return null;
		}
	}
}
