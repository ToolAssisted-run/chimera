#nullable enable

using System.Collections.Generic;
using System.Drawing;
using System.Globalization;
using System.Linq;
using System.Text;

using Chimera.Common;

using Newtonsoft.Json;
using Newtonsoft.Json.Linq;

namespace Chimera.Client.Common
{
	/// <summary>A theme file that cannot be used, and the reason, in words a person can act on.</summary>
	public sealed class ThemeFormatException : Exception
	{
		public ThemeFormatException(string message) : base(message) {}
	}

	/// <summary>
	/// Reads and writes a theme file.
	///
	/// The format is one flat JSON object of role name to colour, because the
	/// people who write themes are not programmers: there is nothing to nest,
	/// nothing to order, and the keys are the same words the roles are called
	/// everywhere else. A file is taken whole or refused whole - a missing or
	/// misspelled entry is an error naming the entry, never a window that comes
	/// up half dark.
	/// </summary>
	public static class ThemeFile
	{
		/// <summary>What a theme file is called on disk.</summary>
		public const string Extension = ".chimeraTheme.json";

		private static readonly IReadOnlyDictionary<string, ThemeColorRole> RolesByName
			= Theme.AllRoles.ToDictionary(static r => r.ToString(), static r => r, StringComparer.OrdinalIgnoreCase);

		/// <summary>
		/// A system colour by the name the theme file used. Only
		/// <see cref="SystemColors.Control"/> is shimmed, and it is shimmed to what
		/// the frontend has always done: Mono hands back an ugly beige for it, so
		/// on a Unix host every window in Chimera has been WhiteSmoke instead since
		/// long before there were themes. Keeping that here is what lets the Light
		/// theme be the palette Chimera already had, on both platforms, rather
		/// than an approximation of it.
		/// </summary>
		public static Color SystemColorByName(string name)
		{
			if (string.Equals(name, nameof(SystemColors.Control), StringComparison.OrdinalIgnoreCase))
			{
				return OSTailoredCode.IsUnixHost ? Color.WhiteSmoke : SystemColors.Control;
			}
			var prop = typeof(SystemColors).GetProperty(name, System.Reflection.BindingFlags.Public | System.Reflection.BindingFlags.Static | System.Reflection.BindingFlags.IgnoreCase);
			if (prop is null || prop.PropertyType != typeof(Color))
			{
				throw new ThemeFormatException($"\"system:{name}\" is not a system colour; see System.Drawing.SystemColors for the names");
			}
			return (Color) prop.GetValue(null)!;
		}

		/// <summary>
		/// One colour value. Either <c>#RRGGBB</c>, or <c>#AARRGGBB</c> when it has
		/// to show through what is under it, or <c>system:Name</c> for whatever the
		/// desktop says that is - which only the built-in Light theme uses, and
		/// only so that it is exactly the palette the frontend had before themes.
		/// </summary>
		public static Color ParseColor(string value, string where)
		{
			var text = value.Trim();
			if (text.Length == 0) throw new ThemeFormatException($"{where}: the colour is empty");
			if (text.StartsWith("system:", StringComparison.OrdinalIgnoreCase))
			{
				try
				{
					return SystemColorByName(text.Substring("system:".Length).Trim());
				}
				catch (ThemeFormatException ex)
				{
					throw new ThemeFormatException($"{where}: {ex.Message}");
				}
			}
			if (text[0] != '#')
			{
				throw new ThemeFormatException($"{where}: \"{value}\" is not a colour - write it as #RRGGBB (or #AARRGGBB when it should show through what is behind it)");
			}
			var digits = text.Substring(1);
			if (digits.Length is not (6 or 8) || !digits.All(static c => Uri.IsHexDigit(c)))
			{
				throw new ThemeFormatException($"{where}: \"{value}\" is not a colour - #RRGGBB is six hex digits, #AARRGGBB is eight");
			}
			var n = uint.Parse(digits, NumberStyles.HexNumber, CultureInfo.InvariantCulture);
			if (digits.Length == 6) n |= 0xFF000000u;
			return Color.FromArgb(unchecked((int) n));
		}

		/// <summary>The other half of <see cref="ParseColor"/>, for writing a theme back out.</summary>
		public static string FormatColor(Color c)
			=> c.A == 0xFF
				? $"#{c.R:X2}{c.G:X2}{c.B:X2}"
				: $"#{c.A:X2}{c.R:X2}{c.G:X2}{c.B:X2}";

