#nullable enable

using System;
using System.Collections.Generic;
using System.Drawing;
using System.IO;
using System.Linq;
using System.Windows.Forms;

using Chimera.Client.Common;
using Chimera.Common;
using Chimera.Emulation.Common.Engine;
using Chimera.Emulation.Common;
using Chimera.Emulation.Common.Waterbox;

namespace Chimera.Client.GUI
{
	/// <summary>
	/// Where a chimera project is born: everything defined at once, up front
	/// (docs/project.md). Step one is the core and nothing else - the first
	/// question is which machine, because every question after it belongs to
	/// that machine. Step two is the core-informed file form - the slots,
	/// cardinalities, formats and tooltips all come from the chosen package's
	/// file_slots.json, this window only renders and enforces; step three is
	/// the settings; step four the firmware those choices call for. On Create,
	/// every file is hashed where it stands and the manifest is validated by
	/// the engine against the core's own declaration - and the project stays
	/// IN MEMORY. Nothing is written until the work is saved, so starting a
	/// project costs no decision about where a file goes.
	/// </summary>
	public sealed class NewProjectWizard : FormBase
	{
		private readonly IReadOnlyList<DiscoveredCorePackage> _cores;
		private readonly Func<ProjectSlotDeclaration.Slot, string[]> _pickFiles;

		private readonly Panel[] _pages = new Panel[4];
		private int _page;

		// page 1
		private readonly ComboBox _core;

		/// <summary>
		/// Which machine, for a package that is several. Genesis Plus GX is one core
		/// that is a Mega Drive, a Master System, a Game Gear or an SG-1000, and the
		/// choice belongs here beside the core: everything after it - the files it
		/// takes, the settings it has, the firmware it wants - depends on it.
		/// </summary>
		private readonly ComboBox _machine;

		private readonly Label _machineLabel;

		/// <summary>
		/// Which renderer draws, for a core that offers a choice. It sits beside the
		/// core rather than among the settings because it is not a property of the
		/// machine: the same machine, drawn twice. A core with one renderer shows it
		/// greyed, so the question is always answered in the same place.
		/// </summary>
		private readonly ComboBox _renderer;

		/// <summary>what a hardware renderer costs, shown only when one is chosen</summary>
		private readonly Label _rendererCaveat;

		// page 2, rebuilt when the chosen core changes
		private readonly Panel _slotsHost;
		private ProjectSlotDeclaration? _declaration;
		private string? _declarationCore;

		/// <summary>the package whose declaration <see cref="_cfg"/> holds</summary>
		private string? _cfgCore;
		private readonly Dictionary<string, ListBox> _slotLists = new();
		private readonly Dictionary<string, GroupBox> _slotGroups = new();
		private readonly ToolTip _tips = new();

		// page 3
		private readonly PropertyGrid _settingsGrid;
		private WaterboxCoreSettings? _settings;
		private WaterboxConfig? _cfg;

		// page 4, computed from every earlier decision (docs/project.md): the
		// decisions nail each requirement to ONE exact file or to nothing -
		// the hash is the identity, the name only a hint, and the Firmware
		// folder answers before the user does
		private readonly Func<string, string?> _pickFirmwareFile;
		private readonly IReadOnlyList<string> _firmwareSearchDirs;
		private readonly Func<string, string, string?> _rememberedFirmwarePath;
		private readonly ListView _firmwareList;
		private readonly Button _firmwareSetButton;
		private readonly Button _firmwareClearButton;
		private List<FirmwareNeed> _firmwareNeeds = new();
		private IReadOnlyList<FirmwareLocator.IndexedFile> _firmwareIndex = [ ];

		private sealed class FirmwareNeed
		{
			public string Id = "";
			public CoreFirmwareDecl? Decl;
			public string? ChosenPath; // for a pinned entry, set only when the hash matched exactly
			public string? ChosenSha1; // the actual hash - equals the pin, or names an unpinned choice

			public bool Satisfied => ChosenPath is not null;
		}

		private readonly Button _backButton;
		private readonly Button _nextButton;
		private readonly Label _status;

		/// <summary>
		/// Set when Create succeeded: the project, in memory and unwritten. The
		/// caller owns it from that moment (it boots the machine from it, and
		/// disposes it when the next project replaces it).
		/// </summary>
		public EngineProject? CreatedProject { get; private set; }

		/// <summary>the chosen core's name, for the owner's firmware bookkeeping</summary>
		public string? ChosenCoreName => ChosenCore?.Name;

		protected override string WindowTitleStatic => "New Chimera Project";

