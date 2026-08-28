using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Windows.Forms;

using Chimera.Client.Common;
using Chimera.Emulation.Common;
using Chimera.Emulation.Common.Engine;
using Chimera.Emulation.Common.Waterbox;

namespace Chimera.Client.GUI
{
	public partial class MainForm
	{
		// The open project: the resolved engine-side instance that drives the
		// session's mounts, kept for as long as the project is the loaded game
		// (reboots go through it again). Replaced on the next LoadProject.
		private EngineProject _openProject;

		/// <summary>
		/// The answers the last project was built from, kept so the wizard opens on
		/// them next time. It survives the project being closed, because closing one
		/// is not a decision to start the next from nothing - most often it is the
		/// step before changing one setting and going again.
		///
		/// A copy rather than the project: the open one gets disposed when it is
		/// replaced, and this has to outlive that.
		/// </summary>
		private ProjectAnswers _lastAnswers;

		/// <summary>guards the two ways a project ends from chasing each other</summary>
		private bool _closingProject;

		/// <summary>
		/// Set for the window between a project queueing its movie and running it.
		/// Inside it the rom load restarts the tools, and TAStudio's restart would
		/// otherwise start a movie of its own and take the queued one with it.
		/// </summary>
		private bool _bootingProject;

		/// <summary>
		/// A project and TAStudio are one thing: the window IS the session, so one is
		/// open exactly when the other is. This is the single place that ends both,
		/// whichever of them was closed first.
		/// </summary>
		public void CloseProject()
		{
			if (_closingProject) return;
			_closingProject = true;
			try
			{
				if (Tools.IsLoaded<TAStudio>()) Tools.Close<TAStudio>();
				LoadNullRom();
			}
			finally
			{
				_closingProject = false;
				// after the close, so a request TAStudio queued on the way out
				// belongs to the session that has just ended rather than to this one
				_projectSession++;
			}
		}

		/// <summary>
		/// Which session is open, counted rather than named. Only ever compared for
		/// equality: what a deferred close needs to know is whether the session it
		/// was asked about is still the one that is open.
		/// </summary>
		private int _projectSession;

		/// <summary>
		/// The same, but after the caller's own close has finished: TAStudio asks this
		/// from inside its FormClosing, where the window it is about to take down is
		/// still standing.
		///
		/// It is deferred, so by the time it runs the session that asked may be long
		/// gone - starting a project closes the open one first, and that close takes
		/// TAStudio down, and TAStudio asks for this on its way out. Running it then
		/// would close the project that had just replaced it. So the request carries
		/// the session it was made for, and is dropped if that is not still the one.
		/// </summary>
		public void QueueProjectClose()
		{
			var session = _projectSession;
			BeginInvoke((Action) (() =>
			{
				if (session != _projectSession) return;
				if (!Emulator.IsNull()) CloseProject();
			}));
		}

		private void NewProjectDialog()
		{
			var created = RunNewProjectWizard();
			if (created is not null) LoadNewProject(created);
		}

		private void OpenProjectDialog()
		{
			var chosen = PickProjectToOpen();
			if (chosen is not null) LoadProject(chosen);
		}

		/// <returns>the created project, in memory and unwritten, or null when cancelled</returns>
		private EngineProject RunNewProjectWizard()
		{
			ScanForCorePackages();
			using NewProjectWizard wizard = new(
				_discoveredCorePackages,
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
				},
				pickFirmwareFile: title =>
				{
					using OpenFileDialog dialog = new() { Title = title };
					return dialog.ShowDialog(this) is DialogResult.OK ? dialog.FileName : null;
				},
				firmwareSearchDirs: [ Config.PathEntries.FirmwareAbsolutePath() ],
				rememberedFirmwarePath: (coreName, id) =>
					Config.CoreFirmware.TryGetValue(CoreFirmwareStore.KeyFor(coreName, id), out var remembered)
						? remembered
						: null);
			// The wizard opens on the last project's answers - the open one, or the
			// last one there was. This is how a project is reconfigured: changing a
			// sync setting changes the machine, so there is no editing one in place,
			// and what made that unbearable was answering every question again to
			// change one of them.
			var answers = _openProject is not null ? ProjectAnswers.Of(_openProject) : _lastAnswers;
			if (answers is not null) wizard.SeedFrom(answers);

			if (wizard.ShowDialog(this) is not DialogResult.OK) return null;

			// remember where the firmware lives, keyed the way the resolver reads
			// it back at load (Config.CoreFirmware)
			var coreName = wizard.ChosenCoreName;
			if (coreName is not null)
			{
				foreach (var (id, path) in wizard.ProvidedFirmwarePaths)
				{
					Config.CoreFirmware[CoreFirmwareStore.KeyFor(coreName, id)] = path;
				}
			}
			return wizard.CreatedProject;
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

