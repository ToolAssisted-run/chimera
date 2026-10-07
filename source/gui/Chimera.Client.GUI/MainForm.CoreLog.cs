#nullable enable

using System;
using System.IO;

using Chimera.Client.Common;
using Chimera.Emulation.Common;
using Chimera.Emulation.Common.Engine;

namespace Chimera.Client.GUI
{
	public partial class MainForm
	{
		/// <summary>
		/// Tools &gt; Export Core Log...: what the core says, kept in a file for an issue
		/// report. Chimera keeps no core log unless somebody asks here, and nothing
		/// remembers that they did - every run starts with it off. The engine and miniBox
		/// do the keeping (ce_core_log); this only asks where, and reboots so the log
		/// covers the core from its start (RPCS3 keeps its full log only from boot).
		/// </summary>
		private void ExportCoreLogMenuItem_Click(object sender, EventArgs e)
		{
			var current = ChimeraEngine.CoreLogPath;
			if (current.Length is not 0)
			{
				StopCoreLogAsked(current);
				return;
			}

			var core = Emulator.IsNull() ? "" : Emulator.Attributes().CoreName;
			var dir = Path.Combine(ProjectCache.DataHome, "Logs");
			Directory.CreateDirectory(dir);
			var path = this.ShowFileSaveDialog(
				initDir: dir,
				fileExt: "txt",
				filter: new FilesystemFilterSet(FilesystemFilter.TextFiles),
				initFileName: $"{(core.Length is 0 ? "Chimera" : core)} core log {DateTime.Now:yyyy-MM-dd HH.mm.ss}.txt");
			if (path is null) return;

			if (!ChimeraEngine.StartCoreLog(path, out var error))
			{
				ShowMessageBox(owner: null, $"The core log could not be started: {error}", "Export Core Log", EMsgBoxIcon.Error);
				return;
			}

			if (Emulator.IsNull())
			{
				ShowMessageBox(owner: null,
					"The core log is on. Load the game and do what shows the problem, then come back to Tools > Export Core Log... to turn it off.",
					"Export Core Log");
				return;
			}

			if (this.ModalMessageBox2(
				caption: "Export Core Log",
				icon: EMsgBoxIcon.Question,
				text: "The core log is on. Reboot the core now, so the log covers it from the start?"
					+ " Some cores keep their full log only from boot."
					+ "\n\nThen do what shows the problem, and come back to Tools > Export Core Log... to turn it off."))
			{
				RebootCore();
			}
		}

		private void StopCoreLogAsked(string current)
		{
			var size = File.Exists(current) ? new FileInfo(current).Length : 0;
			if (!this.ModalMessageBox2(
				caption: "Export Core Log",
				icon: EMsgBoxIcon.Question,
				text: $"The core log is on, and is being written to\n\n{current}\n\n({size / 1024:N0} KB so far). Turn it off?"))
			{
				return;
			}
			ChimeraEngine.StopCoreLog();
			ShowMessageBox(owner: null,
				$"The core log is off. It is in\n\n{current}\n\nAttach that file to your issue.",
				"Export Core Log");
		}
	}
}