		public NewProjectWizard(
			IReadOnlyList<DiscoveredCorePackage> cores,
			Func<ProjectSlotDeclaration.Slot, string[]> pickFiles,
			Func<string, string?>? pickFirmwareFile = null,
			IReadOnlyList<string>? firmwareSearchDirs = null,
			Func<string, string, string?>? rememberedFirmwarePath = null)
		{
			_pickFirmwareFile = pickFirmwareFile ?? (static _ => null);
			_firmwareSearchDirs = firmwareSearchDirs ?? [ ];
			_rememberedFirmwarePath = rememberedFirmwarePath ?? (static (_, _) => null);
			_cores = cores.Where(static c => c.Error is null).ToList();
			_pickFiles = pickFiles;

			SuspendLayout();
			ClientSize = new(UIHelper.ScaleX(560), UIHelper.ScaleY(420));
			MinimumSize = new(UIHelper.ScaleX(480), UIHelper.ScaleY(340));
			StartPosition = FormStartPosition.CenterParent;
			ShowIcon = false;

			for (var i = 0; i < _pages.Length; i++)
			{
				_pages[i] = new Panel
				{
					Anchor = AnchorStyles.Top | AnchorStyles.Bottom | AnchorStyles.Left | AnchorStyles.Right,
					Location = new(0, 0),
					Size = new(UIHelper.ScaleX(560), UIHelper.ScaleY(380)),
					Visible = i is 0,
				};
				Controls.Add(_pages[i]);
			}

			// ---- page 1: the machine ---------------------------------------------
			var p1 = _pages[0];
			p1.Controls.Add(MakeHeading("Please select the emulation core."));
			p1.Controls.Add(MakeLabel("Emulation core:", 8, 52));
			_core = new ComboBox
			{
				Anchor = AnchorStyles.Top | AnchorStyles.Left | AnchorStyles.Right,
				DropDownStyle = ComboBoxStyle.DropDownList,
				Location = Pt(110, 48),
				Width = UIHelper.ScaleX(442),
			};
			foreach (var core in _cores)
			{
				_core.Items.Add($"{core.Name}  ({SystemNames.Of(core.Systems)}{(string.IsNullOrEmpty(core.Version) ? "" : $", {core.Version}")})");
			}
			p1.Controls.Add(_core);

			p1.Controls.Add(MakeLabel("Renderer:", 8, 84));
			_renderer = new ComboBox
			{
				Anchor = AnchorStyles.Top | AnchorStyles.Left | AnchorStyles.Right,
				DropDownStyle = ComboBoxStyle.DropDownList,
				Location = Pt(110, 80),
				Width = UIHelper.ScaleX(442),
			};
			_renderer.SelectedIndexChanged += (_, _) =>
			{
				PinRenderer();
				ShowRendererCaveat();
			};
			p1.Controls.Add(_renderer);

			// What a hardware renderer costs, where the choice is made. The space
			// is reserved whether or not the text is showing, so choosing between
			// renderers does not make the form jump around underneath the hand
			// that is choosing.
			_rendererCaveat = new Label
			{
				Anchor = AnchorStyles.Top | AnchorStyles.Left | AnchorStyles.Right,
				AutoSize = false,
				ForeColor = SystemColors.GrayText,
				Location = Pt(110, 106),
				Size = new(UIHelper.ScaleX(442), UIHelper.ScaleY(46)),
			};
			p1.Controls.Add(_rendererCaveat);

			_machineLabel = MakeLabel("Machine:", 8, 160);
			_machine = new ComboBox
			{
				Anchor = AnchorStyles.Top | AnchorStyles.Left | AnchorStyles.Right,
				DropDownStyle = ComboBoxStyle.DropDownList,
				Location = Pt(110, 156),
				Width = UIHelper.ScaleX(442),
			};
			_machine.SelectedIndexChanged += (_, _) =>
			{
				PinMachine();
				// a package that is several machines may offer different renderers
				RefreshRendererChoices();
			};
			p1.Controls.AddRange([ _machineLabel, _machine ]);
			p1.Controls.Add(MakeIssuesNotice());
			_core.SelectedIndexChanged += (_, _) => LoadChosenPackage();
			if (_core.Items.Count is not 0) _core.SelectedIndex = 0;
			LoadChosenPackage();

			// ---- page 2: the core-informed file form -----------------------------
			var p2 = _pages[1];
			p2.Controls.Add(MakeHeading("Please provide the game ROMs and other persistent data."));
			_slotsHost = new Panel
			{
				Anchor = AnchorStyles.Top | AnchorStyles.Bottom | AnchorStyles.Left | AnchorStyles.Right,
				AutoScroll = true,
				Location = Pt(8, 48),
				Size = new(UIHelper.ScaleX(544), UIHelper.ScaleY(324)),
			};
			p2.Controls.Add(_slotsHost);

			// ---- page 3: settings ------------------------------------------------
			var p3 = _pages[2];
			p3.Controls.Add(MakeHeading("Please specify the emulation configuration settings."));
			_settingsGrid = new PropertyGrid
			{
				Anchor = AnchorStyles.Top | AnchorStyles.Bottom | AnchorStyles.Left | AnchorStyles.Right,
				Location = Pt(8, 48),
				Size = new(UIHelper.ScaleX(544), UIHelper.ScaleY(324)),
				ToolbarVisible = false,
				PropertySort = PropertySort.Alphabetical,
			};
			_settingsGrid.PropertyValueChanged += (_, _) =>
			{
				RefreshExposedSettings();
				UpdateNavLabels();
			};
			p3.Controls.Add(_settingsGrid);

			// ---- page 4: firmware, decided by everything chosen above ------------
			var p4 = _pages[3];
			p4.Controls.Add(MakeHeading("Please indicate where to find the required firmware files."));
			_firmwareList = new ListView
			{
				Anchor = AnchorStyles.Top | AnchorStyles.Bottom | AnchorStyles.Left | AnchorStyles.Right,
				FullRowSelect = true,
				HideSelection = false,
				Location = Pt(8, 56),
				MultiSelect = false,
				ShowItemToolTips = true,
				Size = new(UIHelper.ScaleX(544), UIHelper.ScaleY(282)),
				View = View.Details,
			};
			_firmwareList.Columns.Add("Firmware", UIHelper.ScaleX(150));
			_firmwareList.Columns.Add("Expected file", UIHelper.ScaleX(120));
			_firmwareList.Columns.Add("SHA1", UIHelper.ScaleX(110));
			_firmwareList.Columns.Add("Status", UIHelper.ScaleX(160));
			_firmwareList.SelectedIndexChanged += (_, _) => UpdateFirmwareButtons();
			_firmwareList.DoubleClick += (_, _) => SetFirmwareFile();
			_firmwareSetButton = new Button { AutoSize = true, Location = Pt(8, 344), Text = "Select File..." };
			_firmwareSetButton.Click += (_, _) => SetFirmwareFile();
			_firmwareClearButton = new Button { AutoSize = true, Location = Pt(110, 344), Text = "Clear" };
			_firmwareClearButton.Click += (_, _) => ClearFirmwareFile();
			p4.Controls.AddRange([ _firmwareList, _firmwareSetButton, _firmwareClearButton ]);

			// ---- chrome ----------------------------------------------------------
			// Width is set below, once the buttons have been placed: a fixed one
			// reached under the leftmost of them and painted over it.
			_status = new Label
			{
				Anchor = AnchorStyles.Bottom | AnchorStyles.Left,
				AutoEllipsis = true,
				ForeColor = Color.Firebrick,
				Location = Pt(8, 386),
				Size = new(UIHelper.ScaleX(360), UIHelper.ScaleY(16)),
			};
			_backButton = MakeNavButton("< Back", () => ShowPage(_page - 1));
			_nextButton = MakeNavButton("Next >", Advance);
			Button cancel = MakeNavButton("Cancel", () => { DialogResult = DialogResult.Cancel; Close(); });
			// laid out from the RIGHT EDGE inward, with the same margin the page
			// content has on the left: hand-placed x positions plus an auto-sized
			// button had Cancel hanging over the window's edge
			var margin = UIHelper.ScaleX(8);
			var gap = UIHelper.ScaleX(6);
			var right = ClientSize.Width - margin;
			foreach (var button in new[] { cancel, _nextButton, _backButton })
			{
				right -= button.Width;
				button.Location = new Point(right, UIHelper.ScaleY(382));
				right -= gap;
			}
			// what is left of the row once the buttons have taken their share
			_status.Width = Math.Max(UIHelper.ScaleX(80), right - margin);

			AcceptButton = _nextButton;
			CancelButton = cancel;
			// buttons first, so they sit in front of the status label rather than
			// under it - it is the one thing on this row that can grow
			Controls.AddRange([ _backButton, _nextButton, cancel, _status ]);
			ResumeLayout();
			ShowPage(0);
		}

		private const string IssuesUrl = "https://github.com/ToolAssisted-run/chimera/issues";

		/// <summary>
		/// Where to say something when Chimera gets it wrong. It sits on the first
		/// page because that is the page everyone sees, and it says the URL rather
		/// than hiding it behind a word, so it can be read off a screenshot.
		/// </summary>
		private static LinkLabel MakeIssuesNotice()
		{
			const string text = "Please submit any issues and feature requests here: " + IssuesUrl;
			LinkLabel notice = new()
			{
				Anchor = AnchorStyles.Bottom | AnchorStyles.Left | AnchorStyles.Right,
				AutoSize = false,
				ForeColor = SystemColors.GrayText,
				LinkBehavior = LinkBehavior.HoverUnderline,
				Location = Pt(8, 352),
				Size = new(UIHelper.ScaleX(544), UIHelper.ScaleY(20)),
				Text = text,
			};
			notice.Links.Clear();
			notice.Links.Add(text.IndexOf(IssuesUrl, StringComparison.Ordinal), IssuesUrl.Length, IssuesUrl);
			notice.LinkClicked += static (_, e) => Util.OpenUrlExternal((string) e.Link.LinkData);
			return notice;
		}

