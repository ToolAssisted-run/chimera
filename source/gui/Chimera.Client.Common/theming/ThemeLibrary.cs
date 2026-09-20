#nullable enable

using System.Collections.Generic;
using System.IO;
using System.Linq;

namespace Chimera.Client.Common
{
	/// <summary>A theme file that would not load, and why - kept so the frontend can say so once, in one place.</summary>
	public sealed record ThemeLoadFailure(string Path, string Message);

	/// <summary>
	/// The themes this build can offer, and which one is on.
	///
	/// Two are compiled in and always there; the rest are files somebody dropped
	/// in the themes folder under the data directory. A file that will not parse
	/// is left out and its reason kept in <see cref="Failures"/> - it is not
	/// applied half-way and it does not stop the frontend from starting.
	/// </summary>
	public static class ThemeLibrary
	{
		/// <summary>The theme that is on if nothing says otherwise, and the one a broken choice falls back to.</summary>
		public const string DefaultThemeName = "Light";

		private static readonly Lock Sync = new();

		private static List<Theme> _themes = new();

		private static List<ThemeLoadFailure> _failures = new();

		private static Theme? _current;

		/// <summary>The theme everything paints with. Never null once anything has asked for it.</summary>
		public static Theme Current
		{
			get
			{
				lock (Sync)
				{
					if (_current is null)
					{
						EnsureLoadedLocked();
						_current = FindLocked(DefaultThemeName) ?? _themes[0];
					}
					return _current;
				}
			}
		}

		/// <summary>Raised after <see cref="Current"/> changes, so open windows can repaint.</summary>
		public static event Action<Theme>? Changed;

		/// <summary>Every theme on offer, built-ins first, then files, each named once.</summary>
		public static IReadOnlyList<Theme> All
		{
			get { lock (Sync) { EnsureLoadedLocked(); return _themes.ToList(); } }
		}

		/// <summary>The theme files that would not load, and the reason for each.</summary>
		public static IReadOnlyList<ThemeLoadFailure> Failures
		{
			get { lock (Sync) { EnsureLoadedLocked(); return _failures.ToList(); } }
		}

		/// <summary>Where a person puts a theme they wrote or was given.</summary>
		public static string ThemesDirectory => Path.Combine(ProjectCache.DataHome, "Themes");

		public static Theme? Find(string? name)
		{
			if (string.IsNullOrWhiteSpace(name)) return null;
			lock (Sync) { EnsureLoadedLocked(); return FindLocked(name!); }
		}

		/// <summary>
		/// Turns a theme on. An unknown name falls back to <see cref="DefaultThemeName"/>,
		/// which is what happens when a config names a theme file that has since been
		/// deleted. Returns the theme that is now on.
		/// </summary>
		public static Theme Select(string? name)
		{
			Theme chosen;
			lock (Sync)
			{
				EnsureLoadedLocked();
				chosen = FindLocked(name ?? "") ?? FindLocked(DefaultThemeName) ?? _themes[0];
				if (ReferenceEquals(chosen, _current)) return chosen;
				_current = chosen;
			}
			Changed?.Invoke(chosen);
			return chosen;
		}

		/// <summary>Re-reads the themes folder, keeping the current theme if it is still there.</summary>
		public static void Reload()
		{
			string? wanted;
			lock (Sync)
			{
				wanted = _current?.Name;
				_themes = new();
				_failures = new();
				_current = null;
				EnsureLoadedLocked();
			}
			Select(wanted);
		}

		/// <summary>
		/// Loads the built-ins and then the folder. Used directly by the tests, which
		/// want a library over a directory of their own rather than the user's.
		/// </summary>
		public static void LoadFrom(string? directory)
		{
			lock (Sync)
			{
				_themes = new();
				_failures = new();
				LoadBuiltInsLocked();
				LoadDirectoryLocked(directory);
				_current = FindLocked(_current?.Name ?? DefaultThemeName) ?? FindLocked(DefaultThemeName) ?? _themes[0];
			}
			Changed?.Invoke(Current);
		}

		private static void EnsureLoadedLocked()
		{
			if (_themes.Count is not 0) return;
			LoadBuiltInsLocked();
			LoadDirectoryLocked(SafeThemesDirectory());
		}

		private static string? SafeThemesDirectory()
		{
			try
			{
				return ThemesDirectory;
			}
			catch (Exception)
			{
				// no data directory yet (a unit test, a first run): built-ins only
				return null;
			}
		}

		private static void LoadBuiltInsLocked()
		{
			foreach (var name in new[] { "light", "dark" })
			{
				using var stream = ReflectionCache.EmbeddedResourceStream($"Resources.themes.{name}.json");
				using StreamReader reader = new(stream);
				_themes.Add(ThemeFile.Parse(reader.ReadToEnd(), $"built-in theme \"{name}\"", FindLocked));
			}
		}

		private static void LoadDirectoryLocked(string? directory)
		{
			if (string.IsNullOrWhiteSpace(directory) || !Directory.Exists(directory)) return;
			// sorted so that two files claiming the same name resolve the same way every run
			foreach (var path in Directory.GetFiles(directory!, "*.json").OrderBy(static p => p, StringComparer.Ordinal))
			{
				try
				{
					var theme = ThemeFile.Parse(File.ReadAllText(path), Path.GetFileName(path), FindLocked);
					if (FindLocked(theme.Name) is not null)
					{
						_failures.Add(new(path, $"there is already a theme called \"{theme.Name}\"; rename it in the file's \"name\""));
						continue;
					}
					_themes.Add(theme);
				}
				catch (ThemeFormatException ex)
				{
					_failures.Add(new(path, ex.Message));
				}
				catch (IOException ex)
				{
					_failures.Add(new(path, ex.Message));
				}
				catch (UnauthorizedAccessException ex)
				{
					_failures.Add(new(path, ex.Message));
				}
			}
		}

		private static Theme? FindLocked(string name)
			=> _themes.FirstOrDefault(t => string.Equals(t.Name, name, StringComparison.OrdinalIgnoreCase));
	}
}
