#nullable enable

using System.IO;
using System.Linq;
using System.Windows.Forms;

using Chimera.Client.Common;
using Chimera.Emulation.Common;

namespace Chimera.Client.GUI
{
	/// <summary>
	/// Games that are more than one file, and what a core keeps between sessions.
	///
	/// Chimera has no idea what any of it means. A core says it keeps something, what to
	/// call it, and what to file it under; a bundle names a file for that id; this moves
	/// the bytes between them. There is no "SaveRAM" here, no per-system save directory
	/// and no autosave timer, because those are one console's vocabulary and one era's
	/// habits - a rom that quietly writes a file next to itself is a rom whose behaviour
	/// changes depending on what you did last time, which is the opposite of what this
	/// frontend is for.
	///
	/// So: a game loaded from a bundle starts from what the bundle names, and writes back
	/// to it when you close the rom. A bare rom starts clean every time, and what it wrote
	/// is yours to keep or discard from the core's own menu.
	/// </summary>
	public partial class MainForm
	{
		/// <summary>The bundle the running game came from, or null for a bare rom.</summary>
		private GameBundle? _openBundle;

		/// <summary>The loader that opened the running game, which is what knows about its bundle.</summary>
		private RomLoader? _lastRomLoader;

		/// <summary>The running core's persistent data, or null when this machine keeps nothing.</summary>
		private ICorePersistentData? PersistentData
			=> Emulator.HasPersistentData() ? Emulator.AsPersistentData() : null;

		/// <summary>
		/// Hands the running core whatever the bundle carries for it. Called after a rom
		/// load, in the same place the old build looked for a .SaveRAM file - except that
		/// nothing is looked for: a bundle says what to load, or nothing loads.
		/// </summary>
		private void LoadBundleAttachments()
		{
			_openBundle = _lastRomLoader?.LoadedBundle;
			if (_openBundle is null) return;

			var core = PersistentData;
			foreach (var attachment in _openBundle.Attach)
			{
				if (core is null)
				{
					AddOnScreenMessage($"{attachment.Id}: {Emulator.Attributes().CoreName} keeps nothing for this game", 8);
					continue;
				}
				if (!string.Equals(attachment.Core, Emulator.Attributes().CoreName, StringComparison.OrdinalIgnoreCase))
				{
					AddOnScreenMessage($"{attachment.Id}: made for {attachment.Core}, not {Emulator.Attributes().CoreName}", 8);
					continue;
				}
				if (!string.Equals(attachment.Id, core.PersistentDataId, StringComparison.OrdinalIgnoreCase))
				{
					AddOnScreenMessage($"{attachment.Id}: this core keeps \"{core.PersistentDataId}\"", 8);
					continue;
				}
				try
				{
					core.StorePersistentData(_openBundle.ReadFile(attachment));
				}
				catch (Exception ex)
				{
					// the machine boots clean rather than half-loaded, and says why
					AddOnScreenMessage($"{core.PersistentDataName} not loaded: {ex.Message}", 10);
					Console.Error.WriteLine(ex);
				}
			}
		}

		/// <summary>
		/// Writes what the machine keeps back into the bundle it came from. A bundle naming
		/// a file for this core IS the standing instruction to keep it up to date; a bare
		/// rom writes nothing, ever, without being asked.
		/// </summary>
		/// <returns>false if the user cancelled out of a failure</returns>
		private bool WriteBundleAttachmentsOnClose()
		{
			var core = PersistentData;
			if (_openBundle is null || core is null || !core.PersistentDataModified) return true;
			var attachment = _openBundle.FindAttachment(Emulator.Attributes().CoreName, core.PersistentDataId);
			if (attachment is null) return true;

			var data = core.ClonePersistentData();
			if (data is null) return true;

			var result = this.DoWithTryAgainBox(
				() =>
				{
					try
					{
						_openBundle.WriteAttachment(attachment, data);
						_openBundle.Save(_openBundle.Path!);
						return new();
					}
					catch (Exception ex)
					{
						return new FileWriteResult(FileWriteEnum.FailedDuringWrite, new(_openBundle.Path ?? "", ""), ex);
					}
				},
				$"Failed to write this game's {core.PersistentDataName} back to its bundle.");
			return result != TryAgainResult.Canceled;
		}

