#nullable enable

using System.IO;

using BizHawk.Common;
using BizHawk.Emulation.Common.Engine;

namespace BizHawk.Client.Common
{
	/// <summary>
	/// Writes the savestate/movie container. The engine renders the archive
	/// (see docs/engine-migration.md); this class keeps what was always the C#'s
	/// job - the callback-based lump API and the temp-file/backup dance around
	/// the actual file. As before, a failed lump poisons the write and the error
	/// surfaces at <see cref="CloseAndDispose"/>, not at the failing PutLump.
	/// </summary>
	public class ZipStateSaver : IDisposable
	{
		private readonly EngineStateWriter _writer;
		private FileWriter? _fs;
		private Exception? _writeException;
		private bool _isDisposed;

		private ZipStateSaver(EngineStateWriter writer, FileWriter fs)
		{
			_writer = writer;
			_fs = fs;
		}

		public static FileWriteResult<ZipStateSaver> Create(string path, int compressionLevel)
		{
			FileWriteResult<FileWriter> fs = FileWriter.Create(path);
			if (fs.IsError) return new(fs);
			return fs.Convert(new ZipStateSaver(new(compressionLevel, VersionInfo.GetEmuVersion()), fs.Value!));
		}

		/// <summary>
		/// This method must be called after writing has finished and must not be called twice.
		/// Dispose will be called regardless of the result.
		/// </summary>
		/// <param name="backupPath">If not null, renames the original file to this path.</param>
		public FileWriteResult CloseAndDispose(string? backupPath = null)
		{
			if (_fs == null) throw new ObjectDisposedException("Cannot use disposed ZipStateSaver.");
			FileWriteResult result;
			if (_writeException == null)
			{
				try
				{
					var bytes = _writer.Finish();
					_fs.Stream.Write(bytes, 0, bytes.Length);
				}
				catch (Exception ex)
				{
					_writeException = ex;
				}
			}
			if (_writeException == null)
			{
				result = _fs.CloseAndDispose(backupPath);
			}
			else
			{
				result = new(FileWriteEnum.FailedDuringWrite, _fs.Paths, _writeException);
				_fs.Abort();
			}
			Dispose();
			return result;
		}

		/// <summary>
		/// Closes and deletes the file. Use if there was an error while writing.
		/// Do not call <see cref="CloseAndDispose"/> after this.
		/// </summary>
		public void Abort()
		{
			if (_fs == null) throw new ObjectDisposedException("Cannot use disposed ZipStateSaver.");
			_fs.Abort();
			Dispose();
		}

		public void PutLump(BinaryStateLump lump, Action<Stream> callback, bool zstdCompress = true)
		{
			if (_writeException != null) return;
			try
			{
				using MemoryStream ms = new();
				callback(ms);
				if (!_writer.PutLump(lump.Name, lump.Ext, zstdCompress, ms.ToArray()))
				{
					throw new IOException($"engine could not add {lump.FileName}: {_writer.LastError}");
				}
			}
			catch (Exception ex)
			{
				_writeException = ex;
				// the failure is reported at closing, as it always was
			}
		}

		public void PutLump(BinaryStateLump lump, Action<BinaryWriter> callback)
		{
			PutLump(lump, s =>
			{
				var bw = new BinaryWriter(s);
				callback(bw);
				bw.Flush();
			});
		}

		public void PutLump(BinaryStateLump lump, Action<TextWriter> callback, bool zstdCompress = false)
		{
			PutLump(lump, s =>
			{
				TextWriter tw = new StreamWriter(s);
				callback(tw);
				tw.Flush();
			}, zstdCompress: zstdCompress);
		}

		public void Dispose()
		{
			if (_isDisposed) return;
			_isDisposed = true;
			_writer.Dispose();
			_fs?.Dispose();
			_fs = null;
			GC.SuppressFinalize(this);
		}
	}
}
