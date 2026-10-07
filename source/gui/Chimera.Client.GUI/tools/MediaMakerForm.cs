#nullable enable

using System;
using System.Collections.Generic;
using System.Drawing;
using System.IO;
using System.Threading;
using System.Threading.Tasks;
using System.Windows.Forms;

using Chimera.Common.PathExtensions;
using Chimera.Client.Common;
using Chimera.Emulation.Common.Engine;

namespace Chimera.Client.GUI
{
	/// <summary>
	/// Turns a folder into ONE file, reproducibly: the same folder gives the
	/// same SHA1 on anybody's machine.
	///
	/// WHY THE WINDOW EXISTS. A project stores names and SHA1s and never paths
	/// (docs/project.md), and a movie is only replayable by someone who can
	/// produce files matching those hashes. A game that arrived as a dumped
	/// FOLDER - a PlayStation 3 disc, a floppy's worth of DOS - cannot be named
	/// that way, because a directory has no hash. It has to become a file first,
	/// and everyone who packs it has to get the same file, or the movie travels
	/// no further than the machine that recorded it.
	///
	/// The packing is the engine's (ce_media_make); this window chooses the
	/// folder, the shape and the name, shows how far it has got, and lets it be
	/// stopped. Nothing here decides anything about the bytes.
	///
	/// WHAT A CORE MAY ADD. The packer knows no machine: an image is the files.
	/// One emulator wants more than that of a disc - a table in the part of an
	/// ISO the standard leaves alone - and says so in its package ("media"), as
	/// a recipe the engine understands. This window is given the installed
	/// cores' recipes; when the folder is one a recipe was written for, it says
	/// which, and hands that recipe to the engine with the folder. Which folders
	/// those are and what the image needs are both the recipe's, read by the
	/// engine and by nothing here.
	/// </summary>
	public sealed class MediaMakerForm : FormBase
	{
		private const string TITLE = "Reproducible Media Maker";

		private readonly TextBox _folder;
		private readonly TextBox _output;
		private readonly ComboBox _format;
		private readonly ProgressBar _bar;
		private readonly Label _current;
		private readonly Label _result;
		private readonly Button _make;
		private readonly Button _close;

		private CancellationTokenSource? _cancel;
		private bool _running;

		protected override string WindowTitle => TITLE;

		protected override string WindowTitleStatic => TITLE;

		/// <summary>The formats, in the order the box offers them.</summary>
		private static readonly (string Label, int Format, string Extension, string Help)[] Formats =
		{
			("Zip archive, stored (.zip)", 0, ".zip",
				"No compression, so a core reads it by seeking, the way it would read the loose "
				+ "files. The same size on disk as the folder."),
			("Disc image, ISO 9660 + Joliet (.iso)", 1, ".iso",
				"A disc, which is what the disc-based cores want. Long names are kept in the "
				+ "Joliet tree. A single file of 4 GiB or more cannot be described by this format."),
			("Floppy disk, FAT12 (.img)", 2, ".img",
				"A 1.44 MB floppy with 8.3 names. What does not fit is refused rather than "
				+ "quietly left out."),
		};

		/// <summary>One installed core's recipe, and the core that declares it.</summary>
		public sealed record CoreRecipe(string Core, string Label, string Json);

		private readonly IReadOnlyList<CoreRecipe> _recipes;

		/// <summary>The recipe the chosen folder and format call for, if any.</summary>
		public CoreRecipe? Recognised { get; private set; }

