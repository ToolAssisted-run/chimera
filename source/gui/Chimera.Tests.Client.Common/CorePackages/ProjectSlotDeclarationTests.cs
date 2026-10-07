using Chimera.Client.Common;

namespace Chimera.Tests.Client.Common.CorePackages
{
	/// <summary>
	/// file_slots.json as the new-project wizard reads it. A core that takes no
	/// file at all - a game core whose game is all firmware - declares an empty
	/// list, and that is a declaration the wizard can make a project from; only
	/// a missing or unreadable one is none.
	/// </summary>
	[TestClass]
	public class ProjectSlotDeclarationTests
	{
		[TestMethod]
		public void AnEmptyListIsADeclarationOfNoFiles()
		{
			var declaration = ProjectSlotDeclaration.Parse("""{ "slots": [] }""");
			Assert.IsNotNull(declaration);
			Assert.AreEqual(0, declaration.Slots.Count);
			Assert.AreEqual(0, declaration.AtLeastOneOf.Count);
		}

		[TestMethod]
		public void NoListOrNoTextIsNoDeclaration()
		{
			Assert.IsNull(ProjectSlotDeclaration.Parse("""{ "_comment": "no slots array" }"""));
			Assert.IsNull(ProjectSlotDeclaration.Parse(""));
			Assert.IsNull(ProjectSlotDeclaration.Parse("not json"));
			Assert.IsNull(ProjectSlotDeclaration.Parse("""{ "slots": [ { "title": "no id" } ] }"""));
		}

		[TestMethod]
		public void ADeclaredSlotReadsBack()
		{
			var declaration = ProjectSlotDeclaration.Parse("""{ "slots": [ { "id": "levels", "min": 0, "max": 1, "formats": [ "dat" ] } ] }""");
			Assert.IsNotNull(declaration);
			Assert.AreEqual(1, declaration.Slots.Count);
			Assert.AreEqual("levels", declaration.Slots[0].Id);
			Assert.AreEqual(1, declaration.Slots[0].Max);
		}
	}
}