		/// <summary>
		/// Reads a theme. <paramref name="baseLookup"/> answers a file's
		/// <c>basedOn</c>; a file without one has to name every role itself.
		/// </summary>
		/// <exception cref="ThemeFormatException">the file is not a theme this build can use, and the message says why</exception>
		public static Theme Parse(string json, string origin, Func<string, Theme?>? baseLookup = null)
		{
			JObject root;
			try
			{
				root = JObject.Parse(json);
			}
			catch (JsonException ex)
			{
				throw new ThemeFormatException($"{origin}: not valid JSON ({ex.Message})");
			}

			string[] known = ["name", "description", "author", "dark", "basedOn", "colors"];
			var stray = root.Properties().Select(static p => p.Name)
				.Where(n => !known.Contains(n, StringComparer.OrdinalIgnoreCase))
				.ToList();
			if (stray.Count is not 0)
			{
				throw new ThemeFormatException($"{origin}: there is no such setting as {Join(stray)} - a theme file has name, description, author, dark, basedOn and colors");
			}

			var name = (string?) root["name"];
			if (string.IsNullOrWhiteSpace(name)) throw new ThemeFormatException($"{origin}: \"name\" is missing, and a theme has to be called something");

			var description = (string?) root["description"] ?? "";
			var author = (string?) root["author"] ?? "";
			var isDark = (bool?) root["dark"] ?? false;

			Color[] colors;
			var have = new bool[Theme.RoleCount];
			var basedOn = (string?) root["basedOn"];
			if (!string.IsNullOrWhiteSpace(basedOn))
			{
				var parent = baseLookup?.Invoke(basedOn!);
				if (parent is null)
				{
					throw new ThemeFormatException($"{origin}: basedOn says \"{basedOn}\", and there is no theme by that name (the built-in ones are Light and Dark)");
				}
				if (string.Equals(parent.Name, name, StringComparison.OrdinalIgnoreCase))
				{
					throw new ThemeFormatException($"{origin}: basedOn says \"{basedOn}\", which is this theme");
				}
				colors = parent.ToArray();
				for (var i = 0; i < have.Length; i++) have[i] = true;
			}
			else
			{
				colors = new Color[Theme.RoleCount];
			}

			if (root["colors"] is not JObject table)
			{
				throw new ThemeFormatException($"{origin}: \"colors\" is missing, and it is the part that holds the colours");
			}

			foreach (var prop in table.Properties())
			{
				if (!RolesByName.TryGetValue(prop.Name, out var role))
				{
					throw new ThemeFormatException($"{origin}: \"{prop.Name}\" is not one of the colours this build knows - {Suggest(prop.Name)}");
				}
				if (prop.Value.Type is not JTokenType.String)
				{
					throw new ThemeFormatException($"{origin}: {role} must be written as a string, like \"#1E1E1E\"");
				}
				colors[(int) role] = ParseColor((string) prop.Value!, $"{origin}: {role}");
				have[(int) role] = true;
			}

			var missing = Theme.AllRoles.Where(r => !have[(int) r]).Select(static r => r.ToString()).ToList();
			if (missing.Count is not 0)
			{
				throw new ThemeFormatException(
					$"{origin}: {missing.Count} colour{(missing.Count is 1 ? " is" : "s are")} missing: {Join(missing)}."
						+ " Either give every colour, or add \"basedOn\": \"Light\" (or \"Dark\") and give only the ones that differ");
			}

			return new Theme(name!.Trim(), description, author, isDark, origin, colors);
		}

		/// <summary>Writes a theme out in the form <see cref="Parse"/> reads, for a person to copy and edit.</summary>
		public static string Write(Theme theme)
		{
			StringBuilder sb = new();
			sb.Append("{\n");
			sb.Append($"\t\"name\": {JsonConvert.ToString(theme.Name)},\n");
			sb.Append($"\t\"description\": {JsonConvert.ToString(theme.Description)},\n");
			if (theme.Author.Length is not 0) sb.Append($"\t\"author\": {JsonConvert.ToString(theme.Author)},\n");
			sb.Append($"\t\"dark\": {(theme.IsDark ? "true" : "false")},\n");
			sb.Append("\t\"colors\": {\n");
			var roles = Theme.AllRoles;
			for (var i = 0; i < roles.Count; i++)
			{
				var comma = i == roles.Count - 1 ? "" : ",";
				sb.Append($"\t\t\"{roles[i]}\": \"{FormatColor(theme[roles[i]])}\"{comma}\n");
			}
			sb.Append("\t}\n");
			sb.Append("}\n");
			return sb.ToString();
		}

		private static string Join(IReadOnlyList<string> names)
			=> names.Count <= 8
				? string.Join(", ", names)
				: string.Join(", ", names.Take(8)) + $", and {names.Count - 8} more";

		/// <summary>The closest role name, so a typo says what was probably meant.</summary>
		private static string Suggest(string typo)
		{
			var best = Theme.AllRoles
				.Select(r => (Name: r.ToString(), Distance: Distance(r.ToString(), typo)))
				.OrderBy(static x => x.Distance)
				.First();
			return best.Distance <= Math.Max(3, typo.Length / 3)
				? $"did you mean \"{best.Name}\"?"
				: "see the list in docs/theming.md";
		}

		private static int Distance(string a, string b)
		{
			a = a.ToLowerInvariant();
			b = b.ToLowerInvariant();
			var prev = new int[b.Length + 1];
			var cur = new int[b.Length + 1];
			for (var j = 0; j <= b.Length; j++) prev[j] = j;
			for (var i = 1; i <= a.Length; i++)
			{
				cur[0] = i;
				for (var j = 1; j <= b.Length; j++)
				{
					var cost = a[i - 1] == b[j - 1] ? 0 : 1;
					cur[j] = Math.Min(Math.Min(cur[j - 1] + 1, prev[j] + 1), prev[j - 1] + cost);
				}
				(prev, cur) = (cur, prev);
			}
			return prev[b.Length];
		}
	}
}
