#nullable enable

using System.Collections.Generic;

using Chimera.Emulation.Common.Engine;

namespace Chimera.Client.GUI
{
	/// <summary>
	/// What somebody answered to make a project: the core, the settings, and where
	/// this machine found each of its files.
	///
	/// It exists so the wizard can open on the last project's answers whether or
	/// not that project is still loaded. Closing a project is not a decision to
	/// start the next one from nothing - most often it is the step before changing
	/// one setting and going again - and the project itself cannot be kept for
	/// this, because it is disposed when the next one replaces it.
	///
	/// The paths are of this session only. A project carries names and hashes and
	/// no paths at all, so where a file was found is the frontend's knowledge, and
	/// a file that has since moved is simply not offered again.
	/// </summary>
	public sealed class ProjectAnswers
	{
		public string CoreName { get; private init; } = "";

		/// <summary>The sync settings, as the flat JSON object the engine takes.</summary>
		public string SettingsJson { get; private init; } = "{}";

		/// <summary>Slot id and path, in the order the project holds them.</summary>
		public IReadOnlyList<(string Slot, string Path)> Files { get; private init; } = [ ];

		/// <summary>Reads a project's answers out of it, copying everything it needs.</summary>
		public static ProjectAnswers Of(EngineProject project)
		{
			List<(string, string)> files = new();
			for (var i = 0; i < project.FileCount; i++)
			{
				var path = project.FileSourcePath(i);
				if (!string.IsNullOrEmpty(path)) files.Add((project.FileSlot(i), path));
			}

			return new()
			{
				CoreName = project.CoreName,
				SettingsJson = project.SettingsJson,
				Files = files,
			};
		}
	}
}
