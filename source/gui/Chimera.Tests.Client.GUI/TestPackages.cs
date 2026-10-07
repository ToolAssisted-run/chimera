using Chimera.Client.Common;
using Chimera.Emulation.Common.Waterbox;

namespace Chimera.Tests.Client.GUI
{
	/// <summary>
	/// The declarations these tests write, read back the way the frontend reads a
	/// real package's.
	///
	/// Both parsers answer null for text they cannot use, which for a real package
	/// is the honest answer and for a test is its own mistake. Saying so here means
	/// a typo in a test's JSON fails as "this did not parse" rather than as a null
	/// dereference in whatever the test does with it next.
	/// </summary>
	internal static class TestPackages
	{
		/// <summary>The slot declaration this test wrote (file_slots.json).</summary>
		internal static ProjectSlotDeclaration Slots(string json)
		{
			var declaration = ProjectSlotDeclaration.Parse(json);
			Assert.IsNotNull(declaration, "the slot declaration this test writes did not parse");
			return declaration;
		}

		/// <summary>The core declaration this test wrote (waterbox.config).</summary>
		internal static WaterboxConfig Config(string json)
		{
			var cfg = WaterboxConfig.FromJson(json);
			Assert.IsNotNull(cfg, "the package this test declares did not parse");
			return cfg;
		}
	}
}
