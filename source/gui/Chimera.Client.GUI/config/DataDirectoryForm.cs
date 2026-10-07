#nullable enable

using System;
using System.Diagnostics;
using System.Drawing;
using System.IO;
using System.Windows.Forms;

using Chimera.Client.Common;
using Chimera.Common;

namespace Chimera.Client.GUI
{
	/// <summary>What somebody answered when asked about the data already there.</summary>
	public enum DataDirectoryAnswer
	{
		Cancel,

		/// <summary>Carry the existing data over.</summary>
		Move,

		/// <summary>Use the new place as it is - empty, or with what it already holds - and leave the old alone.</summary>
		LeaveBehind,
	}

	/// <summary>
	/// Config &gt; Data Directory: where everything Chimera keeps per user lives, and a way to send
	/// it somewhere other than the system drive (issue #52).
	///
	/// It moves EVERYTHING (user-decided, 2026-09-17) - project caches, unpacked cores,
	/// precompiled code, downloaded cores, recovery journals, crash notes - which is exactly what
	/// <c>CHIMERA_DATA_HOME</c> always did, with a window on it.
	///
	/// This window only RECORDS the change. The next start carries it out
	/// (<see cref="DataDirectory.ApplyPending"/>), before anything in the directory is open: a
	/// running session holds a state history, a recovery journal and paths into the core store,
	/// and a move under it would be a move of files in use. Thin over <see cref="DataDirectory"/>,
	/// which is proved without a window.
	/// </summary>
	public sealed class DataDirectoryForm : FormBase
	{
		private readonly Config _config;
		private readonly Func<string?> _pickFolder;
		private readonly Func<string, DataDirectoryChoice, long, long, DataDirectoryAnswer> _ask;
		private readonly Func<string, long> _freeSpaceAt;

		private readonly Label _where;
		private readonly Label _figures;
		private readonly Label _pending;
		private readonly Label _status;
		private readonly Button _change;
		private readonly Button _useDefault;
		private readonly Button _cancelPending;

		protected override string WindowTitleStatic => "Data Directory";

		/// <param name="pickFolder">the folder somebody chose, or null; a dialog in the application, a stub in tests</param>
		/// <param name="ask">(target, what it is, bytes to move, bytes free there) - what to do about the existing data</param>
		public DataDirectoryForm(
			Config config,
			Func<string?>? pickFolder = null,
			Func<string, DataDirectoryChoice, long, long, DataDirectoryAnswer>? ask = null,
			Func<string, long>? freeSpaceAt = null)
		{
			_config = config;
			_pickFolder = pickFolder ?? PickFolder;
			_ask = ask ?? Ask;
			_freeSpaceAt = freeSpaceAt ?? CacheSurvey.FreeSpaceAt;

			SuspendLayout();
			ClientSize = new(UIHelper.ScaleX(640), UIHelper.ScaleY(300));
			FormBorderStyle = FormBorderStyle.FixedDialog;
			MaximizeBox = false;
			MinimizeBox = false;
			StartPosition = FormStartPosition.CenterParent;

			Label intro = new()
			{
				Location = new(UIHelper.ScaleX(12), UIHelper.ScaleY(10)),
				Size = new(UIHelper.ScaleX(616), UIHelper.ScaleY(64)),
				Text = "Everything Chimera keeps for you is in one directory: each project's greenzone, unpacked cores, "
					+ "precompiled game code, the cores you downloaded, recovery journals and crash notes. "
					+ "It can run to many gigabytes, and it does not have to be on the system drive.",
			};
			_where = new Label
			{
				Location = new(UIHelper.ScaleX(12), UIHelper.ScaleY(82)),
				Size = new(UIHelper.ScaleX(616), UIHelper.ScaleY(20)),
				Font = new Font(Font, FontStyle.Bold),
				AutoEllipsis = true,
			};
			_figures = new Label { Location = new(UIHelper.ScaleX(12), UIHelper.ScaleY(106)), Size = new(UIHelper.ScaleX(616), UIHelper.ScaleY(20)) };
			_pending = new Label
			{
				Location = new(UIHelper.ScaleX(12), UIHelper.ScaleY(136)),
				Size = new(UIHelper.ScaleX(616), UIHelper.ScaleY(40)),
			};
			_pending.SetForeRole(ThemeColorRole.AccentWarning);
			_status = new Label { Location = new(UIHelper.ScaleX(12), UIHelper.ScaleY(184)), Size = new(UIHelper.ScaleX(616), UIHelper.ScaleY(56)) };

			Button Make(string text, int x, int width, Action click)
			{
				Button b = new() { Text = text, Location = new(UIHelper.ScaleX(x), UIHelper.ScaleY(256)), Size = new(UIHelper.ScaleX(width), UIHelper.ScaleY(28)) };
				b.Click += (_, _) => click();
				return b;
			}
			_change = Make("Change...", 12, 100, ChooseAnother);
			_useDefault = Make("Use Default", 118, 100, () => Request(""));
			_cancelPending = Make("Keep Current", 224, 110, CancelPending);
			var open = Make("Open Folder", 340, 100, OpenFolder);
			var close = Make("Close", 528, 100, Close);
			CancelButton = close;

			Controls.AddRange([ intro, _where, _figures, _pending, _status, _change, _useDefault, _cancelPending, open, close ]);
			ResumeLayout();
			ShowState();
		}

		/// <summary>What the window says, for tests: where, the pending change, and the last thing it told the user.</summary>
		public string WhereText => _where.Text;

