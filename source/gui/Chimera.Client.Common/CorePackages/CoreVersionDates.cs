#nullable enable

using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Text.RegularExpressions;

using Newtonsoft.Json;
using Newtonsoft.Json.Linq;

namespace Chimera.Client.Common
{
	/// <summary>
	/// When a core version was made (issue #67). A commit says which version a package IS and
	/// nothing about which of two is newer, so every place that lists a version shows its date too.
	///
	/// The package says (<c>versionDate</c>, stamped by the core's build script). A package from
	/// before packages said is looked up in what the core manager last heard from the cores'
	/// release indexes - its feed cache, read from disk and never fetched here: showing a list
	/// must not be what puts the frontend on the network. A package neither knows has no date,
	/// and is listed without one rather than with a guess; the file's own time in particular is
	/// when it was copied here, which is not the question.
	/// </summary>
	public static class CoreVersionDates
	{
		private static readonly Regex Commit = new("[0-9a-f]{8,40}", RegexOptions.Compiled);

		private static Dictionary<string, DateTimeOffset>? _published;
		private static string? _publishedFrom;

		public static DateTimeOffset? Parse(string? text)
			=> DateTimeOffset.TryParse(
				text,
				System.Globalization.CultureInfo.InvariantCulture,
				System.Globalization.DateTimeStyles.AssumeUniversal,
				out var parsed) ? parsed : null;

		public static DateTimeOffset? Of(DiscoveredCorePackage package)
			=> package.VersionDate ?? Published(package.Version);

		/// <summary>When the commit in <paramref name="version"/> was first published, if any cached feed lists it.</summary>
		public static DateTimeOffset? Published(string version, string? cacheDir = null)
		{
			var commit = Commit.Match(version);
			if (!commit.Success) return null;
			return Load(cacheDir ?? Path.Combine(CoreStore.Path, ".feed-cache"))
				.TryGetValue(commit.Value.Substring(0, 8), out var when) ? when : null;
		}

		/// <summary>
		/// The order versions are offered in: cores where they were, and the versions of one core
		/// newest first - so the first of a core is its latest, which is what a picker opens on and
		/// what "the first that matches" finds. A version with no known date goes after those that
		/// have one, and otherwise nothing moves.
		/// </summary>
		public static IReadOnlyList<DiscoveredCorePackage> NewestFirst(IEnumerable<DiscoveredCorePackage> packages)
			=> NewestFirst(packages, Of);

		public static IReadOnlyList<DiscoveredCorePackage> NewestFirst(
			IEnumerable<DiscoveredCorePackage> packages, Func<DiscoveredCorePackage, DateTimeOffset?> dateOf)
		{
			var list = packages.ToList();
			var firstSeen = list
				.Select(static (p, i) => (p.Name, i))
				.GroupBy(static x => x.Name, StringComparer.OrdinalIgnoreCase)
				.ToDictionary(static g => g.Key, static g => g.Min(static x => x.i), StringComparer.OrdinalIgnoreCase);
			return list
				.OrderBy(p => firstSeen[p.Name]) // stable: equal keys keep the order they came in
				.ThenByDescending(p => dateOf(p) ?? DateTimeOffset.MinValue)
				.ToList();
		}

		/// <summary>Forgets what was read, so the next question reads the cache again (after a check for updates).</summary>
		public static void Refresh()
		{
			lock (Gate) _published = null;
		}

		private static readonly object Gate = new();

		private static Dictionary<string, DateTimeOffset> Load(string cacheDir)
		{
			lock (Gate)
			{
				if (_published is not null && _publishedFrom == cacheDir) return _published;
			}
			Dictionary<string, DateTimeOffset> found = new(StringComparer.OrdinalIgnoreCase);
			try
			{
				foreach (var file in Directory.Exists(cacheDir) ? Directory.GetFiles(cacheDir, "*.json") : [ ])
				{
					try
					{
						Read(File.ReadAllText(file), found);
					}
					catch (Exception)
					{
						// a damaged cache file is a cache miss, as it is for the feed itself
					}
				}
			}
			catch (Exception)
			{
				// no cache directory to read is no dates, not an error
			}
			// built aside and published whole: a list being painted on one thread never sees half a table
			lock (Gate)
			{
				_published = found;
				_publishedFrom = cacheDir;
			}
			return found;
		}

		private static void Read(string cacheFile, Dictionary<string, DateTimeOffset> found)
		{
			var body = JObject.Parse(cacheFile).Value<string>("body");
			if (string.IsNullOrEmpty(body)) return;
			// dates stay text, as in CoreReleases.Parse: Mono will not cast the DateTime Newtonsoft makes
			using JsonTextReader reader = new(new StringReader(body!)) { DateParseHandling = DateParseHandling.None };
			foreach (var release in JArray.Load(reader).OfType<JObject>())
			{
				var when = Parse(release.Value<string>("published_at")) ?? Parse(release.Value<string>("created_at"));
				if (when is null) continue;
				foreach (var asset in release["assets"]?.OfType<JObject>() ?? Enumerable.Empty<JObject>())
				{
					var name = asset.Value<string>("name") ?? "";
					if (!name.EndsWith(CorePackageDiscovery.Extension, StringComparison.OrdinalIgnoreCase)) continue;
					foreach (Match commit in Commit.Matches(name))
					{
						// one commit is published more than once (dev, then a nightly): the first is when it appeared
						var key = commit.Value.Substring(0, 8);
						if (!found.TryGetValue(key, out var seen) || when.Value < seen) found[key] = when.Value;
					}
				}
			}
		}
	}
}
