using System.Collections.Generic;
using System.Drawing;
using System.Globalization;
using System.IO;
using System.Linq;
using System.Reflection;
using System.ComponentModel;
using System.Windows.Forms;

using Chimera.Client.Common;
using Chimera.Common;
using Chimera.Common.CollectionExtensions;
using Chimera.Common.ReflectionExtensions;
using Chimera.Emulation.Common;
using Chimera.WinForms.Controls;

namespace Chimera.Client.GUI
{
	public class ToolManager : IToolLoader
	{
		static ToolManager()
		{
			// APIs are used by tools, so this seems like a good place to add API types to the manager.
			// Only API types not visible to Chimera.Client.Common need to be added here.
			ApiManager.AddApiType(typeof(ToolApi));
		}

		private readonly MainForm _owner;
		private Config _config;
		private readonly DisplayManager _displayManager;
		private readonly InputManager _inputManager;

		private IExternalApiProvider _apiProvider = null;

		private IEmulator _emulator;
		private readonly IMovieSession _movieSession;
		private IGameInfo _game;

		// TODO: merge ToolHelper code where logical
		// For instance, add an IToolForm property called UsesCheats, so that a UpdateFreezeRelatedTools() method can update all tools of this type
		// Also a UsesRam, and similar method
		private readonly List<IToolForm> _tools = new List<IToolForm>();

		/// <summary>
		/// Initializes a new instance of the <see cref="ToolManager"/> class.
		/// </summary>
		public ToolManager(
			MainForm owner,
			Config config,
			DisplayManager displayManager,
			InputManager inputManager,
			IEmulator emulator,
			IMovieSession movieSession,
			IGameInfo game)
		{
			_owner = owner;
			_config = config;
			_displayManager = displayManager;
			_inputManager = inputManager;
			_emulator = emulator;
			_movieSession = movieSession;
			_game = game;
		}

		private IExternalApiProvider GetOrInitApiProvider()
			=> _apiProvider ??= ApiManager.Restart(
				_emulator.ServiceProvider,
				_owner,
				_displayManager,
				_inputManager,
				_movieSession,
				this,
				_config,
				_emulator,
				_game,
				_owner.DialogController);

		public IToolForm Load(Type toolType, bool focus = true)
		{
			if (!typeof(IToolForm).IsAssignableFrom(toolType))
			{
				throw new ArgumentException(message: $"Type {toolType.Name} does not implement {nameof(IToolForm)}.", paramName: nameof(toolType));
			}
			var mi = typeof(ToolManager).GetMethod(nameof(Load), new[] { typeof(bool), typeof(string) })!.MakeGenericMethod(toolType);
			return (IToolForm) mi.Invoke(this, new object[] { focus, "" });
		}

		// If the form inherits ToolFormBase, it will set base properties such as Tools, Config, etc
		private void SetBaseProperties(IToolForm form)
		{
			if (form is not FormBase f) return;

			f.Config = _config;
			if (form is not ToolFormBase tool) return;
			tool.SetToolFormBaseProps(_displayManager, _inputManager, _owner, _movieSession, this, _game);

			// Lua is a special case, being the only tool that currently uses this.
			if (form is LuaConsole luaConsole) luaConsole.MainFormForApi = _owner;
		}

		public T Load<T>(bool focus = true)
			where T : class, IToolForm
		{
			if (!IsAvailable<T>()) return null;

			var existingTool = _tools.OfType<T>().FirstOrDefault();
			if (existingTool != null)
			{
				if (existingTool.IsLoaded)
				{
					if (focus)
					{
						existingTool.Show();
						existingTool.Focus();
					}

					return existingTool;
				}

				_tools.Remove(existingTool);
			}

			if (CreateInstance<T>() is not T newTool) return null;

			if (newTool is Form form) form.Owner = _owner;
			if (!ServiceInjector.UpdateServices(_emulator.ServiceProvider, newTool)) return null; //TODO pass `true` for `mayCache` when from Chimera assembly
			SetBaseProperties(newTool);
			var toolTypeName = typeof(T).FullName!;
			// auto settings
			if (newTool is IToolFormAutoConfig autoConfigTool)
			{
				AttachSettingHooks(autoConfigTool, _config.CommonToolSettings.GetValueOrPutNew(toolTypeName));
			}
			// custom settings
			if (HasCustomConfig(newTool))
			{
				InstallCustomConfig(newTool, _config.CustomToolSettings.GetValueOrPutNew(toolTypeName));
			}

			newTool.Restart();
			newTool.Show();
			return newTool;
		}

