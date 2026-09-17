using Chimera.Client.Common;
using Chimera.Emulation.Common;

namespace Chimera.Tests.Client.Common
{
	/// <summary>
	/// What the status bar says about a drive that swaps: which image is in, and which the
	/// selector is on when that is another one.
	/// </summary>
	[TestClass]
	public class DriveMediaTextTests
	{
		private static readonly string[] Hayate =
		[
			"Zikusousakan Hayate (1994)(Altacia)(Disk 1 of 4)(Disk A).fdi",
			"Zikusousakan Hayate (1994)(Altacia)(Disk 2 of 4)(Disk B).fdi",
			"Zikusousakan Hayate (1994)(Altacia)(Disk 3 of 4)(Disk C).fdi",
			"Zikusousakan Hayate (1994)(Altacia)(Disk 4 of 4)(Disk D).fdi",
		];

		[TestMethod]
		public void WhenTheSelectedImageIsTheOneInTheDriveItIsSaidOnce()
		{
			Assert.AreEqual("1/2 Disc 1.iso", DriveMediaText.Short(new DriveMedia([ "Disc 1.iso", "Disc 2.iso" ], selected: 0, inserted: 0)));
		}

		[TestMethod]
		public void ASelectorThatMovedWithoutASwapShowsBoth()
		{
			DriveMedia media = new([ "Disc 1.iso", "Disc 2.iso" ], selected: 1, inserted: 0);
			Assert.AreEqual("1/2 Disc 1.iso | selected 2/2 Disc 2.iso", DriveMediaText.Short(media));
			Assert.AreEqual("CD-ROM - in the drive: 1/2 Disc 1.iso; selected, not inserted: 2/2 Disc 2.iso", DriveMediaText.Long("CD-ROM", media));
		}

		[TestMethod]
		public void AnEmptyDriveSaysSo()
		{
			DriveMedia media = new([ "Disc 1.iso", "Disc 2.iso" ], selected: 1, inserted: -1);
			Assert.AreEqual("empty | selected 2/2 Disc 2.iso", DriveMediaText.Short(media));
			StringAssert.StartsWith(DriveMediaText.Long("CD", media), "CD - the drive is empty");
		}

		[TestMethod]
		public void LongNamesAreCutInTheMiddleBecauseDisksOfOneGameDifferAtTheEnd()
		{
			var shown = new System.Collections.Generic.HashSet<string>();
			for (int i = 0; i < Hayate.Length; i++)
			{
				var text = DriveMediaText.Short(new DriveMedia(Hayate, i, i));
				Assert.IsTrue(text.Length <= DriveMediaText.NameRoom + 4, text);
				StringAssert.EndsWith(text, $"(Disk {(char) ('A' + i)}).fdi");
				shown.Add(text);
			}
			Assert.AreEqual(4, shown.Count, "four disks must read as four different things");
			StringAssert.Contains(DriveMediaText.Long("Floppy Disk", new DriveMedia(Hayate, 1, 1)), Hayate[1]);
		}

		[TestMethod]
		public void APathIsShownAsItsFileNameAndAnIndexOutsideTheListIsNotAName()
		{
			Assert.AreEqual("1/1 game.iso", DriveMediaText.Short(new DriveMedia([ "/mnt/discs/game.iso" ], 0, 0)));
			Assert.AreEqual("?", DriveMediaText.Short(new DriveMedia([ "a" ], 7, 7)));
		}
	}
}
