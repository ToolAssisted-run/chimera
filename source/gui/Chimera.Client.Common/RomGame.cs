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

		public RomGame(ChimeraFile file)
			: this(file, null)
		{
		}

		/// <exception cref="Exception"><paramref name="file"/> does not exist</exception>
		public RomGame(ChimeraFile file, string patch)
		{
			if (!file.Exists)
			{
				throw new Exception("The file needs to exist, yo.");
			}

			Extension = file.Extension.ToUpperInvariant();

			var stream = file.GetStream();
			long longLength = stream.Length;

			string hash;
			// A disc-sized file never becomes a byte array: the waterbox
			// adapter mounts it from where it lies (RomPath), and a 2GB+
			// image would overflow the array anyway. Only the identity
			// hash needs the bytes, and it can stream.
			const long StreamHashThreshold = 512L * 1024 * 1024;
			if (longLength > StreamHashThreshold)
			{
				stream.Position = 0;
				using var sha1 = System.Security.Cryptography.SHA1.Create();
				var digest = sha1.ComputeHash(stream);
				hash = string.Concat(Array.ConvertAll(digest, b => b.ToString("X2")));
				FileData = RomData = Array.Empty<byte>();
			}
			else
			{
				int fileLength = (int)longLength;
				FileData = new byte[fileLength];
				stream.Position = 0;
				var bytesRead = stream.Read(FileData, offset: 0, count: fileLength);
				Debug.Assert(bytesRead == fileLength, "failed to read whole rom stream");
				RomData = FileData;
				hash = Chimera.Emulation.Common.Engine.ChimeraEngine.Sha1Hex(FileData);
			}

			GameInfo = new GameInfo
			{
				Name = Path.GetFileNameWithoutExtension(file.Name).Replace('_', ' '),
				Hash = hash,
				System = null, // resolved by the frontend (core package extension map, user preference, or prompt)
				Status = RomStatus.NotInDatabase,
				NotInDatabase = true,
			};

			if (patch is null) return;
			using var patchFile = new ChimeraFile(patch);
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
