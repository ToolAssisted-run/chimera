#nullable enable

using System.Windows.Forms;

using Chimera.Client.Common;

namespace Chimera.Client.GUI
{
	/// <summary>
	/// "Show: All / Emulators / Games" above a list of cores, or, without All, the new-project
	/// wizard's "Kind: Emulator / Game" (docs/game-cores.md). The lists used to set the game cores
	/// apart with divider rows, which Mono's ListView forced on them (it ignores groups in Details
	/// view) and which read as disabled entries; each is now one list with a Type column and this
	/// above it (user-decided, 2026-09-29).
	/// </summary>
	public sealed class CoreKindFilterBox : FlowLayoutPanel
	{
		private readonly RadioButton? _all;
		private readonly RadioButton _emulators;
		private readonly RadioButton _games;
		private bool _setting;

		/// <summary>A person chose another kind; not raised when <see cref="Value"/> is set.</summary>
		public event Action? Changed;

		/// <param name="caption">what the row is headed ("Show:"), or null for a form that labels its rows itself</param>
		/// <param name="offerAll">a filter offers All, and names the kinds in the plural; a choice does not</param>
		public CoreKindFilterBox(string? caption, bool offerAll)
		{
			AutoSize = true;
			AutoSizeMode = AutoSizeMode.GrowAndShrink;
			FlowDirection = FlowDirection.LeftToRight;
			WrapContents = false;
			Margin = Padding.Empty;
			Padding = Padding.Empty;
			Label? label = caption is null ? null : new()
			{
				AutoSize = true,
				Margin = new(0, 0, UIHelper.ScaleX(6), 0),
				Text = caption,
			};
			if (label is not null) Controls.Add(label);
			if (offerAll) _all = Make("All");
			_emulators = Make(offerAll ? "Emulators" : "Emulator");
			_games = Make(offerAll ? "Games" : "Game");
			// the caption on the buttons' line: centred on their height, which the toolkit decides
			if (label is not null) label.Margin = label.Margin with { Top = Math.Max(0, (_games.PreferredSize.Height - label.PreferredSize.Height) / 2) };
			Value = offerAll ? CoreKindFilter.All : CoreKindFilter.Emulators;
		}

		private RadioButton Make(string text)
		{
			RadioButton button = new()
			{
				AutoSize = true,
				Margin = new(0, 0, UIHelper.ScaleX(12), 0),
				Text = text,
			};
			button.CheckedChanged += (_, _) =>
			{
				if (button.Checked && !_setting) Changed?.Invoke();
			};
			Controls.Add(button);
			return button;
		}

		public CoreKindFilter Value
		{
			get => _games.Checked ? CoreKindFilter.Games : _emulators.Checked ? CoreKindFilter.Emulators : CoreKindFilter.All;
			set
			{
				_setting = true;
				(value switch
				{
					CoreKindFilter.Games => _games,
					CoreKindFilter.Emulators => _emulators,
					_ => _all ?? _emulators,
				}).Checked = true;
				_setting = false;
			}
		}

		/// <summary>Which kinds can be chosen: the wizard greys one that no installed core is.</summary>
		public void Offer(bool emulators, bool games)
		{
			_emulators.Enabled = emulators;
			_games.Enabled = games;
		}

		/// <summary>Chooses a kind as a person would, raising <see cref="Changed"/>. For tests.</summary>
		public void ChooseForTest(CoreKindFilter kind)
			=> (kind switch
			{
				CoreKindFilter.Games => _games,
				CoreKindFilter.Emulators => _emulators,
				_ => _all ?? _emulators,
			}).Checked = true;
	}
}
