using System.Collections.Generic;
using System.Globalization;
using System.Linq;
using System.Windows.Forms;

using Chimera.Client.Common;
using Chimera.Common;
using Chimera.Emulation.Common;
using Chimera.Emulation.Common.Waterbox;

namespace Chimera.Client.GUI
{
	public partial class MainForm
	{
		public bool StartNewMovie(IMovie movie, bool newMovie)
		{
			if (movie is null) throw new ArgumentNullException(paramName: nameof(movie));

			if (CheatList.AnyActive)
			{
				var result = this.ModalMessageBox3(
					caption: "Cheats warning",
					text: "Continue playback with cheats enabled?\nChoosing \"No\" will disable cheats but not remove them.",
					icon: EMsgBoxIcon.Question);
				if (result is null) return false;
				if (result is false) CheatList.DisableAll();
			}
			var oldDefaultCores = new Dictionary<string, string>(Config.DefaultCores);
			try
			{
				if (newMovie) PrepopulateMovieHeaderValues(movie);
				try
				{
					MovieSession.QueueNewMovie(
						movie,
						systemId: Emulator.SystemId,
						loadedRomHash: Game.Hash,
						Config.PathEntries,
						Config.DefaultCores);
				}
				catch (MoviePlatformMismatchException ex)
				{
					using var ownerForm = new Form { TopMost = true };
					MessageBox.Show(ownerForm, ex.Message, "Movie/Platform Mismatch", MessageBoxButtons.OK, MessageBoxIcon.Error);
					return false;
				}

				if (!_isLoadingRom)
				{
					var rebootSucceeded = RebootCore();
					if (!rebootSucceeded) return false;
				}

				Config.RecentMovies.Add(movie.Filename);

				MovieSession.RunQueuedMovie(newMovie, Emulator);
				if (newMovie)
				{
					PopulateWithDefaultHeaderValues(movie);
					if (movie is ITasMovie tasMovie)
						tasMovie.ClearChanges();
				}
			}
			finally
			{
				MovieSession.AbortQueuedMovie();
				Config.DefaultCores = oldDefaultCores;
			}

			SetMainformMovieInfo();

			WarnOnMovieVsLoadedCore();

			return !Emulator.IsNull();
		}

		/// <summary>
		/// The active movie held against the machine that actually booted: rom
		/// hash, core version, package build, firmware. Warnings only - the hard
		/// refusals (core pin, file hashes, firmware pins) happened before the
		/// boot, with better dialogs.
		/// </summary>
		internal void WarnOnMovieVsLoadedCore()
		{
			// turns out this was too late for .tasproj autoloading and restoring playback position (loads savestate but wasn't checking game match)
			if (string.IsNullOrEmpty(MovieSession.Movie.Hash))
			{
				AddOnScreenMessage("Movie is missing hash, skipping hash check");
			}
			else if (MovieSession.Movie.Hash != Game.Hash)
			{
				AddOnScreenMessage("Warning: Movie hash does not match the ROM", 5);
			}

			// The core: version first, because it is the one a person can act on ("get that
			// commit"), then the package hash, which distinguishes two builds of that same
			// commit - a different toolchain produces different bytes from identical sources.
			MovieSession.Movie.HeaderEntries.TryGetValue(HeaderKeys.CoreVersion, out var movieCoreVersion);
			var loadedCoreVersion = Emulator.CoreVersion();
			var versionsAgree = !string.IsNullOrWhiteSpace(movieCoreVersion)
				&& movieCoreVersion.Equals(loadedCoreVersion, StringComparison.OrdinalIgnoreCase);
			if (!string.IsNullOrWhiteSpace(movieCoreVersion) && !versionsAgree)
			{
				AddOnScreenMessage($"Warning: movie was recorded on {MovieSession.Movie.Core} {movieCoreVersion}, this is {loadedCoreVersion}", 5);
			}

			// Firmware: a different BIOS is a different machine, so this is not a nicety.
			if (MovieSession.Movie.HeaderEntries.TryGetValue(HeaderKeys.Firmware, out var movieFirmware)
				&& !string.IsNullOrWhiteSpace(movieFirmware))
			{
				var nowFirmware = CoreFirmwareStore.RecordFor(Config, CoreRegistry.Instance, Emulator.Attributes().CoreName);
				if (!movieFirmware.Equals(nowFirmware, StringComparison.OrdinalIgnoreCase))
				{
					AddOnScreenMessage("Warning: this movie was recorded with different firmware", 5);
				}
			}

			if (!MovieSession.Movie.HeaderEntries.TryGetValue(HeaderKeys.CorePackageSha1, out var moviePackageSha1)
				|| string.IsNullOrEmpty(moviePackageSha1))
			{
				AddOnScreenMessage("Movie has no core package hash, skipping core package check");
			}
			else if (!moviePackageSha1.Equals(
				CoreRegistry.Instance.GetPackageSha1ForCore(Emulator.GetType()),
				StringComparison.OrdinalIgnoreCase))
			{
				// Same version, different bytes: almost always a package built somewhere else,
				// which is worth saying plainly rather than as a bare hash mismatch.
				AddOnScreenMessage(versionsAgree
					? "Warning: same core version, but a different build of it (rebuilt, or a different toolchain)"
					: "Warning: Movie's core package hash does not match the loaded core package", 5);
			}
		}