			// resolution: beside the project first, then where this machine last
			// found them (the .chimeraLocal sidecar - a hint, never authority: it
			// resolves nothing whose bytes do not match), then the user's say per file
			project.ResolveDir(Path.GetDirectoryName(Path.GetFullPath(path)));
			var local = ProjectLocalPaths.Read(path);
			local.ApplyTo(project);
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

			// the firmware the project pins is looked for where this machine last
			// had it, as well as in the Firmware folder
			return BootProject(project, path, saved: true, local);
		}

		/// <summary>The same, for a script (client.openproject).</summary>
		public bool OpenProject(string path) => LoadProject(path);

		/// <summary>
		/// Starts a project that has never been written: the wizard just built
		/// it in memory, so there is no file to resolve against and nothing to
		/// remember in recents. The movie carries TAStudio's default name,
		/// which is what makes the first save ask where it goes.
		/// </summary>
		public bool LoadNewProject(EngineProject project)
			=> BootProject(project, MovieService.UnsavedProjectPath(Config.PathEntries), saved: false);

		/// <summary>
		/// The one boot: check the core pin and the firmware, then bring the
		/// machine up EXACTLY ONCE with the project's own core and settings,
		/// with the project queued as the movie it IS.
		/// </summary>
		private bool BootProject(EngineProject project, string path, bool saved, ProjectLocalPaths local = null)
		{
			local ??= new ProjectLocalPaths();
			if (!EnsureProjectCore(project))
			{
				project.Dispose();
				return false;
			}

			// A project and TAStudio are one session, and the next one begins only
			// once this one has ended. Booting over a live session left the old
			// machine's close running inside the new one's load, where it consumed
			// the movie the load had just queued and the boot fell over on it.
			//
			// It happens here rather than at the menu item: everything above can
			// still refuse, and a project that cannot boot must not take down the
			// one that is running. Everything below is committed.
			if (!Emulator.IsNull())
			{
				CloseProject();
				if (!Emulator.IsNull())
				{
					// the close asked about unsaved work and was told no
					project.Dispose();
					return false;
				}
			}

			_openProject?.Dispose();
			_openProject = project;

			// THE MACHINE BOOTS EXACTLY ONCE. The movie (which IS the project) is
			// queued BEFORE the rom load, so the one boot already runs with the
			// project's core and sync settings - never a throwaway config-settings
			// boot followed by a reboot (the BizHawk triple-init lineage: rom
			// load, TAStudio open, tasproj load - each a full core init).
			if (MovieSession.Get(path, loadMovie: saved) is not ITasMovie tasMovie)
			{
				return false;
			}
			// the movie adopts the RESOLVED project, so its saves record what
			// actually ran (an overridden hash included), and the manifest and
			// wizard fields pass through untouched
			tasMovie.UseResolvedProject(project);

			// The core is the project's, and the queue is about to ask the movie
			// which core it wants: a movie with no answer takes the "no core in the
			// movie file, using the default" path, which is for movies that predate
			// projects. A NEW project's movie has no headers at all yet, so the pin
			// is copied across here - before the queue, and before the boot.
			if (string.IsNullOrWhiteSpace(tasMovie.Core) && project.CoreName.Length is not 0)
			{
				tasMovie.Core = project.CoreName;
			}
			// (HeaderEntries is an IDictionary here, so a missing key THROWS rather
			// than reading empty - a fresh project's movie has no keys at all)
			PinIfSilent(tasMovie, HeaderKeys.CoreVersion, project.CoreVersion);
			PinIfSilent(tasMovie, HeaderKeys.CorePackageSha1, project.CoreSha1);

			var isFresh = tasMovie.InputLogLength is 0;

			var oldDefaultCores = new Dictionary<string, string>(Config.DefaultCores);
			_bootingProject = true;
			try
			{
				// the movie's own identity makes the legacy platform/hash checks
				// vacuous on purpose: the resolution dialog and the core pin
				// already vetted everything, with better dialogs
				MovieSession.QueueNewMovie(
					tasMovie,
					systemId: tasMovie.SystemID,
					loadedRomHash: tasMovie.Hash ?? "",
					Config.PathEntries,
					Config.DefaultCores);

				if (!LoadRom(path, new LoadRomArgs(new OpenAdvanced_OpenRom(path))))
				{
					return false;
				}

				MovieSession.RunQueuedMovie(isFresh, Emulator);
			}
			finally
			{
				_bootingProject = false;
				MovieSession.AbortQueuedMovie();
				Config.DefaultCores = oldDefaultCores;
			}

			if (isFresh)
			{
				// the wizard recorded the core, settings and firmware; what only a
				// RUNNING machine knows is filled in here, on the one boot - and
				// nothing the project already says is overwritten
				if (string.IsNullOrEmpty(tasMovie.SystemID)) tasMovie.SystemID = Emulator.SystemId;
				if (!string.IsNullOrWhiteSpace(WaterboxCore.HostBuildInfo))
				{
					tasMovie.HeaderEntries[HeaderKeys.WaterboxHost] = WaterboxCore.HostBuildInfo;
				}
				PopulateWithDefaultHeaderValues(tasMovie);
				tasMovie.ClearChanges();
			}

			// what this project was built from, for the next wizard to open on -
			// taken here rather than at the wizard, so opening a project remembers
			// its answers as much as making one does
			_lastAnswers = ProjectAnswers.Of(project);

			SetMainformMovieInfo();
			WarnOnMovieVsLoadedCore();

			// only a project that HAS a file can be recent, and only one that has a
			// file has somewhere to keep the note of where its files were found
			if (saved)
			{
				Config.RecentProjects.Add(path);
				local.Save(path, project);
			}
			// a project IS a TAStudio session; from the commandline the window is
			// not up yet, so the landing waits for it. Headless runs have nobody
			// to operate TAStudio (which opens PAUSED at the session frame) -
			// they just play the project (gates, dumps).
			if (!HeadlessMode.Enabled)
			{
				if (Visible) Tools.Load<TAStudio>();
				else Shown += (_, _) => Tools.Load<TAStudio>();
			}
			return true;
		}

