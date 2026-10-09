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
	/// What TAStudio writes at the top of an input column, checked against what
	/// real cores declare.
	///
	/// The letters and headers are the CORES' now (waterbox.config's
	/// input.mnemonics and each axis's header); this frontend has no table of
	/// them, and the engine holds only a rule for a name nobody declared. So
	/// what is checked is the declarations: that a core which declares any
	/// declares them all, usably, and tells one player's controls apart.
	///
	/// They are READ from installed packages rather than copied here, so a core
	/// that grows a button is covered the day it does. Chimera ships no cores,
	/// so what is checked is whatever build/Cores holds - in CI, every core's
	/// newest published package. With none there these are inconclusive rather
	/// than green: a check that silently passes when its subject is missing is
	/// worse than no check. A package from before cores declared these is not
	/// judged: it gets the rule, by decision, until it is rebuilt.
	/// </summary>
	[TestClass]
	public class MnemonicUniquenessTests
	{
		private sealed class Controller
		{
			public string Core = "";
			public string Where = "";
			public List<string> Buttons = new();
			public List<(string Name, string? Header)> Axes = new();
			public Dictionary<string, string>? Declared;
			public Dictionary<string, string> Headers = new();

			/// <summary>What heads the button's column: its header, or its letter.</summary>
			public string? HeaderOf(string button)
			{
				if (Headers.TryGetValue(button, out var whole)) return whole;
				return Headers.TryGetValue(Bare(button), out var bare) ? bare : LetterOf(button);
			}

			/// <summary>The declared letter: by the whole name, then without the player.</summary>
			public string? LetterOf(string button)
			{
				if (Declared is null) return null;
				if (Declared.TryGetValue(button, out var whole)) return whole;
				return Declared.TryGetValue(Bare(button), out var bare) ? bare : null;
			}
		}

		/// <summary>"P2 Start" -> "Start"; "Reset" stays.</summary>
		private static string Bare(string button)
		{
			var space = button.IndexOf(' ');
			return space > 1 && button[0] is 'P' && button.Substring(1, space - 1).All(char.IsDigit) && space + 1 < button.Length
				? button.Substring(space + 1)
				: button;
		}

		private static Controller? Read(string core, string where, JObject? input)
		{
			if (input is null) return null;
			Controller c = new() { Core = core, Where = where };
			foreach (var b in input["buttons"] as JArray ?? new JArray()) c.Buttons.Add(b.Value<string>()!);
			foreach (var a in input["axes"] as JArray ?? new JArray())
				c.Axes.Add((a["name"]?.Value<string>() ?? "", a["header"]?.Value<string>()));
			if (input["mnemonics"] is JObject declared)
				c.Declared = declared.Properties().ToDictionary(static p => p.Name, static p => p.Value.ToString());
			if (input["headers"] is JObject headers)
				c.Headers = headers.Properties().ToDictionary(static p => p.Name, static p => p.Value.ToString());
			return c;
		}

		/// <summary>Every controller the installed packages declare: each package's own, and each machine's.</summary>
		private static IReadOnlyList<Controller> Controllers()
		{
			var found = new List<Controller>();
			foreach (var package in Chimera.Tests.Client.Common.CorePackages.InstalledPackages.Files)
			{
				var text = Chimera.Tests.Client.Common.CorePackages.InstalledPackages.ConfigOf(package);
				if (text is null) continue;

				JObject root;
				try { root = JObject.Parse(text); }
				catch { continue; }   // a core mid-edit is not this test's business

				var name = Chimera.Tests.Client.Common.CorePackages.InstalledPackages.NameOf(package);
				if (Read(name, "the package", root["input"] as JObject) is { } own) found.Add(own);
				foreach (var machine in root["machines"] as JArray ?? new JArray())
				{
					if (Read(name, machine["label"]?.Value<string>() ?? machine["id"]?.Value<string>() ?? "a machine", machine["input"] as JObject) is { } theirs)
						found.Add(theirs);
				}
			}
			return found;
		}

		/// <summary>The controllers whose cores declare their letters.</summary>
		private static IReadOnlyList<Controller> Declaring()
		{
			var all = Controllers();
			if (all.Count is 0) Assert.Inconclusive("no core packages in build/Cores (see tools/fetch-cores.sh)");
			var declaring = all.Where(static c => c.Declared is not null || c.Axes.Any(static a => a.Header is not null)).ToList();
			if (declaring.Count is 0) Assert.Inconclusive("no installed package declares its mnemonics yet");
			return declaring;
		}

		/// <summary>
		/// A core that declares letters declares one for every button: a button
		/// left out gets the rule's guess, which is how two columns of one pad
		/// come to share a letter with nobody having decided it.
		/// </summary>
		[TestMethod]
		public void ACoreThatDeclaresLettersDeclaresThemAll()
		{
			var missing = Declaring()
				.Where(static c => c.Buttons.Count is not 0)
				.SelectMany(c => c.Buttons.Where(b => c.LetterOf(b) is null).Select(b => $"{c.Core} ({c.Where}): {b}"))
				.ToList();
			Assert.AreEqual(0, missing.Count, string.Join("; ", missing.Take(40)));
		}

		/// <summary>
		/// A letter is one character an entry can carry: printable ASCII, and
		/// neither '.' (a button not pressed) nor '|' (which parts an entry's
		/// groups). The engine refuses anything else and answers with the rule,
		/// so a bad letter is a silent one.
		/// </summary>
		[TestMethod]
		public void EveryDeclaredLetterCanBeWritten()
		{
			var bad = Declaring()
				.Where(static c => c.Declared is not null)
				.SelectMany(c => c.Declared!.Where(static pair => pair.Value.Length is not 1 || pair.Value[0] <= ' ' || pair.Value[0] >= 0x7F || pair.Value[0] is '.' or '|')
					.Select(pair => $"{c.Core} ({c.Where}): {pair.Key} = \"{pair.Value}\""))
				.ToList();
			Assert.AreEqual(0, bad.Count, string.Join("; ", bad.Take(40)));
		}

		/// <summary>
		/// What is declared is what the engine answers: the letters reach a
		/// window through ce_session_mnemonic_of, and this asks the same rule the
		/// engine falls back on for a name with no declaration, to pin that a
		/// declared letter is never the rule's by accident of a typo in the key.
		/// </summary>
		[TestMethod]
		public void NoDeclarationNamesAControlThatIsNotThere()
		{
			var stray = Declaring()
				.Where(static c => c.Declared is not null)
				.SelectMany(c =>
				{
					var names = new HashSet<string>(c.Buttons.Concat(c.Buttons.Select(Bare)));
					return c.Declared!.Keys.Where(k => !names.Contains(k)).Select(k => $"{c.Core} ({c.Where}): {k}");
				})
				.ToList();
			Assert.AreEqual(0, stray.Count, string.Join("; ", stray.Take(40)));
		}

		/// <summary>
		/// An axis header is as wide as its text and the input roll is as wide as
		/// its columns, so a full name like "Right Stick X" costs real screen
		/// four times over. Five characters is enough for P1GSX, which is the
		/// longest any shipped controller needs.
		/// </summary>
		[TestMethod]
		public void AxisHeadersAreDeclaredAndShort()
		{
			const int LIMIT = 5;
			var wrong = Declaring()
				.SelectMany(c => c.Axes.Select(a => (c.Core, c.Where, a.Name, a.Header)))
				.Where(static x => x.Header is null || x.Header.Length is 0 || x.Header.Length > LIMIT)
				.Select(static x => $"{x.Core} ({x.Where}): {x.Name} -> {(x.Header is null ? "no header" : $"\"{x.Header}\"")}")
				.ToList();

			Assert.AreEqual(0, wrong.Count, string.Join("; ", wrong.Take(40)));
		}

		/// <summary>
		/// Two controls of one machine may not share a mnemonic - it is both the
		/// character a log is written with and the header of the control's
		/// column, so a collision is an ambiguous log AND two columns nobody can
		/// tell apart. That is what happened to the PlayStation 2: L2 had the 'L'
		/// of Left and R2 the 'R' of Right.
		///
		/// Held per PLAYER, not per controller: a declaration by the bare name
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
			foreach (var c in Declaring().Where(static c => c.Declared is not null))
			{
				foreach (var group in c.Buttons.GroupBy(PlayerOf))
				{
					if (group.Count() > GAMEPAD_SIZED) continue;

					var byChar = new Dictionary<string, string>();
					foreach (var button in group)
					{
						// (an undeclared button is the other test's complaint)
						if (c.LetterOf(button) is not { } ch) continue;
						if (byChar.TryGetValue(ch, out var first))
							complaints.Add($"{c.Core} ({c.Where}) {group.Key}: '{ch}' is both {first} and {button}");
						else
							byChar[ch] = button;
					}
				}
			}

			Assert.AreEqual(0, complaints.Count, string.Join("; ", complaints.Take(40)));
		}

		/// <summary>
		/// A header is what a package gives a button whose letter does not tell
		/// it from its neighbours (chimera#225): one to eight characters of
		/// printable ASCII with no space at either end, which is what the engine
		/// takes - anything else it drops without a word and the column is
		/// headed by the letter again. It names a control that is there, and it
		/// heads one column of a player: a header that another column of the
		/// same player is also headed by has told nothing apart.
		/// </summary>
		[TestMethod]
		public void ADeclaredHeaderIsUsableAndHeadsOneColumn()
		{
			var complaints = new List<string>();
			foreach (var c in Controllers().Where(static c => c.Headers.Count is not 0))
			{
				var names = new HashSet<string>(c.Buttons.Concat(c.Buttons.Select(Bare)));
				foreach (var pair in c.Headers)
				{
					var h = pair.Value;
					if (h.Length is 0 or > 8 || h[0] is ' ' || h[h.Length - 1] is ' ' || h.Any(static ch => ch < ' ' || ch >= 0x7F))
						complaints.Add($"{c.Core} ({c.Where}): {pair.Key} = \"{h}\" is not a header");
					if (!names.Contains(pair.Key))
						complaints.Add($"{c.Core} ({c.Where}): a header for {pair.Key}, which is not a button");
				}

				foreach (var group in c.Buttons.GroupBy(PlayerOf))
				{
					var headed = group.Where(b => c.Headers.ContainsKey(b) || c.Headers.ContainsKey(Bare(b))).ToList();
					foreach (var button in headed)
					{
						var twin = group.FirstOrDefault(other => other != button && c.HeaderOf(other) == c.HeaderOf(button));
						if (twin is not null)
							complaints.Add($"{c.Core} ({c.Where}) {group.Key}: \"{c.HeaderOf(button)}\" heads both {button} and {twin}");
					}
				}
			}

			Assert.AreEqual(0, complaints.Count, string.Join("; ", complaints.Take(40)));
		}

		/// <summary>"P2 Start" belongs to player 2; "Reset" belongs to the console.</summary>
		private static string PlayerOf(string button)
			=> Bare(button) == button ? "console" : button.Substring(0, button.IndexOf(' '));

		/// <summary>
		/// The rule for a name nobody declared, as this frontend sees it through
		/// the engine: the same answers the engine's own test pins, asked across
		/// the boundary a window asks across.
		/// </summary>
		[TestMethod]
		public void TheRuleForAnUndeclaredName()
		{
			Assert.AreEqual('U', GenericControlNames.Instance.MnemonicOf("P1 Up"));
			Assert.AreEqual('F', GenericControlNames.Instance.MnemonicOf("Stick Fire"));
			Assert.AreEqual('2', GenericControlNames.Instance.MnemonicOf("Insert Disk 2"));
			Assert.AreEqual("P1LSX", GenericControlNames.Instance.AxisHeaderOf("P1 Left Stick X"));
			Assert.AreEqual("LT", GenericControlNames.Instance.AxisHeaderOf("Left Trigger"));
			// a button nobody gave a header is headed by its letter
			Assert.AreEqual("U", GenericControlNames.Instance.ButtonHeaderOf("P1 Up"));
		}

		/// <summary>
		/// A definition is called what it is told its controls are called, and a
		/// definition made from a movie's key takes the names of the machine it
		/// is shown beside - including for a control that machine does not have.
		/// </summary>
		[TestMethod]
		public void ADefinitionUsesTheNamesItIsGiven()
		{
			FixedControlNames names = new(
				new Dictionary<string, char> { ["P1 Cross"] = 'X', ["P2 Cross"] = 'X', ["P1 Up"] = 'U' },
				new Dictionary<string, string> { ["P1 Left Stick X"] = "LX" },
				new Dictionary<string, string> { ["P1 Up"] = "UP" });
			var machine = new ControllerDefinition("pad") { BoolButtons = { "P1 Cross", "P1 Up" } }
				.WithControlNames(names)
				.MakeImmutable();
			machine.BuildMnemonicsCache();
			Assert.AreEqual('X', machine.MnemonicFor("P1 Cross"));
			Assert.AreEqual("LX", machine.AxisHeaderFor("P1 Left Stick X"));
			// a header is the column's and not the movie's: the letter stays
			Assert.AreEqual("UP", machine.ButtonHeaderFor("P1 Up"));
			Assert.AreEqual('U', machine.MnemonicFor("P1 Up"));
			Assert.AreEqual("X", machine.ButtonHeaderFor("P1 Cross"));
			// not in the cache, still the machine's word
			Assert.AreEqual('X', machine.MnemonicFor("P2 Cross"));
			// and one nobody named at all: the rule
			Assert.AreEqual('C', machine.MnemonicFor("P2 Circle"));

			var fromAKey = new ControllerDefinition("movie") { BoolButtons = { "P1 Cross", "P2 Cross" } }.MakeImmutable();
			fromAKey.BuildMnemonicsCache(machine.ControlNames);
			Assert.AreEqual('X', fromAKey.MnemonicFor("P2 Cross"));
			// a copy keeps them
			Assert.AreEqual('X', new ControllerDefinition(machine).MnemonicFor("P1 Cross"));
		}
	}
}
