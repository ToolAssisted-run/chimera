using System.IO;

using Chimera.Client.Common;
using Chimera.Client.GUI;

namespace Chimera.Tests.Client.GUI
{
	/// <summary>
	/// Config &gt; Data Directory (issue #52). The window only records what the next start is to
	/// do; what is pinned is that it records the right thing, refuses the dangerous ones, and
	/// touches nothing on disk itself.
	/// </summary>
	[TestClass]
	[DoNotParallelize] // the environment variable is the process's
	public class DataDirectoryFormTests
	{
		private string _root = "";
		private string? _env;

		[TestInitialize]
		public void Setup()
		{
			_env = Environment.GetEnvironmentVariable("CHIMERA_DATA_HOME");
			Environment.SetEnvironmentVariable("CHIMERA_DATA_HOME", null);
			_root = Path.Combine(Path.GetTempPath(), "chimera-datadir-form-" + Path.GetRandomFileName());
			Directory.CreateDirectory(Path.Combine(_root, "current", "Projects"));
			File.WriteAllBytes(Path.Combine(_root, "current", "Projects", "history.bin"), new byte[2048]);
		}

		[TestCleanup]
		public void Teardown()
		{
			Environment.SetEnvironmentVariable("CHIMERA_DATA_HOME", _env);
			if (Directory.Exists(_root)) Directory.Delete(_root, recursive: true);
		}

		private Config ConfigAtCurrent() => new() { DataDirectory = Path.Combine(_root, "current") };

		[TestMethod]
		public void ChoosingAFolderRecordsAMoveForTheNextStartAndTouchesNothing()
		{
			var config = ConfigAtCurrent();
			var target = Path.Combine(_root, "elsewhere");
			long askedSize = -1;
			using DataDirectoryForm form = new(config, pickFolder: () => target,
				ask: (_, what, size, _) => { askedSize = size; Assert.AreEqual(DataDirectoryChoice.Empty, what); return DataDirectoryAnswer.Move; },
				freeSpaceAt: static _ => 1L << 40);
			form.Request(DataDirectory.TargetFor(target));

			Assert.AreEqual(target, config.DataDirectoryPending);
			Assert.IsTrue(config.DataDirectoryPendingMove);
			Assert.AreEqual(2048, askedSize, "the question says how much there is to move");
			Assert.AreEqual(Path.Combine(_root, "current"), config.DataDirectory, "the setting itself changes at the next start, with the data");
			Assert.IsTrue(File.Exists(Path.Combine(_root, "current", "Projects", "history.bin")), "nothing is moved by the window");
			StringAssert.Contains(form.PendingText, "everything here is moved there");
		}

		[TestMethod]
		public void AFolderThatAlreadyHoldsChimeraDataIsAdoptedNotMergedInto()
		{
			var config = ConfigAtCurrent();
			var other = Path.Combine(_root, "other");
			Directory.CreateDirectory(Path.Combine(other, "Cores"));
			using DataDirectoryForm form = new(config, ask: static (_, what, _, _) =>
			{
				Assert.AreEqual(DataDirectoryChoice.HoldsChimeraData, what);
				return DataDirectoryAnswer.LeaveBehind;
			}, freeSpaceAt: static _ => 0);
			form.Request(other);
			Assert.AreEqual(other, config.DataDirectoryPending);
			Assert.IsFalse(config.DataDirectoryPendingMove);
			StringAssert.Contains(form.PendingText, "What is here stays here");
		}

		[TestMethod]
		public void ADirectoryInsideTheCurrentOneIsRefusedWithoutAsking()
		{
			var config = ConfigAtCurrent();
			using DataDirectoryForm form = new(config, ask: static (_, _, _, _) => throw new InvalidOperationException("must not ask"),
				freeSpaceAt: static _ => 0);
			form.Request(Path.Combine(_root, "current", "Projects", "deeper"));
			Assert.IsNull(config.DataDirectoryPending);
			StringAssert.Contains(form.StatusText, "cannot be moved into itself");
		}

		[TestMethod]
		public void CancellingTheQuestionRecordsNothingAndAskingForTheCurrentPlaceWithdrawsAChange()
		{
			var config = ConfigAtCurrent();
			using DataDirectoryForm form = new(config, ask: static (_, _, _, _) => DataDirectoryAnswer.Cancel, freeSpaceAt: static _ => 0);
			form.Request(Path.Combine(_root, "elsewhere"));
			Assert.IsNull(config.DataDirectoryPending);

			config.DataDirectoryPending = Path.Combine(_root, "elsewhere");
			config.DataDirectoryPendingMove = true;
			form.Request(Path.Combine(_root, "current"));
			Assert.IsNull(config.DataDirectoryPending, "where it already is, is no change");
			Assert.IsFalse(config.DataDirectoryPendingMove);
		}

		[TestMethod]
		public void WhenTheEnvironmentDecidesTheWindowSaysSoAndOffersNothing()
		{
			Environment.SetEnvironmentVariable("CHIMERA_DATA_HOME", Path.Combine(_root, "portable"));
			using DataDirectoryForm form = new(ConfigAtCurrent(), freeSpaceAt: static _ => 0);
			Assert.AreEqual(Path.Combine(_root, "portable"), form.WhereText);
			Assert.IsFalse(form.CanChange);
			StringAssert.Contains(form.PendingText, "CHIMERA_DATA_HOME");
		}
	}
}
