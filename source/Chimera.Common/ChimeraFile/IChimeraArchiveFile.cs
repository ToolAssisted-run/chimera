using System.Collections.Generic;
using System.IO;

namespace Chimera.Common
{
	/// <seealso cref="IFileDearchivalMethod{T}"/>
	public interface IChimeraArchiveFile : IDisposable
	{
		void ExtractFile(int index, Stream stream);

		/// <returns><see langword="null"/> on failure</returns>
		List<ChimeraArchiveFileItem>? Scan();
	}
}
