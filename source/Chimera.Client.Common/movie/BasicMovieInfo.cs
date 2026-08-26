using System.Collections.Generic;
using System.Globalization;
using System.IO;

using Chimera.Emulation.Common;
using Chimera.Emulation.Common.Engine;

namespace Chimera.Client.Common
{
	public class BasicMovieInfo : IBasicMovieInfo
	{
		private string _filename;
		private bool IsPal => Header[HeaderKeys.Pal] == "1";

		protected readonly Bk2Header Header = new();

		public BasicMovieInfo(string filename)
		{
			if (string.IsNullOrWhiteSpace(filename))
			{
				throw filename is null
					? new ArgumentNullException(paramName: nameof(filename))
					: new ArgumentException(message: "path cannot be blank", paramName: nameof(filename));
			}

			Filename = filename;
		}

		public string Name { get; private set; }

		public string Filename
		{
			get => _filename;
			set
			{
				_filename = value;
				Name = Path.GetFileName(Filename);
			}
		}

		public virtual int FrameCount { get; private set; }

		public TimeSpan TimeLength
		{
			get
			{
				double numSeconds;

				if (Header.TryGetValue(HeaderKeys.CycleCount, out var numCyclesStr) && Header.TryGetValue(HeaderKeys.ClockRate, out var clockRateStr))
				{
					var numCycles = ulong.Parse(numCyclesStr);
					var clockRate = double.Parse(clockRateStr, CultureInfo.InvariantCulture);
					numSeconds = numCycles / clockRate;
				}
				else
				{
					var numFrames = (ulong)FrameCount;
					numSeconds = numFrames / FrameRate;
				}

				return TimeSpan.FromSeconds(numSeconds);
			}
		}

		public double FrameRate
		{
			get
			{
				if (SystemID == VSystemID.Raw.Arcade && Header.TryGetValue(HeaderKeys.VsyncAttoseconds, out var vsyncAttoStr))
				{
					const decimal attosInSec = 1_000_000_000_000_000_000.0M;
					var m = attosInSec;
					m /= ulong.Parse(vsyncAttoStr);
					return decimal.ToDouble(m);
				}

				return PlatformFrameRates.GetFrameRate(SystemID, IsPal);
			}
		}

		public SubtitleList Subtitles { get; } = new();
		public IList<string> Comments { get; } = new List<string>();

		public virtual string GameName
		{
			get => Header[HeaderKeys.GameName];
			set => Header[HeaderKeys.GameName] = value;
		}

		public virtual string SystemID
		{
			get => Header[HeaderKeys.Platform];
			set => Header[HeaderKeys.Platform] = value;
		}

		public virtual ulong Rerecords
		{
			get => Header.TryGetValue(HeaderKeys.Rerecords, out var rerecords) ? ulong.Parse(rerecords) : 0;
			set => Header[HeaderKeys.Rerecords] = value.ToString();
		}

		public virtual string Hash
		{
			get => Header[HeaderKeys.Sha1].ToUpperInvariant();
			set => Header[HeaderKeys.Sha1] = value;
		}

		public virtual string Author
		{
			get => Header[HeaderKeys.Author];
			set => Header[HeaderKeys.Author] = value;
		}

		public virtual string Core
		{
			get => Header[HeaderKeys.Core];
			set => Header[HeaderKeys.Core] = value;
		}

		public virtual string BoardName
		{
			get => Header[HeaderKeys.BoardName];
			set => Header[HeaderKeys.BoardName] = value;
		}

		public virtual string EmulatorVersion
		{
			get => Header[HeaderKeys.EmulatorVersion];
			set => Header[HeaderKeys.EmulatorVersion] = value;
		}

		public virtual string OriginalEmulatorVersion
		{
			get => Header[HeaderKeys.OriginalEmulatorVersion];
			set => Header[HeaderKeys.OriginalEmulatorVersion] = value;
		}

		public IDictionary<string, string> HeaderEntries => Header;

		public bool Load()
		{
			if (!File.Exists(Filename))
			{
				return false;
			}

			// the JSON project (docs/project.md) is the only movie-bearing file
			// chimera reads; anything else - a zip movie of any provenance
			// included - is refused with its reason
			if (!LooksLikeProjectJson(Filename))
			{
				throw new InvalidOperationException(
					"this is not a chimera project; chimera reads no other movie format (docs/project.md)");
			}
			return LoadProjectFormat();
		}

		private static bool LooksLikeProjectJson(string path)
		{
			using var fs = File.OpenRead(path);
			int b;
			while ((b = fs.ReadByte()) is not -1)
			{
				if (b is ' ' or '\t' or '\r' or '\n') continue;
				return b is '{';
			}
			return false;
		}

		/// <summary>Only the TAS movie understands the project format; anything else refuses.</summary>
		protected virtual bool LoadProjectFormat() => false;

		protected virtual void ClearBeforeLoad()
		{
			Header.Clear();
			Subtitles.Clear();
			Comments.Clear();
		}
	}
}