		/// <summary>
		/// The core's own menu entry for what it keeps: write it somewhere, or compose a
		/// bundle so a rom and what it kept become one thing you can open (and a movie can
		/// cite). Absent entirely when the machine keeps nothing.
		/// </summary>
		private ToolStripMenuItem? MakePersistentDataMenuItem()
		{
			var core = PersistentData;
			if (core is null) return null;

			ToolStripMenuItem submenu = new() { Text = core.PersistentDataName };

			ToolStripMenuItem write = new() { Text = "&Write to File..." };
			write.Click += (_, _) => WritePersistentDataToFile(core);
			submenu.DropDownItems.Add(write);

			ToolStripMenuItem toBundle = new()
			{
				Text = "Write to &Bundle",
				Enabled = _openBundle is not null
					&& _openBundle.FindAttachment(Emulator.Attributes().CoreName, core.PersistentDataId) is not null,
			};
			toBundle.Click += (_, _) =>
			{
				if (WriteBundleAttachmentsOnClose()) AddOnScreenMessage($"{core.PersistentDataName} written to {Path.GetFileName(_openBundle!.Path)}");
			};
			submenu.DropDownItems.Add(toBundle);

			ToolStripMenuItem compose = new() { Text = "&Compose Bundle..." };
			compose.Click += (_, _) => ComposeBundle(core);
			submenu.DropDownItems.Add(compose);

			return submenu;
		}

		private void WritePersistentDataToFile(ICorePersistentData core)
		{
			var data = core.ClonePersistentData();
			if (data is null) return;
			var suggested = $"{Game.FilesystemSafeName()}.{core.PersistentDataId}";
			using SaveFileDialog sfd = new()
			{
				Title = $"Write {core.PersistentDataName}",
				FileName = suggested,
				InitialDirectory = Path.GetDirectoryName(CurrentlyOpenRom) ?? "",
				Filter = new FilesystemFilterSet(new FilesystemFilter(core.PersistentDataName, [ core.PersistentDataId ])).ToString(),
			};
			if (sfd.ShowDialog(this) is not DialogResult.OK) return;
			try
			{
				File.WriteAllBytes(sfd.FileName, data);
				AddOnScreenMessage($"{core.PersistentDataName} written to {Path.GetFileName(sfd.FileName)}");
			}
			catch (Exception ex)
			{
				AddOnScreenMessage($"Could not write it: {ex.Message}", 10);
			}
		}

		/// <summary>
		/// Writes the rom's current persistent data beside the rom and catalogues both in a
		/// new bundle - which is how a run that starts from a save becomes something a movie
		/// can cite, instead of a blob hidden inside the movie file.
		/// </summary>
		private void ComposeBundle(ICorePersistentData core)
		{
			var romPath = CurrentlyOpenRom;
			if (string.IsNullOrWhiteSpace(romPath) || !File.Exists(romPath))
			{
				AddOnScreenMessage("A bundle catalogues files on disk, and this game did not come from one", 8);
				return;
			}
			var data = core.ClonePersistentData();
			if (data is null) return;

			var romDir = Path.GetDirectoryName(Path.GetFullPath(romPath))!;
			using SaveFileDialog sfd = new()
			{
				Title = "Compose Bundle",
				FileName = $"{Path.GetFileNameWithoutExtension(romPath)}{GameBundle.Extension}",
				InitialDirectory = romDir,
				Filter = new FilesystemFilterSet(new FilesystemFilter("Game Bundles", [ GameBundle.Extension.TrimStart('.') ])).ToString(),
			};
			if (sfd.ShowDialog(this) is not DialogResult.OK) return;

			// Everything a bundle names has to sit beside it, so a bundle put somewhere else
			// than the rom would name a file that is not there.
			var bundleDir = Path.GetDirectoryName(Path.GetFullPath(sfd.FileName))!;
			if (!string.Equals(bundleDir.TrimEnd(Path.DirectorySeparatorChar), romDir.TrimEnd(Path.DirectorySeparatorChar), StringComparison.OrdinalIgnoreCase))
			{
				AddOnScreenMessage("A bundle has to sit in the same folder as its rom", 10);
				return;
			}

			try
			{
				var dataPath = Path.Combine(bundleDir, $"{Path.GetFileNameWithoutExtension(sfd.FileName)}.{core.PersistentDataId}");
				File.WriteAllBytes(dataPath, data);
				var bundle = GameBundle.Compose(
					sfd.FileName,
					romPath,
					Emulator.Attributes().CoreName,
					core.PersistentDataId,
					dataPath,
					name: Game.Name);
				bundle.Save(sfd.FileName);
				_openBundle = bundle;
				Config.RecentRoms.Add(sfd.FileName);
				AddOnScreenMessage($"Bundle written: {Path.GetFileName(sfd.FileName)}");
			}
			catch (Exception ex)
			{
				AddOnScreenMessage($"Could not write the bundle: {ex.Message}", 10);
			}
		}
	}
}
