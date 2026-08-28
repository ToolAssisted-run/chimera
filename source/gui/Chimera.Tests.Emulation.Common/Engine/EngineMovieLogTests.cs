using Chimera.Emulation.Common.Engine;

namespace Chimera.Tests.Emulation.Common.Engine
{
	/// <summary>
	/// The engine's own tests (engine/tests/) pin the format byte for byte; these
	/// pin the OTHER half of the pilot: that the ABI actually works through
	/// BizInvoke and Mono's marshalling, and that the managed wrapper preserves
	/// the C# parser's observable behaviour (quirks included).
	/// </summary>
	[TestClass]
	public class EngineMovieLogTests
	{
		[TestMethod]
		public void TheEngineIsLoadableAndSpeaksOurAbi()
		{
			// Instance throws on ABI mismatch; BuildInfo exercises a string return
			StringAssert.Contains(ChimeraEngine.BuildInfo, "\"component\":\"chimera engine\"");
		}

		[TestMethod]
		public void AMovieInputLogRoundTripsByteForByte()
		{
			const string lump = "[Input]\nLogKey:#Reset|Power|#P1 Up|Down|Left|Right|Start|Select|B|A|\n|..|.U......|\n|..|........|\n[/Input]\n";
			using EngineMovieLog log = new();
			Assert.IsTrue(log.Parse(lump, out var error), error);
			Assert.AreEqual(2, log.Count);
			Assert.AreEqual("|..|.U......|", log[0]);
			Assert.AreEqual("#Reset|Power|#P1 Up|Down|Left|Right|Start|Select|B|A|", log.Key);
			Assert.IsFalse(log.HasStateFrame);
			Assert.AreEqual(lump, log.Serialize(crlf: false));
			Assert.AreEqual(lump.Replace("\n", "\r\n"), log.Serialize(crlf: true), "same lump, Windows line ends");
		}

		[TestMethod]
		public void ASavestateInputBlockCarriesItsFrame()
		{
			using EngineMovieLog log = new();
			Assert.IsTrue(log.Parse("|.|\n|U|\nFrame 2\n", out _));
			Assert.IsTrue(log.HasStateFrame);
			Assert.AreEqual(2, log.StateFrame);
		}

		/// <summary>The quirk the old parser had, kept on purpose: a malformed frame number is a hard error, a missing one is not.</summary>
		[TestMethod]
		public void FrameNumberQuirksArePreserved()
		{
			using EngineMovieLog log = new();
			Assert.IsFalse(log.Parse("|.|\nFrame x\n", out var error));
			Assert.AreEqual("Savestate Frame number failed to parse", error);
			Assert.IsTrue(log.Parse("|.|\n", out _), "no Frame line is fine");
			Assert.IsFalse(log.HasStateFrame);
		}

		[TestMethod]
		public void ALogWithNoKeyStillWritesTheKeyLine()
		{
			using EngineMovieLog log = new();
			Assert.IsTrue(log.Parse("|.|\n", out _));
			Assert.IsNull(log.Key);
			Assert.AreEqual("[Input]\nLogKey:\n|.|\n[/Input]\n", log.Serialize(crlf: false));
			log.Key = "#K|";
			Assert.AreEqual("[Input]\nLogKey:#K|\n|.|\n[/Input]\n", log.Serialize(crlf: false));
		}
	}
}