		/// <summary>Writes what the project pins into a movie header that does not carry it.</summary>
		private static void PinIfSilent(ITasMovie movie, string key, string pinned)
		{
			if (pinned.Length is 0) return;
			if (movie.HeaderEntries.TryGetValue(key, out var existing) && !string.IsNullOrWhiteSpace(existing)) return;
			movie.HeaderEntries[key] = pinned;
		}

		/// <summary>
		/// The project's firmware pins, honored exactly: for each pinned file,
		/// a file with THAT hash must be on hand (the Firmware folder, the paths
		/// remembered for this core, and the ones this project's own sidecar
		/// remembers are searched; whichever matches is what the session mounts).
		/// A pin nothing satisfies is a different machine, so it takes a severe
		/// are-you-sure to proceed (docs/project.md).
		/// </summary>
		private bool VerifyFirmwarePins(EngineProject project, ProjectLocalPaths local)
		{
			List<(string Id, string Sha1)> pins = new();
			try
			{
				foreach (var pin in Newtonsoft.Json.Linq.JArray.Parse(project.FirmwareJson))
				{
					var id = pin.Value<string>("id");
					var sha1 = pin.Value<string>("sha1");
					if (id is not null && sha1 is not null) pins.Add((id, sha1));
				}
			}
			catch (Newtonsoft.Json.JsonException)
			{
				return true; // no usable pins is no constraint
			}
			if (pins.Count is 0) return true;

			var coreName = project.CoreName;
			var index = FirmwareLocator.BuildIndex(
				[ Config.PathEntries.FirmwareAbsolutePath() ],
				pins.SelectMany(pin => new[]
					{
						Config.CoreFirmware.TryGetValue(CoreFirmwareStore.KeyFor(coreName, pin.Id), out var remembered)
							? remembered
							: null,
						local.Firmware.TryGetValue(pin.Id, out var beside) ? beside : null,
					})
					.Where(static path => path is not null)!);

			List<string> unsatisfied = new();
			foreach (var (id, sha1) in pins)
			{
				var exact = index.FirstOrDefault(f => f.Sha1.Equals(sha1, StringComparison.OrdinalIgnoreCase));
				if (exact is not null)
				{
					// the session mounts THIS file, exactly what the project names -
					// and where it was is worth remembering, for this core and for
					// this project
					Config.CoreFirmware[CoreFirmwareStore.KeyFor(coreName, id)] = exact.Path;
					local.RememberFirmware(id, exact.Path);
				}
				else
				{
					unsatisfied.Add($"{id} = {sha1}");
				}
			}
			if (unsatisfied.Count is 0) return true;

			return this.ModalMessageBox2(
				caption: "The project's firmware is not on hand",
				icon: EMsgBoxIcon.Warning,
				text: "This project was made with firmware that could not be found by hash:"
					+ $"\n\n{string.Join("\n", unsatisfied)}\n\n"
					+ "Running with DIFFERENT firmware is a DIFFERENT MACHINE: the movie"
					+ " will very likely DESYNC, and anything recorded will not reproduce"
					+ " the original work. Are you sure you want to open it anyway?");
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
							+ "\n\nPut its package in the Cores folder, then open the project again.",
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
