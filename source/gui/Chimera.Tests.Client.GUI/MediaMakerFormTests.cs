using System.IO;
using System.Linq;
using System.Windows.Forms;

using Chimera.Client.Common;
using Chimera.Client.GUI;

namespace Chimera.Tests.Client.GUI
{
	/// <summary>
	/// The Reproducible Media Maker window. It decides nothing about the bytes -
	/// the engine packs - so what is worth pinning here is that it opens, that it
	/// offers the three shapes, that it will not start without somewhere to write,
	/// and that nothing sits on top of anything else.
	/// </summary>
	[TestClass]
	public class MediaMakerFormTests
	{
		[TestMethod]
		public void ItOpens()
		{
			using MediaMakerForm form = new();
			form.Show();
			Assert.AreEqual("Reproducible Media Maker", form.Text);
		}

		/// <summary>
		/// Somebody recording with static titles on does not want the window bar
		/// naming their work; this window has nothing project-specific to say, so
		/// its title is the same either way.
		/// </summary>
		[TestMethod]
		public void StaticTitlesChangeNothing()
		{
			using MediaMakerForm form = new();
			form.Config = new Config { UseStaticWindowTitles = true };
			form.Show();
			Assert.AreEqual("Reproducible Media Maker", form.Text);
		}

		/// <summary>Zip, ISO and floppy, and the box opens on one of them.</summary>
		[TestMethod]
		public void ItOffersTheThreeShapes()
		{
			using MediaMakerForm form = new();
			form.Show();
			var combo = form.Controls.OfType<ComboBox>().Single();
			Assert.AreEqual(3, combo.Items.Count);
			Assert.AreEqual(0, combo.SelectedIndex);
			CollectionAssert.AllItemsAreNotNull(combo.Items.Cast<object>().ToList());
		}

		/// <summary>
		/// Nothing is chosen yet, so there is nothing to make: the button is off
		/// rather than offering to pack a folder that was never named.
		/// </summary>
		[TestMethod]
		public void MakeIsOffUntilThereIsSomethingToMake()
		{
			using MediaMakerForm form = new();
			form.Show();
			var make = form.Controls.OfType<Button>().Single(b => b.Text == "Make");
			Assert.IsFalse(make.Enabled);
		}

		private const string DiscRecipe =
			@"{ ""id"": ""disc"", ""label"": ""Testbox disc"", ""format"": ""iso9660"", ""when"": { ""rootFile"": ""DISC.ID"" },
			    ""systemArea"": [ { ""at"": 0, ""u32be"": 1 }, { ""at"": 12, ""u32be"": ""lastSector"" } ] }";

		private static string Folder(string name, params string[] files)
		{
			var dir = Path.Combine(Path.GetTempPath(), "chimera-mediamaker-tests", name);
			if (Directory.Exists(dir)) Directory.Delete(dir, recursive: true);
			Directory.CreateDirectory(dir);
			foreach (var f in files) File.WriteAllText(Path.Combine(dir, f), f);
			return dir;
		}

		/// <summary>
		/// The window knows no machine. What a disc of some console needs beyond
		/// its files is the core's to declare, and all the window does with a
		/// recipe is ask the engine whether the folder is one it was written for,
		/// say so, and hand it over.
		/// </summary>
		[TestMethod]
		public void AFolderACoreRecognisesIsSaidToBe()
		{
			using MediaMakerForm form = new([ new MediaMakerForm.CoreRecipe("Testbox", "Testbox disc", DiscRecipe) ]);
			form.Show();
			form.SetFormat(1); // the ISO

			form.SetFolder(Folder("disc", "disc.id", "game.bin")); // the name is matched whatever its case
			Assert.IsNotNull(form.Recognised);
			StringAssert.Contains(form.RecognisedText, "Testbox disc");
			StringAssert.Contains(form.RecognisedText, "Testbox reads it");

			// the recipe is for an ISO: as a zip the same folder is just its files
			form.SetFormat(0);
			Assert.IsNull(form.Recognised);
			Assert.AreEqual("", form.RecognisedText);
			form.SetFormat(1);
			Assert.IsNotNull(form.Recognised);

			// and a folder it was not written for
			form.SetFolder(Folder("plain", "readme.txt"));
			Assert.IsNull(form.Recognised);
			Assert.AreEqual("", form.RecognisedText);
		}

		/// <summary>With no core declaring anything, nothing is ever recognised - whatever the folder holds.</summary>
		[TestMethod]
		public void WithNoRecipesAnImageIsItsFiles()
		{
			using MediaMakerForm form = new();
			form.Show();
			form.SetFormat(1);
			form.SetFolder(Folder("disc2", "DISC.ID", "PS3_DISC.SFB"));
			Assert.IsNull(form.Recognised);
			Assert.AreEqual("", form.RecognisedText);
		}

		/// <summary>
		/// The recipe reaches the image: the same folder packed with and without
		/// it differs in the system area and nowhere else, and the engine's own
		/// test says what the values are.
		/// </summary>
		[TestMethod]
		public void TheRecipeReachesTheImage()
		{
			var folder = Folder("disc3", "DISC.ID", "a.bin");
			var with = Path.Combine(Path.GetTempPath(), "chimera-mediamaker-tests", "with.iso");
			var without = Path.Combine(Path.GetTempPath(), "chimera-mediamaker-tests", "without.iso");
			Assert.IsTrue(Chimera.Emulation.Common.Engine.ChimeraEngine.MakeMedia(folder, with, 1, DiscRecipe, null, out var shaWith, out var error), error);
			Assert.IsTrue(Chimera.Emulation.Common.Engine.ChimeraEngine.MakeMedia(folder, without, 1, null, out var shaWithout, out error), error);
			Assert.AreNotEqual(shaWith, shaWithout);
			var a = File.ReadAllBytes(with);
			var b = File.ReadAllBytes(without);
			Assert.AreEqual(a.Length, b.Length);
			Assert.AreEqual(1, a[3], "the count the recipe asks for, big-endian at 0");
			Assert.IsTrue(b.Take(32768).All(static x => x is 0), "no recipe: a system area of zeros");
			Assert.IsTrue(a.Skip(32768).SequenceEqual(b.Skip(32768)), "and past it the same image");
		}

		/// <summary>
		/// A label that runs on over the field beside it hides what somebody
		/// typed. That happened in the greenzone budgets dialog and was invisible
		/// to every test of behaviour, so this window gets the geometric check
		/// from the start: nothing overlaps a text box or the progress bar.
		/// </summary>
		[TestMethod]
		public void NothingSitsOnTopOfAField()
		{
			using MediaMakerForm form = new();
			form.Show();
			var fields = form.Controls.OfType<Control>()
				.Where(static c => c is TextBox or ComboBox or ProgressBar)
				.ToList();
			Assert.IsTrue(fields.Count >= 4);
			foreach (var field in fields)
			{
				foreach (var label in form.Controls.OfType<Label>())
				{
					Assert.IsFalse(
						label.Bounds.IntersectsWith(field.Bounds),
						$"the label \"{label.Text}\" at {label.Bounds} covers {field.GetType().Name} at {field.Bounds}");
				}
			}
		}
	}
}
