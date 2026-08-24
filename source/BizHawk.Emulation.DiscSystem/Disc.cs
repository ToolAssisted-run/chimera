using System.Collections.Generic;

// ARCHITECTURE NOTE:
// No provisions are made for caching synthesized data for later accelerated use.
// This is because, in the worst case that might result in synthesizing an entire disc in memory.
// (Upstream BizHawk's answer was to pre-convert the disc with its DiscoHawk tool; that tool does not exist here.)

// TODO: in principle, we could mount audio to decode only on an as-needed basis
// this might result in hiccups during emulation, though, so it should be an option.
// This would imply either decode-length processing (scan file without decoding) or decoding and discarding the data.
// Alternate policies would probably be associated with copious warnings (examples: ? ? ?)

namespace BizHawk.Emulation.DiscSystem
{
	public sealed class Disc : IDisposable
	{
		/// <summary>
		/// Automagically loads a disc, without any fine-tuned control at all
		/// </summary>
		public static Disc LoadAutomagic(string path)
		{
			var job = new DiscMountJob(fromPath: path/*, discInterface: DiscInterface.MednaDisc <-- TEST*/);
			job.Run();
			return job.OUT_Disc;
		}

		/// <summary>
		/// This is a 1-indexed list of sessions (session 1 is at [1])
		/// To prevent duplicate Add(null) calls around the code, we'll have it already have [0] with null
		/// So the first Add() call will put a session at [1], the second will put a session at [2], and so on
		/// </summary>
		public readonly IList<DiscSession> Sessions = new List<DiscSession> { null };

		/// <summary>
		/// Session 1 of the disc, since that's all that's needed most of the time.
		/// </summary>
		public DiscSession Session1 => Sessions[1];

		/// <summary>
		/// The DiscTOC corresponding to Session1.
		/// </summary>
		public DiscTOC TOC => Session1.TOC;

		/// <summary>
		/// The name of a disc. Loosely based on the filename. Just for informational purposes.
		/// </summary>
		public string Name;

		/// <summary>
		/// Free-form optional memos about the disc
		/// </summary>
		public readonly IDictionary<string, object> Memos = new Dictionary<string, object>();

		public void Dispose()
		{
			foreach (var res in DisposableResources)
			{
				res.Dispose();
			}
		}

		/// <summary>
		/// Easily extracts a mode1 sector range (suitable for extracting ISO FS data files)
		/// </summary>
		public byte[] Easy_Extract_Mode1(int lba_start, int lba_count, int byteLength = -1)
		{
			int totsize = lba_count * 2048;
			byte[] ret = new byte[totsize];
			var dsr = new DiscSectorReader(this) { Policy = { DeterministicClearBuffer = false } };
			for (int i = 0; i < lba_count; i++)
			{
				dsr.ReadLBA_2048(lba_start + i, ret, i*2048);
			}
			if (byteLength != -1 && byteLength != totsize)
			{
				byte[] newret = new byte[byteLength];
				Array.Copy(ret, newret, byteLength);
				return newret;
			}
			return ret;
		}

//		/// <summary>
//		/// The DiscMountPolicy used to mount the disc. Consider this read-only.
//		/// NOT SURE WE NEED THIS
//		/// </summary>
//		public DiscMountPolicy DiscMountPolicy;



		//----------------------------------------------------------------------------

		/// <summary>
		/// Disposable resources (blobs, mostly) referenced by this disc
		/// </summary>
		internal readonly IList<IDisposable> DisposableResources = new List<IDisposable>();

		/// <summary>
		/// The sectors on the disc. Don't use this directly! Use the SectorSynthProvider instead.
		/// TODO - eliminate this entirely and do entirely with the delegate (much faster disc loading... but massively annoying architecture inside-out logic)
		/// </summary>
		internal List<ISectorSynthJob2448> _Sectors = new();

		/// <summary>
		/// ISectorSynthProvider instance for the disc. May be daisy-chained
		/// </summary>
		internal ISectorSynthProvider SynthProvider;

		/// <summary>
		/// Parameters set during disc loading which can be referenced by the sector synthesizers
		/// </summary>
		internal SectorSynthParams SynthParams = default;

		/// <summary>
		/// Forbid public construction
		/// </summary>
		internal Disc()
		{}

		public static bool IsValidExtension(string extension)
			=> extension.ToLowerInvariant() is ".ccd" or ".cdi" or ".chd" or ".cue" or ".iso" or ".toc" or ".mds" or ".nrg";
	}
}