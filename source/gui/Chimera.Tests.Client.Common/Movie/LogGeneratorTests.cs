using Chimera.Client.Common;
using Chimera.Common;
using Chimera.Emulation.Common;

namespace Chimera.Tests.Client.Common.Movie
{
	[TestClass]
	public class LogGeneratorTests
	{
		private SimpleController _boolController = null!;
		private SimpleController _axisController = null!;

		[TestInitialize]
		public void Initializer()
		{
			_boolController = new(new ControllerDefinition("Dummy Gamepad") { BoolButtons = { "A" } }.MakeImmutable());
			_boolController.Definition.BuildMnemonicsCache(VSystemID.Raw.NES);
			_axisController = new(
				new ControllerDefinition("Dummy Gamepad")
					.AddXYPair("Stick{0}", AxisPairOrientation.RightAndUp, 0.RangeTo(200), 100)
					.MakeImmutable());
			_axisController.Definition.BuildMnemonicsCache(VSystemID.Raw.NES);
		}

#pragma warning disable BHI1600 //TODO disambiguate assert calls
		/// <summary>
		/// A movie's log key can name controls the running machine does not have:
		/// a project recorded with a second controller, reopened with that port
		/// empty. The movie's definition takes its columns from the key, but the
		/// mnemonic cache came from the machine's definition, so looking a
		/// pressed P2 button up in it threw - and TAStudio, which asks for every
		/// column's mnemonic when it opens, could not open the movie at all
		/// (KeyNotFoundException in MnemonicMap).
		/// </summary>
		[TestMethod]
		public void LogKeyControlsTheMachineLacksDoNotThrow()
		{
			var machine = new ControllerDefinition("NES Controller") { BoolButtons = { "P1 A", "P1 B" } }.MakeImmutable();
			machine.BuildMnemonicsCache(VSystemID.Raw.NES);

			var movie = new MovieController(machine, "#P1 A|P1 B|#P2 A|P2 B|");
			movie.SetFromMnemonic("|A.|AB|");

			Assert.AreEqual("|A.|AB|", LogEntryGenerator.GenerateLogEntry(movie));
			Assert.AreEqual('A', movie.Definition.MnemonicFor("P2 A"));
			Assert.AreEqual('B', movie.Definition.MnemonicFor("P2 B"));
			// the display string asks the same question
			_ = InputDisplayGenerator.Generate(movie);
		}

		/// <summary>
		/// A name no table knows is abbreviated rather than refused. It used to
		/// come out as '!', which heads a TAStudio column with a character that
		/// names nothing; the last word of the name at least points at what was
		/// pressed. Whole controllers came out that way - all sixty of the 3DO's
		/// peripherals, the Super Scope's buttons, a keypad key with a typo in
		/// its table entry.
		/// </summary>
		[TestMethod]
		public void GenerateLogEntry_UnknownButtonsAreAbbreviated()
		{
			SimpleController controller = new(new ControllerDefinition("Dummy Gamepad") { BoolButtons = { "Unknown Button" } }.MakeImmutable());
			controller.Definition.BuildMnemonicsCache(VSystemID.Raw.NES);
			controller["Unknown Button"] = true;
			var actual = LogEntryGenerator.GenerateLogEntry(controller);
			Assert.AreEqual("|B|", actual);
		}

		[TestMethod]
		public void GenerateLogEntry_BoolPressed_GeneratesMnemonic()
		{
			_boolController["A"] = true;
			var actual = LogEntryGenerator.GenerateLogEntry(_boolController);
			Assert.AreEqual("|A|", actual);
		}

		[TestMethod]
		public void GenerateLogEntry_BoolUnPressed_GeneratesPeriod()
		{
			_boolController["A"] = false;
			var actual = LogEntryGenerator.GenerateLogEntry(_boolController);
			Assert.AreEqual("|.|", actual);
		}

		[TestMethod]
		public void GenerateLogEntry_Floats()
		{
			var actual = LogEntryGenerator.GenerateLogEntry(_axisController);
			Assert.AreEqual("|  100,  100,|", actual);
		}

		[TestMethod]
		public void GenerateLogEntry_NoStupidHack()
		{
			var upController = new SimpleController(new ControllerDefinition("Dummy Gamepad") { BoolButtons = { "Up" } }.MakeImmutable());
			upController.Definition.BuildMnemonicsCache(VSystemID.Raw.NES);

			var logEntry = LogEntryGenerator.GenerateLogEntry(upController);
			Assert.AreEqual("|.|", logEntry);
		}

		[TestMethod]
		public void GenerateLogEntry_EmptyPlayerGroups()
		{
			var upController = new SimpleController(new ControllerDefinition("Dummy Gamepad") { BoolButtons = { "P2 Up" } }.MakeImmutable());
			upController.Definition.BuildMnemonicsCache(VSystemID.Raw.NES);

			var logEntry = LogEntryGenerator.GenerateLogEntry(upController);
			Assert.AreEqual("|||.|", logEntry);
		}

		[TestMethod]
		public void GenerateLogKey_EmptyPlayerGroups()
		{
			var upControllerDefinition = new ControllerDefinition("Dummy Gamepad") { BoolButtons = { "P2 Up" } }.MakeImmutable();
			upControllerDefinition.BuildMnemonicsCache(VSystemID.Raw.NES);

			var logKey = LogEntryGenerator.GenerateLogKey(upControllerDefinition);
			Assert.AreEqual("###P2 Up|", logKey);
		}

		[TestMethod]
		public void GenerateLogEntry_MovieController()
		{
			var simpleController = new SimpleController(new ControllerDefinition("Dummy Gamepad") { BoolButtons = { "P1 Up", "P3 A" } }.MakeImmutable());
			simpleController.Definition.BuildMnemonicsCache(VSystemID.Raw.NES);

			var originalLogEntry = LogEntryGenerator.GenerateLogEntry(simpleController);
			var originalLogKey = LogEntryGenerator.GenerateLogKey(simpleController.Definition);

			// just for safety, should be covered by the above tests already
			Assert.AreEqual("||.||.|", originalLogEntry);
			Assert.AreEqual("##P1 Up|##P3 A|", originalLogKey);

			// ensure a MovieController constructed with ControllerDefinition and LogKey
			// generates the exact same outputs as the original SimpleController
			MovieController movieController = new MovieController(simpleController.Definition, originalLogKey);

			var newLogEntry = LogEntryGenerator.GenerateLogEntry(movieController);
			Assert.AreEqual(originalLogEntry, newLogEntry);

			var newLogKey = LogEntryGenerator.GenerateLogKey(movieController.Definition);
			Assert.AreEqual(originalLogKey, newLogKey);
		}
#pragma warning restore BHI1600
	}
}