		private void RefreshSettings(Form form, ToolStripItemCollection menu, ToolDialogSettings settings, int idx)
		{
			((ToolStripMenuItem)menu[idx + 0]).Checked = settings.SaveWindowPosition;
			var stayOnTopItem = (ToolStripMenuItem)menu[idx + 1];
			stayOnTopItem.Checked = settings.TopMost;
			if (OSTailoredCode.IsUnixHost)
			{
				// This is the job of the WM, and is usually exposed in window decorations or a context menu on them
				stayOnTopItem.Enabled = false;
				stayOnTopItem.Visible = false;
			}
			else
			{
				form.TopMost = settings.TopMost;
			}
			((ToolStripMenuItem)menu[idx + 2]).Checked = settings.FloatingWindow;

			// do we need to do this OnShown() as well?
			form.Owner = settings.FloatingWindow ? null : _owner;
		}

		private void AddCloseButton(ToolStripMenuItem subMenu, Form form)
		{
			if (subMenu.DropDownItems.Count > 0)
			{
				subMenu.DropDownItems.Add(new ToolStripSeparatorEx());
			}

			var closeMenuItem = new ToolStripMenuItem
			{
				Name = "CloseBtn",
				Text = "&Close",
				ShortcutKeyDisplayString = "Alt+F4",
			};

			closeMenuItem.Click += (o, e) => { form.Close(); };
			subMenu.DropDownItems.Add(closeMenuItem);
		}

		private void AttachSettingHooks(IToolFormAutoConfig tool, ToolDialogSettings settings)
		{
			var form = (Form)tool;
			ToolStripItemCollection dest = null;
			var oldSize = form.Size; // this should be the right time to grab this size
			foreach (Control c in form.Controls)
			{
				if (c is MenuStrip ms)
				{
					// where the window options go: a tool with a Window menu says so
					// by having one, and they belong there rather than under
					// Settings, which is about the tool rather than its window
					ToolStripItemCollection window = null, settingsMenu = null;
					foreach (ToolStripMenuItem submenu in ms.Items)
					{
						if (submenu.Text.Contains("Window")) window = submenu.DropDownItems;
						else if (submenu.Text.Contains("Settings")) settingsMenu = submenu.DropDownItems;
						else if (submenu.Text.Contains("File")) AddCloseButton(submenu, form);
					}

					// A Window menu is all one kind of thing, so what is appended
					// needs no rule drawn across it. A Settings menu is the tool's
					// own settings and then its window's, which does.
					dest = window ?? settingsMenu;
					if (window is null && settingsMenu is { Count: > 0 }) settingsMenu.Add(new ToolStripSeparator());

					if (dest == null)
					{
						var submenu = new ToolStripMenuItem("&Settings");
						ms.Items.Add(submenu);
						dest = submenu.DropDownItems;
					}

					break;
				}
			}

			if (dest == null)
			{
				throw new InvalidOperationException($"{nameof(IToolFormAutoConfig)} must have menu to bind to! (need {nameof(Form.MainMenuStrip)} or other {nameof(MenuStrip)} w/ menu labelled \"Settings\")");
			}

			int idx = dest.Count;

			dest.Add("Save Window &Position");
			dest.Add("Stay on &Top");
			dest.Add("&Float From Main Window");
			dest.Add("Restore &Defaults");

			RefreshSettings(form, dest, settings, idx);

			if (settings.UseWindowPosition && IsOnScreen(settings.TopLeft))
			{
				form.StartPosition = FormStartPosition.Manual;
				form.Location = settings.WindowPosition;
			}

			if (settings.UseWindowSize)
			{
				if (form.FormBorderStyle is FormBorderStyle.Sizable or FormBorderStyle.SizableToolWindow)
				{
					form.Size = settings.WindowSize;
				}
			}

			form.FormClosing += (o, e) =>
			{
				if (form.WindowState == FormWindowState.Normal)
				{
					settings.Wndx = form.Location.X;
					settings.Wndy = form.Location.Y;
					if (settings.Wndx < 0)
					{
						settings.Wndx = 0;
					}

					if (settings.Wndy < 0)
					{
						settings.Wndy = 0;
					}

					settings.Width = form.Right - form.Left; // why not form.Size.Width?
					settings.Height = form.Bottom - form.Top;
				}
			};

			dest[idx + 0].Click += (o, e) =>
			{
				bool val = !((ToolStripMenuItem)o).Checked;
				settings.SaveWindowPosition = val;
				((ToolStripMenuItem)o).Checked = val;
			};
			dest[idx + 1].Click += (o, e) =>
			{
				bool val = !((ToolStripMenuItem)o).Checked;
				settings.TopMost = val;
				((ToolStripMenuItem)o).Checked = val;
				form.TopMost = val;
			};
			dest[idx + 2].Click += (o, e) =>
			{
				bool val = !((ToolStripMenuItem)o).Checked;
				settings.FloatingWindow = val;
				((ToolStripMenuItem)o).Checked = val;
				form.Owner = val ? null : _owner;
			};
			dest[idx + 3].Click += (o, e) =>
			{
				settings.RestoreDefaults();
				RefreshSettings(form, dest, settings, idx);
				form.Size = oldSize;

				form.GetType().GetMethodsWithAttrib(typeof(RestoreDefaultsAttribute))
					.FirstOrDefault()?.Invoke(form, Array.Empty<object>());
			};
		}

