using Chimera.Client.Common;
using Chimera.Client.GUI;

using NLua;

namespace Chimera.Tests.Client.GUI
{
	/// <summary>
	/// The tastudio Lua library, called on a session with no piano roll open.
	///
	/// Every one of these used to reach straight for <c>Tools.TAStudio</c>, which
	/// is a get-OR-CREATE: the call built a TAStudio window and then died inside
	/// it on the project that was not there, so what the script got back was a
	/// raw NullReferenceException naming a property it had never heard of
	/// (PlaybackBox.RecordingMode). <c>tastudio.engaged()</c> is the question a
	/// script is meant to ask first; a script that did not ask still has to get
	/// an answer rather than a stack trace.
	///
	/// No window is constructed here, and that is the point: if a guard goes
	/// missing, the library tries to build one out of a ToolManager that has no
	/// owner and the test fails loudly.
	/// </summary>
	[TestClass]
	public class TAStudioLuaSafetyTests
	{
		/// <summary>
		/// Enough of <see cref="ILuaLibraries"/> to construct a library. The
		/// methods under test are the ones that answer before touching anything,
		/// so none of this is reached.
		/// </summary>
		private sealed class NoLuaLibraries : ILuaLibraries
		{
			public LuaFile CurrentFile => null;

			public bool IsRebootingCore { get; set; }

			public bool IsUpdateSupressed { get; set; }

			public PathEntryCollection PathEntries => null;

			public ApiGroup ProhibitedApis => ApiGroup.NONE;

			public NLuaTableHelper GetTableHelper() => null;

			public void Sandbox(LuaFile luaFile, Action callback, Action<string> exceptionCallback = null, ApiGroup prohibitedApis = ApiGroup.NONE)
				=> callback();
		}

		private static TAStudioLuaLibrary LibraryWithNothingOpen()
			=> new(new NoLuaLibraries(), apiContainer: null, logOutputCallback: static _ => { })
			{
				// a tool manager holding no tools: Has<TAStudio>() is false, and
				// asking it for one would try to build a window with no owner
				Tools = new ToolManager(
					owner: null,
					config: null,
					displayManager: null,
					inputManager: null,
					emulator: null,
					movieSession: null,
					game: null),
			};

		[TestMethod]
		public void EngagedSaysNoWhenNothingIsOpen()
			=> Assert.IsFalse(LibraryWithNothingOpen().Engaged());

		[TestMethod]
		public void GetRecordingAnswersFalseInsteadOfThrowing()
			=> Assert.IsFalse(LibraryWithNothingOpen().GetRecording());

		[TestMethod]
		public void SetRecordingDoesNothingInsteadOfThrowing()
		{
			var lib = LibraryWithNothingOpen();
			lib.SetRecording(true);
			lib.SetRecording(false);
			Assert.IsFalse(lib.GetRecording());
		}

		[TestMethod]
		public void ToggleRecordingDoesNothingInsteadOfThrowing()
		{
			var lib = LibraryWithNothingOpen();
			lib.SetRecording();
			Assert.IsFalse(lib.GetRecording());
		}

		[TestMethod]
		public void SetBranchTextDoesNothingInsteadOfThrowing()
		{
			var lib = LibraryWithNothingOpen();
			lib.SetBranchText("a label");
			lib.SetBranchText("a label", index: 0);
		}

		[TestMethod]
		public void BranchIndexByIdAnswersNilInsteadOfThrowing()
		{
			var lib = LibraryWithNothingOpen();
			Assert.IsNull(lib.GetBranchIndexByID("97021544-2454-4483-824f-47f75e7fcb6a"));
			Assert.IsNull(lib.GetBranchIndexByID("not a uuid"));
		}
	}
}
