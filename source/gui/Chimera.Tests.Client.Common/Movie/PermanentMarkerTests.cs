using System.Linq;

using Chimera.Client.Common;

namespace Chimera.Tests.Client.Common.Movie
{
	/// <summary>
	/// The three markers a run always has: where it starts, where its input stops,
	/// and where it ends. They are derived from the movie rather than placed, so
	/// they follow every edit and cannot be moved, renamed or deleted.
	/// </summary>
	[TestClass]
	public class PermanentMarkerTests
	{
		private static TasMovie MakeMovie(int frames) => TasMovieTests.MakeMovie(frames);

		private static TasMovieMarker Permanent(TasMovie movie, MarkerPermanence kind)
			=> movie.Markers.Permanent(kind);

		[TestMethod]
		public void AllThreeExistOnAFreshMovie()
		{
			var movie = MakeMovie(10);
			foreach (var kind in new[] { MarkerPermanence.RunStart, MarkerPermanence.LastInput, MarkerPermanence.RunEnd })
			{
				Assert.IsNotNull(Permanent(movie, kind), $"{kind} should exist without anyone placing it");
			}
		}

		[TestMethod]
		public void RunStartIsAlwaysFrameZero()
		{
			var movie = MakeMovie(10);
			Assert.AreEqual(0, Permanent(movie, MarkerPermanence.RunStart).Frame);

			movie.SetBoolState(5, "A", true);
			movie.InsertEmptyFrame(0, 5);
			movie.Truncate(3);

			Assert.AreEqual(0, Permanent(movie, MarkerPermanence.RunStart).Frame,
				"nothing moves the start of a run");
		}

		[TestMethod]
		public void RunEndFollowsTheLengthOfTheMovie()
		{
			var movie = MakeMovie(10);
			Assert.AreEqual(9, Permanent(movie, MarkerPermanence.RunEnd).Frame);

			movie.InsertEmptyFrame(10, 5);
			Assert.AreEqual(14, Permanent(movie, MarkerPermanence.RunEnd).Frame, "extending moves the end");

			movie.SetBoolState(3, "A", true);   // so the truncation has something to keep
			movie.Truncate(6);
			Assert.AreEqual(5, Permanent(movie, MarkerPermanence.RunEnd).Frame, "truncating moves the end back");
		}

		[TestMethod]
		public void LastInputFollowsTheLastFrameAnythingIsPressedOn()
		{
			var movie = MakeMovie(20);
			Assert.AreEqual(0, Permanent(movie, MarkerPermanence.LastInput).Frame,
				"nothing pressed anywhere: it sits at the start");

			movie.SetBoolState(7, "A", true);
			Assert.AreEqual(7, Permanent(movie, MarkerPermanence.LastInput).Frame);

			movie.SetBoolState(12, "A", true);
			Assert.AreEqual(12, Permanent(movie, MarkerPermanence.LastInput).Frame, "a later press moves it on");

			movie.SetBoolState(12, "A", false);
			Assert.AreEqual(7, Permanent(movie, MarkerPermanence.LastInput).Frame,
				"and releasing it moves it back to the one before");
		}

		[TestMethod]
		public void LastInputSurvivesEditsThatDoNotTouchIt()
		{
			// the answer is kept between edits rather than re-read, so the two ways
			// it can go wrong are worth pinning: an edit AFTER it that presses
			// nothing must leave it alone, and one that erases it must find it again
			var movie = MakeMovie(20);
			movie.SetBoolState(3, "A", true);

			movie.InsertEmptyFrame(10, 5);
			Assert.AreEqual(3, Permanent(movie, MarkerPermanence.LastInput).Frame,
				"padding a run does not extend its input");

			movie.ClearFrame(3);
			Assert.AreEqual(0, Permanent(movie, MarkerPermanence.LastInput).Frame,
				"and erasing the only press takes it back to the start");
		}