		private static bool HasCustomConfig(IToolForm tool)
		{
			return tool.GetType().GetPropertiesWithAttrib(typeof(ConfigPersistAttribute)).Any();
		}

		private static void InstallCustomConfig(IToolForm tool, Dictionary<string, object> data)
		{
			Type type = tool.GetType();
			var props = type.GetPropertiesWithAttrib(typeof(ConfigPersistAttribute)).ToList();
			if (props.Count == 0)
			{
				return;
			}

			foreach (var prop in props)
			{
				if (data.TryGetValue(prop.Name, out var val))
				{
					if (val is string str && prop.PropertyType != typeof(string))
					{
						// if a type has a TypeConverter, and that converter can convert to string,
						// that will be used in place of object markup by JSON.NET

						// but that doesn't work with $type metadata, and JSON.NET fails to fall
						// back on regular object serialization when needed.  so try to undo a TypeConverter
						// operation here
						var converter = TypeDescriptor.GetConverter(prop.PropertyType);
						val = converter.ConvertFromString(null, CultureInfo.InvariantCulture, str);
					}
					else if (val is not bool && prop.PropertyType.IsPrimitive)
					{
						// numeric constants are similarly hosed
						val = Convert.ChangeType(val, prop.PropertyType, CultureInfo.InvariantCulture);
					}

					prop.SetValue(tool, val, null);
				}
			}

			((Form)tool).FormClosing += (o, e) => SaveCustomConfig(tool, data, props);
		}

		private static void SaveCustomConfig(IToolForm tool, Dictionary<string, object> data, List<PropertyInfo> props)
		{
			data.Clear();
			foreach (var prop in props)
			{
				data.Add(prop.Name, prop.GetValue(tool, BindingFlags.GetProperty, Type.DefaultBinder, null, CultureInfo.InvariantCulture));
			}
		}

		/// <summary>
		/// Determines whether a given IToolForm is already loaded
		/// </summary>
		/// <typeparam name="T">Type of tool to check</typeparam>
		/// <remarks>yo why do we have 4 versions of this, each with slightly different behaviour in edge cases --yoshi</remarks>
		public bool IsLoaded<T>() where T : IToolForm
			=> _tools.Find(static t => t is T)?.IsActive is true;

		public bool IsLoaded(Type toolType)
			=> _tools.Find(t => t.GetType() == toolType)?.IsActive is true;

		public bool IsOnScreen(Point topLeft)
			=> DrawingExtensions.BoundsOfDisplayContaining(topLeft) is not null;

