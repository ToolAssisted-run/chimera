using System.IO;

namespace Chimera.Client.Common
{
	internal partial class TasMovie
	{
		public Func<string> ClientSettingsForSave { get; set; }
		public string LoadedClientSettings { get; private set; }

		protected override void AddLumps(ZipStateSaver bs, bool isBackup = false)
		{
			// a TAS movie IS a project (docs/project.md); nothing writes it as a
			// zip any more, and the write path routes by extension before here
			throw new InvalidOperationException("a chimera project is a JSON file, never a zip");
		}

		protected override void ClearBeforeLoad()
		{
			base.ClearBeforeLoad();
			ClearTasprojExtras();
		}

		private void ClearTasprojExtras()
		{
			LagLog.Clear();
			TasStateManager?.Clear();
			Markers.Clear();
			ChangeLog.Clear();
		}

		protected override void LoadFields(ZipStateLoader bl)
		{
			// the zip tasproj is a legacy format chimera does not read
			throw new InvalidOperationException(
				"this file is a legacy zip project; a chimera project is a JSON .chimeraProject (docs/project.md)");
		}
	}
}