		private static Point Pt(int x, int y) => new(UIHelper.ScaleX(x), UIHelper.ScaleY(y));

		private static Label MakeLabel(string text, int x, int y)
			=> new() { AutoSize = true, Location = Pt(x, y), Text = text };

		/// <summary>
		/// A step's one question, across the full width of the window. Not
		/// auto-sized and never hand-wrapped: the text stops where the window
		/// stops, and follows it when it is resized.
		/// </summary>
		private static Label MakeHeading(string text)
			=> new()
			{
				Anchor = AnchorStyles.Top | AnchorStyles.Left | AnchorStyles.Right,
				AutoSize = false,
				Location = Pt(8, 10),
				Size = new(UIHelper.ScaleX(544), UIHelper.ScaleY(20)),
				Text = text,
			};

		/// <summary>
		/// One of the three navigation buttons. A FIXED size, because they are
		/// laid out from the window's right edge and an auto-sized button's width
		/// is not known until it has been laid out - which is how Cancel ended up
		/// past the edge of the window.
		/// </summary>
		private Button MakeNavButton(string text, Action onClick)
		{
			Button b = new()
			{
				Anchor = AnchorStyles.Bottom | AnchorStyles.Right,
				AutoSize = false,
				Size = new(UIHelper.ScaleX(76), UIHelper.ScaleY(24)),
				Text = text,
			};
			b.Click += (_, _) => onClick();
			return b;
		}

		private DiscoveredCorePackage? ChosenCore
			=> _core.SelectedIndex is >= 0 and var i && i < _cores.Count ? _cores[i] : null;

		/// <summary>
		/// Reads the chosen package's declaration and offers its machines. Done as
		/// soon as a core is picked, because the machine is part of picking it: a
		/// package that is one machine simply has nothing to offer here.
		/// </summary>
		private void LoadChosenPackage()
		{
			var core = ChosenCore;
			if (core is null || _cfgCore == core.Path) return;
			_cfgCore = core.Path;
			_cfg = null;
			_settings = new WaterboxCoreSettings();
			try
			{
				using var pkg = EnginePackage.Open(core.Path);
				_cfg = WaterboxConfig.FromJson(pkg?.EntryText(WaterboxCoreFactory.ConfigFileName) ?? "");
			}
			catch (InvalidOperationException)
			{
				// an unreadable package complains when the user tries to go on
			}
			_machine.Items.Clear();
			var machines = _cfg?.Machines;
			if (machines is { Count: > 0 })
			{
				foreach (var machine in machines) _machine.Items.Add(machine.DisplayName);
				_machine.SelectedIndex = 0;
			}
			var several = _machine.Items.Count > 1;
			_machine.Visible = several;
			_machineLabel.Visible = several;
			PinMachine();
			RefreshRendererChoices();
		}

		/// <summary>
		/// Opens the wizard already filled in from a project: its core, its machine,
		/// its settings and the files it is running on.
		///
		/// This IS how a project is reconfigured. Changing a sync setting changes the
		/// machine, so there is no editing a project in place that would not amount
		/// to building another one - and what made that unbearable was re-answering
		/// every question to change one of them. Answer them once; come back and
		/// change the one.
		///
		/// The files are seeded from where a run found them, which is the only place
		/// a path exists: a project carries names and hashes and no paths at all. A
		/// file that is no longer there is skipped rather than guessed at.
		/// </summary>
		/// <summary>
		/// Opens on a file somebody dropped: the core that claims its extension,
		/// and the file already in the slot that takes that kind of file.
		///
		/// The guess is the packages' own, not a table kept here. Every package
		/// declares the rom extensions it handles, so "which core is this .gg for"
		/// is a question the installed cores have already answered - and a core
		/// installed tomorrow answers it without this file changing. When two
		/// claim the same extension the first is chosen and the picker is left
		/// alone: a guess is a starting point, and the core box is right there.
		/// </summary>
		/// <returns>false when no installed core claims the extension</returns>
		public bool StartFrom(string path)
		{
			var index = GuessCoreIndexFor(path);
			if (index < 0) return false;

			_core.SelectedIndex = index;
			LoadChosenPackage();
			if (!BuildSlotForm()) return false;

			AddFileToSlot(SlotForFile(path), path);
			RefreshExposedSettings();
			_settingsGrid.SelectedObject = _settings;
			return true;
		}

		/// <summary>Which installed package says it handles this file, or -1.</summary>
		public int GuessCoreIndexFor(string path)
		{
			var extension = Path.GetExtension(path);
			if (string.IsNullOrEmpty(extension)) return -1;
			return _cores.ToList().FindIndex(c => c.Extensions.ContainsKey(extension.ToLowerInvariant()));
		}

		/// <summary>
		/// Which slot a dropped file belongs in: the first that declares its
		/// extension, and otherwise the first slot there is. A package lists the
		/// formats each slot takes, so a disc lands in the disc slot rather than
		/// in whatever happens to be at the top of the form.
		/// </summary>
		public string SlotForFile(string path)
		{
			var extension = Path.GetExtension(path).TrimStart('.').ToLowerInvariant();
			var slots = _declaration!.Slots;
			var match = slots.FirstOrDefault(slot =>
				slot.Formats.Any(f => string.Equals(f, extension, StringComparison.OrdinalIgnoreCase)));
			return (match ?? slots[0]).Id;
		}

		public void SeedFrom(ProjectAnswers answers)
		{
			var index = _cores.ToList().FindIndex(c =>
				string.Equals(c.Name, answers.CoreName, StringComparison.OrdinalIgnoreCase));
			if (index < 0) return;   // that core is not installed; leave the wizard blank
			_core.SelectedIndex = index;
			LoadChosenPackage();

			SeedSettings(answers.SettingsJson);

			// the slot form has to exist before anything can be put in it
			if (!BuildSlotForm()) return;
			foreach (var (slot, path) in answers.Files)
			{
				if (File.Exists(path)) AddFileToSlot(slot, path);
			}

			// and the files can decide settings in their turn
			RefreshExposedSettings();
			_settingsGrid.SelectedObject = _settings;
		}

		/// <summary>
		/// Puts a project's saved values into the settings object, then lets the
		/// machine and renderer pickers read themselves back out of it - they are
		/// settings like any other, and this is where they were recorded.
		/// </summary>
		private void SeedSettings(string flatJson)
		{
			_settings ??= new WaterboxCoreSettings();
			if (!string.IsNullOrWhiteSpace(flatJson))
			{
				try
				{
					var values = Newtonsoft.Json.Linq.JObject.Parse(flatJson);
					foreach (var pair in values)
					{
						if (pair.Value is Newtonsoft.Json.Linq.JValue v && v.Value is not null)
						{
							_settings.Values[pair.Key] = v.Value;
						}
					}
				}
				catch (Newtonsoft.Json.JsonException)
				{
					// a project whose settings will not parse gets the defaults
				}
			}

			if (_cfg?.HasMachines is true)
			{
				var effective = WaterboxCore.EffectiveSettingsFor(_cfg, _settings);
				var machine = _cfg.MachineFor(effective);
				var at = machine is null ? -1 : _cfg.Machines.IndexOf(machine);
				if (at >= 0) _machine.SelectedIndex = at;
			}
			// read before the pickers are refreshed: refreshing them selects the
			// core's default and writes THAT back over what the project said
			var renderer = _settings.Values.TryGetValue(RendererSetting, out var chosen) ? chosen?.ToString() : null;

			PinMachine();
			RefreshRendererChoices();
			if (renderer is not null) SetRenderer(renderer);
		}

