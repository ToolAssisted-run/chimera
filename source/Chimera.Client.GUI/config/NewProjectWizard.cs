#nullable enable

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
		private readonly ToolTip _tips = new();

		// page 3
		private readonly PropertyGrid _settingsGrid;
		private WaterboxCoreSyncSettings? _syncSettings;
		private WaterboxConfig? _cfg;

		// page 4, computed from every earlier decision (docs/project.md): each
		// requirement lists its candidates openly; SHA1 is the identity, names
		// are hints; the firmware folder answers first, the user only when it
		// cannot
		private readonly Func<string, string?> _pickFirmwareFile;
		private readonly IReadOnlyList<string> _firmwareSearchDirs;
		private readonly Func<string, string, string?> _rememberedFirmwarePath;
		private readonly TreeView _firmwareTree;
		private readonly Button _firmwareUseButton;
		private readonly Button _firmwareSetButton;
		private readonly Button _firmwareClearButton;
		private List<FirmwareNeed> _firmwareNeeds = new();
		private IReadOnlyList<FirmwareLocator.IndexedFile> _firmwareIndex = [ ];

		private sealed class FirmwareNeed
		{
			public string Id = "";
			public bool Required;
			public CoreFirmwareDecl? Decl;
			public IReadOnlyList<FirmwareLocator.Match> Found = [ ];
			public string? ChosenPath;
			public string? ChosenSha1;
			public int ChosenCandidate = -1; // -1 = nothing chosen, or a custom (non-candidate) file

			public bool Satisfied => ChosenPath is not null && ChosenCandidate >= 0;
			public bool HasCustom => ChosenPath is not null && ChosenCandidate < 0;
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
			p3.Controls.Add(_settingsGrid);

			// ---- page 4: firmware, decided by everything chosen above ------------
			var p4 = _pages[3];
			p4.Controls.Add(MakeLabel("The firmware these decisions call for. Each requirement lists the dumps that\nsatisfy it - the hash is the identity, the file name only a hint. The Firmware\nfolder was searched already; set a file yourself where nothing was found.", 8, 8));
			_firmwareTree = new TreeView
			{
				Anchor = AnchorStyles.Top | AnchorStyles.Bottom | AnchorStyles.Left | AnchorStyles.Right,
				FullRowSelect = true,
				HideSelection = false,
				Location = Pt(8, 56),
				ShowNodeToolTips = true,
				Size = new(UIHelper.ScaleX(544), UIHelper.ScaleY(282)),
			};
			_firmwareTree.AfterSelect += (_, _) => UpdateFirmwareButtons();
			_firmwareTree.NodeMouseDoubleClick += (_, _) => UseSelectedFirmware();
			_firmwareUseButton = new Button { AutoSize = true, Location = Pt(8, 344), Text = "Use Selected" };
			_firmwareUseButton.Click += (_, _) => UseSelectedFirmware();
			_firmwareSetButton = new Button { AutoSize = true, Location = Pt(110, 344), Text = "Select File..." };
			_firmwareSetButton.Click += (_, _) => SetFirmwareFile();
			_firmwareClearButton = new Button { AutoSize = true, Location = Pt(212, 344), Text = "Clear" };
			_firmwareClearButton.Click += (_, _) => ClearFirmwareFile();
			p4.Controls.AddRange([ _firmwareTree, _firmwareUseButton, _firmwareSetButton, _firmwareClearButton ]);

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
				};
				group.Controls.AddRange([ help, list, add, remove ]);
				_slotsHost.Controls.Add(group);
				_slotLists[slot.Id] = list;
				y += 102;
			}
		}

		/// <summary>One picked file, kept as its canonical (bare) name plus where it is now.</summary>
		private sealed record PickedFile(string Name, string Path)
		{
			public override string ToString() => Name;
		}

		/// <summary>Adds a file to a slot's list; public in behaviour so tests can drive the form without a picker.</summary>
		public void AddFileToSlot(string slotId, string path)
		{
			if (!_slotLists.TryGetValue(slotId, out var list)) return;
			var name = Path.GetFileName(path);
			if (list.Items.OfType<PickedFile>().Any(f => f.Name == name)) return;
			list.Items.Add(new PickedFile(name, path));
		}

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
				if (count < slot.Min) return $"{slot.Title}: needs {slot.CardinalityText}, has {count}";
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
				var declarations = (cfg?.Settings ?? [ ]).Where(static d => d.Sync).ToList();
				_syncSettings = new WaterboxCoreSyncSettings { Declarations = declarations };
				_settingsGrid.SelectedObject = _syncSettings;
				return true;
			}
			catch (InvalidOperationException ex)
			{
				_status.Text = ex.Message;
				return false;
			}
		}

		/// <summary>Renders given firmware needs directly - the test and screenshot door.</summary>
		internal void UseFirmwareNeeds(
			WaterboxConfig cfg,
			IReadOnlyList<(string Id, bool Required)> needed,
			IReadOnlyList<FirmwareLocator.IndexedFile>? index = null)
		{
			_cfg = cfg;
			_firmwareIndex = index ?? [ ];
			BuildFirmwareNeeds(needed);
			RenderFirmwareTree();
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
			RenderFirmwareTree();
		}

		private void BuildFirmwareNeeds(IReadOnlyList<(string Id, bool Required)> needed)
		{
			_firmwareNeeds = needed.Select(entry =>
			{
				FirmwareNeed need = new()
				{
					Id = entry.Id,
					Required = entry.Required,
					Decl = DeclFor(entry.Id),
				};
				need.Found = need.Decl is null ? [ ] : FirmwareLocator.MatchesFor(need.Decl, _firmwareIndex);
				// auto-satisfy from what was found; a remembered file wins over
				// a folder hit so yesterday's choice stays today's
				var remembered = _rememberedFirmwarePath(ChosenCoreName ?? "", entry.Id);
				var auto = need.Found.FirstOrDefault(m => m.Path == remembered) ?? need.Found.FirstOrDefault();
				if (auto is not null)
				{
					need.ChosenPath = auto.Path;
					need.ChosenSha1 = auto.Sha1;
					need.ChosenCandidate = auto.CandidateIndex;
				}
				return need;
			}).ToList();
		}

		private CoreFirmwareDecl? DeclFor(string id)
			=> (_cfg?.Firmware ?? [ ]).FirstOrDefault(d => d.Id == id);

		private const string CHECK = "\u2714 ";

		private void RenderFirmwareTree()
		{
			_firmwareTree.BeginUpdate();
			_firmwareTree.Nodes.Clear();
			for (var n = 0; n < _firmwareNeeds.Count; n++)
			{
				var need = _firmwareNeeds[n];
				var title = need.Decl?.DisplayName ?? need.Id;
				var suffix = need.Required ? "required" : "optional";
				TreeNode reqNode = new(
					need.Satisfied ? $"{CHECK}{title}  ({suffix} - satisfied)"
					: need.HasCustom ? $"{title}  ({suffix} - custom dump, unrecognised)"
					: $"{title}  ({suffix} - not satisfied)")
				{
					Tag = ValueTuple.Create(n, -1, (string)null),
					ToolTipText = need.Decl?.Description ?? "",
					ForeColor = need.Satisfied ? System.Drawing.Color.DarkGreen
						: need.Required ? System.Drawing.Color.Firebrick
						: System.Drawing.SystemColors.ControlText,
				};

				var candidates = need.Decl?.AllCandidates ?? [ ];
				for (var c = 0; c < candidates.Count; c++)
				{
					var inUse = need.ChosenCandidate == c;
					var matches = need.Found.Where(m => m.CandidateIndex == c).ToList();
					var text = candidates[c].DisplayText;
					if (inUse) text = $"{CHECK}{text}  -  in use: {Path.GetFileName(need.ChosenPath)}";
					else if (matches.Count is not 0) text += $"  -  found: {string.Join(", ", matches.Select(static m => Path.GetFileName(m.Path)))}";
					TreeNode candNode = new(text)
					{
						Tag = ValueTuple.Create(n, c, matches.FirstOrDefault()?.Path),
						ForeColor = inUse ? System.Drawing.Color.DarkGreen : System.Drawing.SystemColors.ControlText,
					};
					// several files can satisfy one requirement; each is selectable
					foreach (var match in matches)
					{
						var active = inUse && match.Path == need.ChosenPath;
						candNode.Nodes.Add(new TreeNode(
							(active ? CHECK : "") + match.Path)
						{
							Tag = ValueTuple.Create(n, c, match.Path),
							ForeColor = active ? System.Drawing.Color.DarkGreen : System.Drawing.SystemColors.ControlText,
						});
					}
					reqNode.Nodes.Add(candNode);
				}
				if (need.HasCustom)
				{
					reqNode.Nodes.Add(new TreeNode($"custom: {need.ChosenPath}  {need.ChosenSha1}")
					{
						Tag = ValueTuple.Create(n, -1, need.ChosenPath),
					});
				}
				reqNode.Expand();
				_firmwareTree.Nodes.Add(reqNode);
			}
			if (_firmwareNeeds.Count is 0)
			{
				_firmwareTree.Nodes.Add(new TreeNode("(these decisions need no firmware)"));
			}
			_firmwareTree.EndUpdate();
			UpdateFirmwareButtons();
			UpdateCreateEnabled();
		}

		private (FirmwareNeed Need, int Candidate, string? Path)? SelectedFirmware()
		{
			if (_firmwareTree.SelectedNode?.Tag is not ValueTuple<int, int, string> tag) return null;
			var (n, c, path) = tag;
			if (n < 0 || n >= _firmwareNeeds.Count) return null;
			return (_firmwareNeeds[n], c, path);
		}

		private void UpdateFirmwareButtons()
		{
			var selected = SelectedFirmware();
			_firmwareSetButton.Enabled = selected is not null;
			_firmwareUseButton.Enabled = selected is { Path: not null } sel
				&& !(sel.Need.ChosenPath == sel.Path);
			_firmwareClearButton.Enabled = selected is { } s && s.Need.ChosenPath is not null;
		}

		/// <summary>Activates the found file (or candidate's file) the tree has selected.</summary>
		private void UseSelectedFirmware()
		{
			if (SelectedFirmware() is not { Path: not null } sel) return;
			var match = sel.Need.Found.FirstOrDefault(m => m.Path == sel.Path);
			if (match is null) return;
			sel.Need.ChosenPath = match.Path;
			sel.Need.ChosenSha1 = match.Sha1;
			sel.Need.ChosenCandidate = match.CandidateIndex;
			RenderFirmwareTree();
		}

		private void SetFirmwareFile()
		{
			if (SelectedFirmware() is not { } sel) return;
			var path = _pickFirmwareFile($"Locate {sel.Need.Decl?.DisplayName ?? sel.Need.Id}");
			if (path is null) return;
			ProvideFirmware(sel.Need.Id, path);
		}

		/// <summary>
		/// Points one firmware id at a file the user chose - allowed even when
		/// something was already found. The hash decides everything: a required
		/// entry only accepts a listed candidate; an optional one also takes a
		/// custom dump, recorded as such.
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
			var candidates = need.Decl?.AllCandidates ?? [ ];
			var matched = -1;
			for (var c = 0; c < candidates.Count; c++)
			{
				if (sha1.Equals(candidates[c].Sha1, StringComparison.OrdinalIgnoreCase)) { matched = c; break; }
			}
			if (matched < 0 && need.Required)
			{
				_status.Text = $"{System.IO.Path.GetFileName(path)} matches none of the listed dumps for {need.Decl?.DisplayName ?? id} (hash {sha1})";
				return;
			}
			need.ChosenPath = System.IO.Path.GetFullPath(path);
			need.ChosenSha1 = sha1;
			need.ChosenCandidate = matched;
			_status.Text = "";
			RenderFirmwareTree();
		}

		private void ClearFirmwareFile()
		{
			if (SelectedFirmware() is not { } sel) return;
			sel.Need.ChosenPath = null;
			sel.Need.ChosenSha1 = null;
			sel.Need.ChosenCandidate = -1;
			RenderFirmwareTree();
		}

		private string? MissingRequiredFirmware()
		{
			foreach (var need in _firmwareNeeds)
			{
				if (need.Required && !need.Satisfied) return need.Decl?.DisplayName ?? need.Id;
			}
			return null;
		}

		/// <summary>What the user ended up with, for the owner to remember paths per core (Config.CoreFirmware).</summary>
		public IReadOnlyDictionary<string, string> ProvidedFirmwarePaths
			=> _firmwareNeeds.Where(static n => n.ChosenPath is not null)
				.ToDictionary(static n => n.Id, static n => n.ChosenPath!);

		/// <summary>the requirement states, for tests</summary>
		public bool FirmwareSatisfied(string id)
			=> _firmwareNeeds.FirstOrDefault(n => n.Id == id)?.Satisfied is true;

		public (string? Path, string? Sha1, int Candidate) ChosenFirmware(string id)
		{
			var need = _firmwareNeeds.FirstOrDefault(n => n.Id == id);
			return (need?.ChosenPath, need?.ChosenSha1, need?.ChosenCandidate ?? -1);
		}

		// ---- creation -------------------------------------------------------------

		private void Create()
		{
			var core = ChosenCore!;
			using var project = EngineProject.New();
			project.Title = _title.Text.Trim();
			project.Description = _description.Text.Replace("\r\n", "\n");
			project.SetCore(core.Name, core.Version ?? "", core.Sha1 ?? "");

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

			if (_syncSettings?.Values is { Count: not 0 } values)
			{
				project.SetSettingsJson(Newtonsoft.Json.JsonConvert.SerializeObject(values));
			}

			{
				Newtonsoft.Json.Linq.JArray pins = new();
				foreach (var need in _firmwareNeeds)
				{
					if (need.ChosenSha1 is null) continue;
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
