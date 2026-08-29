using System.IO;
using Chimera.Client.Common;

namespace Chimera.Client.GUI
{
	public class LuaAutocompleteInstaller
	{
		public enum TextEditors
		{
			Sublime2,
			NotePad,
		}

		public bool IsInstalled(TextEditors editor)
		{
			return editor switch
			{
				TextEditors.Sublime2 => IsSublimeInstalled(),
				TextEditors.NotePad => IsNotepadInstalled(),
				_ => false,
			};
		}

		public bool IsChimeraLuaRegistered(TextEditors editor)
		{
			return editor switch
			{
				TextEditors.Sublime2 => IsChimeraLuaSublimeInstalled(),
				TextEditors.NotePad => IsChimeraLuaNotepadInstalled(),
				_ => false,
			};
		}

		public void InstallChimeraLua(TextEditors editor, LuaDocumentation docs)
		{
			switch (editor)
			{
				case TextEditors.Sublime2:
					InstallChimeraLuaToSublime2(docs);
					break;
				case TextEditors.NotePad:
					InstallChimeraLuaToNotepad(docs);
					break;
			}
		}

		private string AppDataFolder => Environment.GetFolderPath(Environment.SpecialFolder.ApplicationData);

		private bool IsSublimeInstalled()
		{
			// The most likely location of the app, eventually we should consider looking through the registry or installed apps as a more robust way to detect it;
			string exePath = @"C:\Program Files\Sublime Text 2\sublime_text.exe";
			return File.Exists(exePath);
		}

		private bool IsNotepadInstalled()
		{
			// The most likely location of the app, eventually we should consider looking through the registry or installed apps as a more robust way to detect it;
			string exePath = @"C:\Program Files (x86)\Notepad++\notepad++.exe";
			return File.Exists(exePath);
		}

		private readonly string SublimeLuaPath = @"Sublime Text 2\Packages\Lua";
		private readonly string SublimeCompletionsFilename = "bizhawk.lua.sublime-completions";

		private bool IsChimeraLuaSublimeInstalled()
		{
			var bizCompletions = Path.Combine(AppDataFolder, SublimeLuaPath, SublimeCompletionsFilename);
			return File.Exists(bizCompletions);
		}

		private readonly string NotepadPath = "TODO";
		private readonly string NotepadAutoCompleteFileName = "TODO";

		private bool IsChimeraLuaNotepadInstalled()
		{
			var bizCompletions = Path.Combine(AppDataFolder, NotepadPath, NotepadAutoCompleteFileName);
			return File.Exists(bizCompletions);
		}

		private void InstallChimeraLuaToSublime2(LuaDocumentation docs)
		{
			var bizCompletions = Path.Combine(AppDataFolder, SublimeLuaPath, SublimeCompletionsFilename);

			var text = docs.ToSublime2CompletionList();
			File.WriteAllText(bizCompletions, text);
		}

		private void InstallChimeraLuaToNotepad(LuaDocumentation docs)
		{
			var bizAutocomplete = Path.Combine(AppDataFolder, NotepadPath, NotepadAutoCompleteFileName);

			var text = docs.ToNotepadPlusPlusAutoComplete();

			// TODO
			//File.WriteAllText(bizCompletions, text);
		}
	}
}