		/// <summary>The setting that names the renderers, if this core has one.</summary>
		private const string RendererSetting = "renderer";

		/// <summary>
		/// One entry in the renderer dropdown: the name the CORE knows it by, shown
		/// as what it actually is. The distinction a person is owed here is where
		/// the drawing happens - the core's own rasteriser and its OpenGL path over
		/// a software OpenGL both run on the CPU inside the sandbox and are
		/// deterministic; the hardware one leaves the sandbox for a GPU and is not.
		/// The value is what the project records; renaming what is on screen must
		/// never rename what is in the file.
		/// </summary>
		private sealed class RendererChoice
		{
			internal RendererChoice(string value) => Value = value;

			internal string Value { get; }

			public override string ToString() => Value switch
			{
				"software" or "default" => "Software (Native)",
				"opengl" => "Software (OpenGL)",
				"opengl-hw" => "Hardware (OpenGL)",
				_ => Value,
			};
		}

		/// <summary>
		/// Offers the chosen core's renderers. A core that declares none draws one
		/// way, so there is one entry and nothing to choose; a core that declares
		/// them starts on the one it declared as its default, which is the faster of
		/// the two everywhere this exists so far.
		///
		/// Read through the CHOSEN MACHINE, because a package that is several
		/// machines may narrow a setting per machine.
		/// </summary>
		private void RefreshRendererChoices()
		{
			var decl = RendererDecl();
			var options = decl?.Options;

			_renderer.Items.Clear();
			if (options is { Count: > 0 })
			{
				foreach (var option in options) _renderer.Items.Add(new RendererChoice(option));
			}
			else
			{
				// nothing declared: the core has one renderer and does not name it
				_renderer.Items.Add(new RendererChoice("default"));
			}

			var wanted = decl?.DefaultValue as string;
			var index = wanted is null ? -1 : Array.IndexOf(RendererOptions, wanted);
			_renderer.SelectedIndex = index >= 0 ? index : 0;

			// one renderer is not a choice, and a disabled box still says what it is
			_renderer.Enabled = _renderer.Items.Count > 1;
			PinRenderer();
			ShowRendererCaveat();
		}

		/// <summary>
		/// What a hardware renderer costs, said where the choice is made rather
		/// than in a manual nobody opens. Only for the hardware ones: a person who
		/// picked a software renderer has nothing to be careful about, and a
		/// warning that is always on screen is a warning nobody reads.
		/// </summary>
		internal const string HardwareRendererCaveat =
			"Hardware renderers are much faster, but carry a slightly higher chance of desync. "
			+ "Check now and then that your movie still syncs: clear its greenzone in TAStudio and replay it from the start. "
			+ "If you need a guarantee, choose a software renderer.";

		private void ShowRendererCaveat()
			=> _rendererCaveat.Text = IsHardware(ChosenRenderer) ? HardwareRendererCaveat : "";

		/// <summary>
		/// Whether a renderer's value names one that draws on the machine's own
		/// GPU. The suffix is the convention every core shares (see WaterboxCore),
		/// so this window understands a core it has never heard of.
		/// </summary>
		internal static bool IsHardware(string? renderer)
			=> renderer is not null && renderer.EndsWith("-hw", StringComparison.Ordinal);

		/// <summary>The caveat as it stands, for tests ("" when none is showing).</summary>
		public string RendererCaveatText => _rendererCaveat.Text;

		private WaterboxConfig.SettingDecl? RendererDecl()
		{
			if (_cfg is null) return null;
			var effective = _settings is null
				? new Dictionary<string, object>()
				: WaterboxCore.EffectiveSettingsFor(_cfg, _settings);
			return _cfg.SettingsFor(_cfg.MachineFor(effective))
				.FirstOrDefault(static decl => decl.Name == RendererSetting);
		}

		/// <summary>
		/// Writes the chosen renderer into the settings, where it is a setting like
		/// any other - so the project records it and the movie cites it. A core that
		/// declares no renderer has nothing to write.
		/// </summary>
		private void PinRenderer()
		{
			if (_settings is null || RendererDecl() is null) return;
			if (_renderer.SelectedItem is RendererChoice chosen) _settings.Values[RendererSetting] = chosen.Value;
		}

		/// <summary>
		/// Writes the chosen machine into the settings, where it is a setting like
		/// any other - so the project records it, the movie cites it, and the file
		/// and firmware gates can ask which machine this is.
		/// </summary>
		private void PinMachine()
		{
			if (_cfg?.HasMachines is not true || _settings is null) return;
			var index = Math.Max(0, Math.Min(_cfg.Machines.Count - 1, _machine.SelectedIndex));
			var machine = _cfg.Machines[index];
			_settings.Values[_cfg.MachineSetting] = machine.When is { Count: > 0 } ? machine.When[0] : machine.Id;
			// the machine decides which files the project takes, so a form built for
			// another machine is stale
			_declarationCore = null;
		}

		/// <summary>The machine chosen on page one, for tests.</summary>
		public string? ChosenMachine
			=> _cfg?.HasMachines is true && _machine.SelectedIndex >= 0
				? _cfg.Machines[_machine.SelectedIndex].Id
				: null;

		private void ShowPage(int page)
		{
			_page = Math.Max(0, Math.Min(_pages.Length - 1, page));
			for (var i = 0; i < _pages.Length; i++) _pages[i].Visible = i == _page;
			_backButton.Enabled = _page > 0;
			_status.Text = "";
			UpdateNavLabels();
		}

		/// <summary>
		/// A machine that needs no firmware is not asked about firmware: the
		/// settings step is then the last one, and its button says Create.
		/// Whether it is depends on the current files and settings, so this is
		/// re-asked whenever either changes.
		/// </summary>
		private bool IsLastPage(int page)
			=> page == _pages.Length - 1 || (page == 2 && !AnyFirmwareRequired());

		/// <summary>
		/// Which firmware the decisions so far call for - the cheap half of the
		/// firmware step (the engine's decision tree only; no folder is indexed
		/// and nothing is hashed).
		/// </summary>
		private bool AnyFirmwareRequired()
		{
			if (_cfg?.RawFirmwareJson is not { Length: not 0 } declJson) return false;
			var effective = WaterboxCore.EffectiveSettingsFor(_cfg, _settings);
			return Chimera.Emulation.Common.Engine.EngineFirmware.Evaluate(
				declJson,
				CurrentSlotsJson(),
				Newtonsoft.Json.JsonConvert.SerializeObject(effective)).Count is not 0;
		}

		private void UpdateNavLabels()
		{
			_nextButton.Text = IsLastPage(_page) ? "Create" : "Next >";
			UpdateCreateEnabled();
		}

