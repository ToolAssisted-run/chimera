using System;
using System.IO;

using Chimera.Client.Common;

namespace Chimera.Tests.Client.Common.Movie
{
	[TestClass]
	public class MovieSessionGetTests
	{
		private static MovieSession MakeSession()
			=> new(
				new MovieConfig(),
				Path.GetTempPath(),
				dialogParent: null,
				pauseCallback: static () => { },
				modeChangedCallback: static () => { },
				changesChangedCallback: static (_, _) => { });

		/// <summary>
		/// The project extension must be recognised in whatever case the file
		/// arrived with: a case-preserving filesystem hands back what the
		/// downloader wrote, and a run downloaded as .chimeraproject used to
		/// fail this check and end the whole open-project flow in silence
		/// (issue #21) - the ONE case-sensitive extension check on that path.
		/// </summary>
		[TestMethod]
		[DataRow("M100049.chimeraProject")]
		[DataRow("M100049.chimeraproject")]
		[DataRow("M100049.CHIMERAPROJECT")]
		public void GetRecognisesProjectExtensionInAnyCase(string fileName)
		{
			var movie = MakeSession().Get(Path.Combine(Path.GetTempPath(), fileName), loadMovie: false);
			Assert.IsNotNull(movie, $"'{fileName}' should be recognised as a project");
			Assert.IsInstanceOfType(movie, typeof(ITasMovie));
		}

		[TestMethod]
		public void GetRefusesWhatIsNotAProject()
		{
			Assert.IsNull(MakeSession().Get(Path.Combine(Path.GetTempPath(), "movie.bk2"), loadMovie: false));
		}
	}
}