		/// <summary>
		/// Returns true if an instance of T exists
		/// </summary>
		/// <typeparam name="T">Type of tool to check</typeparam>
		public bool Has<T>() where T : IToolForm
			=> _tools.Exists(static t => t is T && t.IsActive);

		/// <returns><see langword="true"/> iff a tool of the given <paramref name="toolType"/> is <see cref="IToolForm.IsActive">active</see></returns>
		public bool Has(Type toolType)
			=> typeof(IToolForm).IsAssignableFrom(toolType)
				&& _tools.Exists(t => toolType.IsInstanceOfType(t) && t.IsActive);

		/// <summary>
		/// Gets the instance of T, or creates and returns a new instance
		/// </summary>
		/// <typeparam name="T">Type of tool to get</typeparam>
		public IToolForm Get<T>() where T : class, IToolForm
		{
			return Load<T>(false);
		}

		/// <summary>
		/// Returns the first tool of type <typeparamref name="T"/> that fulfills the given condition
		/// </summary>
		/// <param name="condition">The condition to check for</param>
		/// <typeparam name="T">Type of tools to check</typeparam>
		public T FirstOrNull<T>(Predicate<T> condition) where T : class
		{
			foreach (var tool in _tools) // not bothering to copy here since `condition` is expected to have no side-effects
			{
				if (tool.IsActive && tool is T specialTool && condition(specialTool))
				{
					return specialTool;
				}
			}

			return null;
		}

		/// <summary>
		/// returns the instance of <paramref name="toolType"/>, regardless of whether it's loaded,<br/>
		/// but doesn't create and load a new instance if it's not found
		/// </summary>
		/// <remarks>
		/// does not check <paramref name="toolType"/> is a class implementing <see cref="IToolForm"/>;<br/>
		/// you may pass any class or interface
		/// </remarks>
		public IToolForm/*?*/ LazyGet(Type toolType)
			=> _tools.Find(toolType.IsInstanceOfType);

		internal static readonly IDictionary<Type, (Image/*?*/ Icon, string Name)> IconAndNameCache = new Dictionary<Type, (Image/*?*/ Icon, string Name)>
		{
			[typeof(LogWindow)] = (LogWindow.ToolIcon.ToBitmap(), "Log Window"), // can't do this lazily, see https://github.com/TASEmulators/BizHawk/issues/2741#issuecomment-1421014589
		};

		private static PropertyInfo/*?*/ _PInfo_FormBase_WindowTitleStatic = null;

		private static PropertyInfo PInfo_FormBase_WindowTitleStatic
			=> _PInfo_FormBase_WindowTitleStatic ??= typeof(FormBase).GetProperty("WindowTitleStatic", BindingFlags.NonPublic | BindingFlags.Instance);

		private static bool CaptureIconAndName(object tool, Type toolType, ref Image/*?*/ icon, ref string/*?*/ name)
		{
			if (IconAndNameCache.ContainsKey(toolType)) return true;
			Form winform = null;
			if (name is null)
			{
				winform = tool as FormBase;
				if (winform is not null)
				{
					// then `tool is Formbase` and this getter call is safe
					name = (string) PInfo_FormBase_WindowTitleStatic.GetValue(tool);
					// could do `tool._windowTitleStatic ??= tool.WindowTitleStatic`, but the getter's only being run 1 extra time here anyway so not worth the LOC
				}
				winform ??= tool as Form;
				if (winform is not null)
				{
					icon = winform.Icon?.ToBitmap();
					name ??= winform.Name;
				}
			}
			if (!string.IsNullOrWhiteSpace(name))
			{
				IconAndNameCache[toolType] = (icon, name);
				return true;
			}
			// else don't cache anything
			name = winform?.Text;
			return false;
		}

		private static void CaptureIconAndName(object tool, Type toolType)
		{
			Image/*?*/ icon = null;
			string/*?*/ name = null;
			CaptureIconAndName(tool, toolType, ref icon, ref name);
		}

