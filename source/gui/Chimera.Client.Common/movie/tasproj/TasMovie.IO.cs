using System.IO;

namespace Chimera.Client.Common
{
	internal partial class TasMovie
	{
		public Func<string> ClientSettingsForSave { get; set; }
		public string LoadedClientSettings { get; private set; }

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
	}
}
