using System.IO;
using System.Linq;

using Chimera.Client.Common;
using Chimera.Emulation.Common.Engine;

namespace Chimera.Tests.Client.Common.Movie
{
	/// <summary>
	/// End to end through the real container: engine-rendered text lumps into a
	/// real zip via ZipStateSaver, read back through BasicMovieInfo - the exact
	/// path a movie file takes, minus a live emulation session.
	/// </summary>
	[TestClass]
	public class MovieLumpRoundTripTests
	{
		[TestMethod]
		public void AMovieFileWrittenThroughTheEngineReadsBackIdentically()
		{
			var path = Path.Combine(Path.GetTempPath(), $"chimera-movie-{Path.GetRandomFileName()}.bk2");
			try
			{
				var create = ZipStateSaver.Create(path, compressionLevel: 1);
				Assert.IsFalse(create.IsError, "could not create the movie zip");
				var saver = create.Value!;
				saver.PutLump(BinaryStateLump.Movieheader, (TextWriter tw) =>
				{
					using EngineMovieHeader header = new();
					header.Set(HeaderKeys.MovieVersion, "BizHawk v2.0");
					header.Set(HeaderKeys.Author, "round trip");
					header.Set(HeaderKeys.Platform, "NES");
					tw.Write(header.Serialize(crlf: tw.NewLine == "\r\n"));
				});
				saver.PutLump(BinaryStateLump.Comments, (TextWriter tw) =>
				{
					using EngineTextLines lines = new();
					lines.Add("first comment");
					lines.Add("second comment");
					tw.Write(lines.Serialize(crlf: tw.NewLine == "\r\n"));
				});
				saver.PutLump(BinaryStateLump.Subtitles, (TextWriter tw) =>
				{
					using EngineTextLines lines = new();
					lines.Add("subtitle 5 0 0 120 FFFFFFFF hi there");
					tw.Write(lines.Serialize(crlf: tw.NewLine == "\r\n"));
				});
				saver.PutLump(BinaryStateLump.Input, (TextWriter tw) =>
				{
					using EngineMovieLog log = new();
					log.Key = "#P1 Up|Down|";
					log.Add("|..|");
					log.Add("|U.|");
					tw.Write(log.Serialize(crlf: tw.NewLine == "\r\n"));
				});
				Assert.IsFalse(saver.CloseAndDispose().IsError);

				BasicMovieInfo movie = new(path);
				Assert.IsTrue(movie.Load());
				Assert.AreEqual("round trip", movie.Author);
				Assert.AreEqual("NES", movie.SystemID);
				Assert.AreEqual(2, movie.FrameCount);
				CollectionAssert.AreEqual(new[] { "first comment", "second comment" }, movie.Comments.ToArray());
				Assert.AreEqual(1, movie.Subtitles.Count);
				Assert.AreEqual("hi there", movie.Subtitles[0].Message);
				Assert.AreEqual(5, movie.Subtitles[0].Frame);
				Assert.AreEqual("subtitle 5 0 0 120 FFFFFFFF hi there", movie.Subtitles[0].ToString());
			}
			finally
			{
				try { File.Delete(path); } catch { /* leftover temp file, not a failure */ }
			}
		}
	}
}
