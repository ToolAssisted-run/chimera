using System.IO;

using Chimera.Common.StringExtensions;

namespace Chimera.Common
{
	public static partial class VersionInfo
	{
		public static readonly string HomePage = "https://github.com/ToolAssisted-run/chimera";

		public static readonly string? CustomBuildString;

		public static readonly string UserAgentEscaped;

		static VersionInfo()
		{
			var path = Path.Combine(
				AppContext.BaseDirectory.RemoveSuffix(Path.DirectorySeparatorChar),
				"dll",
				"custombuild.txt"
			);
			if (File.Exists(path))
			{
				var lines = File.ReadAllLines(path);
				if (lines.Length > 0)
				{
					CustomBuildString = lines[0];
				}
			}
			UserAgentEscaped = $"{
				(string.IsNullOrWhiteSpace(CustomBuildString) ? "Chimera" : CustomBuildString!.OnlyAlphanumeric())
			}/{GIT_SHORTHASH}";
		}

		/// <summary>The project's site.</summary>
		public static readonly string SiteURI = "https://toolassisted.run";

		/// <summary>The commit this build was made from, on the web.</summary>
		public static readonly string CommitURI = $"https://github.com/ToolAssisted-run/chimera/commit/{GIT_HASH}";

		/// <summary>
		/// What this build is called wherever it is named - the About box, a bug
		/// report: its commit, and when that commit was made.
		/// </summary>
		public static string GetBuildName()
			=> $"Commit {GIT_SHORTHASH} ({GIT_COMMITTIME} UTC)";

		/// <summary>
		/// Chimera has no versions: a build is identified by its commit and that
		/// commit's date (never a build wall-clock, which would break reproducible
		/// builds).
		/// </summary>
		public static string GetEmuVersion()
			=> $"Commit {GIT_SHORTHASH}";

	}
}
