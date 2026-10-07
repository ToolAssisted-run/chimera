using System.IO;

using Chimera.Emulation.Common.Engine;

namespace Chimera.Tests.Emulation.Common.Engine
{
	/// <summary>
	/// The core log (Tools &gt; Export Core Log...) through NativeInvoke: off until
	/// somebody asks, a file that says when and by which Chimera once they do, and
	/// a refusal that says why when the file cannot be written. What the cores
	/// write into it is the witness's E:core-log leg.
	///
	/// Serial: the log is one per process, and this assembly runs methods in
	/// parallel, so one test's log was the next one's "it is off" failing.
	/// </summary>
	[TestClass]
	[DoNotParallelize]
	public class EngineCoreLogTests
	{
		[TestCleanup]
		public void Off() => ChimeraEngine.StopCoreLog();

		[TestMethod]
		public void ItIsOffUntilSomebodyAsks()
		{
			Assert.AreEqual("", ChimeraEngine.CoreLogPath);
		}

		[TestMethod]
		public void AskedForItStartsAFileAndSaysWhere()
		{
			var path = Path.Combine(Path.GetTempPath(), $"chimera-core-log-test-{Path.GetRandomFileName()}.txt");
			try
			{
				Assert.IsTrue(ChimeraEngine.StartCoreLog(path, out var error), error);
				Assert.AreEqual(path, ChimeraEngine.CoreLogPath);
				StringAssert.StartsWith(File.ReadAllText(path), "Chimera core log, started ");
				StringAssert.Contains(File.ReadAllText(path), "\"component\":\"chimera engine\"", "the build that kept it");

				ChimeraEngine.StopCoreLog();
				Assert.AreEqual("", ChimeraEngine.CoreLogPath);
				Assert.IsTrue(File.Exists(path), "turning it off leaves the file");
				StringAssert.Contains(File.ReadAllText(path), "the core log was turned off");
			}
			finally
			{
				File.Delete(path);
			}
		}

		[TestMethod]
		public void AFileThatCannotBeWrittenIsSaid()
		{
			var path = Path.Combine(Path.GetTempPath(), Path.GetRandomFileName(), "no-such-folder", "log.txt");
			Assert.IsFalse(ChimeraEngine.StartCoreLog(path, out var error));
			Assert.AreNotEqual("", error);
			Assert.AreEqual("", ChimeraEngine.CoreLogPath, "a refused start leaves it off");
		}
	}
}