		public void SetMainformMovieInfo()
		{
			if (MovieSession.Movie.IsPlayingOrFinished())
			{
				PlayRecordStatusButton.Image = Properties.Resources.Play;
				PlayRecordStatusButton.ToolTipText = "Movie is in playback mode";
				PlayRecordStatusButton.Visible = true;
			}
			else if (MovieSession.Movie.IsRecording())
			{
				PlayRecordStatusButton.Image = Properties.Resources.Record;
				PlayRecordStatusButton.ToolTipText = "Movie is in record mode";
				PlayRecordStatusButton.Visible = true;
			}
			else if (MovieSession.Movie.NotActive())
			{
				PlayRecordStatusButton.Image = Properties.Resources.Blank;
				PlayRecordStatusButton.ToolTipText = "No movie is active";
				PlayRecordStatusButton.Visible = false;
			}

			UpdateWindowTitle();
		}

		public void StopMovie(bool saveChanges = true)
		{
			if (ToolControllingStopMovie is { } tool)
			{
				tool.StopMovie(!saveChanges);
			}
			else
			{
				FileWriteResult saveResult = MovieSession.StopMovie(saveChanges);
				if (saveResult.IsError)
				{
					this.ShowMessageBox(
						$"Failed to save movie.\n{saveResult.UserFriendlyErrorMessage()}\n{saveResult.Exception.Message}",
						"Error",
						EMsgBoxIcon.Error);
				}
				SetMainformMovieInfo();
			}
		}

		public bool RestartMovie()
		{
			if (ToolControllingRestartMovie is { } tool) return tool.RestartMovie();
			if (!MovieSession.Movie.IsActive()) return false;
			var success = StartNewMovie(MovieSession.Movie, false);
			if (success) AddOnScreenMessage("Replaying movie file in read-only mode");
			return success;
		}

		private void ToggleReadOnly()
		{
			if (ToolControllingReadOnly is { } tool)
			{
				tool.ToggleReadOnly();
			}
			else
			{
				if (MovieSession.Movie.IsActive())
				{
					MovieSession.ReadOnly = !MovieSession.ReadOnly;
					AddOnScreenMessage(MovieSession.ReadOnly ? "Movie read-only mode" : "Movie read+write mode");
				}
				else
				{
					AddOnScreenMessage("No movie active");
				}
			}
		}

		/// <summary>
		/// Sets necessary movie values in order to be able to start this <paramref name="movie"/>
		/// Only required when creating a new movie from scratch.
		/// </summary>
		/// <param name="movie">The movie to fill with values</param>
		private void PrepopulateMovieHeaderValues(IMovie movie)
		{
			movie.Core = Emulator.Attributes().CoreName;
			movie.SystemID = Emulator.SystemId; // TODO: I feel like setting this shouldn't be necessary, but it is currently

			// A core's version is the commit its published build was made from, which is what
			// a movie can cite and a person can look up; the package's SHA1 says WHICH BUILD of
			// that commit ran, since the same source built with a different toolchain is
			// different bytes. Both, therefore: one is meaningful, the other is exact.
			var coreVersion = Emulator.CoreVersion();
			if (!string.IsNullOrWhiteSpace(coreVersion)) movie.HeaderEntries[HeaderKeys.CoreVersion] = coreVersion;

			var packageSha1 = CoreRegistry.Instance.GetPackageSha1ForCore(Emulator.GetType());
			if (packageSha1 is not null) movie.HeaderEntries[HeaderKeys.CorePackageSha1] = packageSha1;

			// The sandbox is meant to change no emulation, which is a claim worth being able
			// to check rather than assert: record which build of it ran.
			if (!string.IsNullOrWhiteSpace(WaterboxCore.HostBuildInfo))
			{
				movie.HeaderEntries[HeaderKeys.WaterboxHost] = WaterboxCore.HostBuildInfo;
			}

			// Firmware decides what the machine IS - a disk system with a different BIOS is a
			// different machine - so a movie that does not record it is not reproducible.
			var firmware = CoreFirmwareStore.RecordFor(Config, CoreRegistry.Instance, Emulator.Attributes().CoreName);
			if (!string.IsNullOrWhiteSpace(firmware)) movie.HeaderEntries[HeaderKeys.Firmware] = firmware;

			var settable = GetSettingsAdapterForLoadedCoreUntyped();
			if (settable.HasSyncSettings)
			{
				movie.SyncSettingsJson = ConfigService.SaveWithType(settable.GetSyncSettings());
			}
		}

		/// <summary>
		/// Sets default header values for the given <paramref name="movie"/>. Notably needs to be done after loading the core
		/// to make sure all values are in the correct state, see https://github.com/TASEmulators/BizHawk/issues/3980
		/// </summary>
		/// <param name="movie">The movie to fill with values</param>
		private void PopulateWithDefaultHeaderValues(IMovie movie)
		{
			movie.EmulatorVersion = VersionInfo.GetEmuVersion();
			movie.OriginalEmulatorVersion = VersionInfo.GetEmuVersion();

			movie.GameName = Game.FilesystemSafeName();
			movie.Hash = Game.Hash;
			if (Emulator.HasBoardInfo())
			{
				movie.BoardName = Emulator.AsBoardInfo().BoardName;
			}

			if (Emulator.HasRegions())
			{
				var region = Emulator.AsRegionable().Region;
				if (region == DisplayType.PAL)
				{
					movie.HeaderEntries.Add(HeaderKeys.Pal, "1");
				}
			}

			if (Emulator.HasCycleTiming())
			{
				movie.HeaderEntries.Add(HeaderKeys.ClockRate, Emulator.AsCycleTiming().ClockRate.ToString(NumberFormatInfo.InvariantInfo));
			}
		}
	}
}