		public (Image/*?*/ Icon, string Name) GetIconAndNameFor(Type toolType)
		{
			if (IconAndNameCache.TryGetValue(toolType, out var tuple)) return tuple;
			Image/*?*/ icon = null;
			var name = toolType.GetCustomAttribute<SpecializedToolAttribute>()?.DisplayName; //TODO codegen ToolIcon and WindowTitleStatic from [Tool] or some new attribute -- Bitmap..ctor(Type, string)
			var instance = LazyGet(toolType);
			if (instance is not null)
			{
				if (CaptureIconAndName(instance, toolType, ref icon, ref name)) return (icon, name);
				// else fall through
			}
			return (
				icon ?? (toolType.GetProperty("ToolIcon", BindingFlags.Public | BindingFlags.Static)?.GetValue(null) as Icon)?.ToBitmap(),
				string.IsNullOrWhiteSpace(name) ? toolType.Name : name);
		}

		public IEnumerable<Type> AvailableTools => ReflectionCache.Types
			.Where(t => !t.IsInterface && typeof(IToolForm).IsAssignableFrom(t) && IsAvailable(t));

		/// <summary>
		/// Calls UpdateValues() on an instance of T, if it exists
		/// </summary>
		/// <typeparam name="T">Type of tool to update</typeparam>
		public void UpdateValues<T>() where T : IToolForm
		{
			var tool = _tools.OfType<T>().FirstOrDefault();
			if (tool?.IsActive is true)
			{
				tool.UpdateValues(ToolFormUpdateType.General);
			}
		}

		public void Restart(Config config, IEmulator emulator, IGameInfo game)
		{
			_config = config;
			_emulator = emulator;
			_game = game;
			_apiProvider = null;

			var unavailable = new List<IToolForm>();

			foreach (var tool in _tools.ToArray()) // copy because a tool may open another
			{
				SetBaseProperties(tool);
				if (ServiceInjector.UpdateServices(_emulator.ServiceProvider, tool))
				{
					if (tool.IsActive) tool.Restart();
				}
				else
				{
					unavailable.Add(tool);
				}
			}

			foreach (var tool in unavailable)
			{
				tool.Close();
				_tools.Remove(tool);
			}
		}

		/// <summary>
		/// Calls Restart() on an instance of T, if it exists
		/// </summary>
		/// <typeparam name="T">Type of tool to restart</typeparam>
		public void Restart<T>() where T : IToolForm
			=> _tools.OfType<T>().FirstOrDefault()?.Restart();

		/// <summary>
		/// Runs AskSave on every tool dialog, false is returned if any tool returns false
		/// </summary>
		public bool AskSave()
		{
			if (_config.SuppressAskSave) // User has elected to not be nagged
			{
				return true;
			}

			return _tools.TrueForAll(tool => !tool.IsActive || tool.AskSaveChanges());
		}

		/// <summary>
		/// If T exists, this call will close the tool, and remove it from memory
		/// </summary>
		/// <typeparam name="T">Type of tool to close</typeparam>
		public void Close<T>() where T : IToolForm
		{
			var tool = _tools.OfType<T>().FirstOrDefault();
			if (tool != null)
			{
				tool.Close();
				_tools.Remove(tool);
			}
		}

		public void Close(Type toolType)
		{
			var tool = _tools.Find(toolType.IsInstanceOfType);
			if (tool != null)
			{
				tool.Close();
				_tools.Remove(tool);
			}
		}

		public void Close()
		{
			var toolsCopy = _tools.ToArray();
			_tools.Clear();
			foreach (var t in toolsCopy)
			{
				try
				{
					t.Close();
				}
				catch (Exception e)
				{
					Console.WriteLine($"caught while calling Form.Close on tool: {e}");
				}
			}
		}

		/// <summary>Creates a new instance of an IToolForm and returns it.</summary>
		private IToolForm CreateInstance<T>()
			where T : IToolForm
			=> CreateInstance(typeof(T));

		/// <summary>Creates a new instance of an IToolForm and returns it.</summary>
		private IToolForm CreateInstance(Type toolType)
		{
			var tool = (IToolForm) Activator.CreateInstance(toolType);
			CaptureIconAndName(tool, toolType);
			_tools.Add(tool);
			return tool;
		}

		public void UpdateToolsBefore()
		{
			foreach (var tool in _tools.ToArray()) // copy because a tool may open another
			{
				if (tool.IsActive)
				{
					tool.UpdateValues(ToolFormUpdateType.PreFrame);
				}
			}
		}

