#nullable enable

using System;
using System.Collections.Generic;
using System.Drawing;
using System.IO;
using System.Linq;
using System.Windows.Forms;

using Chimera.Client.Common;
using Chimera.Emulation.Common.Engine;
using Chimera.Emulation.Common;
using Chimera.Emulation.Common.Waterbox;

namespace Chimera.Client.GUI
{
	/// <summary>
	/// Where a chimera project is born: everything defined at once, up front
	/// (docs/project.md). Step one names the project and picks the core; step
	/// two is the core-informed file form - the slots, cardinalities, formats
	/// and tooltips all come from the chosen package's file_slots.json, this
	/// window only renders and enforces; step three is the sync settings. On
	/// Create, every file is hashed where it stands, the manifest is validated
	/// by the engine against the core's own declaration, and the project file
	/// is written.
	/// </summary>
	public sealed class NewProjectWizard : FormBase
	{
		private readonly IReadOnlyList<DiscoveredCorePackage> _cores;
		private readonly Func<string?> _pickSavePath;
		private readonly Func<ProjectSlotDeclaration.Slot, string[]> _pickFiles;

		private readonly Panel[] _pages = new Panel[4];
		private int _page;

		// page 1
		private readonly TextBox _projectPath;
		private readonly TextBox _title;
		private readonly ComboBox _core;
		private readonly TextBox _description;

		// page 2, rebuilt when the chosen core changes
		private readonly Panel _slotsHost;
		private ProjectSlotDeclaration? _declaration;
		private string? _declarationCore;
		private readonly Dictionary<string, ListBox> _slotLists = new();
		private readonly Dictionary<string, GroupBox> _slotGroups = new();
		private readonly ToolTip _tips = new();

		// page 3
		private readonly PropertyGrid _settingsGrid;
		private WaterboxCoreSyncSettings? _syncSettings;
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

		/// <summary>set when Create succeeded: the path of the written project</summary>
		public string? CreatedProjectPath { get; private set; }

		/// <summary>the chosen core's name, for the owner's firmware bookkeeping</summary>
		public string? ChosenCoreName => ChosenCore?.Name;

		protected override string WindowTitleStatic => "New Chimera Project";

