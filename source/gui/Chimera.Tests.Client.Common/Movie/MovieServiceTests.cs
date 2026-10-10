using Chimera.Client.Common;

namespace Chimera.Tests.Client.Common.Movie
{
	[TestClass]
	public class MovieServiceTests
	{
		[TestMethod]
		[DataRow(null, 1.0)]
		[DataRow("", 1.0)]
		[DataRow(" ", 1.0)]
		[DataRow("NonsenseString", 1.0)]
		[DataRow("Chimera v1.0.0", 1.0)]
		[DataRow("Chimera Project File v1.0", 1.0)]
		[DataRow("Chimera Project File v1.1", TasMovie.CurrentVersion)]
		public void ParseTasMovieVersion(string movieVersion, double expected)
		{
			var actual = MovieService.ParseTasMovieVersion(movieVersion);
#pragma warning disable BHI1600 // wants message argument
			Assert.AreEqual(expected, actual);
#pragma warning restore BHI1600
		}

		/// <summary>
		/// The Movies folder is made when it is missing (issue 233), and asking again when it
		/// is there changes nothing.
		/// </summary>
		[TestMethod]
		public void TheMoviesFolderIsMadeWhenMissing()
		{
			var root = System.IO.Path.Combine(System.IO.Path.GetTempPath(), "chimera-movies-" + System.Guid.NewGuid().ToString("N"));
			try
			{
				PathEntryCollection paths = new();
				var movies = System.IO.Path.Combine(root, "Movies");
				paths[PathEntryCollection.GLOBAL, "Movies"].Path = movies;
				Assert.IsFalse(System.IO.Directory.Exists(movies));
				Assert.IsTrue(MovieService.EnsureMoviesFolder(paths));
				Assert.IsTrue(System.IO.Directory.Exists(movies), "the folder was made");
				System.IO.File.WriteAllText(System.IO.Path.Combine(movies, "kept.txt"), "x");
				Assert.IsTrue(MovieService.EnsureMoviesFolder(paths));
				Assert.IsTrue(System.IO.File.Exists(System.IO.Path.Combine(movies, "kept.txt")), "what was in it is still there");
			}
			finally
			{
				if (System.IO.Directory.Exists(root)) System.IO.Directory.Delete(root, recursive: true);
			}
		}

		/// <summary>
		/// "Open Project" starts in the folder of the project opened last; with none, or when
		/// that folder is gone, in the Movies folder; and nowhere in particular when neither exists.
		/// </summary>
		[TestMethod]
		public void OpenProjectStartsWhereTheLastOneWasOrInMovies()
		{
			var root = System.IO.Path.Combine(System.IO.Path.GetTempPath(), "chimera-open-" + System.Guid.NewGuid().ToString("N"));
			var movies = System.IO.Path.Combine(root, "Movies");
			var elsewhere = System.IO.Path.Combine(root, "elsewhere");
			System.IO.Directory.CreateDirectory(movies);
			System.IO.Directory.CreateDirectory(elsewhere);
			try
			{
				Assert.AreEqual(movies, MovieService.ProjectsOpenIn("", movies), "nothing opened yet");
				Assert.AreEqual(elsewhere, MovieService.ProjectsOpenIn(System.IO.Path.Combine(elsewhere, "run.chimeraProject"), movies));
				Assert.AreEqual(movies, MovieService.ProjectsOpenIn(System.IO.Path.Combine(root, "gone", "run.chimeraProject"), movies),
					"the last project's folder no longer exists");
				Assert.AreEqual("", MovieService.ProjectsOpenIn(null, System.IO.Path.Combine(root, "no-movies")));
			}
			finally
			{
				System.IO.Directory.Delete(root, recursive: true);
			}
		}
	}
}