		public void UpdateToolsAfter()
		{
			foreach (var tool in _tools.ToArray()) // copy because a tool may open another
			{
				if (tool.IsActive)
				{
					tool.UpdateValues(ToolFormUpdateType.PostFrame);
				}
			}
		}

		public void FastUpdateBefore()
		{
			foreach (var tool in _tools.ToArray()) // copy because a tool may open another
			{
				if (tool.IsActive)
				{
					tool.UpdateValues(ToolFormUpdateType.FastPreFrame);
				}
			}
		}

		public void FastUpdateAfter()
		{
			foreach (var tool in _tools.ToArray()) // copy because a tool may open another
			{
				if (tool.IsActive)
				{
					tool.UpdateValues(ToolFormUpdateType.FastPostFrame);
				}
			}
		}

		public void HandleHotkeyUpdate()
		{
			foreach (var tool in _tools)
			{
				if (tool.IsActive && tool is ToolFormBase toolForm)
				{
					toolForm.HandleHotkeyUpdate();
				}
			}
		}

		public void OnPauseToggle(bool newPauseState)
		{
			foreach (var tool in _tools)
			{
				if (tool.IsActive && tool is ToolFormBase toolForm)
				{
					toolForm.OnPauseToggle(newPauseState);
				}
			}
		}

		private static readonly IReadOnlyCollection<string> PossibleToolTypeNames = ReflectionCache.Types
			.Select(static t => t.AssemblyQualifiedName).ToHashSet();

		public bool IsAvailable(Type tool)
		{
			if (!ServiceInjector.IsAvailable(_emulator.ServiceProvider, tool)) return false;
			if (!PossibleToolTypeNames.Contains(tool.AssemblyQualifiedName)) return false; // not a tool

			ToolAttribute attr = tool.GetCustomAttributes(false).OfType<ToolAttribute>().SingleOrDefault();
			if (attr == null)
			{
				return true; // no ToolAttribute on given type -> assumed all supported
			}

			return !attr.UnsupportedCores.Contains(_emulator.Attributes().CoreName) // not unsupported
				&& (!attr.SupportedSystems.Any() || attr.SupportedSystems.Contains(_emulator.SystemId)); // supported (no supported list -> assumed all supported)
		}

		public bool IsAvailable<T>() => IsAvailable(typeof(T));

		// Note: Referencing these properties creates an instance of the tool and persists it.  They should be referenced by type if this is not desired

		private T GetTool<T>() where T : class, IToolForm, new()
		{
			T tool = _tools.OfType<T>().FirstOrDefault();
			if (tool != null)
			{
				if (tool.IsActive)
				{
					return tool;
				}

				_tools.Remove(tool);
			}
			tool = new T();
			CaptureIconAndName(tool, typeof(T));
			_tools.Add(tool);
			return tool;
		}

		public RamWatch RamWatch => GetTool<RamWatch>();

		public RamSearch RamSearch => GetTool<RamSearch>();

		public HexEditor HexEditor => GetTool<HexEditor>();

		public LuaConsole LuaConsole => GetTool<LuaConsole>();

		public TAStudio TAStudio => GetTool<TAStudio>();

		public void LoadRamWatch(bool loadDialog)
		{
			if (IsLoaded<RamWatch>() && !_config.DisplayRamWatch)
			{
				return;
			}

			Load<RamWatch>();

			if (IsAvailable<RamWatch>()) // Just because we attempted to load it, doesn't mean it was, the current core may not have the correct dependencies
			{
				if (_config.RecentWatches.AutoLoad && !_config.RecentWatches.Empty)
				{
					RamWatch.LoadFileFromRecent(_config.RecentWatches.MostRecent);
				}

				if (!loadDialog)
				{
					Get<RamWatch>().Close();
				}
			}
		}

		/// <summary>The frozen-address list changed, so everything that shows frozen addresses is stale.</summary>
		public void UpdateFreezeRelatedTools(object sender, CheatCollection.CheatListEventArgs e)
		{
			if (!_emulator.HasMemoryDomains())
			{
				return;
			}

			UpdateValues<RamWatch>();
			UpdateValues<RamSearch>();
			UpdateValues<HexEditor>();

			_owner.UpdateCheatStatus();
		}
	}
}