		public NewProjectWizard(
			IReadOnlyList<DiscoveredCorePackage> cores,
			Func<string?> pickSavePath,
			Func<ProjectSlotDeclaration.Slot, string[]> pickFiles,
			Func<string, string?>? pickFirmwareFile = null,
			IReadOnlyList<string>? firmwareSearchDirs = null,
			Func<string, string, string?>? rememberedFirmwarePath = null)
		{
			_pickFirmwareFile = pickFirmwareFile ?? (static _ => null);
			_firmwareSearchDirs = firmwareSearchDirs ?? [ ];
			_rememberedFirmwarePath = rememberedFirmwarePath ?? (static (_, _) => null);
			_cores = cores.Where(static c => c.Error is null).ToList();
			_pickSavePath = pickSavePath;
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

			// ---- page 1: identity ------------------------------------------------
			var p1 = _pages[0];
			p1.Controls.Add(MakeLabel("A project holds everything the work needs: the core, the files, the settings,\nand the TAS itself. Everything is defined here, once.", 8, 8));
			p1.Controls.Add(MakeLabel("Project file:", 8, 56));
			_projectPath = new TextBox { Location = Pt(110, 52), Width = UIHelper.ScaleX(360) };
			Button browse = new() { Text = "Browse...", Location = Pt(476, 50), AutoSize = true };
			browse.Click += (_, _) =>
			{
				var chosen = _pickSavePath();
				if (chosen is not null) _projectPath.Text = chosen;
			};
			p1.Controls.Add(_projectPath);
			p1.Controls.Add(browse);
			p1.Controls.Add(MakeLabel("Title:", 8, 88));
			_title = new TextBox { Location = Pt(110, 84), Width = UIHelper.ScaleX(360) };
			p1.Controls.Add(_title);
			p1.Controls.Add(MakeLabel("Emulation core:", 8, 120));
			_core = new ComboBox { Location = Pt(110, 116), Width = UIHelper.ScaleX(360), DropDownStyle = ComboBoxStyle.DropDownList };
			foreach (var core in _cores)
			{
				_core.Items.Add($"{core.Name}  ({string.Join(", ", core.Systems)}{(string.IsNullOrEmpty(core.Version) ? "" : $", {core.Version}")})");
			}
			if (_core.Items.Count is not 0) _core.SelectedIndex = 0;
			p1.Controls.Add(_core);
			p1.Controls.Add(MakeLabel("Description:", 8, 152));
			_description = new TextBox
			{
				Location = Pt(110, 148),
				Size = new(UIHelper.ScaleX(360), UIHelper.ScaleY(180)),
				Multiline = true,
				ScrollBars = ScrollBars.Vertical,
			};
			p1.Controls.Add(_description);

			// ---- page 2: the core-informed file form -----------------------------
			var p2 = _pages[1];
			p2.Controls.Add(MakeLabel("The files this machine takes, as the core declares them. Order within a\ncategory is the swap order.", 8, 8));
			_slotsHost = new Panel
			{
				Anchor = AnchorStyles.Top | AnchorStyles.Bottom | AnchorStyles.Left | AnchorStyles.Right,
				AutoScroll = true,
				Location = Pt(8, 48),
				Size = new(UIHelper.ScaleX(544), UIHelper.ScaleY(324)),
			};
			p2.Controls.Add(_slotsHost);

			// ---- page 3: sync settings -------------------------------------------
			var p3 = _pages[2];
			p3.Controls.Add(MakeLabel("The sync settings this machine starts with. They shape the machine, so the\nproject records them; a structural change later restarts from frame 0.", 8, 8));
			_settingsGrid = new PropertyGrid
			{
				Anchor = AnchorStyles.Top | AnchorStyles.Bottom | AnchorStyles.Left | AnchorStyles.Right,
				Location = Pt(8, 48),
				Size = new(UIHelper.ScaleX(544), UIHelper.ScaleY(324)),
				ToolbarVisible = false,
				PropertySort = PropertySort.Alphabetical,
			};
			_settingsGrid.PropertyValueChanged += (_, _) => RefreshExposedSettings();
			p3.Controls.Add(_settingsGrid);

			// ---- page 4: firmware, decided by everything chosen above ------------
			var p4 = _pages[3];
			p4.Controls.Add(MakeLabel("The firmware these decisions call for - each requirement is one exact file,\nnamed by hash (the file name is only a hint). The Firmware folder was\nsearched already; select a file yourself where nothing was found.", 8, 8));
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
			_status = new Label
			{
				Anchor = AnchorStyles.Bottom | AnchorStyles.Left | AnchorStyles.Right,
				AutoEllipsis = true,
				ForeColor = Color.Firebrick,
				Location = Pt(8, 386),
				Size = new(UIHelper.ScaleX(360), UIHelper.ScaleY(16)),
			};
			_backButton = MakeNavButton("< Back", 370, () => ShowPage(_page - 1));
			_nextButton = MakeNavButton("Next >", 442, Advance);
			Button cancel = MakeNavButton("Cancel", 506, () => { DialogResult = DialogResult.Cancel; Close(); });
			AcceptButton = _nextButton;
			CancelButton = cancel;
			Controls.AddRange([ _status, _backButton, _nextButton, cancel ]);
			ResumeLayout();
			ShowPage(0);
		}

		private static Point Pt(int x, int y) => new(UIHelper.ScaleX(x), UIHelper.ScaleY(y));

		private static Label MakeLabel(string text, int x, int y)
			=> new() { AutoSize = true, Location = Pt(x, y), Text = text };

		private Button MakeNavButton(string text, int x, Action onClick)
		{
			Button b = new()
			{
				Anchor = AnchorStyles.Bottom | AnchorStyles.Right,
				AutoSize = true,
				Location = Pt(x, 382),
				Text = text,
			};
			b.Click += (_, _) => onClick();
			return b;
		}

