#nullable enable

namespace Chimera.Client.Common
{
	/// <summary>
	/// What a bug report has to say first, in the words the report's template asks
	/// for it: which build of Chimera, and which core in which version. Written
	/// here so that nobody has to read it off a window and type it out (issue
	/// #188) - a version typed from memory is how a report names the wrong one.
	/// </summary>
	public static class VersionReport
	{
		/// <param name="build">this build, as <c>VersionInfo.GetBuildName</c> names it</param>
		/// <param name="coreName">the running core, or null when none is</param>
		/// <param name="coreVersion">its version as the Core Manager lists it</param>
		public static string Text(string build, string? coreName, string? coreVersion)
		{
			var text = $"**Chimera build:** {build}";
			if (string.IsNullOrWhiteSpace(coreName)) return text;
			var core = string.IsNullOrWhiteSpace(coreVersion) ? coreName!.Trim() : $"{coreName!.Trim()} {coreVersion!.Trim()}";
			return $"{text}\n**Core and version:** {core}";
		}
	}
}
