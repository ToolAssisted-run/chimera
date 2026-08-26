using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Windows.Forms;

using Chimera.Client.Common;
using Chimera.Emulation.Common.Engine;

namespace Chimera.Client.GUI
{
	public partial class MainForm
	{
		// The open project: the resolved engine-side instance that drives the
		// session's mounts, kept for as long as the project is the loaded game
		// (reboots go through it again). Replaced on the next LoadProject.
		private EngineProject _openProject;

		/// <summary>
		/// The front door: New / Open / recents, modal over the empty main
		/// window. Closing it leaves the empty window up - File > Open Project
		/// still works - because quitting is the user's call, not this form's.
		/// </summary>
		private void ShowStartScreen()
		{
			List<string> recents = new();
			foreach (var recent in Config.RecentProjects) recents.Add(recent);
			using StartScreenForm screen = new(
				recents,
				newProject: RunNewProjectWizard,
				openProject: PickProjectToOpen);
			if (screen.ShowDialog(this) is DialogResult.OK && screen.ChosenProjectPath is not null)
			{
				LoadProject(screen.ChosenProjectPath);
			}
		}

		private void NewProjectDialog()
		{
			var created = RunNewProjectWizard();
			if (created is not null) LoadProject(created);
		}

		private void OpenProjectDialog()
		{
			var chosen = PickProjectToOpen();
			if (chosen is not null) LoadProject(chosen);
		}

		/// <returns>the created project's path, or null when cancelled</returns>
		private string RunNewProjectWizard()
		{
			ScanForCorePackages();
			using NewProjectWizard wizard = new(
				_discoveredCorePackages,
				pickSavePath: () =>
				{
					using SaveFileDialog dialog = new()
					{
						Filter = new FilesystemFilterSet(FilesystemFilter.TAStudioProjects).ToString(),
						Title = "Where the project file goes (its files may live anywhere)",
					};
					return dialog.ShowDialog(this) is DialogResult.OK ? dialog.FileName : null;
				},
				pickFiles: slot =>
				{
					using OpenFileDialog dialog = new()
					{
						Multiselect = true,
						Title = $"Add to {slot.Title}",
						Filter = ProjectSlotDeclaration.FilterFor(slot) is { } filter
							? $"{slot.Title} ({filter})|{filter}|All files (*.*)|*.*"
							: "All files (*.*)|*.*",
					};
					return dialog.ShowDialog(this) is DialogResult.OK ? dialog.FileNames : [ ];
				});
			return wizard.ShowDialog(this) is DialogResult.OK ? wizard.CreatedProjectPath : null;
		}

		private string PickProjectToOpen()
		{
			using OpenFileDialog dialog = new()
			{
				Filter = new FilesystemFilterSet(FilesystemFilter.TAStudioProjects).ToString(),
				Title = "Open Chimera Project",
			};
			return dialog.ShowDialog(this) is DialogResult.OK ? dialog.FileName : null;
		}

		/// <summary>
		/// Opens a project: resolve its files for this session, check the core
		/// pin, boot the machine from the manifest's mounts, start the project
		/// as the movie it IS, and land in TAStudio (docs/project.md).
		/// </summary>
		public bool LoadProject(string path)
		{
			EngineProject project;
			try
			{
				project = EngineProject.Open(path);
			}
			catch (InvalidOperationException ex)
			{
				ShowMessageBox(owner: null, ex.Message, "Cannot open the project");
				return false;
			}

			// resolution: beside the project first, then the user's say per file
			project.ResolveDir(Path.GetDirectoryName(Path.GetFullPath(path)));
			if (!project.FilesOk)
			{
				using ProjectResolutionForm dialog = new(project, locateFile: title =>
				{
					using OpenFileDialog picker = new() { Title = title };
					return picker.ShowDialog(this) is DialogResult.OK ? picker.FileName : null;
				});
				if (dialog.ShowDialog(this) is not DialogResult.OK)
				{
					project.Dispose();
					return false;
				}
			}

			if (!EnsureProjectCore(project))
			{
				project.Dispose();
				return false;
			}

			_openProject?.Dispose();
			_openProject = project;

			if (!LoadRom(path, new LoadRomArgs(new OpenAdvanced_OpenRom(path))))
			{
				return false;
			}

			var movie = MovieSession.Get(path, loadMovie: true);
			if (movie is ITasMovie tasMovie)
			{
				// the movie adopts the RESOLVED project, so its saves record what
				// actually ran (an overridden hash included), and the manifest and
				// wizard fields pass through untouched
				tasMovie.UseResolvedProject(project);
				var isFresh = movie.InputLogLength is 0;
				if (!StartNewMovie(movie, isFresh)) return false;
			}

			Config.RecentProjects.Add(path);
			Tools.Load<TAStudio>();
			return true;
		}

		/// <summary>
		/// The pinned core, honored: load its package if it is discoverable and
		/// not yet loaded, and refuse a differing build unless the user knowingly
		/// proceeds - the project will then record what actually ran.
		/// </summary>
		private bool EnsureProjectCore(EngineProject project)
		{
			var coreName = project.CoreName;
			if (coreName.Length is 0)
			{
				ShowMessageBox(owner: null, "This project pins no core, so nothing can run it.", "No core in the project");
				return false;
			}

			if (CoreRegistry.Instance.AllFactories.All(f => f.CoreName != coreName))
			{
				ScanForCorePackages();
				var candidate =
					_discoveredCorePackages.FirstOrDefault(pkg => pkg.Error is null && pkg.Sha1 is not null
						&& pkg.Sha1.Equals(project.CoreSha1, StringComparison.OrdinalIgnoreCase))
					?? _discoveredCorePackages.FirstOrDefault(pkg => pkg.Error is null && pkg.Name == coreName);
				if (candidate is null)
				{
					ShowMessageBox(
						owner: null,
						$"This project runs on \"{coreName}\", and no such core package is installed."
							+ "\n\nPut its package in the Cores folder (File > Open Core... shows where), then open the project again.",
						"The project's core is not installed");
					return false;
				}
				if (!LoadCorePackage(candidate.Path)) return false;
			}

			var pin = project.CoreSha1;
			var actual = CoreRegistry.Instance.LoadedPackages
				.FirstOrDefault(pkg => pkg.CoreNames.Contains(coreName))?.Sha1;
			if (pin.Length is not 0 && actual is not null && !pin.Equals(actual, StringComparison.OrdinalIgnoreCase))
			{
				return this.ModalMessageBox2(
					caption: "Not the project's core build",
					icon: EMsgBoxIcon.Warning,
					text: $"The project pins {coreName} package {pin.Substring(0, 8)}..., but the installed package is"
						+ $" {actual.Substring(0, 8)}... - a different build, possibly a different machine."
						+ "\n\nRun on the installed build anyway? The project will record what actually ran.");
			}
			return true;
		}
	}
}