		public string PendingText => _pending.Text;

		public string StatusText => _status.Text;

		public bool CanChange => _change.Enabled;

		private string Current => ProjectCache.DecidedByEnvironment ? ProjectCache.DataHome : ProjectCache.DataHomeFor(_config.DataDirectory);

		private void ShowState()
		{
			_where.Text = Current;
			_figures.Text = $"{Bytes(DataDirectory.SizeOf(Current))} in use here, {Bytes(_freeSpaceAt(Current))} free on its drive";
			var environment = ProjectCache.DecidedByEnvironment;
			if (environment)
			{
				_pending.Text = "CHIMERA_DATA_HOME is set, and it decides: this is a portable install, or somebody meant it. Unset it to choose here.";
			}
			else if (_config.DataDirectoryPending is { } pending)
			{
				_pending.Text = $"At the next start this becomes {ProjectCache.DataHomeFor(pending)}"
					+ (_config.DataDirectoryPendingMove ? ", and everything here is moved there." : ". What is here stays here.");
			}
			else
			{
				_pending.Text = "";
			}
			_change.Enabled = !environment;
			_useDefault.Enabled = !environment && (_config.DataDirectoryPending ?? _config.DataDirectory).Length is not 0;
			_cancelPending.Visible = !environment && _config.DataDirectoryPending is not null;
		}

		private void ChooseAnother()
		{
			if (_pickFolder() is { Length: not 0 } picked) Request(DataDirectory.TargetFor(picked));
		}

		/// <summary>Asks for the data directory to become <paramref name="setting"/> (empty: the default) at the next start.</summary>
		public void Request(string setting)
		{
			var target = ProjectCache.DataHomeFor(setting);
			var what = DataDirectory.Inspect(Current, target);
			switch (what)
			{
				case DataDirectoryChoice.Same:
					// asking for where it already is also withdraws a change that was waiting
					_config.DataDirectoryPending = null;
					_config.DataDirectoryPendingMove = false;
					_status.Text = "That is where it already is.";
					ShowState();
					return;
				case DataDirectoryChoice.Nested:
					_status.Text = $"{target} is inside the current data directory, or around it. A directory cannot be moved into itself.";
					return;
				case DataDirectoryChoice.Unwritable:
					_status.Text = $"{target} cannot be written to.";
					return;
			}

			var size = DataDirectory.SizeOf(Current);
			var answer = _ask(target, what, size, _freeSpaceAt(target));
			if (answer is DataDirectoryAnswer.Cancel) return;
			_config.DataDirectoryPending = setting;
			_config.DataDirectoryPendingMove = answer is DataDirectoryAnswer.Move;
			_status.Text = "Recorded. Nothing has been touched yet: Chimera does it when it next starts, before any project is open.";
			ShowState();
		}

		private void CancelPending()
		{
			_config.DataDirectoryPending = null;
			_config.DataDirectoryPendingMove = false;
			_status.Text = "The change was withdrawn.";
			ShowState();
		}

		private void OpenFolder()
		{
			try
			{
				Directory.CreateDirectory(Current);
				Process.Start(new ProcessStartInfo(Current) { UseShellExecute = true });
			}
			catch (Exception ex)
			{
				_status.Text = $"Could not open {Current}: {ex.Message}";
			}
		}

		private string? PickFolder()
		{
			using FolderBrowserDialog dialog = new()
			{
				Description = "Where Chimera should keep its data. A folder that already has other things in it gets a Chimera folder of its own inside.",
				ShowNewFolderButton = true,
			};
			return dialog.ShowDialog(this) is DialogResult.OK ? dialog.SelectedPath : null;
		}

		private DataDirectoryAnswer Ask(string target, DataDirectoryChoice what, long size, long free)
		{
			if (what is DataDirectoryChoice.HoldsChimeraData)
			{
				// its contents cannot be merged with these, so there is nothing to move INTO it
				return MessageBox.Show(this,
					$"{target} already holds Chimera data.\n\nUse it as it is? What is in the current directory stays where it is, untouched.",
					"Data Directory", MessageBoxButtons.OKCancel, MessageBoxIcon.Question) is DialogResult.OK
					? DataDirectoryAnswer.LeaveBehind
					: DataDirectoryAnswer.Cancel;
			}
			var room = free > 0 && free < size
				? $"\n\nThere may not be room: {Bytes(size)} to move, {Bytes(free)} free there. A move that does not fit is undone and nothing is lost."
				: "";
			var result = MessageBox.Show(this,
				$"Move the {Bytes(size)} already here to {target}?{room}\n\n"
					+ "Yes: it is moved when Chimera next starts.\n"
					+ "No: start empty there. Greenzones and compiled code are made again as they are needed; "
					+ "downloaded cores would have to be downloaded again. What is here stays here.",
				"Data Directory", MessageBoxButtons.YesNoCancel, MessageBoxIcon.Question);
			return result switch
			{
				DialogResult.Yes => DataDirectoryAnswer.Move,
				DialogResult.No => DataDirectoryAnswer.LeaveBehind,
				_ => DataDirectoryAnswer.Cancel,
			};
		}

		private static string Bytes(long bytes)
			=> bytes switch
			{
				>= 1L << 30 => $"{bytes / (double) (1L << 30):0.0} GB",
				>= 1L << 20 => $"{bytes / (double) (1L << 20):0.0} MB",
				_ => $"{bytes / 1024.0:0} KB",
			};
	}
}
