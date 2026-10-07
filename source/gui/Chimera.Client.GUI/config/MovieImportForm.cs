#nullable enable

using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Windows.Forms;

using Chimera.Client.Common;
using Chimera.Emulation.Common.Waterbox;

namespace Chimera.Client.GUI
{
	/// <summary>
	/// The import dialog for a movie made elsewhere - a Doom demo - rendered from
	/// what the core declares (<see cref="WaterboxConfig.MovieImport"/>), the way
	/// the settings grid is rendered from its settings: no core's specifics live
	/// here. It asks for the movie, the files it was made with and the import
	/// options, and has the core read them (<see cref="MovieImportRequest"/>).
	///
	/// A refusal is shown here and the dialog stays open with everything still
	/// picked, so the file the core named can be added and the import tried again.
	/// An answer closes it: the project is built from that answer by the
	/// frontend's own project creation, not here.
	/// </summary>
	public sealed class MovieImportForm : FormBase
	{
		private readonly WaterboxConfig.MovieImportDecl _decl;
		private readonly Func<string, IReadOnlyList<string>, bool, string[]> _pickFiles;
		private readonly Func<MovieImportRequest, (MovieImportAnswer? Answer, string? Failure)> _import;

		private readonly TextBox _movie;
		private readonly List<(WaterboxConfig.MovieImportFile Decl, ListBox? List, TextBox? Single)> _inputs = new();
		private readonly List<(WaterboxConfig.SettingDecl Decl, CheckBox Box)> _options = new();
		private readonly Label _status;
		private readonly Button _importButton;
		private readonly ToolTip _tips = new();

		/// <summary>Every picked file by its name - the name it is mounted under - and where it is.</summary>
		private readonly Dictionary<string, string> _paths = new(StringComparer.Ordinal);

		private readonly string _title;

		protected override string WindowTitleStatic => _title;

		/// <summary>What the core answered, once it has; the dialog closes with OK then.</summary>
		public MovieImportAnswer? Answer { get; private set; }

		/// <summary>What was asked of the core - its files carry the paths the project is built from.</summary>
		public MovieImportRequest? Request { get; private set; }

