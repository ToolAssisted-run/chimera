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
		[TestMethod]
		public void GenerateLogEntry_ExclamationForUnknownButtons()
		{
			SimpleController controller = new(new ControllerDefinition("Dummy Gamepad") { BoolButtons = { "Unknown Button" } }.MakeImmutable());
			controller.Definition.BuildMnemonicsCache(VSystemID.Raw.NES);
			controller["Unknown Button"] = true;
			var actual = LogEntryGenerator.GenerateLogEntry(controller);
			Assert.AreEqual("|!|", actual);
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
