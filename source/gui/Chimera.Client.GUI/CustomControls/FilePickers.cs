#nullable enable

using System.IO;
using System.Linq;
using System.Windows.Forms;

namespace Chimera.Client.GUI
{
	/// <summary>
	/// What the open and save pickers share. They take the same properties as WinForms'
	/// <see cref="OpenFileDialog"/> and <see cref="SaveFileDialog"/>, and show the desktop's
	/// own file chooser (<see cref="PortalFileChooser"/>) where there is one - on Linux and
	/// the BSDs, through the XDG desktop portal - and the toolkit's dialog everywhere else.
	/// </summary>
	public abstract class FilePicker : IDisposable
	{
		public string Title = "";

		/// <summary>The WinForms filter string, <c>Label|*.a;*.b|All files|*.*</c>.</summary>
		public string Filter = "";

		/// <summary>1-based, as WinForms counts. In, the filter to open on; out, the one in use.</summary>
		public int FilterIndex = 1;

		public string InitialDirectory = "";

		public string FileName = "";

		/// <summary>Whether the working directory is put back after the toolkit dialog moves it; the portal never moves it.</summary>
		public bool RestoreDirectory;

		/// <summary>Passed to the toolkit dialog; the portal has nothing like it.</summary>
		public bool ValidateNames = true;

		protected abstract bool Saving { get; }

		protected abstract FileDialog ToolkitDialog();

		protected abstract void TakeFrom(FileDialog dialog);

		private protected abstract void TakeFrom(PortalFileChooser.Answer answer);

		private protected virtual void Describe(PortalFileChooser.Request request) { }

		public DialogResult ShowDialog(IWin32Window? owner = null)
		{
			var request = new PortalFileChooser.Request
			{
				Save = Saving,
				Title = Title,
				Filter = Filter,
				FilterIndex = FilterIndex,
				// WinForms opens beside the file named when no folder is
				Folder = InitialDirectory.Length is not 0 ? InitialDirectory : DirectoryOf(FileName),
				FileName = FileName,
			};
			Describe(request);
			switch (PortalFileChooser.Show(owner, request, out var answer))
			{
				case PortalFileChooser.Outcome.Chosen:
					if (answer.FilterIndex is not 0) FilterIndex = answer.FilterIndex;
					TakeFrom(answer);
					return DialogResult.OK;
				case PortalFileChooser.Outcome.Cancelled:
					return DialogResult.Cancel;
			}

			using var dialog = ToolkitDialog();
			dialog.Title = Title;
			dialog.Filter = Filter;
			dialog.FilterIndex = FilterIndex;
			dialog.InitialDirectory = InitialDirectory;
			dialog.FileName = FileName;
			dialog.RestoreDirectory = RestoreDirectory;
			dialog.ValidateNames = ValidateNames;
			var result = dialog.ShowDialog(owner);
			FilterIndex = dialog.FilterIndex;
			if (result is DialogResult.OK) TakeFrom(dialog);
			return result;
		}

		private static string DirectoryOf(string path)
		{
			try
			{
				return path.Length is 0 ? "" : Path.GetDirectoryName(Path.GetFullPath(path)) ?? "";
			}
			catch (Exception e) when (e is ArgumentException or NotSupportedException or PathTooLongException)
			{
				return "";
			}
		}

		public void Dispose() { }
	}

	/// <summary>Picks one file to open, or several; see <see cref="FilePicker"/>.</summary>
	public sealed class OpenFilePicker : FilePicker
	{
		public bool Multiselect;

		/// <summary>Every file chosen; <see cref="FilePicker.FileName"/> is the first.</summary>
		public string[] FileNames { get; private set; } = [ ];

		protected override bool Saving => false;

		private protected override void Describe(PortalFileChooser.Request request) => request.Multiple = Multiselect;

		protected override FileDialog ToolkitDialog() => new OpenFileDialog { Multiselect = Multiselect };

		protected override void TakeFrom(FileDialog dialog)
		{
			FileNames = dialog.FileNames;
			FileName = dialog.FileName;
		}

		private protected override void TakeFrom(PortalFileChooser.Answer answer)
		{
			FileNames = answer.Paths.ToArray();
			FileName = FileNames[0];
		}
	}

	/// <summary>Picks where to save a file; see <see cref="FilePicker"/>.</summary>
	public sealed class SaveFilePicker : FilePicker
	{
		/// <summary>Put on a name typed with no extension, when the filter in use has none to give; no leading dot.</summary>
		public string DefaultExt = "";

		public bool AddExtension = true;

		/// <summary>Asked of the toolkit dialog. Every portal's chooser asks before it overwrites, of its own accord.</summary>
		public bool OverwritePrompt = true;

		protected override bool Saving => true;

		protected override FileDialog ToolkitDialog() => new SaveFileDialog { DefaultExt = DefaultExt, AddExtension = AddExtension, OverwritePrompt = OverwritePrompt };

		protected override void TakeFrom(FileDialog dialog) => FileName = dialog.FileName;

		private protected override void TakeFrom(PortalFileChooser.Answer answer)
			=> FileName = AddExtension ? WithExtension(answer.Paths[0], Filter, FilterIndex, DefaultExt) : answer.Paths[0];

		/// <summary>
		/// What the toolkit dialog does to a name typed without an extension, done for the
		/// portal's, which hands back the name exactly as typed: the extension of the filter in
		/// use, if it names exactly one (<c>*.zip</c>), and otherwise the default.
		/// </summary>
		internal static string WithExtension(string path, string filter, int filterIndex, string defaultExt)
		{
			if (Path.GetExtension(path).Length is not 0) return path;
			var filters = PortalFileChooser.FiltersFrom(filter);
			var globs = filterIndex >= 1 && filterIndex <= filters.Count ? filters[filterIndex - 1].Globs : null;
			var fromFilter = globs is [ var only ] && only.StartsWith("*.", StringComparison.Ordinal) && only.IndexOfAny([ '*', '?', '[' ], 2) < 0
				? only.Substring(2)
				: null;
			var extension = fromFilter ?? defaultExt.TrimStart('.');
			return extension.Length is 0 ? path : $"{path}.{extension}";
		}
	}
}