		[TestMethod]
		public void TheyMayShareAFrameAndAreStillThreeMarkers()
		{
			var movie = MakeMovie(1);
			var frames = new[] { MarkerPermanence.RunStart, MarkerPermanence.LastInput, MarkerPermanence.RunEnd }
				.Select(kind => Permanent(movie, kind).Frame)
				.ToArray();

			CollectionAssert.AreEqual(new[] { 0, 0, 0 }, frames, "a one frame movie starts and ends in the same place");
			Assert.AreEqual(3, movie.Markers.Count(static m => m.IsPermanent), "converging does not merge them");
		}

		[TestMethod]
		public void SharingAFrameTheyStillReadInRunOrder()
		{
			var movie = MakeMovie(1);   // one frame: all three land on frame zero
			CollectionAssert.AreEqual(
				new[] { MarkerPermanence.RunStart, MarkerPermanence.LastInput, MarkerPermanence.RunEnd },
				movie.Markers.Where(static m => m.IsPermanent).Select(static m => m.Permanence).ToArray(),
				"a run starts, then stops pressing, then ends - in that order, whatever frame they are on");
		}

		[TestMethod]
		public void AUserMarkerOnTheirFrameComesAfterThem()
		{
			var movie = MakeMovie(1);
			movie.Markers.Add(0, "mine");
			Assert.IsTrue(movie.Markers[0].IsPermanent, "the run's own come first on a shared frame");
			Assert.AreEqual("mine", movie.Markers.Last().Message);
		}

		[TestMethod]
		public void TheyCannotBeRemoved()
		{
			var movie = MakeMovie(10);
			var end = Permanent(movie, MarkerPermanence.RunEnd);

			movie.Markers.Remove(end);
			Assert.IsNotNull(Permanent(movie, MarkerPermanence.RunEnd), "Remove leaves it alone");

			movie.Markers.RemoveAll(static _ => true);
			Assert.AreEqual(3, movie.Markers.Count(static m => m.IsPermanent), "RemoveAll leaves all three");
		}

		[TestMethod]
		public void TheyCannotBeMoved()
		{
			var movie = MakeMovie(10);
			movie.Markers.Move(9, 4);   // frame 9 is the run end
			Assert.AreEqual(9, Permanent(movie, MarkerPermanence.RunEnd).Frame);
		}

		[TestMethod]
		public void TruncationLeavesThemAndPutsThemWhereItEnded()
		{
			var movie = MakeMovie(20);
			movie.BindMarkersToInput = true;   // so the truncation reaches the markers at all
			movie.SetBoolState(3, "A", true);
			movie.Markers.Add(15, "the user's own");

			movie.Truncate(10);

			Assert.AreEqual(3, movie.Markers.Count(static m => m.IsPermanent), "all three survive a truncation");
			Assert.AreEqual(9, Permanent(movie, MarkerPermanence.RunEnd).Frame, "and the end is where the movie now ends");
			Assert.IsFalse(movie.Markers.Any(static m => !m.IsPermanent && m.Frame is 15),
				"while the user's marker past the end is truncated away, as before");
		}

		[TestMethod]
		public void AUserMarkerMayShareAFrameWithOneAndDoesNotRenameIt()
		{
			var movie = MakeMovie(10);
			var end = Permanent(movie, MarkerPermanence.RunEnd);
			var wasCalled = end.Message;

			movie.Markers.Add(9, "mine");

			Assert.AreEqual(wasCalled, end.Message, "the run's own marker keeps its name");
			Assert.IsTrue(movie.Markers.Any(static m => m.Frame is 9 && !m.IsPermanent && m.Message == "mine"),
				"and the user's marker is there beside it");
		}

		[TestMethod]
		public void TheyAreNotWrittenToTheFile()
		{
			var movie = MakeMovie(10);
			movie.Markers.Add(4, "the user's own");

			var written = movie.Markers.ToString();

			Assert.IsTrue(written.Contains("the user's own"), "the user's markers are written");
			foreach (var line in written.Split('\n').Skip(1))
			{
				Assert.IsFalse(line.Contains("Run start") || line.Contains("Last input") || line.Contains("Run end"),
					"the run's own markers are derived, and writing them down would let them go stale");
			}
		}
	}
}
