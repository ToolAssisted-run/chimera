#nullable enable

using System.Globalization;
using System.IO;

using Chimera.Common;
using Chimera.Common.StringExtensions;
using Chimera.Emulation.Common;

namespace Chimera.Client.Common
{
	public partial class Bk2Movie
	{
		public FileWriteResult Save()
		{
			return Write(Filename);
		}

		public FileWriteResult SaveBackup()
		{
			if (string.IsNullOrWhiteSpace(Filename))
			{
				return new();
			}

			string backupName = Filename.InsertBeforeLast('.', insert: $".{DateTime.Now:yyyy-MM-dd HH.mm.ss}", out _);
			backupName = Path.Combine(Session.BackupDirectory, Path.GetFileName(backupName));

			return Write(backupName, isBackup: true);
		}

		// The project (docs/project.md) is the only on-disk movie form; the
		// subclass that IS the project writes it. There is no zip movie.
		protected abstract FileWriteResult Write(string fn, bool isBackup = false);

		public void SetCycleValues() //TODO IEmulator should not be an instance prop of movies, it should be passed in to every call (i.e. from MovieService) --yoshi
		{
			// The saved cycle value will only be valid if the end of the movie has been emulated.
			if (this.IsAtEnd() && Emulator.AsCycleTiming() is { } cycleCore)
			{
				// legacy movies may incorrectly have no ClockRate header value set
				Header[HeaderKeys.ClockRate] = cycleCore.ClockRate.ToString(NumberFormatInfo.InvariantInfo);
				Header[HeaderKeys.CycleCount] = cycleCore.CycleCount.ToString();
			}
			else
			{
				Header.Remove(HeaderKeys.CycleCount); // don't allow invalid cycle count fields to stay set
			}
		}

		protected override void ClearBeforeLoad()
		{
			base.ClearBeforeLoad();
			Log.Clear();
			_syncSettingsJson = "";
		}
	}
}