		/// <summary>Create is not an error message: it is simply unavailable until every required firmware is satisfied.</summary>
		private void UpdateCreateEnabled()
			=> _nextButton.Enabled = _page != _pages.Length - 1 || MissingRequiredFirmware() is null;

		/// <summary>whether Create is currently available, for tests</summary>
		public bool CreateEnabled => _nextButton.Enabled;

		/// <summary>what the forward button offers right now - "Next &gt;" or "Create", for tests</summary>
		public string NextButtonText => _nextButton.Text;

		private void Advance()
		{
			switch (_page)
			{
				case 0:
					if (ChosenCore is null) { _status.Text = "pick a core (none are installed?)"; return; }
					if (!BuildSlotForm()) return;
					ShowPage(1);
					break;
				case 1:
					var complaint = CardinalityComplaint();
					if (complaint is not null) { _status.Text = complaint; return; }
					if (!BuildSettingsPage()) return;
					ShowPage(2);
					break;
				case 2:
					if (!AnyFirmwareRequired())
					{
						// nothing to ask for: the settings step was the last one
						Create();
						break;
					}
					BuildFirmwarePage();
					ShowPage(3);
					break;
				default:
					var missing = MissingRequiredFirmware();
					if (missing is not null) { _status.Text = $"{missing} is required and not provided"; return; }
					Create();
					break;
			}
		}

		// ---- the core-informed form ----------------------------------------------

		/// <summary>The chosen package's file_slots.json rendered as slot groups; false when the package has none.</summary>
		private bool BuildSlotForm()
		{
			var core = ChosenCore!;
			if (_declarationCore == core.Path && _declaration is not null) return true;


			string? declarationJson = null;
			try
			{
				using var pkg = EnginePackage.Open(core.Path);
				declarationJson = pkg?.EntryText("file_slots.json");
			}
			catch (InvalidOperationException)
			{
				// unreadable package: the message below covers it
			}
			_declaration = ProjectSlotDeclaration.Parse(declarationJson);
			if (_declaration is null)
			{
				_status.Text = $"{core.Name} declares no file form (no usable file_slots.json in the package)";
				return false;
			}
			_declarationCore = core.Path;
			RenderSlotForm();
			return true;
		}

		/// <summary>Renders a given declaration directly - the test and screenshot door.</summary>
		internal void UseDeclaration(ProjectSlotDeclaration declaration)
		{
			_declaration = declaration;
			_declarationCore = "<injected>";
			RenderSlotForm();
			ShowPage(1);
		}

		/// <summary>Runs the real exposure gate over a given config - the test door.</summary>
		internal void UseSettingsFrom(WaterboxConfig cfg)
		{
			_cfg = cfg;
			_settings = new WaterboxCoreSettings();
			RefreshRendererChoices();   // as choosing the core does
			RefreshExposedSettings();
			_settingsGrid.SelectedObject = _settings;
			ShowPage(2);
		}

		/// <summary>
		/// Offers a given core's renderers without leaving page one - the test and
		/// screenshot door for the dropdown beside the core.
		/// </summary>
		internal void UseRenderersFrom(WaterboxConfig cfg)
		{
			_cfg = cfg;
			_settings = new WaterboxCoreSettings();
			RefreshRendererChoices();
		}

		/// <summary>the renderers offered on page one, in order, for tests</summary>
		public string[] RendererOptions
			=> _renderer.Items.Cast<RendererChoice>().Select(static item => item.Value).ToArray();

		/// <summary>how those renderers are spelled in the dropdown, for tests</summary>
		public string[] RendererLabels
			=> _renderer.Items.Cast<RendererChoice>().Select(static item => item.ToString()).ToArray();

		/// <summary>the renderer chosen on page one, for tests</summary>
		public string? ChosenRenderer => (_renderer.SelectedItem as RendererChoice)?.Value;

		/// <summary>whether the renderer is a choice at all, for tests</summary>
		public bool RendererIsAChoice => _renderer.Enabled;

		/// <summary>what the project would record for a setting - for tests</summary>
		public object? SettingValue(string name)
			=> _settings?.Values is not null && _settings.Values.TryGetValue(name, out var value) ? value : null;

		/// <summary>picks a renderer as the dropdown would - for tests</summary>
		internal void SetRenderer(string name)
		{
			var index = Array.IndexOf(RendererOptions, name);
			if (index >= 0) _renderer.SelectedIndex = index;
		}

		/// <summary>the exposed settings, in order, for tests</summary>
		public string[] ExposedSettingNames
			=> (_settings?.Declarations ?? [ ]).Select(static d => d.Name).ToArray();

		/// <summary>sets a value as the grid would, re-running the gate - for tests</summary>
		internal void SetSettingValue(string name, object value)
		{
			// exactly what the grid does when a value is edited, so the test
			// door cannot drift from the real path
			_settings!.Values[name] = value;
			RefreshExposedSettings();
			UpdateNavLabels();
		}

		/// <summary>Renders given sync-setting declarations directly - the test and screenshot door.</summary>
		internal void UseSettingsDecls(IReadOnlyList<WaterboxConfig.SettingDecl> declarations)
		{
			_settings = new WaterboxCoreSettings { Declarations = declarations };
			_settingsGrid.SelectedObject = _settings;
			ShowPage(2);
		}

		private void RenderSlotForm()
		{
			_slotsHost.Controls.Clear();
			_slotLists.Clear();
			_slotGroups.Clear();
			var y = 0;
			foreach (var slot in _declaration!.Slots)
			{
				GroupBox group = new()
				{
					Anchor = AnchorStyles.Top | AnchorStyles.Left | AnchorStyles.Right,
					Location = Pt(0, y),
					Size = new(UIHelper.ScaleX(520), UIHelper.ScaleY(96)),
					Text = $"{slot.Title}  ({slot.CardinalityText}{(slot.Formats.Count is 0 ? "" : $"; {string.Join(", ", slot.Formats.Select(static f => "." + f))}")})",
				};
				// the core's own explanation of the slot, on hover - the row IS
				// the affordance, so there is no glyph to wonder about
				var help = slot.Help.Length is 0 ? "the core did not explain this slot" : slot.Help;
				_tips.SetToolTip(group, help);
				ListBox list = new()
				{
					Anchor = AnchorStyles.Top | AnchorStyles.Left | AnchorStyles.Right,
					IntegralHeight = false,
					Location = Pt(8, 18),
					Size = new(UIHelper.ScaleX(402), UIHelper.ScaleY(70)),
				};
				_tips.SetToolTip(list, help);

				// A file arrives here the same way it arrives anywhere else on a
				// desktop: dragged onto the thing it belongs to. The list IS the
				// target, so what a person drops on it lands in that slot and no
				// other - which is the whole reason not to make the window one
				// big drop zone that has to guess.
				list.AllowDrop = true;
				list.DragEnter += (_, e) => e.Effect = DroppedPaths(e).Length is 0
					? DragDropEffects.None
					: DragDropEffects.Copy;
				list.DragDrop += (_, e) =>
				{
					foreach (var path in DroppedPaths(e)) AddFileToSlot(slot.Id, path);
				};

				Button add = new() { AutoSize = true, Location = Pt(416, 18), Text = "Add..." };
				add.Click += (_, _) =>
				{
					foreach (var path in _pickFiles(slot)) AddFileToSlot(slot.Id, path);
				};
				Button remove = new() { AutoSize = true, Location = Pt(416, 48), Text = "Remove" };
				remove.Click += (_, _) =>
				{
					if (list.SelectedIndex >= 0) list.Items.RemoveAt(list.SelectedIndex);
					RefreshSlotAvailability();
				};
				group.Controls.AddRange([ list, add, remove ]);
				_slotsHost.Controls.Add(group);
				_slotLists[slot.Id] = list;
				_slotGroups[slot.Id] = group;
				group.Tag = group.Text;
				y += 102;
			}
			RefreshSlotAvailability();
		}

