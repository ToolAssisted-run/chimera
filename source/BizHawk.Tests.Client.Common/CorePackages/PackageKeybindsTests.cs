using System.IO;

using BizHawk.Client.Common;

namespace BizHawk.Tests.Client.Common.CorePackages
{
	/// <summary>
	/// A core package brings the default bindings for the controllers it declares; the frontend ships
	/// none. These cover what happens when the file is there, absent, or broken - the last one
	/// mattering most, since it must not cost the user the core itself.
	/// </summary>
	[TestClass]
	public class PackageKeybindsTests
	{
		private string _dir;

		[TestInitialize]
		public void Setup()
		{
			_dir = Path.Combine(Path.GetTempPath(), $"minihawk-keybinds-{Path.GetRandomFileName()}");
			Directory.CreateDirectory(_dir);
		}

		[TestCleanup]
		public void Cleanup()
		{
			if (Directory.Exists(_dir)) Directory.Delete(_dir, recursive: true);
		}

		private void Write(string json) => File.WriteAllText(Path.Combine(_dir, PackageKeybinds.FileName), json);

		[TestMethod]
		public void APackageWithoutBindingsSaysSo()
			=> Assert.IsNull(PackageKeybinds.Read(_dir), "shipping bindings is optional");

		[TestMethod]
		public void BindingsAreReadPerController()
		{
			Write("""
			{
				"AllTrollers": { "NES Controller": { "P1 A": "X, J1 B2", "P1 B": "Z" } },
				"AllTrollersAutoFire": { "NES Controller": { "P1 A": "S" } }
			}
			""");
			var binds = PackageKeybinds.Read(_dir);
			Assert.IsNotNull(binds);
			Assert.AreEqual("X, J1 B2", binds.AllTrollers["NES Controller"]["P1 A"]);
			Assert.AreEqual("S", binds.AllTrollersAutoFire["NES Controller"]["P1 A"]);
		}

		[TestMethod]
		public void AnalogBindingsSurviveTheirShape()
		{
			// analog binds are objects, not strings; a package that gets this wrong would otherwise
			// silently lose its paddle
			Write("""
			{
				"AllTrollersAnalog": {
					"NES Controller": { "P2 Paddle": { "Value": "WMouse X", "Mult": 1.0, "Deadzone": 0.0 } }
				}
			}
			""");
			var bind = PackageKeybinds.Read(_dir).AllTrollersAnalog["NES Controller"]["P2 Paddle"];
			Assert.AreEqual("WMouse X", bind.Value);
			Assert.AreEqual(1.0f, bind.Mult);
		}

		[TestMethod]
		public void ABrokenFileCostsTheBindings_NotTheCore()
		{
			// a package is a core first and a convenience file second: unreadable JSON here must not
			// throw out of package loading
			Write("{ this is not json");
			Assert.IsNull(PackageKeybinds.Read(_dir));
		}

		[TestMethod]
		public void CommentKeysAreIgnored()
		{
			// the shipped files carry a "_comment" block explaining themselves
			Write("""
			{
				"_comment": [ "why these bindings are what they are" ],
				"AllTrollers": { "Synth Controller": { "P1 A": "X" } }
			}
			""");
			Assert.AreEqual("X", PackageKeybinds.Read(_dir).AllTrollers["Synth Controller"]["P1 A"]);
		}

		[TestMethod]
		public void TheFirstPackageToNameAControllerWins()
		{
			// two NES packages both declare "NES Controller"; whichever loaded first keeps it, so a
			// second core cannot rebind a controller the user is already playing with
			DefaultControls first = new();
			first.AllTrollers["NES Controller"] = new() { ["P1 A"] = "X" };
			DefaultControls second = new();
			second.AllTrollers["NES Controller"] = new() { ["P1 A"] = "Q" };
			second.AllTrollers["Other Controller"] = new() { ["P1 A"] = "W" };

			first.OverlayMissingFrom(second);
			Assert.AreEqual("X", first.AllTrollers["NES Controller"]["P1 A"]);
			Assert.AreEqual("W", first.AllTrollers["Other Controller"]["P1 A"], "but a controller nobody claimed yet is taken");
		}
	}
}
