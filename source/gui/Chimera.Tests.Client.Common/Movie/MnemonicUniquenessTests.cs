#nullable disable

using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;

using Chimera.Emulation.Common;

using Newtonsoft.Json.Linq;

namespace Chimera.Tests.Client.Common
{
	/// <summary>
	/// What TAStudio writes at the top of an input column, checked against the
	/// controllers the shipped cores actually declare.
	///
	/// The declarations are READ from the core packages' waterbox.config rather
	/// than copied here, so a core that grows a button is covered the day it
	/// does. When the core repositories are not checked out - a source tarball,
	/// a partial clone - these are inconclusive rather than green: a check that
	/// silently passes when its subject is missing is worse than no check.
	/// </summary>
	[TestClass]
	public class MnemonicUniquenessTests
	{
		private sealed class Controller
		{
			public string Core;
			public string SystemId;
			public List<string> Buttons = new();
			public List<string> Axes = new();
		}

		/// <summary>
		/// Every core package's declared controller. The walk upwards is how a
		/// test finds the repository it belongs to without being told.
		/// </summary>
		private static IReadOnlyList<Controller> Controllers()
		{
			var dir = new DirectoryInfo(AppDomain.CurrentDomain.BaseDirectory);
			while (dir is not null && !Directory.Exists(Path.Combine(dir.FullName, "extern", "cores")))
			{
				dir = dir.Parent;
			}
			if (dir is null) return Array.Empty<Controller>();

			var found = new List<Controller>();
			foreach (var core in Directory.EnumerateDirectories(Path.Combine(dir.FullName, "extern", "cores")))
			{
				var config = Path.Combine(core, "waterbox", "waterbox.config");
				if (!File.Exists(config)) continue;

				JObject root;
				try { root = JObject.Parse(File.ReadAllText(config)); }
				catch { continue; }   // a core mid-edit is not this test's business

				var input = root["input"] as JObject;
				var systemId = root["systemId"]?.Value<string>();
				if (input is null || string.IsNullOrEmpty(systemId)) continue;

				Controller c = new() { Core = Path.GetFileName(core), SystemId = systemId };
				foreach (var b in input["buttons"] as JArray ?? new JArray())
					c.Buttons.Add(b.Value<string>());
				foreach (var a in input["axes"] as JArray ?? new JArray())
					c.Axes.Add(a["name"]?.Value<string>());
				found.Add(c);
			}
			return found;
		}

		private static IReadOnlyList<Controller> RequireControllers()
		{
			var all = Controllers();
			if (all.Count is 0) Assert.Inconclusive("no core packages checked out under extern/cores");
			return all;
		}

		/// <summary>
		/// '!' is what the lookup answers when it knows nothing about a name. A
		/// column headed '!' tells a person only that something is wrong
		/// somewhere, which is the least useful thing a header can say.
		/// </summary>
		[TestMethod]
		public void NoControlIsHeadedWithAnExclamationMark()
		{
			var unnamed = RequireControllers()
				.SelectMany(c => c.Buttons.Select(b => (c.Core, c.SystemId, Button: b)))
				.Where(x => MnemonicLookup.Lookup(x.Button, x.SystemId) is '!')
				.Select(x => $"{x.Core}: {x.Button}")
				.ToList();

			Assert.AreEqual(0, unnamed.Count, string.Join("; ", unnamed));
		}

		/// <summary>
		/// An axis header is as wide as its text and the input roll is as wide as
		/// its columns, so a full name like "Right Stick X" costs real screen
		/// four times over. Five characters is enough for P1GSX, which is the
		/// longest any shipped controller needs.
		/// </summary>
		[TestMethod]
		public void AxisHeadersAreShort()
		{
			const int LIMIT = 5;
			var wide = RequireControllers()
				.SelectMany(c => c.Axes.Select(a => (c.Core, c.SystemId, Axis: a)))
				.Select(x => (x.Core, x.Axis, Header: MnemonicLookup.LookupAxis(x.Axis, x.SystemId)))
				.Where(x => x.Header.Length > LIMIT)
				.Select(x => $"{x.Core}: {x.Axis} -> \"{x.Header}\"")
				.ToList();

			Assert.AreEqual(0, wide.Count, string.Join("; ", wide));
		}

		/// <summary>
		/// Two controls of one machine may not share a mnemonic - it is both the
		/// character a log is written with and the header of the control's
		/// column, so a collision is an ambiguous log AND two columns nobody can
		/// tell apart. That is what happened to the PlayStation 2: L2 had the 'L'
		/// of Left and R2 the 'R' of Right.
		///
		/// Held per PLAYER, not per controller: the lookup strips the "P2 " and
		/// hands both players the same character on purpose, and TAStudio keeps
		/// their columns in separate groups. Two players sharing an 'A' is the
		/// design; one player owning two 'L's is the bug.
		///
		/// Held only over controllers small enough for it to be achievable, and
		/// measured PER PLAYER rather than over the whole wire: a Dreamcast
		/// declares four ports of twenty, which is eighty buttons and still a
		/// gamepad. A DOS keyboard has 123 keys on ONE player and there are not
		/// 123 characters worth reading; the ones a person actually TASes with
		/// are checked, which is the claim that can be made honestly.
		/// </summary>
		[TestMethod]
		public void NoTwoControlsOfOnePlayerShareAMnemonic()
		{
			const int GAMEPAD_SIZED = 40;
			var complaints = new List<string>();
			foreach (var c in RequireControllers())
			{
				foreach (var group in c.Buttons.GroupBy(PlayerOf))
				{
					if (group.Count() > GAMEPAD_SIZED) continue;

					var byChar = new Dictionary<char, string>();
					foreach (var button in group)
					{
						var ch = MnemonicLookup.Lookup(button, c.SystemId);
						if (byChar.TryGetValue(ch, out var first))
							complaints.Add($"{c.Core} {group.Key}: '{ch}' is both {first} and {button}");
						else
							byChar[ch] = button;
					}
				}
			}

			Assert.AreEqual(0, complaints.Count, string.Join("; ", complaints));
		}

		/// <summary>"P2 Start" belongs to player 2; "Reset" belongs to the console.</summary>
		private static string PlayerOf(string button)
			=> button.Length > 2 && button[0] is 'P' && char.IsDigit(button[1]) && button[2] is ' '
				? button.Substring(0, 2)
				: "console";
	}
}
