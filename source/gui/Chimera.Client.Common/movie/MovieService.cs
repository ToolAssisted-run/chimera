using System.Collections.Generic;
using System.Globalization;
using System.Linq;
using Chimera.Common.NumberExtensions;

namespace Chimera.Client.Common
{
	public static class MovieService
	{
		public static string TasMovieExtension => TasMovie.Extension;

		/// <summary>
		/// The name a project carries before it has one of its own. A movie
		/// filed under this is UNWRITTEN: saving it asks where it goes instead
		/// of writing here, which is what lets a new project start with no
		/// question about files at all.
		/// </summary>
		public const string UnsavedProjectName = "default";

		/// <summary>The unwritten project's stand-in path, under the movies directory.</summary>
		public static string UnsavedProjectPath(PathEntryCollection paths)
			=> System.IO.Path.Combine(paths.MovieAbsolutePath(), $"{UnsavedProjectName}.{TasMovie.Extension}");

		/// <summary>
		/// Gets a list of extensions for all <see cref="IMovie"/> implementations
		/// </summary>
		public static IEnumerable<string> MovieExtensions => new[] { TasMovie.Extension };

		public static bool IsValidMovieExtension(string ext)
		{
			return MovieExtensions.Contains(ext.Replace(".", ""), StringComparer.OrdinalIgnoreCase);
		}

		public static bool IsCurrentTasVersion(string movieVersion)
		{
			var actual = ParseTasMovieVersion(movieVersion);
			return actual.ApproxFloatEquality(TasMovie.CurrentVersion);
		}

		internal static double ParseTasMovieVersion(string movieVersion)
		{
			if (string.IsNullOrWhiteSpace(movieVersion))
			{
				return 1.0F;
			}

			// the header reads "Chimera Project File vN.N"; the version is whatever
			// follows the format name. (It read "Chimera Tasproj vN.N" through
			// the BizHawk lineage - renamed 2026-09-05, no longer accepted.)
			var split = movieVersion
				.ToLowerInvariant()
				.Split(new[] {"project file"}, StringSplitOptions.RemoveEmptyEntries);

			if (split.Length == 1)
			{
				return 1.0F;
			}

			var versionStr = split[1]
				.Trim()
				.Replace("v", "");

			if (double.TryParse(versionStr, NumberStyles.Float, CultureInfo.InvariantCulture, out var parsedWithPeriod))
			{
				return parsedWithPeriod;
			}

			// Accept version numbers written where the host culture used ',' for the decimal point
			if (double.TryParse(versionStr.Replace(',', '.'), NumberStyles.Float, CultureInfo.InvariantCulture, out var parsedWithComma))
			{
				return parsedWithComma;
			}

			return 1.0F;
		}
	}
}
