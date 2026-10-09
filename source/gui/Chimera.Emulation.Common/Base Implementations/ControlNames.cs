#nullable enable

using System.Collections.Generic;

using Chimera.Emulation.Common.Engine;

namespace Chimera.Emulation.Common
{
	/// <summary>
	/// What a controller's controls are called where a person reads very little
	/// of them: the one character a pressed button writes into a movie's text,
	/// what heads its input column, and the short header of an axis's column.
	///
	/// This frontend used to keep those itself, in tables keyed by system. It
	/// keeps none now: a core's package declares them beside the controls they
	/// name, the engine reads the declaration (and holds the one rule for a
	/// name nobody declared), and all a window does is ask. Whoever makes a
	/// <see cref="ControllerDefinition"/> for a running machine hands it the
	/// machine's own answers; a definition nobody gave any gets the rule.
	///
	/// None of it is what a movie means: an entry is read by position, and any
	/// character but '.' is a pressed button.
	/// </summary>
	public interface IControlNames
	{
		/// <summary>The letter of a button. Any name may be asked, not only this controller's own.</summary>
		char MnemonicOf(string button);

		/// <summary>
		/// What heads a button's column and fills its pressed cells: the header
		/// its package declared, or its letter. Never written into a movie.
		/// </summary>
		string ButtonHeaderOf(string button);

		/// <summary>The header of an axis's column.</summary>
		string AxisHeaderOf(string axis);
	}

	/// <summary>The engine's rule and nothing else: for when there is no machine to ask.</summary>
	public sealed class GenericControlNames : IControlNames
	{
		public static readonly GenericControlNames Instance = new();

		private GenericControlNames() {}

		public char MnemonicOf(string button) => ChimeraEngine.ControlMnemonic(button);

		public string ButtonHeaderOf(string button) => ChimeraEngine.ControlButtonHeader(button);

		public string AxisHeaderOf(string axis) => ChimeraEngine.ControlAxisHeader(axis);
	}

	/// <summary>
	/// Answers given outright, with the rule behind them: a machine's names kept
	/// after the machine is gone, or the names a test wants.
	/// </summary>
	public sealed class FixedControlNames : IControlNames
	{
		private readonly IReadOnlyDictionary<string, char> _mnemonics;

		private readonly IReadOnlyDictionary<string, string> _axisHeaders;

		private readonly IReadOnlyDictionary<string, string> _buttonHeaders;

		public FixedControlNames(
			IReadOnlyDictionary<string, char>? mnemonics = null,
			IReadOnlyDictionary<string, string>? axisHeaders = null,
			IReadOnlyDictionary<string, string>? buttonHeaders = null)
		{
			_mnemonics = mnemonics ?? new Dictionary<string, char>();
			_axisHeaders = axisHeaders ?? new Dictionary<string, string>();
			_buttonHeaders = buttonHeaders ?? new Dictionary<string, string>();
		}

		public string ButtonHeaderOf(string button)
			=> _buttonHeaders.TryGetValue(button, out var s) ? s : MnemonicOf(button).ToString();

		public char MnemonicOf(string button)
			=> _mnemonics.TryGetValue(button, out var c) ? c : GenericControlNames.Instance.MnemonicOf(button);

		public string AxisHeaderOf(string axis)
			=> _axisHeaders.TryGetValue(axis, out var s) ? s : GenericControlNames.Instance.AxisHeaderOf(axis);
	}
}
