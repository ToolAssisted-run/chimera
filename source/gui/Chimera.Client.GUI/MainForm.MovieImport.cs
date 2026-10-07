#nullable enable

using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Windows.Forms;

using Chimera.Client.Common;
using Chimera.Common.PathExtensions;
using Chimera.Emulation.Common;
using Chimera.Emulation.Common.Engine;
using Chimera.Emulation.Common.Waterbox;

namespace Chimera.Client.GUI
{
	public partial class MainForm
	{
		/// <summary>
		/// The running core's movie import, when it declares one
		/// (<see cref="WaterboxConfig.MovieImport"/>): the core whose project is open
		/// is the core a movie is imported into, so the item is in its Game menu.
		/// </summary>
		private (WaterboxCoreFactory Factory, WaterboxConfig.MovieImportDecl Decl)? RunningMovieImport()
		{
			if (_openProject is null) return null;
			if (CoreRegistry.Instance.FactoryFor(_openProject.CoreName, _openProject.CoreSha1) is not WaterboxCoreFactory factory) return null;
			return factory.Config.MovieImport is { } decl ? (factory, decl) : null;
		}

		/// <summary>
		/// Game &gt; Import ...: a movie made elsewhere becomes a project (docs/project.md,
		/// "Importing a movie"). The core reads the movie and the files it was made with
		/// and SUGGESTS what it dictates - the machine and release, the settings, the
		/// firmware, the files in load order - and supplies the inputs. Everything else
		/// is the frontend's own creation: the New Project wizard opens on those
		/// answers, and the project it builds is the one written, so an imported
		/// project is made exactly as any other is.
		/// </summary>
		private void ImportMovieDialog()
		{
			if (RunningMovieImport() is not var (factory, decl)) return;
			var coreName = factory.CoreName;
			var pin = _openProject!.CoreSha1;
			var package = _discoveredCorePackages
				.FirstOrDefault(p => p.Error is null && p.Sha1 is not null && p.Sha1.Equals(pin, StringComparison.OrdinalIgnoreCase))
				?.Path ?? factory.PackageDir;

			using MovieImportForm form = new(
				decl,
				pickFiles: (title, extensions, several) =>
				{
					var filter = extensions.Count is 0 ? null : string.Join(";", extensions.Select(static e => "*." + e.TrimStart('.')));
					using OpenFileDialog dialog = new()
					{
						Multiselect = several,
						Title = title,
						Filter = filter is null ? "All files (*.*)|*.*" : $"{title} ({filter})|{filter}|All files (*.*)|*.*",
					};
					return dialog.ShowDialog(this) is DialogResult.OK ? dialog.FileNames.Select(static f => f.WithoutWslgMirror()).ToArray() : [ ];
				},
				import: request =>
				{
					request.Package = package;
					return AskCoreToImport(request);
				});
			if (form.ShowDialog(this) is not DialogResult.OK) return;
			var answer = form.Answer!;
			var request = form.Request!;

			if (answer.Notes.Count is not 0)
			{
				ShowMessageBox(
					owner: null,
					$"{coreName} read a {answer.Format} movie of {answer.Frames} frames, and notes:\n\n"
						+ string.Join("\n\n", answer.Notes.Select(static n => "- " + n)),
					form.Text);
			}

			if (SeedFromImport(factory.Config, coreName, package, decl, request, answer) is not { } seed) return;
			var created = RunNewProjectWizard(seed: seed);
			if (created is null) return;
			created.LogText = answer.Input;

			var path = PickWhereImportGoes(request.Movie);
			if (path is null)
			{
				created.Dispose();
				return;
			}
			try
			{
				created.Save(path);
			}
			catch (InvalidOperationException ex)
			{
				created.Dispose();
				ShowMessageBox(owner: null, ex.Message, "Cannot save the project");
				return;
			}
			// where its files are, for the open below: the project carries names and
			// hashes, and they need not be beside it
			new ProjectLocalPaths().Save(created);
			created.Dispose();
			LoadProject(path, firstBoot: true);
		}

