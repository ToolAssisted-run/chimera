#nullable enable

using System.Collections.Generic;
using System.Drawing;
using System.Linq;

namespace Chimera.Client.Common
{
	/// <summary>
	/// One colour for every <see cref="ThemeColorRole"/>, and a name to choose it by.
	/// Immutable: a theme is data somebody wrote down, not a thing the frontend edits.
	/// </summary>
	public sealed class Theme
	{
		/// <summary>How many roles there are. A theme carries exactly this many colours.</summary>
		public static readonly int RoleCount = Enum.GetValues(typeof(ThemeColorRole)).Length;

		public static IReadOnlyList<ThemeColorRole> AllRoles { get; }
			= Enum.GetValues(typeof(ThemeColorRole)).Cast<ThemeColorRole>().ToArray();

		private readonly Color[] _colors;

		/// <summary>What the menu calls it. Unique among the themes on offer; case-insensitive.</summary>
		public string Name { get; }

		/// <summary>One line saying what it is for. May be empty.</summary>
		public string Description { get; }

		/// <summary>Who wrote it. May be empty.</summary>
		public string Author { get; }

		/// <summary>
		/// Whether this is a dark theme. Not derived from the colours: a theme says
		/// so itself, because a few places need to pick an ASSET rather than a
		/// colour (the logo has a light and a dark drawing) and guessing from a
		/// background colour gets that wrong for anything in between.
		/// </summary>
		public bool IsDark { get; }

		/// <summary>Where it came from, for a message: a file path, or "built in".</summary>
		public string Origin { get; }

		public Theme(string name, string description, string author, bool isDark, string origin, Color[] colors)
		{
			if (colors.Length != RoleCount) throw new ArgumentException($"a theme needs {RoleCount} colours, got {colors.Length}", nameof(colors));
			Name = name;
			Description = description;
			Author = author;
			IsDark = isDark;
			Origin = origin;
			_colors = (Color[]) colors.Clone();
		}

		public Color this[ThemeColorRole role] => _colors[(int) role];

		/// <summary>The colours, in role order. A copy; changing it changes nothing.</summary>
		public Color[] ToArray() => (Color[]) _colors.Clone();

		public override string ToString() => Name;
	}
}