		/// <summary>
		/// The settings as the decision trees should read them: the package's
		/// defaults and per-machine narrowing applied over what has been chosen.
		/// Without a package there is nothing to apply, but the chosen values are
		/// still the truth - answering with an empty map instead would make every
		/// condition that names a setting quietly false.
		/// </summary>
		private System.Collections.Generic.Dictionary<string, object> EffectiveSettings()
			=> _cfg is not null
				? WaterboxCore.EffectiveSettingsFor(_cfg, _settings)
				: new(_settings?.Values ?? new System.Collections.Generic.Dictionary<string, object>());

		/// <summary>
		/// The slots themselves join the decision tree: a slot's exposedWhen is
		/// evaluated over the CURRENT picks, so filling one slot can make
		/// another unavailable until it is unloaded (a Famicom disk rules out
		/// a cartridge and vice versa). Greyed, never hidden - the person sees
		/// what the machine could also take and why it cannot right now.
		/// </summary>
		private void RefreshSlotAvailability()
		{
			if (_declaration is null) return;
			// the settings go in because the MACHINE is one: a package that is
			// several machines has slots only some of them have
			var exposed = EngineSlotsGate.Evaluate(
				_declaration.RawJson,
				CurrentSlotsJson(),
				Newtonsoft.Json.JsonConvert.SerializeObject(EffectiveSettings()));
			foreach (var slot in _declaration.Slots)
			{
				if (!_slotGroups.TryGetValue(slot.Id, out var group)) continue;
				var available = exposed.Contains(slot.Id);
				group.Enabled = available;
				group.Text = available
					? (string)group.Tag!
					: $"{(string)group.Tag!} - unavailable with the current files";
			}
		}

		/// <summary>Whether a slot currently accepts files, for tests.</summary>
		public bool SlotAvailable(string slotId)
			=> _slotGroups.TryGetValue(slotId, out var group) && group.Enabled;

		/// <summary>
		/// One picked file, kept as its canonical (bare) name plus where it is
		/// now. A cue sheet counts as ONE pick: its referenced track files ride
		/// along automatically at creation (the engine adds them as support
		/// files, from the cue's own folder), so the row says how many.
		/// </summary>
		private sealed record PickedFile(string Name, string Path, int TrackCount = 0)
		{
			public override string ToString()
				=> TrackCount is 0 ? Name : $"{Name}  (+ {TrackCount} track file{(TrackCount is 1 ? "" : "s")})";
		}

		/// <summary>
		/// The files in a drag, if it is carrying any. Directories are dropped
		/// silently: a slot takes files, and expanding a folder into one would be
		/// a different decision made on the person's behalf.
		/// </summary>
		private static string[] DroppedPaths(DragEventArgs e)
			=> e.Data?.GetDataPresent(DataFormats.FileDrop) is true
				&& e.Data.GetData(DataFormats.FileDrop) is string[] paths
					? paths.Where(File.Exists).ToArray()
					: [ ];

		/// <summary>Adds a file to a slot's list; public in behaviour so tests can drive the form without a picker.</summary>
		public void AddFileToSlot(string slotId, string path)
		{
			if (!_slotLists.TryGetValue(slotId, out var list)) return;
			if (_slotGroups.TryGetValue(slotId, out var g) && !g.Enabled) return;
			var name = Path.GetFileName(path);
			if (list.Items.OfType<PickedFile>().Any(f => f.Name == name)) return;

			// a cue sheet is one pick, but its track files must be next to it -
			// the same all-or-nothing rule the engine applies at creation,
			// raised here so the complaint lands at pick time
			var tracks = 0;
			if (name.EndsWith(".cue", StringComparison.OrdinalIgnoreCase) && File.Exists(path))
			{
				var refs = EngineCue.References(File.ReadAllBytes(path));
				var folder = Path.GetDirectoryName(path) ?? "";
				var missing = refs.Where(r => !File.Exists(Path.Combine(folder, r))).ToList();
				if (missing.Count is not 0)
				{
					_status.Text = $"{name} references {missing[0]}, which is not next to it - a cue's files are added together";
					return;
				}
				tracks = refs.Count;
			}

			list.Items.Add(new PickedFile(name, path, tracks));

			// A slot that names its formats has still not been given a rule: the
			// picker offers "All files" too, and a person who renamed something
			// knows better than this form does. So an unexpected extension is
			// SAID rather than refused - the complaint a mis-drop would otherwise
			// become is an engine error at Create, long after the mistake.
			var slot = _declaration?.Slots.FirstOrDefault(s => s.Id == slotId);
			var extension = Path.GetExtension(name).TrimStart('.');
			_status.Text = slot is { Formats.Count: > 0 }
				&& !slot.Formats.Any(f => string.Equals(f, extension, StringComparison.OrdinalIgnoreCase))
					? $"{name} is not one of the {string.Join(", ", slot.Formats.Select(static f => "." + f))} this slot expects"
					: "";
			RefreshSlotAvailability();
		}

		/// <summary>The remove button's behaviour, for tests: drop one picked file by canonical name.</summary>
		public void RemoveFileFromSlot(string slotId, string name)
		{
			if (!_slotLists.TryGetValue(slotId, out var list)) return;
			var found = list.Items.OfType<PickedFile>().FirstOrDefault(f => f.Name == name);
			if (found is null) return;
			list.Items.Remove(found);
			RefreshSlotAvailability();
		}

		/// <summary>Whether a slot's list takes dropped files, for tests.</summary>
		public bool SlotAcceptsDrops(string slotId)
			=> _slotLists.TryGetValue(slotId, out var list) && list.AllowDrop;

		/// <summary>What still blocks the file page, for tests (null = nothing).</summary>
		public string? FilesComplaint() => CardinalityComplaint();

		/// <summary>The rows as the list shows them (cue rows carry their track count), for tests.</summary>
		public string[] SlotRowTexts(string slotId)
			=> _slotLists.TryGetValue(slotId, out var list)
				? list.Items.OfType<PickedFile>().Select(static f => f.ToString()).ToArray()
				: [ ];

		/// <summary>The page's live complaint line, for tests.</summary>
		public string StatusText => _status.Text;

		/// <summary>The canonical names in one slot, in order, for tests.</summary>
		public string[] SlotFileNames(string slotId)
			=> _slotLists.TryGetValue(slotId, out var list)
				? list.Items.OfType<PickedFile>().Select(static f => f.Name).ToArray()
				: [ ];

