using System.IO;

using BizHawk.Emulation.Common.Engine;

namespace BizHawk.Client.Common
{
	/// <summary>
	/// Reads the savestate/movie container. The engine parses the archive
	/// (see docs/engine-migration.md) - the lump naming rules, the ".zst"
	/// handling, the BizState version quirks and the old non-tarbomb layout all
	/// live there now. Lump callbacks receive the DECOMPRESSED length; the only
	/// caller that ever read it (the version lump's emptiness check) wanted
	/// exactly that.
	/// </summary>
	public class ZipStateLoader : IDisposable
	{
		private EngineStateReader _reader;
		private bool _isDisposed;

		public int Version => _reader.Version;

		private ZipStateLoader(EngineStateReader reader)
		{
			_reader = reader;
		}

		public void Dispose()
		{
			if (_isDisposed) return;
			_isDisposed = true;
			_reader.Dispose();
			_reader = null;
		}

		public static ZipStateLoader LoadAndDetect(string filename, bool isMovieLoad = false)
		{
			byte[] data;
			try
			{
				data = File.ReadAllBytes(filename);
			}
			catch (IOException)
			{
				return null;
			}

			var reader = EngineStateReader.Open(data, isMovieLoad);
			if (reader is null) return null;
			Console.WriteLine("Read a zipstate of version 1.0.{0}", reader.Version);
			return new(reader);
		}

		/// <param name="lump">lump to retrieve</param>
		/// <param name="abort">pass true to throw exception instead of returning false</param>
		/// <param name="callback">function to call with the desired stream</param>
		/// <returns>true iff stream was loaded</returns>
		/// <exception cref="Exception">stream not found and <paramref name="abort"/> is <see langword="true"/></exception>
		public bool GetLump(BinaryStateLump lump, bool abort, Action<Stream, long> callback)
		{
			var bytes = _reader.Lump(lump.Name, lump.Ext);
			if (bytes is null)
			{
				if (abort)
				{
					throw new Exception($"Essential zip section not found: {lump.FileName}");
				}
				return false;
			}
			using MemoryStream ms = new(bytes, writable: false);
			callback(ms, bytes.LongLength);
			return true;
		}

		public bool GetLump(BinaryStateLump lump, bool abort, Action<BinaryReader> callback)
			=> GetLump(lump, abort, (s, _) => callback(new(s)));

		public bool GetLump(BinaryStateLump lump, bool abort, Action<TextReader> callback)
			=> GetLump(lump, abort, (s, _) => callback(new StreamReader(s)));

		/// <exception cref="Exception">couldn't find Binary or Text savestate</exception>
		public void GetCoreState(Action<BinaryReader> callbackBinary, Action<TextReader> callbackText)
		{
			if (!GetLump(BinaryStateLump.Corestate, false, callbackBinary)
				&& !GetLump(BinaryStateLump.CorestateText, false, callbackText))
			{
				throw new Exception("Couldn't find Binary or Text savestate");
			}
		}
	}
}