		/// <param name="pickFiles">(title, extensions, several) - the files somebody chose; a dialog in the application, a stub in tests</param>
		/// <param name="import">runs the core's importer on a request; its answer, or why there is none</param>
		public MovieImportForm(
			WaterboxConfig.MovieImportDecl decl,
			Func<string, IReadOnlyList<string>, bool, string[]> pickFiles,
			Func<MovieImportRequest, (MovieImportAnswer? Answer, string? Failure)> import)
		{
			_decl = decl;
			_pickFiles = pickFiles;
			_import = import;
			_title = (decl.Menu ?? "Import Movie").TrimEnd('.', ' ').Replace("&", "");

			SuspendLayout();
			FormBorderStyle = FormBorderStyle.FixedDialog;
			MaximizeBox = false;
			MinimizeBox = false;
			StartPosition = FormStartPosition.CenterParent;

			var y = 10;
			Label intro = new()
			{
				Location = new(UIHelper.ScaleX(12), UIHelper.ScaleY(y)),
				Size = new(UIHelper.ScaleX(536), UIHelper.ScaleY(36)),
				Text = "The core reads these files and suggests the project they make: its machine, settings, firmware, "
					+ "files and inputs. You then check it in the New Project wizard and choose where it is saved.",
			};
			Controls.Add(intro);
			y += 44;

			Label RowLabel(string text, int top)
			{
				Label label = new()
				{
					AutoEllipsis = true,
					Location = new(UIHelper.ScaleX(12), UIHelper.ScaleY(top + 4)),
					Size = new(UIHelper.ScaleX(120), UIHelper.ScaleY(20)),
					Text = text + ":",
				};
				Controls.Add(label);
				return label;
			}
			Button SideButton(string text, int top, Action click)
			{
				Button button = new()
				{
					Location = new(UIHelper.ScaleX(472), UIHelper.ScaleY(top)),
					Size = new(UIHelper.ScaleX(76), UIHelper.ScaleY(24)),
					Text = text,
				};
				button.Click += (_, _) => click();
				Controls.Add(button);
				return button;
			}
			TextBox SingleBox(int top)
			{
				TextBox box = new()
				{
					Location = new(UIHelper.ScaleX(136), UIHelper.ScaleY(top + 1)),
					ReadOnly = true,
					TabStop = false,
					Width = UIHelper.ScaleX(330),
				};
				Controls.Add(box);
				return box;
			}

			var movieLabel = decl.Movie?.Label ?? "Movie";
			RowLabel(movieLabel, y);
			_movie = SingleBox(y);
			SideButton("Browse...", y, () =>
			{
				if (_pickFiles(movieLabel, decl.Movie?.Extensions ?? [ ], false) is [ var picked, .. ]) SetMovie(picked);
			});
			y += 32;

			foreach (var file in decl.Files ?? [ ])
			{
				var label = file.Label ?? file.Option ?? "File";
				RowLabel(label, y);
				if (file.Multiple)
				{
					ListBox list = new()
					{
						HorizontalScrollbar = true,
						IntegralHeight = false,
						Location = new(UIHelper.ScaleX(136), UIHelper.ScaleY(y + 1)),
						Size = new(UIHelper.ScaleX(330), UIHelper.ScaleY(108)),
					};
					Controls.Add(list);
					_inputs.Add((file, list, null));
					SideButton("Add...", y, () => AddFiles(file.Option ?? "", _pickFiles(label, file.Extensions ?? [ ], true)));
					SideButton("Remove", y + 28, () =>
					{
						if (list.SelectedItem is string name) RemoveFile(file.Option ?? "", name);
					});
					SideButton("Up", y + 56, () => MoveSelected(list, -1));
					SideButton("Down", y + 84, () => MoveSelected(list, +1));
					y += 116;
				}
				else
				{
					var box = SingleBox(y);
					_inputs.Add((file, null, box));
					SideButton("Browse...", y, () => AddFiles(file.Option ?? "", _pickFiles(label, file.Extensions ?? [ ], false)));
					y += 32;
				}
			}

			foreach (var option in decl.Options ?? [ ])
			{
				// the importer's options are switches: absent is false, and only a
				// ticked one is sent (a declaration of any other type is not offered)
				if (option.EffectiveType is not "bool") continue;
				CheckBox box = new()
				{
					AutoSize = true,
					Location = new(UIHelper.ScaleX(136), UIHelper.ScaleY(y)),
					Text = option.DisplayName,
				};
				if (!string.IsNullOrWhiteSpace(option.Description)) _tips.SetToolTip(box, option.Description);
				Controls.Add(box);
				_options.Add((option, box));
				y += 24;
			}
			y += 6;

			_status = new Label
			{
				Location = new(UIHelper.ScaleX(12), UIHelper.ScaleY(y)),
				Size = new(UIHelper.ScaleX(536), UIHelper.ScaleY(48)),
			};
			_status.SetForeRole(ThemeColorRole.AccentError);
			Controls.Add(_status);
			y += 54;

			_importButton = new Button
			{
				Location = new(UIHelper.ScaleX(384), UIHelper.ScaleY(y)),
				Size = new(UIHelper.ScaleX(80), UIHelper.ScaleY(28)),
				Text = "Import",
			};
			_importButton.Click += (_, _) => Import();
			Button cancel = new()
			{
				DialogResult = DialogResult.Cancel,
				Location = new(UIHelper.ScaleX(472), UIHelper.ScaleY(y)),
				Size = new(UIHelper.ScaleX(76), UIHelper.ScaleY(28)),
				Text = "Cancel",
			};
			Controls.AddRange([ _importButton, cancel ]);
			AcceptButton = _importButton;
			CancelButton = cancel;
			ClientSize = new(UIHelper.ScaleX(560), UIHelper.ScaleY(y + 40));
			ResumeLayout();
		}

		/// <summary>What the dialog says, for tests: the refusal or the complaint, or "".</summary>
		public string StatusText => _status.Text;

		/// <summary>The files picked for an input, in its order, for tests.</summary>
		public IReadOnlyList<string> FilesFor(string option)
		{
			var input = _inputs.FirstOrDefault(i => i.Decl.Option == option);
			if (input.List is not null) return input.List.Items.Cast<string>().ToList();
			return input.Single is { Text.Length: not 0 } single ? [ single.Text ] : [ ];
		}

