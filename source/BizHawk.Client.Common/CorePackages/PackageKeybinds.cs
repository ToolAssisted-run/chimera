#nullable enable

using System;
using System.IO;

namespace BizHawk.Client.Common
{
	/// <summary>
	/// The default key bindings a core package ships for the controllers it declares.
	///
	/// BizHawk kept one <c>defctrl.json</c> listing every console it knew about, which only works
	/// while the frontend knows every console. miniHawk's cores arrive from outside it, so the
	/// bindings arrive with them: a package that declares a controller declares how it is played by
	/// default, in a <c>default_keybinds.json</c> beside its <c>waterbox.config</c>. The frontend
	/// ships none of its own.
	///
	/// They are only DEFAULTS: they fill in for a controller the user's config has never seen (see
	/// <c>InputManager.SyncControls</c>), and pressing Defaults in the controller config brings them
	/// back. A user's own bindings always win.
	/// </summary>
	public static class PackageKeybinds
	{
		public const string FileName = "default_keybinds.json";

		/// <summary>
		/// Reads the bindings a package ships, or null if it ships none. A malformed file is treated
		/// as "none": bad JSON in an optional convenience file must not stop a core from loading,
		/// and the message says which package to look at.
		/// </summary>
		public static DefaultControls? Read(string packageDir)
		{
			var path = Path.Combine(packageDir, FileName);
			if (!File.Exists(path)) return null;
			try
			{
				return ConfigService.Load<DefaultControls>(path);
			}
			catch (Exception ex)
			{
				Console.WriteLine($"CoreRegistry: ignoring unreadable {FileName} in {packageDir}: {ex.Message}");
				return null;
			}
		}
	}
}
