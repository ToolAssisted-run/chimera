using System.Diagnostics;
using System.IO;

using Chimera.Common;
using Chimera.Common.IOExtensions;
using Chimera.Emulation.Common;

namespace Chimera.Client.Common
{
	/// <summary>
	/// A rom file read into memory, with a generic <see cref="GameInfo"/> derived from it.
	/// Chimera knows nothing about rom formats: no header detection, no per-system
	/// preprocessing: cores receive the file as-is (any format knowledge belongs to the
	/// core package). The identifying hash is of the whole file.
	/// </summary>
	public class RomGame
	{
		public byte[] RomData { get; private set; }
		public byte[] FileData { get; private set; }
		public GameInfo GameInfo { get; }
		public string Extension { get; }

		public RomGame(HawkFile file)
			: this(file, null)
		{
		}

		/// <exception cref="Exception"><paramref name="file"/> does not exist</exception>
		public RomGame(HawkFile file, string patch)
		{
			if (!file.Exists)
			{
				throw new Exception("The file needs to exist, yo.");
			}

			Extension = file.Extension.ToUpperInvariant();

			var stream = file.GetStream();
			int fileLength = (int)stream.Length;
			FileData = new byte[fileLength];
			stream.Position = 0;
			var bytesRead = stream.Read(FileData, offset: 0, count: fileLength);
			Debug.Assert(bytesRead == fileLength, "failed to read whole rom stream");
			RomData = FileData;

			GameInfo = new GameInfo
			{
				Name = Path.GetFileNameWithoutExtension(file.Name).Replace('_', ' '),
				Hash = Chimera.Emulation.Common.Engine.ChimeraEngine.Sha1Hex(FileData),
				System = null, // resolved by the frontend (core package extension map, user preference, or prompt)
				Status = RomStatus.NotInDatabase,
				NotInDatabase = true,
			};

			if (patch is null) return;
			using var patchFile = new HawkFile(patch);
			patchFile.BindFirstOf(".ips");
			if (!patchFile.IsBound) patchFile.BindFirstOf(".bps");
			if (!patchFile.IsBound) return;
			var patchBytes = patchFile.GetStream().ReadAllBytes();
			if (BPSPatcher.IsIPSFile(patchBytes))
			{
				RomData = BPSPatcher.Patch(RomData, new BPSPatcher.IPSPayload(patchBytes));
			}
			else if (BPSPatcher.IsBPSFile(patchBytes, out var patchStruct))
			{
				var ignoreBaseChecksum = true; //TODO check base checksum and ask user before continuing
				RomData = BPSPatcher.Patch(RomData, patchStruct, out var checksumsMatch);
				if (!checksumsMatch && !ignoreBaseChecksum) throw new Exception("BPS patch didn't produce the expected output");
			}
			else
			{
				throw new Exception("doesn't appear to be a BPS or IPS patch");
			}
		}
	}
}
