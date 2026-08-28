using System.Collections.Generic;
using System.IO;

namespace Chimera.Common
{
	/// <summary>Used by <see cref="ChimeraFile"/> to delegate archive management.</summary>
	public interface IFileDearchivalMethod<out T> where T : IChimeraArchiveFile
	{
		/// <remarks>TODO could this receive a <see cref="ChimeraFile"/> itself? possibly handy, in very clever scenarios of mounting fake files</remarks>
		bool CheckSignature(string fileName);

		/// <remarks>for now, only used in tests</remarks>
		bool CheckSignature(Stream fileStream, string? filenameHint = null);

		IReadOnlyCollection<string> AllowedArchiveExtensions { get; }

		T Construct(string path);

		/// <remarks>for now, only used in tests</remarks>
		T Construct(Stream fileStream);
	}
}
