using System.IO;

using Chimera.Client.Common;
using Chimera.Client.GUI;

namespace Chimera.Tests.Client.GUI
{
	/// <summary>
	/// A CD declared as one .cue and many .bin is ONE pick: the user selects
	/// the cue, the referenced track files are found next to it and ride
	/// along (the engine adds them as support files at creation), and the row
	/// says how many came aboard. A cue whose tracks are not all next to it
	/// is refused at pick time, not at create time.
	/// </summary>
	[TestClass]
	public class WizardCueTests
	{
		private const string Declaration = """
			{
			  "slots": [
			    { "id": "cdrom", "title": "CD-ROMs", "min": 0, "max": -1, "formats": ["iso", "cue"] }
			  ]
			}
			""";

		private static NewProjectWizard MakeForm()
		{
			NewProjectWizard form = new([ ], static () => null, static _ => [ ]);
			form.Show();
			form.UseDeclaration(ProjectSlotDeclaration.Parse(Declaration));
			return form;
		}

		[TestMethod]
		public void ACueAndItsTracksAreOnePick()
		{
			var dir = Path.Combine(Path.GetTempPath(), $"chimera-cue-{System.Diagnostics.Process.GetCurrentProcess().Id}");
			Directory.CreateDirectory(dir);
			try
			{
				File.WriteAllText(Path.Combine(dir, "game.cue"),
					"FILE \"track01.bin\" BINARY\n  TRACK 01 MODE1/2352\nFILE \"track02.bin\" BINARY\n  TRACK 02 AUDIO\n");
				File.WriteAllText(Path.Combine(dir, "track01.bin"), "data");
				File.WriteAllText(Path.Combine(dir, "track02.bin"), "audio");

				using var form = MakeForm();
				form.AddFileToSlot("cdrom", Path.Combine(dir, "game.cue"));
				CollectionAssert.AreEqual(new[ ] { "game.cue" }, form.SlotFileNames("cdrom"), "one row for the whole disc");
				CollectionAssert.AreEqual(new[ ] { "game.cue  (+ 2 track files)" }, form.SlotRowTexts("cdrom"));
			}
			finally
			{
				Directory.Delete(dir, recursive: true);
			}
		}

		[TestMethod]
		public void AMissingTrackRefusesTheCueAtPickTime()
		{
			var dir = Path.Combine(Path.GetTempPath(), $"chimera-cue-miss-{System.Diagnostics.Process.GetCurrentProcess().Id}");
			Directory.CreateDirectory(dir);
			try
			{
				File.WriteAllText(Path.Combine(dir, "game.cue"),
					"FILE \"track01.bin\" BINARY\n  TRACK 01 MODE1/2352\n");

				using var form = MakeForm();
				form.AddFileToSlot("cdrom", Path.Combine(dir, "game.cue"));
				CollectionAssert.AreEqual(new string[ ] { }, form.SlotFileNames("cdrom"), "all or nothing");
				StringAssert.Contains(form.StatusText, "track01.bin");
			}
			finally
			{
				Directory.Delete(dir, recursive: true);
			}
		}
	}
}