		/// <param name="recipes">What the installed cores say their media needs; none, and an image is the files alone.</param>
		public MediaMakerForm(IReadOnlyList<CoreRecipe>? recipes = null)
		{
			_recipes = recipes ?? [ ];
			SuspendLayout();
			FormBorderStyle = FormBorderStyle.FixedDialog;
			MaximizeBox = false;
			MinimizeBox = false;
			ShowIcon = false;
			StartPosition = FormStartPosition.CenterParent;

			var margin = UIHelper.ScaleX(12);
			var row = UIHelper.ScaleY(28);
			var labelWidth = UIHelper.ScaleX(74);
			var browseWidth = UIHelper.ScaleX(90);
			var width = UIHelper.ScaleX(560);
			var fieldWidth = width - (2 * margin) - labelWidth - browseWidth - UIHelper.ScaleX(8);

			var y = UIHelper.ScaleY(12);

			Controls.Add(Note(
				"A folder becomes one file, and the same folder becomes the same file on any "
				+ "machine - the dates, the permissions and the order it was read in are left out "
				+ "on purpose. That is what lets a project name it by hash.",
				margin, y, width - (2 * margin), UIHelper.ScaleY(46)));
			y += UIHelper.ScaleY(52);

			Controls.Add(Caption("Folder", margin, y, labelWidth));
			_folder = new TextBox
			{
				Location = new(margin + labelWidth, y),
				Width = fieldWidth,
				ReadOnly = true,
			};
			Controls.Add(_folder);
			var browseFolder = new Button
			{
				Location = new(margin + labelWidth + fieldWidth + UIHelper.ScaleX(8), y - UIHelper.ScaleY(1)),
				Size = new(browseWidth, UIHelper.ScaleY(24)),
				Text = "Browse...",
			};
			browseFolder.Click += (_, _) => PickFolder();
			Controls.Add(browseFolder);
			y += row;

			Controls.Add(Caption("Format", margin, y, labelWidth));
			_format = new ComboBox
			{
				DropDownStyle = ComboBoxStyle.DropDownList,
				Location = new(margin + labelWidth, y),
				Width = fieldWidth,
			};
			foreach (var f in Formats) _format.Items.Add(f.Label);
			_format.SelectedIndex = 0;
			_format.SelectedIndexChanged += (_, _) => FormatChanged();
			Controls.Add(_format);
			y += row;

			_formatHelp = Note("", margin + labelWidth, y, fieldWidth, UIHelper.ScaleY(32));
			Controls.Add(_formatHelp);
			y += UIHelper.ScaleY(36);

			// what a core's recipe recognised the folder as; empty for most folders
			_recognised = Note("", margin + labelWidth, y, fieldWidth, UIHelper.ScaleY(32));
			Controls.Add(_recognised);
			y += UIHelper.ScaleY(36);

			Controls.Add(Caption("Save as", margin, y, labelWidth));
			_output = new TextBox
			{
				Location = new(margin + labelWidth, y),
				Width = fieldWidth,
			};
			Controls.Add(_output);
			var browseOut = new Button
			{
				Location = new(margin + labelWidth + fieldWidth + UIHelper.ScaleX(8), y - UIHelper.ScaleY(1)),
				Size = new(browseWidth, UIHelper.ScaleY(24)),
				Text = "Save as...",
			};
			browseOut.Click += (_, _) => PickOutput();
			Controls.Add(browseOut);
			y += row + UIHelper.ScaleY(6);

			_bar = new ProgressBar
			{
				Location = new(margin, y),
				Size = new(width - (2 * margin), UIHelper.ScaleY(18)),
				Maximum = 1000,
			};
			Controls.Add(_bar);
			y += UIHelper.ScaleY(22);

			_current = new Label
			{
				AutoSize = false,
				AutoEllipsis = true,
				Location = new(margin, y),
				Size = new(width - (2 * margin), UIHelper.ScaleY(18)),
				Text = "",
			};
			Controls.Add(_current);
			y += UIHelper.ScaleY(22);

			_result = new Label
			{
				AutoSize = false,
				Location = new(margin, y),
				Size = new(width - (2 * margin), UIHelper.ScaleY(34)),
				Text = "",
			};
			Controls.Add(_result);
			y += UIHelper.ScaleY(38);

			_make = new Button
			{
				Location = new(width - margin - (2 * UIHelper.ScaleX(96)) - UIHelper.ScaleX(8), y),
				Size = new(UIHelper.ScaleX(96), UIHelper.ScaleY(26)),
				Text = "Make",
			};
			_make.Click += (_, _) => MakeOrCancel();
			Controls.Add(_make);

			_close = new Button
			{
				DialogResult = DialogResult.Cancel,
				Location = new(width - margin - UIHelper.ScaleX(96), y),
				Size = new(UIHelper.ScaleX(96), UIHelper.ScaleY(26)),
				Text = "Close",
			};
			Controls.Add(_close);
			CancelButton = _close;

			ClientSize = new(width, y + UIHelper.ScaleY(38));
			FormatChanged();
			UpdateEnabled();
			ResumeLayout();
		}

		private readonly Label _formatHelp;

		private readonly Label _recognised;

		/// <summary>The line that says what the folder was recognised as (for the tests to read).</summary>
		public string RecognisedText => _recognised.Text;

		/// <summary>
		/// Asks the engine which installed core's recipe, if any, the folder and
		/// the chosen shape call for. The first that applies is taken, in the
		/// order the cores were listed.
		/// </summary>
		private void Recognise()
		{
			Recognised = null;
			var folder = _folder.Text;
			if (folder.Length is not 0)
			{
				foreach (var recipe in _recipes)
				{
					if (ChimeraEngine.MediaRecipeFormat(recipe.Json) != Chosen.Format) continue;
					if (!ChimeraEngine.MediaRecipeApplies(recipe.Json, folder)) continue;
					Recognised = recipe;
					break;
				}
			}
			_recognised.Text = Recognised is null
				? ""
				: $"This folder is a {Recognised.Label}: the image is written the way {Recognised.Core} reads it.";
		}