		public void SetMovie(string path)
		{
			_movie.Text = path;
			_status.Text = "";
		}

		/// <summary>
		/// Puts files into an input: a list takes them all, in the order given, a
		/// single input the first. A file whose name another input already holds
		/// is refused - every file is mounted under its own name, and two of one
		/// name cannot both be.
		/// </summary>
		public void AddFiles(string option, IReadOnlyList<string> paths)
		{
			var input = _inputs.FirstOrDefault(i => i.Decl.Option == option);
			if (input.Decl is null || paths.Count is 0) return;
			_status.Text = "";
			foreach (var path in input.List is null ? paths.Take(1) : paths)
			{
				var name = Path.GetFileName(path);
				// a single input gives its file up when another is chosen for it
				if (input.Single is { Text.Length: not 0 } single && Path.GetFileName(single.Text) is var old)
				{
					_paths.Remove(old);
					single.Text = "";
				}
				if (_paths.Keys.Any(n => string.Equals(n, name, StringComparison.OrdinalIgnoreCase)))
				{
					_status.Text = $"Two files named {name}: each file is read under its own name, so rename one.";
					continue;
				}
				_paths[name] = path;
				if (input.List is not null) input.List.Items.Add(name);
				else input.Single!.Text = path;
			}
		}

		public void RemoveFile(string option, string name)
		{
			var input = _inputs.FirstOrDefault(i => i.Decl.Option == option);
			if (input.List is null) return;
			input.List.Items.Remove(name);
			_paths.Remove(name);
		}

		private static void MoveSelected(ListBox list, int delta)
		{
			var at = list.SelectedIndex;
			var to = at + delta;
			if (at < 0 || to < 0 || to >= list.Items.Count) return;
			var item = list.Items[at];
			list.Items.RemoveAt(at);
			list.Items.Insert(to, item);
			list.SelectedIndex = to;
		}

		public void SetOption(string name, bool on)
		{
			foreach (var (decl, box) in _options)
			{
				if (decl.Name == name) box.Checked = on;
			}
		}

		/// <summary>
		/// The request the dialog stands for, or why there is none yet. Each input's
		/// option carries the mounted name - a list's names joined in the user's
		/// order, and a list left empty sent not at all, so the core falls back on
		/// what the movie itself names (and says which file it wants when that is
		/// not at hand). Options are sent only when ticked.
		/// </summary>
		public (MovieImportRequest? Request, string? Complaint) BuildRequest()
		{
			if (_movie.Text.Length is 0) return (null, $"Choose the {(_decl.Movie?.Label ?? "movie").ToLowerInvariant()} to import.");
			MovieImportRequest request = new() { Movie = _movie.Text, Settings = new() };
			foreach (var (decl, list, single) in _inputs)
			{
				var names = list is not null
					? list.Items.Cast<string>().ToList()
					: single is { Text.Length: not 0 } ? [ Path.GetFileName(single.Text) ] : new List<string>();
				if (names.Count is 0)
				{
					if (decl.Required) return (null, $"Choose the {decl.Label ?? decl.Option}.");
					continue;
				}
				foreach (var name in names) request.Files.Add(new() { Name = name, Path = _paths[name] });
				if (decl.Option is { Length: > 0 } option) request.Settings[option] = string.Join(decl.Separator ?? ";", names);
			}
			foreach (var (decl, box) in _options)
			{
				if (box.Checked) request.Settings[decl.Key] = true;
			}
			return (request, null);
		}

		/// <summary>Presses Import: the core reads the files, and either answers or says why not.</summary>
		public void Import()
		{
			var (request, complaint) = BuildRequest();
			if (request is null)
			{
				_status.Text = complaint ?? "";
				return;
			}
			_status.Text = "";
			UseWaitCursor = true;
			_importButton.Enabled = false;
			try
			{
				var (answer, failure) = _import(request);
				if (answer is null || answer.Error is not null)
				{
					_status.Text = answer?.Error ?? failure ?? "The core did not answer.";
					return;
				}
				Request = request;
				Answer = answer;
				DialogResult = DialogResult.OK;
				Close();
			}
			finally
			{
				UseWaitCursor = false;
				_importButton.Enabled = true;
			}
		}
	}
}