		/// <summary>The declaration's own cardinality rules, phrased for the person filling the form.</summary>
		private string? CardinalityComplaint()
		{
			foreach (var slot in _declaration!.Slots)
			{
				var count = _slotLists[slot.Id].Items.Count;
				if (count < slot.Min && SlotAvailable(slot.Id)) return $"{slot.Title}: needs {slot.CardinalityText}, has {count}";
				if (slot.Max >= 0 && count > slot.Max) return $"{slot.Title}: takes {slot.CardinalityText}, has {count}";
			}
			foreach (var group in _declaration.AtLeastOneOf)
			{
				var total = group.Sum(id => _slotLists.TryGetValue(id, out var l) ? l.Items.Count : 0);
				if (total is 0)
				{
					var titles = group.Select(id => _declaration.Slots.FirstOrDefault(s => s.Id == id)?.Title ?? id);
					return $"at least one file is needed across: {string.Join(", ", titles)}";
				}
			}
			return null;
		}

		// ---- settings -------------------------------------------------------------

		private bool BuildSettingsPage()
		{
			// the package was read when its core was chosen (the machine is picked
			// there, and everything since has depended on it)
			if (_cfg is null)
			{
				_status.Text = $"{ChosenCore?.Name} has no usable {WaterboxCoreFactory.ConfigFileName} in its package";
				return false;
			}
			_settings ??= new WaterboxCoreSettings();
			RefreshExposedSettings();
			_settingsGrid.SelectedObject = _settings;
			return true;
		}

		/// <summary>
		/// The settings the DECISIONS expose (docs/project.md): the chosen files
		/// gate which settings exist at all (a Game Gear cart exposes its
		/// sound chip, a Genesis cart its own), and a setting may gate further
		/// settings - so the set is re-evaluated when a value changes.
		/// </summary>
		private void RefreshExposedSettings()
		{
			if (_cfg is null || _settings is null) return;
			var effective = WaterboxCore.EffectiveSettingsFor(_cfg, _settings);
			var exposed = Chimera.Emulation.Common.Engine.EngineSettingsGate.Evaluate(
				_cfg.RawSettingsJson,
				CurrentSlotsJson(),
				Newtonsoft.Json.JsonConvert.SerializeObject(effective));
			// as the CHOSEN MACHINE has them: a package that is several machines
			// narrows some settings per machine (a Master System port takes a pad
			// or nothing, where a Mega Drive port takes six devices)
			var all = _cfg.SettingsFor(_cfg.MachineFor(effective));
			var declarations = exposed
				.Where(entry => entry.Index >= 0 && entry.Index < all.Count && all[entry.Index].Name == entry.Name)
				.Select(entry => all[entry.Index])
				// ...except the renderer, which is asked beside the core on page one
				// and would only be asked twice here
				.Where(static decl => decl.Name != RendererSetting)
				.ToList();
			var current = _settings.Declarations;
			if (current is not null && current.Count == declarations.Count
				&& current.Zip(declarations, static (a, b) => ReferenceEquals(a, b)).All(static same => same))
			{
				return; // the exposed set did not change
			}
			_settings.Declarations = declarations;
			_settingsGrid.Refresh();
		}

		/// <summary>Renders given firmware needs directly - the test and screenshot door.</summary>
		internal void UseFirmwareNeeds(
			WaterboxConfig cfg,
			IReadOnlyList<(string Id, int Index)> needed,
			IReadOnlyList<FirmwareLocator.IndexedFile>? index = null)
		{
			_cfg = cfg;
			_firmwareIndex = index ?? [ ];
			BuildFirmwareNeeds(needed);
			RenderFirmwareRows();
			ShowPage(3);
		}

		// ---- firmware: the last word, decided by everything before it ------------

		/// <summary>The slot map exactly as the session will mount it, from the form's current picks.</summary>
		private string CurrentSlotsJson()
		{
			Newtonsoft.Json.Linq.JObject slots = new();
			if (_declaration is null) return slots.ToString(Newtonsoft.Json.Formatting.None);
			foreach (var slot in _declaration.Slots)
			{
				if (!_slotLists.TryGetValue(slot.Id, out var list) || list.Items.Count is 0) continue;
				Newtonsoft.Json.Linq.JArray names = new();
				foreach (var file in list.Items.OfType<PickedFile>()) names.Add(file.Name);
				slots[slot.Id] = names;
			}
			return slots.ToString(Newtonsoft.Json.Formatting.None);
		}

		private void BuildFirmwarePage()
		{
			var effective = WaterboxCore.EffectiveSettingsFor(
				_cfg ?? new WaterboxConfig(), _settings);
			var needed = Chimera.Emulation.Common.Engine.EngineFirmware.Evaluate(
				_cfg?.RawFirmwareJson ?? "[]",
				CurrentSlotsJson(),
				Newtonsoft.Json.JsonConvert.SerializeObject(effective));

			// the Firmware folder answers first, plus anything remembered from
			// earlier sessions - the user is only asked for what neither has
			var remembered = needed
				.Select(need => _rememberedFirmwarePath(ChosenCoreName ?? "", need.Id))
				.Where(static path => path is not null)
				.Select(static path => path!);
			_firmwareIndex = FirmwareLocator.BuildIndex(_firmwareSearchDirs, remembered);
			BuildFirmwareNeeds(needed);
			RenderFirmwareRows();
		}

		private void BuildFirmwareNeeds(IReadOnlyList<(string Id, int Index)> needed)
		{
			var decls = _cfg?.Firmware ?? [ ];
			_firmwareNeeds = needed.Select(entry =>
			{
				FirmwareNeed need = new()
				{
					Id = entry.Id,
					Decl = entry.Index >= 0 && entry.Index < decls.Count && decls[entry.Index].Id == entry.Id
						? decls[entry.Index]
						: decls.FirstOrDefault(d => d.Id == entry.Id),
				};
				// the requirement is one exact file; the folder either holds it
				// (any name) or the user is asked
				if (need.Decl is not null
					&& FirmwareLocator.FindFor(need.Decl, _firmwareIndex) is { } found)
				{
					need.ChosenPath = found.Path;
					need.ChosenSha1 = found.Sha1;
				}
				return need;
			}).ToList();
		}

		private void RenderFirmwareRows()
		{
			var selected = _firmwareList.SelectedIndices.Count is 0 ? -1 : _firmwareList.SelectedIndices[0];
			_firmwareList.BeginUpdate();
			_firmwareList.Items.Clear();
			foreach (var need in _firmwareNeeds)
			{
				var title = need.Decl?.DisplayName ?? need.Id;
				if (!string.IsNullOrEmpty(need.Decl?.Label)) title += $"  ({need.Decl!.Label})";
				// no tick glyph: it renders badly at this size on every platform, and
				// prefixing only the satisfied rows left the column ragged. Green
				// against red says the same thing and says it in line.
				ListViewItem item = new(title)
				{
					ToolTipText = need.Decl?.Description ?? "",
					ForeColor = need.Satisfied ? System.Drawing.Color.DarkGreen : System.Drawing.Color.Firebrick,
				};
				item.SubItems.Add(need.Decl?.Name ?? "");
				item.SubItems.Add(string.IsNullOrEmpty(need.Decl?.Sha1) ? "(your own dump)" : need.Decl!.Sha1);
				item.SubItems.Add(need.Satisfied
					? $"found: {Path.GetFileName(need.ChosenPath)}"
					: "not found");
				_firmwareList.Items.Add(item);
			}
			if (_firmwareNeeds.Count is 0)
			{
				_firmwareList.Items.Add(new ListViewItem("(these decisions need no firmware)"));
			}
			if (selected >= 0 && selected < _firmwareList.Items.Count) _firmwareList.Items[selected].Selected = true;
			_firmwareList.EndUpdate();
			UpdateFirmwareButtons();
			UpdateCreateEnabled();
		}