		private DiscoveredCorePackage? ChosenCore
			=> _core.SelectedIndex is >= 0 and var i && i < _cores.Count ? _cores[i] : null;

		private void ShowPage(int page)
		{
			_page = Math.Max(0, Math.Min(_pages.Length - 1, page));
			for (var i = 0; i < _pages.Length; i++) _pages[i].Visible = i == _page;
			_backButton.Enabled = _page > 0;
			_nextButton.Text = _page == _pages.Length - 1 ? "Create" : "Next >";
			_status.Text = "";
			UpdateCreateEnabled();
		}

		/// <summary>Create is not an error message: it is simply unavailable until every required firmware is satisfied.</summary>
		private void UpdateCreateEnabled()
			=> _nextButton.Enabled = _page != _pages.Length - 1 || MissingRequiredFirmware() is null;

		/// <summary>whether Create is currently available, for tests</summary>
		public bool CreateEnabled => _nextButton.Enabled;

		private void Advance()
		{
			switch (_page)
			{
				case 0:
					if (string.IsNullOrWhiteSpace(_projectPath.Text)) { _status.Text = "pick where the project file goes"; return; }
					if (string.IsNullOrWhiteSpace(_title.Text)) { _status.Text = "give the project a title"; return; }
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
			_syncSettings = new WaterboxCoreSyncSettings();
			RefreshExposedSettings();
			_settingsGrid.SelectedObject = _syncSettings;
			ShowPage(2);
		}

		/// <summary>the exposed sync settings, in order, for tests</summary>
		public string[] ExposedSettingNames
			=> (_syncSettings?.Declarations ?? [ ]).Select(static d => d.Name).ToArray();

		/// <summary>sets a value as the grid would, re-running the gate - for tests</summary>
		internal void SetSettingValue(string name, object value)
		{
			_syncSettings!.Values[name] = value;
			RefreshExposedSettings();
		}

		/// <summary>Renders given sync-setting declarations directly - the test and screenshot door.</summary>
		internal void UseSyncSettingsDecls(IReadOnlyList<WaterboxConfig.SettingDecl> declarations)
		{
			_syncSettings = new WaterboxCoreSyncSettings { Declarations = declarations };
			_settingsGrid.SelectedObject = _syncSettings;
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
				Label help = new()
				{
					AutoSize = true,
					Font = new Font(Font, FontStyle.Bold),
					Location = Pt(6, 20),
					Text = "(?)",
				};
				_tips.SetToolTip(help, slot.Help.Length is 0 ? "the core did not explain this slot" : slot.Help);
				ListBox list = new()
				{
					Anchor = AnchorStyles.Top | AnchorStyles.Left | AnchorStyles.Right,
					IntegralHeight = false,
					Location = Pt(30, 18),
					Size = new(UIHelper.ScaleX(380), UIHelper.ScaleY(70)),
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
				group.Controls.AddRange([ help, list, add, remove ]);
				_slotsHost.Controls.Add(group);
				_slotLists[slot.Id] = list;
				_slotGroups[slot.Id] = group;
				group.Tag = group.Text;
				y += 102;
			}
			RefreshSlotAvailability();
		}

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
			var exposed = EngineSlotsGate.Evaluate(_declaration.RawJson, CurrentSlotsJson());
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
			_status.Text = "";
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

		// ---- sync settings --------------------------------------------------------

		private bool BuildSettingsPage()
		{
			var core = ChosenCore!;
			try
			{
				using var pkg = EnginePackage.Open(core.Path);
				var cfg = WaterboxConfig.FromJson(pkg?.EntryText(WaterboxCoreFactory.ConfigFileName) ?? "");
				_cfg = cfg;
				_syncSettings = new WaterboxCoreSyncSettings();
				RefreshExposedSettings();
				_settingsGrid.SelectedObject = _syncSettings;
				return true;
			}
			catch (InvalidOperationException ex)
			{
				_status.Text = ex.Message;
				return false;
			}
		}

		/// <summary>
		/// The settings the DECISIONS expose (docs/project.md): the chosen files
		/// gate which sync settings exist at all (a Game Gear cart exposes its
		/// sound chip, a Genesis cart its own), and a setting may gate further
		/// settings - so the set is re-evaluated when a value changes.
		/// </summary>
		private void RefreshExposedSettings()
		{
			if (_cfg is null || _syncSettings is null) return;
			var effective = WaterboxCore.EffectiveSettingsFor(_cfg, null, _syncSettings);
			var exposed = Chimera.Emulation.Common.Engine.EngineSettingsGate.Evaluate(
				_cfg.RawSettingsJson,
				CurrentSlotsJson(),
				Newtonsoft.Json.JsonConvert.SerializeObject(effective));
			var all = _cfg.Settings ?? [ ];
			var declarations = exposed
				.Where(entry => entry.Index >= 0 && entry.Index < all.Count && all[entry.Index].Name == entry.Name)
				.Select(entry => all[entry.Index])
				.Where(static d => d.Sync)
				.ToList();
			var current = _syncSettings.Declarations;
			if (current is not null && current.Count == declarations.Count
				&& current.Zip(declarations, static (a, b) => ReferenceEquals(a, b)).All(static same => same))
			{
				return; // the exposed set did not change
			}
			_syncSettings.Declarations = declarations;
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
				_cfg ?? new WaterboxConfig(), null, _syncSettings);
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

		private const string CHECK = "\u2714 ";

		private void RenderFirmwareRows()
		{
			var selected = _firmwareList.SelectedIndices.Count is 0 ? -1 : _firmwareList.SelectedIndices[0];
			_firmwareList.BeginUpdate();
			_firmwareList.Items.Clear();
			foreach (var need in _firmwareNeeds)
			{
				var title = need.Decl?.DisplayName ?? need.Id;
				if (!string.IsNullOrEmpty(need.Decl?.Label)) title += $"  ({need.Decl!.Label})";
				ListViewItem item = new(need.Satisfied ? CHECK + title : title)
				{
					ToolTipText = need.Decl?.Description ?? "",
					ForeColor = need.Satisfied ? System.Drawing.Color.DarkGreen : System.Drawing.Color.Firebrick,
				};
				item.SubItems.Add(need.Decl?.Name ?? "");
				item.SubItems.Add(string.IsNullOrEmpty(need.Decl?.Sha1) ? "(your own dump)" : need.Decl!.Sha1);
				item.SubItems.Add(need.Satisfied
					? $"{CHECK}found: {Path.GetFileName(need.ChosenPath)}"
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
			using var project = EngineProject.New();
			project.Title = _title.Text.Trim();
			project.Description = _description.Text.Replace("\r\n", "\n");
			project.SetCore(core.Name, core.Version ?? "", core.Sha1 ?? "");
			// the platform, stamped at creation: reopening forces the pinned core
			// through the movie queue, and that check wants to know the system
			if (core.Systems.FirstOrDefault() is { Length: not 0 } system)
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
				ShowPage(1);
				return;
			}

			// the project records every EXPOSED sync setting explicitly, at its
			// effective value: the settings section is exactly the knobs these
			// decisions offered, nothing else
			if (_syncSettings?.Declarations is { Count: not 0 } exposedDecls)
			{
				Dictionary<string, object> recorded = new();
				foreach (var decl in exposedDecls)
				{
					recorded[decl.Name] = _syncSettings.Values is not null
						&& _syncSettings.Values.TryGetValue(decl.Name, out var value)
							? value
							: decl.DefaultValue;
				}
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
				ShowPage(1);
				return;
			}

			var path = _projectPath.Text;
			if (!path.EndsWith(".chimeraProject", StringComparison.OrdinalIgnoreCase)) path += ".chimeraProject";
			try
			{
				project.Save(path);
			}
			catch (InvalidOperationException ex)
			{
				_status.Text = ex.Message;
				return;
			}

			CreatedProjectPath = path;
			DialogResult = DialogResult.OK;
			Close();
		}
	}
}
