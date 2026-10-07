using System.Collections.Generic;

using Chimera.Client.Common;

namespace Chimera.Tests.Client.Common
{
	/// <summary>
	/// A core package's default bindings reach the config: a controller never
	/// bound takes them, one still holding what it took follows the package when
	/// its defaults change (issue #157: the Triforce panel's keyboard stick never
	/// reached anyone who had played it before the fix), and one a person changed
	/// keeps the person's bindings.
	/// </summary>
	[TestClass]
	public class ControlDefaultsAdoptionTests
	{
		private const string Panel = "Triforce Panel";

		private static DefaultControls Package(bool keyboardStick)
		{
			DefaultControls d = new();
			d.AllTrollers[Panel] = new() { ["A"] = "Z", ["L"] = "Q", ["R"] = "W" };
			d.AllTrollersAnalog[Panel] = new()
			{
				["Main Stick X"] = new AnalogBind("X1 LeftThumbX Axis", 1.0f, 0.1f, keyboardStick ? "Right" : null, keyboardStick ? "Left" : null),
			};
			return d;
		}

		private static string StickRight(Config config) => config.AllTrollersAnalog[Panel]["Main Stick X"].ButtonBindPositive;

		[TestMethod]
		public void AControllerNeverBoundTakesThePackagesDefaults()
		{
			Config config = new();
			ControlDefaultsAdoption.Apply(config, Package(keyboardStick: true), Panel);
			Assert.AreEqual("Z", config.AllTrollers[Panel]["A"]);
			Assert.AreEqual("Right", StickRight(config));
			Assert.IsTrue(config.AdoptedControlDefaults.ContainsKey(Panel), "and remembers what it took");
		}

		[TestMethod]
		public void UntouchedBindingsFollowThePackagesNewDefaults()
		{
			Config config = new();
			ControlDefaultsAdoption.Apply(config, Package(keyboardStick: false), Panel);
			Assert.IsNull(StickRight(config), "the old package bound no key to the stick");

			ControlDefaultsAdoption.Apply(config, Package(keyboardStick: true), Panel);
			Assert.AreEqual("Right", StickRight(config), "the fixed package's defaults reach a config nobody changed");
		}

		[TestMethod]
		public void BindingsSomebodyChangedStayTheirs()
		{
			Config config = new();
			ControlDefaultsAdoption.Apply(config, Package(keyboardStick: false), Panel);
			config.AllTrollers[Panel]["A"] = "Space";

			ControlDefaultsAdoption.Apply(config, Package(keyboardStick: true), Panel);
			Assert.AreEqual("Space", config.AllTrollers[Panel]["A"], "the person's binding stays");
			Assert.IsNull(StickRight(config), "and nothing else is swapped under them either");
		}

		[TestMethod]
		public void OpeningAndSavingTheDialogIsNotAChange()
		{
			Config config = new();
			ControlDefaultsAdoption.Apply(config, Package(keyboardStick: false), Panel);
			// the dialog writes a row for every control, bound or not
			config.AllTrollers[Panel]["Start"] = "";

			ControlDefaultsAdoption.Apply(config, Package(keyboardStick: true), Panel);
			Assert.AreEqual("Right", StickRight(config));
		}

		[TestMethod]
		public void AConfigFromBeforeTheRecordWaitsForLoadDefaults()
		{
			// what the reporter's config held: the old defaults, taken before anything was recorded
			Config config = new();
			config.AllTrollers[Panel] = new() { ["A"] = "Z", ["L"] = "Q", ["R"] = "W" };
			config.AllTrollersAnalog[Panel] = new() { ["Main Stick X"] = new AnalogBind("X1 LeftThumbX Axis", 1.0f, 0.1f, null, null) };

			ControlDefaultsAdoption.Apply(config, Package(keyboardStick: true), Panel);
			Assert.IsNull(StickRight(config), "nothing says these were never changed, so they stay");
			Assert.IsFalse(config.AdoptedControlDefaults.ContainsKey(Panel));

			// Load Defaults in the controller dialog, then Save
			config.AllTrollersAnalog[Panel] = new(Package(keyboardStick: true).AllTrollersAnalog[Panel]);
			ControlDefaultsAdoption.Apply(config, Package(keyboardStick: true), Panel);
			Assert.IsTrue(config.AdoptedControlDefaults.ContainsKey(Panel), "from then on the config follows the package");
		}
	}
}