		private FirmwareNeed? SelectedFirmwareNeed()
		{
			var index = _firmwareList.SelectedIndices.Count is 0 ? -1 : _firmwareList.SelectedIndices[0];
			return index >= 0 && index < _firmwareNeeds.Count ? _firmwareNeeds[index] : null;
		}

		private void UpdateFirmwareButtons()
		{
			var need = SelectedFirmwareNeed();
			_firmwareSetButton.Enabled = need is not null;
			_firmwareClearButton.Enabled = need?.ChosenPath is not null;
		}

		private void SetFirmwareFile()
		{
			if (SelectedFirmwareNeed() is not { } need) return;
			var path = _pickFirmwareFile($"Locate {need.Decl?.DisplayName ?? need.Id}");
			if (path is null) return;
			ProvideFirmware(need.Id, path);
		}

		/// <summary>
		/// Points one requirement at a file the user chose - allowed even when
		/// the folder already found one. The hash decides: the requirement names
		/// ONE exact file, and only that file satisfies it.
		/// </summary>
		public void ProvideFirmware(string id, string path)
		{
			var need = _firmwareNeeds.FirstOrDefault(n => n.Id == id);
			if (need is null) return;
			byte[] bytes;
			try
			{
				bytes = System.IO.File.ReadAllBytes(path);
			}
			catch (System.IO.IOException ex)
			{
				_status.Text = ex.Message;
				return;
			}
			var sha1 = Chimera.Emulation.Common.Engine.ChimeraEngine.Sha1Hex(bytes);
			// a pinned entry names ONE exact file; an unpinned one (no declared
			// hash - a file only the user can own, like a console's own font
			// region nothing ships) takes what is chosen and records its hash
			if (!string.IsNullOrEmpty(need.Decl?.Sha1)
				&& !sha1.Equals(need.Decl!.Sha1, StringComparison.OrdinalIgnoreCase))
			{
				_status.Text = $"{System.IO.Path.GetFileName(path)} is not this file: its hash is {sha1},"
					+ $" the requirement is {need.Decl!.Sha1}";
				return;
			}
			need.ChosenPath = System.IO.Path.GetFullPath(path);
			need.ChosenSha1 = sha1;
			_status.Text = "";
			RenderFirmwareRows();
		}

		private void ClearFirmwareFile()
		{
			if (SelectedFirmwareNeed() is not { } need) return;
			need.ChosenPath = null;
			RenderFirmwareRows();
		}

		private string? MissingRequiredFirmware()
		{
			foreach (var need in _firmwareNeeds)
			{
				if (!need.Satisfied) return need.Decl?.DisplayName ?? need.Id;
			}
			return null;
		}

		/// <summary>What was found or chosen, for the owner to remember paths per core (Config.CoreFirmware).</summary>
		public IReadOnlyDictionary<string, string> ProvidedFirmwarePaths
			=> _firmwareNeeds.Where(static n => n.ChosenPath is not null)
				.ToDictionary(static n => n.Id, static n => n.ChosenPath!);

		/// <summary>the requirement states, for tests</summary>
		public bool FirmwareSatisfied(string id)
			=> _firmwareNeeds.FirstOrDefault(n => n.Id == id)?.Satisfied is true;

		public string? ChosenFirmwarePath(string id)
			=> _firmwareNeeds.FirstOrDefault(n => n.Id == id)?.ChosenPath;

		// ---- creation -------------------------------------------------------------

		private void Create()
		{
			var core = ChosenCore!;
			// NOT disposed here on success: the caller takes ownership of the
			// finished project and boots the machine from it. On any failure
			// below we dispose it ourselves and stay on the page.
			var project = EngineProject.New();
			project.SetCore(core.Name, core.Version ?? "", core.Sha1 ?? "");
			// The platform, stamped at creation: reopening forces the pinned core
			// through the movie queue, and that check wants to know the system. A
			// package that is several machines says which one through its settings,
			// which the user has just chosen.
			var system = (_cfg is not null && _settings is not null
					? _cfg.MachineFor(WaterboxCore.EffectiveSettingsFor(_cfg, _settings))?.Id
					: null)
				?? core.Systems.FirstOrDefault();
			if (system is { Length: not 0 })
			{
				project.HeaderSet("Platform", system);
			}

			try
			{
				foreach (var slot in _declaration!.Slots)
				{
					foreach (var file in _slotLists[slot.Id].Items.OfType<PickedFile>())
					{
						project.FileAdd(file.Name, slot.Id, file.Path);
					}
				}
			}
			catch (InvalidOperationException ex)
			{
				_status.Text = ex.Message;
				project.Dispose();
				ShowPage(1);
				return;
			}

			// the project records every EXPOSED sync setting explicitly, at its
			// effective value: the settings section is exactly the knobs these
			// decisions offered, nothing else
			{
				Dictionary<string, object> recorded = new();
				foreach (var decl in _settings?.Declarations ?? [ ])
				{
					recorded[decl.Name] = _settings!.Values is not null
						&& _settings.Values.TryGetValue(decl.Name, out var value)
							? value
							: decl.DefaultValue;
				}
				// the renderer is not in that list - it is chosen on page one - but it
				// is a setting like any other and the project records it the same way
				if (RendererDecl() is { } rendererDecl)
				{
					recorded[rendererDecl.Name] = _settings?.Values is not null
						&& _settings.Values.TryGetValue(rendererDecl.Name, out var chosen)
							? chosen
							: rendererDecl.DefaultValue;
				}
				if (recorded.Count is not 0)
					project.SetSettingsJson(Newtonsoft.Json.JsonConvert.SerializeObject(recorded));
			}

			{
				Newtonsoft.Json.Linq.JArray pins = new();
				foreach (var need in _firmwareNeeds)
				{
					if (!need.Satisfied || need.ChosenSha1 is null) continue;
					pins.Add(new Newtonsoft.Json.Linq.JObject { ["id"] = need.Id, ["sha1"] = need.ChosenSha1 });
				}
				if (pins.Count is not 0) project.SetFirmwareJson(pins.ToString(Newtonsoft.Json.Formatting.None));
			}

			string? declarationJson = null;
			try
			{
				using var pkg = EnginePackage.Open(core.Path);
				declarationJson = pkg?.EntryText("file_slots.json");
			}
			catch (InvalidOperationException)
			{
			}
			if (declarationJson is not null && project.Validate(declarationJson) is { } refusal)
			{
				_status.Text = refusal;
				project.Dispose();
				ShowPage(1);
				return;
			}

			// nothing is written: the project lives in memory until the work is
			// saved, which is also when the user is asked where it goes
			CreatedProject = project;
			DialogResult = DialogResult.OK;
			Close();
		}
	}
}
