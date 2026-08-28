using System.IO;

using Chimera.Common.StringExtensions;

namespace Chimera.Common
{
	public static partial class VersionInfo
	{
		public static readonly string HomePage = "https://github.com/ToolAssisted-run/chimera";

		public static readonly string? CustomBuildString;

		/// <summary>Chimera's contributors, and through them BizHawk's: the About box links here.</summary>
		public static readonly string CreditsListURI = "https://github.com/ToolAssisted-run/chimera/blob/main/CREDITS.md";

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

		public static (string Label, string TargetURI) GetGitCommitLink()
			=> ($"Commit {GIT_SHORTHASH} ({GIT_SHORTDATE})", $"https://github.com/ToolAssisted-run/chimera/commit/{GIT_HASH}");

		/// <summary>
		/// Chimera has no versions: a build is identified by its commit and that
		/// commit's date (never a build wall-clock, which would break reproducible
		/// builds).
		/// </summary>
		public static string GetFullVersionDetails()
		{
			//TODO prepare for AArch64/RISC-V
			var targetArch = UIntPtr.Size is 8 ? "x64" : "x86";
#if DEBUG
			const string buildConfig = "Debug";
#else
			const string buildConfig = "Release";
#endif
			return $"Commit {GIT_SHORTHASH} ({buildConfig}, {targetArch})";
		}

		public static string GetEmuVersion()
			=> $"Commit {GIT_SHORTHASH}";

	}
}
