using BizHawk.Emulation.Common.Engine;

namespace BizHawk.Tests.Emulation.Common.Engine
{
	/// <summary>
	/// The movie's text lumps through the managed wrappers - the marshalling half
	/// of what engine/tests/test_movie_text.cpp pins byte for byte.
	/// </summary>
	[TestClass]
	public class EngineMovieTextTests
	{
		[TestMethod]
		public void AHeaderRoundTripsWithItsQuirks()
		{
			const string lump = "MovieVersion BizHawk v2.0\nAuthor jaffar\nCore QuickerNesHawk\n\n";
			using EngineMovieHeader header = new();
			header.Parse(lump);
			Assert.AreEqual(3L, header.Count);
			Assert.AreEqual(("Author", "jaffar"), header[1]);
			Assert.AreEqual(lump, header.Serialize(crlf: false));

			// first occurrence wins on parse, exactly as the old loader behaved
			header.Parse("Author first\nAuthor second\n");
			Assert.AreEqual(1L, header.Count);
			Assert.AreEqual(("Author", "first"), header[0]);
		}

		[TestMethod]
		public void CommentsKeepOrderAndDuplicatesAndDropBlanks()
		{
			using EngineTextLines lines = new();
			lines.Parse("one\n\none\n   \ntwo\n\n");
			Assert.AreEqual(3L, lines.Count);
			Assert.AreEqual("one", lines[1]);
			Assert.AreEqual("one\none\ntwo\n\n", lines.Serialize(crlf: false));
		}

		[TestMethod]
		public void ASubtitleLineRoundTrips()
		{
			Assert.IsTrue(EngineSubtitleLine.TryParse("subtitle 100 10 20 120 FFFFFFFF hello world", out var fields, out var message));
			Assert.AreEqual(100, fields.Frame);
			Assert.AreEqual(0xFFFFFFFFu, fields.Color);
			Assert.AreEqual("hello world", message);
			Assert.AreEqual("subtitle 100 10 20 120 FFFFFFFF hello world", EngineSubtitleLine.Format(fields, message));

			Assert.IsFalse(EngineSubtitleLine.TryParse("subtitle x 2 3 4 AABBCCDD nope", out _, out _), "a garbage line is refused, not fatal");
		}
	}
}
