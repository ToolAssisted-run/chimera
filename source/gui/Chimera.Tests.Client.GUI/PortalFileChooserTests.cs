using System.Linq;

using Chimera.Client.GUI;

namespace Chimera.Tests.Client.GUI
{
	/// <summary>
	/// What the desktop portal's file chooser is told and what is made of its answer. The
	/// D-Bus conversation itself is not here: it needs a session bus with a portal on it,
	/// which the test machine does not have (docs/folder-picker.md says how it was tried).
	/// </summary>
	[TestClass]
	public class PortalFileChooserTests
	{
		[TestMethod]
		public void AWinFormsFilterBecomesTheSameFiltersInOrder()
		{
			var filters = PortalFileChooser.FiltersFrom("Projects (*.tasproj)|*.tasproj|Discs (*.cue;*.iso)|*.cue; *.iso|All files (*.*)|*.*");
			CollectionAssert.AreEqual(new[] { "Projects (*.tasproj)", "Discs (*.cue;*.iso)", "All files (*.*)" }, filters.Select(static f => f.Label).ToArray());
			CollectionAssert.AreEqual(new[] { "*.cue", "*.iso" }, filters[1].Globs);
		}

		[TestMethod]
		public void StarDotStarMeansEveryFileEvenOneWithNoDot()
		{
			// as a glob "*.*" would leave out a file named "README"; WinForms never does
			var filters = PortalFileChooser.FiltersFrom("All files (*.*)|*.*");
			CollectionAssert.AreEqual(new[] { "*" }, filters[0].Globs);
		}

		[TestMethod]
		public void NoFilterOrAHalfOneSendsNothing()
		{
			Assert.AreEqual(0, PortalFileChooser.FiltersFrom("").Count);
			Assert.AreEqual(0, PortalFileChooser.FiltersFrom("A label with no patterns").Count);
			Assert.AreEqual(0, PortalFileChooser.FiltersFrom("Empty|").Count);
		}

		[TestMethod]
		public void AFileUriIsTheLocalPathWithItsEscapesUndone()
		{
			Assert.AreEqual("/home/someone/a b's #1.tasproj", PortalFileChooser.PathFromUri("file:///home/someone/a%20b%27s%20%231.tasproj"));
			Assert.IsNull(PortalFileChooser.PathFromUri("smb://server/share/file.iso"), "only a file on this machine is a path");
			Assert.IsNull(PortalFileChooser.PathFromUri("not a uri"));
		}

		[TestMethod]
		public void TheRequestObjectIsNamedAfterTheConnection()
		{
			Assert.AreEqual("1_42", PortalFileChooser.UniqueNameAsPathPart(":1.42"));
		}

		[TestMethod]
		public void ANameSavedWithoutAnExtensionGetsTheFiltersOne()
		{
			Assert.AreEqual("/tmp/report.zip", SaveFilePicker.WithExtension("/tmp/report", "Zip archive (*.zip)|*.zip", 1, ""));
			Assert.AreEqual("/tmp/log.txt", SaveFilePicker.WithExtension("/tmp/log", "Text|*.txt|All files|*.*", 1, "log"), "the filter in use comes before the default");
			Assert.AreEqual("/tmp/log.log", SaveFilePicker.WithExtension("/tmp/log", "Text|*.txt|All files|*.*", 2, "log"), "a filter that names no one extension gives way to the default");
			Assert.AreEqual("/tmp/log", SaveFilePicker.WithExtension("/tmp/log", "All files|*.*", 1, ""), "with neither, the name stays as typed");
		}

		[TestMethod]
		public void ANameSavedWithAnExtensionKeepsIt()
		{
			Assert.AreEqual("/tmp/report.tar", SaveFilePicker.WithExtension("/tmp/report.tar", "Zip archive (*.zip)|*.zip", 1, "zip"));
		}
	}
}