		/// <summary>Sets the folder as browsing for it does (for the tests, which cannot browse).</summary>
		public void SetFolder(string folder)
		{
			_folder.Text = folder;
			if (_output.Text.Length == 0)
			{
				// beside the folder, named after it: the obvious answer, and one
				// that never silently overwrites what is inside it
				var name = new DirectoryInfo(folder).Name;
				var parent = Path.GetDirectoryName(folder.TrimEnd(Path.DirectorySeparatorChar));
				_output.Text = Path.Combine(parent ?? folder, name + Chosen.Extension);
			}
			Recognise();
			UpdateEnabled();
		}

		/// <summary>Chooses a shape by its place in the box (for the tests).</summary>
		public void SetFormat(int index) => _format.SelectedIndex = index;

		private static Label Caption(string text, int x, int y, int width) => new()
		{
			AutoSize = false,
			Location = new(x, y + UIHelper.ScaleY(3)),
			Size = new(width, UIHelper.ScaleY(20)),
			Text = text,
		};

		private static Label Note(string text, int x, int y, int width, int height) => new()
		{
			AutoSize = false,
			Location = new(x, y),
			Size = new(width, height),
			Text = text,
		};

		private (string Label, int Format, string Extension, string Help) Chosen
			=> Formats[Math.Max(0, _format.SelectedIndex)];

		private void FormatChanged()
		{
			_formatHelp.Text = Chosen.Help;
			// keep the suggested name in step with the shape being made
			if (_output.Text.Length != 0 && _folder.Text.Length != 0)
				_output.Text = Path.ChangeExtension(_output.Text, Chosen.Extension);
			Recognise();
		}

		private void PickFolder()
		{
			using FolderBrowserEx dialog = new()
			{
				Description = "The folder to pack",
			};
			if (dialog.ShowDialog(this) is not DialogResult.OK) return;
			SetFolder(dialog.SelectedPath.WithoutWslgMirror());
		}

		private void PickOutput()
		{
			using SaveFileDialog dialog = new()
			{
				Title = "Write the image to",
				FileName = _output.Text,
				DefaultExt = Chosen.Extension.TrimStart('.'),
				Filter = $"{Chosen.Label}|*{Chosen.Extension}|All Files|*.*",
				OverwritePrompt = true,
			};
			if (dialog.ShowDialog(this) is not DialogResult.OK) return;
			_output.Text = dialog.FileName.WithoutWslgMirror();
			UpdateEnabled();
		}

		private void UpdateEnabled()
		{
			_make.Enabled = _running || (_folder.Text.Length != 0 && _output.Text.Length != 0);
			_make.Text = _running ? "Cancel" : "Make";
		}

		private void MakeOrCancel()
		{
			if (_running)
			{
				_cancel?.Cancel();
				_current.Text = "Stopping...";
				return;
			}
			_ = MakeAsync();
		}

		private async Task MakeAsync()
		{
			_running = true;
			_cancel = new CancellationTokenSource();
			_result.Text = "";
			_bar.Value = 0;
			UpdateEnabled();

			var folder = _folder.Text;
			var output = _output.Text;
			var format = Chosen.Format;
			// what the folder was recognised as travels with it: the recipe is
			// the core's, and the engine does what it says
			var recipe = Recognised?.Json;
			var token = _cancel.Token;

			// The bar is redrawn from the packing thread through Invoke, but not
			// on every byte: a disc is millions of callbacks and the window would
			// spend its time painting instead of letting the work happen.
			var lastPaint = DateTime.MinValue;

			var ok = false;
			var sha1 = "";
			var error = "";
			await Task.Run(() =>
			{
				ok = ChimeraEngine.MakeMedia(folder, output, format, recipe,
					(file, done, total, filesDone, filesTotal) =>
					{
						if (token.IsCancellationRequested) return false;
						var now = DateTime.UtcNow;
						if ((now - lastPaint).TotalMilliseconds >= 50)
						{
							lastPaint = now;
							var fraction = total == 0 ? 0 : (int)(done * 1000 / total);
							try
							{
								BeginInvoke(() =>
								{
									_bar.Value = Math.Min(1000, fraction);
									_current.Text = $"{file}  ({filesDone} of {filesTotal})";
								});
							}
							catch (Exception)
							{
								// the window went away under us; stop rather than throw
								return false;
							}
						}
						return true;
					},
					out sha1, out error);
			});

			_running = false;
			_cancel.Dispose();
			_cancel = null;
			UpdateEnabled();
			_current.Text = "";

			if (ok)
			{
				_bar.Value = 1000;
				var size = new FileInfo(output).Length;
				_result.Text = $"Written: {output}\nSHA1 {sha1}  ({size:N0} bytes)";
			}
			else
			{
				_bar.Value = 0;
				_result.Text = error == "cancelled" ? "Cancelled; nothing was written." : error;
			}
		}
	}
}
