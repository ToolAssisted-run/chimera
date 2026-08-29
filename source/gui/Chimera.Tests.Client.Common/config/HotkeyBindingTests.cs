#nullable disable

using System.Linq;

using Chimera.Client.Common;

namespace Chimera.Tests.Client.Common
{
	/// <summary>
	/// The Hotkeys window builds itself from <see cref="HotkeyInfo.AllHotkeys"/>:
	/// a tab per group, a row per entry, each row a rebindable widget. So a
	/// binding that is in this dictionary, in a group the window shows, IS
	/// remappable, and that is what these check - the window itself needs the
	/// host input layer running and cannot be constructed here.
	/// </summary>
	[TestClass]
	public class HotkeyBindingTests
	{
		[TestMethod]
		public void TheNumberedBranchSlotsAreBoundAndRemappable()
		{
			for (int slot = 1; slot <= 10; slot++)
			{
				foreach (var (name, expected) in new[]
					{
						($"Save Branch {slot}", $"Shift+F{slot}"),
						($"Load Branch {slot}", $"F{slot}"),
					})
				{
					Assert.IsTrue(HotkeyInfo.AllHotkeys.TryGetValue(name, out var info), $"{name} is not a hotkey");
					Assert.AreEqual("TAStudio", info.TabGroup, $"{name} is in the wrong tab");
					Assert.AreEqual(expected, info.DefaultBinding, $"{name} has the wrong default");
					Assert.IsTrue(HotkeyInfo.Groupings.Contains(info.TabGroup),
						$"{name} is in a group the Hotkeys window does not show");
				}
			}
		}

		/// <summary>
		/// No two hotkeys may default to the same combination - the release build
		/// has no assert for this, and a clash means one of the two silently
		/// never fires.
		/// </summary>
		[TestMethod]
		public void NoTwoDefaultsCollide()
		{
			var clashes = HotkeyInfo.AllHotkeys.Values
				.Where(static i => !string.IsNullOrEmpty(i.DefaultBinding))
				// analog hotkeys are exempt by design: nothing else fires while
				// TAStudio is in analog editing mode
				.Where(static i => !(i.TabGroup is "TAStudio" && i.DisplayName.StartsWith("Analog ")))
				.GroupBy(static i => i.DefaultBinding)
				.Where(static g => g.Count() > 1)
				.Select(static g => $"{g.Key} = {string.Join(" / ", g.Select(static i => i.DisplayName))}")
				.ToList();

			Assert.AreEqual(0, clashes.Count, string.Join("; ", clashes));
		}
	}
}