		/// <summary>
		/// The wizard's starting answers from the core's: its settings with the
		/// machine it named, the files it listed - in ITS order, the order they
		/// load in - at the paths they were picked from, and the firmware picked
		/// for each requirement it named. The firmware is the file the person gave,
		/// whichever release it is: the wizard takes it as if picked on its own
		/// page, and remembers it as it remembers any firmware.
		/// </summary>
		private ProjectAnswers? SeedFromImport(
			WaterboxConfig cfg,
			string coreName,
			string package,
			WaterboxConfig.MovieImportDecl decl,
			MovieImportRequest request,
			MovieImportAnswer answer)
		{
			var settings = new Dictionary<string, object>(answer.Settings);
			if (cfg.MachineSetting is { Length: > 0 } machineSetting && answer.Game.Length is not 0 && !settings.ContainsKey(machineSetting))
			{
				settings[machineSetting] = answer.Game;
			}

			var picked = request.Files.ToDictionary(static f => f.Name, static f => f.Path, StringComparer.Ordinal);
			List<(string Slot, string Path)> files = new();
			foreach (var (name, _, slot) in answer.Files)
			{
				if (!picked.TryGetValue(name, out var path))
				{
					ShowMessageBox(owner: null, $"{coreName} listed {name}, which is not one of the files given to it.", "Cannot import the movie");
					return null;
				}
				var declared = decl.Files?.FirstOrDefault(f => f.Slot is { Length: > 0 } && FileIsIn(request, f, name))?.Slot;
				files.Add((slot.Length is not 0 ? slot : declared ?? "", path));
			}

			// the firmware inputs' files, matched to what the core named by hash -
			// or, with one file and one name, by being the only one there is
			var firmwareFiles = request.Files
				.Where(f => decl.Files?.Any(d => d.Firmware && FileIsIn(request, d, f.Name)) is true)
				.Select(static f => f.Path)
				.ToList();
			List<(string Id, string Path)> firmware = new();
			foreach (var (id, sha1) in answer.Firmware)
			{
				var path = firmwareFiles.FirstOrDefault(f => Sha1Of(f).Equals(sha1, StringComparison.OrdinalIgnoreCase))
					?? (firmwareFiles.Count is 1 && answer.Firmware.Count is 1 ? firmwareFiles[0] : null);
				if (path is not null) firmware.Add((id, Path.GetFullPath(path)));
			}

			return ProjectAnswers.For(coreName, package, Newtonsoft.Json.JsonConvert.SerializeObject(settings), files, firmware);
		}

		/// <summary>Whether a file input of the dialog is where this file was picked (its option names it).</summary>
		private static bool FileIsIn(MovieImportRequest request, WaterboxConfig.MovieImportFile input, string name)
		{
			if (input.Option is not { Length: > 0 } option || request.Settings?.TryGetValue(option, out var value) is not true) return false;
			var names = input.Multiple ? (value?.ToString() ?? "").Split([ input.Separator ?? ";" ], StringSplitOptions.None) : [ value?.ToString() ?? "" ];
			return names.Contains(name, StringComparer.Ordinal);
		}

		private static string Sha1Of(string path)
		{
			try
			{
				return ChimeraEngine.Sha1Hex(File.ReadAllBytes(path));
			}
			catch (Exception ex) when (ex is IOException or UnauthorizedAccessException)
			{
				return "";
			}
		}

		/// <summary>Where the imported project is written: the person's choice, offered beside the movie under its name.</summary>
		private string? PickWhereImportGoes(string moviePath)
		{
			var extension = new FilesystemFilterSet(FilesystemFilter.TAStudioProjects).ToString();
			using SaveFileDialog dialog = new()
			{
				Title = "Save the imported project",
				Filter = extension,
				FileName = Path.GetFileNameWithoutExtension(moviePath) + "." + MovieService.TasMovieExtension,
				InitialDirectory = Path.GetDirectoryName(Path.GetFullPath(moviePath)) ?? "",
				OverwritePrompt = true,
			};
			return dialog.ShowDialog(this) is DialogResult.OK ? dialog.FileName.WithoutWslgMirror() : null;
		}

		/// <summary>
		/// Has the core read a movie (<c>--import-movie</c>), in a process of its own:
		/// a core that falls over while it reads must not take the frontend with it.
		/// The answer comes back through a file, never stdout, where whatever the
		/// core prints could land in the middle of it.
		/// </summary>
		private static (MovieImportAnswer? Answer, string? Failure) AskCoreToImport(MovieImportRequest request)
		{
			var dir = Path.Combine(Path.GetTempPath(), $"chimera-import-{System.Diagnostics.Process.GetCurrentProcess().Id}-{Guid.NewGuid():N}");
			Directory.CreateDirectory(dir);
			try
			{
				var requestPath = Path.Combine(dir, "request.json");
				var answerPath = Path.Combine(dir, "answer.json");
				request.Write(requestPath);
				var lines = new List<string>();
				using var p = SelfProcess.Start([ "--import-movie", requestPath, answerPath ], line =>
				{
					lock (lines) lines.Add(line);
				});
				if (p is null) return (null, "the import could not be started");
				var clock = System.Diagnostics.Stopwatch.StartNew();
				while (!p.WaitForExit(50))
				{
					Application.DoEvents();
					if (clock.Elapsed > TimeSpan.FromMinutes(2))
					{
						try { p.Kill(); } catch (InvalidOperationException) { }
						return (null, "the core took too long to answer");
					}
				}
				p.WaitForExit(); // drains the redirected streams
				if (p.ExitCode is 0 && File.Exists(answerPath))
				{
					try
					{
						return (MovieImportAnswer.Parse(File.ReadAllText(answerPath)), null);
					}
					catch (Newtonsoft.Json.JsonException ex)
					{
						return (null, $"the core's answer could not be read: {ex.Message}");
					}
				}
				lock (lines)
				{
					var said = lines.LastOrDefault(static l => !string.IsNullOrWhiteSpace(l));
					return (null, said ?? $"the import ended with code {p.ExitCode}");
				}
			}
			finally
			{
				try { Directory.Delete(dir, recursive: true); } catch (IOException) { } catch (UnauthorizedAccessException) { }
			}
		}
	}
}
