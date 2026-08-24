using System.Diagnostics;
using System.IO;
using System.Collections.Generic;
using System.Linq;
using System.Reflection;
using System.Runtime.InteropServices;
using System.Windows.Forms;

using Chimera.BizInvoke;
using Chimera.Bizware.Audio;
using Chimera.Bizware.Graphics;
using Chimera.Bizware.Graphics.Controls;
using Chimera.Bizware.Input;
using Chimera.Common;
using Chimera.Common.PathExtensions;
using Chimera.Common.StringExtensions;
using Chimera.Client.Common;
using Chimera.Client.GUI.CustomControls;
using Chimera.Emulation.Common;
using Chimera.Emulation.DiscSystem;
using Chimera.WinForms.Controls;

namespace Chimera.Client.GUI
{
	internal static class Program
	{
		// Declared here instead of a more usual place to avoid dependencies on the more usual place

		[DllImport("kernel32.dll", CharSet = CharSet.Unicode, ExactSpelling = true, SetLastError = true)]
		[return: MarshalAs(UnmanagedType.Bool)]
		private static extern bool DeleteFileW(string lpFileName);

		public static void EnsureWinFormsInitialized()
		{
			Application.EnableVisualStyles();
			Application.SetCompatibleTextRenderingDefault(false); // prepopulates `Label.UseCompatibleTextRendering` and friends; `false` means to use the "new" renderer rather than the compatibility renderer
		}

		private static void Show32BitWarningDialog()
		{
			EnsureWinFormsInitialized();
			using ExceptionBox box = new("Chimera requires a 64 bit environment in order to run! Chimera will now close.");
			box.ShowDialog();
		}

		static Program()
		{
			// Quickly check if the user is running this as a 32 bit process somehow
			// TODO: We may want to remove this sometime, Chimera should be able to run somewhat as 32 bit if the user really wants to
			// (There are no longer any hard 64 bit deps, i.e. SlimDX is no longer around)
			if (!Environment.Is64BitProcess)
			{
				Show32BitWarningDialog();
				Process.GetCurrentProcess().Kill();
				return;
			}

			try
			{
				_ = Assembly.ReflectionOnlyLoad("System.Windows.Forms, Version=4.0.0.0, Culture=neutral, PublicKeyToken=b77a5c561934e089");
			}
			catch (FileNotFoundException)
			{
				Console.Error.WriteLine("The WinForms assembly can't be found. Make sure you've installed the \"complete\" Mono package if one is available, or else look into alternate repos. Try Nix!");
				throw;
			}

			// In case assembly resolution fails, such as if we moved them into the dll subdiretory, this event handler can reroute to them
			AppDomain.CurrentDomain.AssemblyResolve += CurrentDomain_AssemblyResolve;

			if (!RuntimeInformation.IsOSPlatform(OSPlatform.Windows))
			{
				// Windows needs extra considerations for the dll directory
				// we can skip all this on non-Windows platforms
				return;
			}

			try
			{
				// before we load anything from the dll dir, whack the MOTW from everything in that directory (that's a dll)
				// otherwise, some people will have crashes at boot-up due to .net security disliking MOTW.
				// some people are getting MOTW through a combination of browser used to download bizhawk, and program used to dearchive it
				// We need to do it here too... otherwise people get exceptions when externaltools we distribute try to startup
				static void RemoveMOTW(string path) => DeleteFileW($"{path}:Zone.Identifier");
				var dllDir = Path.Combine(AppContext.BaseDirectory, "dll");
				var todo = new Queue<DirectoryInfo>([ new DirectoryInfo(dllDir) ]);
				while (todo.Count != 0)
				{
					var di = todo.Dequeue();
					foreach (var disub in di.GetDirectories()) todo.Enqueue(disub);
					foreach (var fi in di.GetFiles("*.dll")) RemoveMOTW(fi.FullName);
					foreach (var fi in di.GetFiles("*.exe")) RemoveMOTW(fi.FullName);
				}
			}
			catch (Exception e)
			{
				Console.WriteLine($"MotW remover failed: {e}");
			}
		}

		[STAThread]
		private static int Main(string[] args)
			=> SubMain(args);

		// NoInlining should keep this code from getting jammed into Main() which would create dependencies on types which havent been setup by the resolver yet... or something like that
		[System.Runtime.CompilerServices.MethodImpl(System.Runtime.CompilerServices.MethodImplOptions.NoInlining)]
		private static int SubMain(string[] args)
		{
			// Assemblies were named BizHawk.* before the Chimera rename; anything
			// that asks for one by its old name (an external tool built against the
			// old ABI, a reflective load from an old file) gets the renamed one.
			AppDomain.CurrentDomain.AssemblyResolve += static (_, resolveArgs) =>
			{
				var wanted = new System.Reflection.AssemblyName(resolveArgs.Name).Name;
				if (wanted is "EmuHawk" or "BizHawk.Client.EmuHawk" or "Chimera.Client.EmuHawk") return typeof(Program).Assembly;
				if (wanted?.StartsWith("BizHawk.", StringComparison.Ordinal) is not true) return null;
				var renamed = "Chimera." + wanted.Substring("BizHawk.".Length);
				if (renamed.StartsWith("Chimera.Client.EmuHawk", StringComparison.Ordinal)) renamed = "Chimera.Client.GUI" + renamed.Substring("Chimera.Client.EmuHawk".Length);
				foreach (var assy in AppDomain.CurrentDomain.GetAssemblies())
				{
					if (assy.GetName().Name == renamed) return assy;
				}
				return null;
			};

			// raw scan, not ArgParser: several dialogs below can fire before arguments are parsed
			if (Array.IndexOf(args, "--headless") >= 0) HeadlessMode.Enabled = true;

			// this check has to be done VERY early.  i stepped through a debug build with wrong .dll versions purposely used,
			// and there was a TypeLoadException before the first line of SubMain was reached (some static ColorType init?)
			var thisAsmVer = ReflectionCache.AsmVersion;
			if (new[]
				{
					ReflectionCache_Chi_Biz.AsmVersion,
					ReflectionCache_Chi_Biz_Aud.AsmVersion,
					ReflectionCache_Chi_Biz_Gra.AsmVersion,
					ReflectionCache_Chi_Biz_Gra_Con.AsmVersion,
					ReflectionCache_Chi_Biz_Inp.AsmVersion,
					ReflectionCache_Chi_Cli_Com.AsmVersion,
					ReflectionCache_Chi_Com.AsmVersion,
					ReflectionCache_Chi_Emu_Com.AsmVersion,
					ReflectionCache_Chi_Emu_Dis.AsmVersion,
					ReflectionCache_Chi_Win_Con.AsmVersion,
				}.Any(asmVer => asmVer != thisAsmVer))
			{
				const string MISMATCH_MSG = "One or more of the BizHawk.* assemblies have the wrong version!\n(Did you attempt to update by overwriting an existing install?)";
				if (HeadlessMode.Enabled)
				{
					Console.Error.WriteLine(MISMATCH_MSG);
					return -1;
				}
				EnsureWinFormsInitialized();
				MessageBox.Show(MISMATCH_MSG);
				return -1;
			}

			string dllDir = null;
			if (!OSTailoredCode.IsUnixHost)
			{
				// this will look in subdirectory "dll" to load pinvoked stuff
				// declared above to be re-used later on, see second SetDllDirectoryW call
				dllDir = Path.Combine(AppContext.BaseDirectory, "dll");

				// windows prohibits a semicolon for SetDllDirectoryW, although such paths are fully valid otherwise
				// presumingly windows internally has ; used as a path separator, like with PATH
				// or perhaps this is just some legacy junk windows keeps around for backwards compatibility reasons
				// we can possibly workaround this by using the "short path name" rather (but this isn't guaranteed to exist)
				const string SEMICOLON_IN_DIR_MSG =
					"The path to the Chimera folder contains a ';', which doesn't work with one of the Windows APIs used by Chimera."
						+ "\nFind and rename the folder, or move Chimera somewhere else.";
				static int SetDllDirectoryFailed(string errMsg = SEMICOLON_IN_DIR_MSG)
				{
					EnsureWinFormsInitialized();
					MessageBox.Show(errMsg);
					return -1;
				}

				if (dllDir.ContainsOrdinal(';'))
				{
					var dllShortPathLen = Win32Imports.GetShortPathNameW(dllDir, null, 0);
					if (dllShortPathLen is 0) return SetDllDirectoryFailed();

					var dllShortPathBuffer = new char[dllShortPathLen];
					var dllShortPathLen1 = Win32Imports.GetShortPathNameW(dllDir, dllShortPathBuffer, dllShortPathLen);
					if (dllShortPathLen1 is 0) return SetDllDirectoryFailed();

					dllDir = new string(dllShortPathBuffer, 0, dllShortPathLen1);
					if (dllDir.ContainsOrdinal(';')) return SetDllDirectoryFailed();
				}

				if (!Win32Imports.SetDllDirectoryW(dllDir))
				{
					return SetDllDirectoryFailed(
						$"SetDllDirectoryW failed with error code {Marshal.GetLastWin32Error()}, this is fatal. Chimera will now close.");
				}

				// Check if we have the C++ VS2015-2022 redist all in one redist be installed
				var p = OSTailoredCode.LinkedLibManager.LoadOrZero("vcruntime140_1.dll");
				if (p != IntPtr.Zero)
				{
					OSTailoredCode.LinkedLibManager.FreeByPtr(p);
				}
				else
				{
					// else it's missing or corrupted
					const string desc =
						"Microsoft Visual C++ Redistributable for Visual Studio 2015, 2017, 2019, and 2022 (x64)";
					return SetDllDirectoryFailed(
						$"Chimera needs {desc} in order to run! See the readme on GitHub for more info. (Chimera will now close.)"
							+ $" Internal error message: {OSTailoredCode.LinkedLibManager.GetErrorMessage()}");
				}
			}

			TempFileManager.Start();

			ChimeraFile.DearchivalMethod = SharpCompressDearchivalMethod.Instance;

			ParsedCLIFlags cliFlags = default;
			try
			{
				if (ArgParser.ParseArguments(out cliFlags, args) is int exitCode1) return exitCode1;
			}
			catch (ArgParser.ArgParserException e)
			{
				if (HeadlessMode.Enabled)
				{
					Console.Error.WriteLine(e.Message);
					return 1;
				}
				EnsureWinFormsInitialized();
				new ExceptionBox(e.Message).ShowDialog();
				return 1;
			}

			EnsureWinFormsInitialized();
			typeof(Form).GetField(OSTailoredCode.IsUnixHost ? "default_icon" : "defaultIcon", BindingFlags.NonPublic | BindingFlags.Static)!
				.SetValue(null, Properties.Resources.Logo);

			var configPath = cliFlags.cmdConfigFile ?? Path.Combine(PathUtils.ExeDirectoryPath, "config.ini");

			Config initialConfig;
			try
			{
				initialConfig = ConfigService.Load<Config>(configPath);
			}
			catch (Exception e)
			{
				var corruptConfigMsg = string.Join("\n",
					"It appears your config file (config.ini) is corrupted; an exception was thrown while loading it.",
					"On closing this warning, Chimera will delete your config file and generate a new one. You can go make a backup now if you'd like to look into diffs.",
					"The caught exception was:",
					e.ToString());
				if (HeadlessMode.Enabled) HeadlessMode.LogSuppressedWarning(corruptConfigMsg);
				else new ExceptionBox(corruptConfigMsg).ShowDialog();
				File.Delete(configPath);
				initialConfig = ConfigService.Load<Config>(configPath);
			}
			initialConfig.ResolveDefaults();
			// initialConfig should really be globalConfig as it's mutable

			// must be done VERY early, before any SDL_Init calls can be done
			// if this isn't done, SIGINT/SIGTERM get swallowed by SDL
			if (OSTailoredCode.IsUnixHost)
			{
				SDL2.SDL.SDL_SetHintWithPriority(SDL2.SDL.SDL_HINT_NO_SIGNAL_HANDLERS, "1", SDL2.SDL.SDL_HintPriority.SDL_HINT_OVERRIDE);
			}

			// OpenGL is the one display driver: no multiplexing, no fallbacks. A machine
			// that cannot give us a 3.2 context gets a clear refusal, not a degraded mode.
			IGL workingGL;
			try
			{
				if (!IGL_OpenGL.Available) throw new InvalidOperationException("no OpenGL 3.2 context could be created");

				// need to have a context active for checking the renderer, will be disposed afterwards
				using (new SDL2OpenGLContext(3, 2, true))
				{
					using var testOpenGL = new IGL_OpenGL();
					testOpenGL.InitGLState();
					using (testOpenGL.CreateGuiRenderer()) {}
				}

				// don't return the same IGL, we don't want the test context to be part of this IGL
				workingGL = new IGL_OpenGL();
			}
			catch (Exception ex)
			{
				const string GL_FATAL_MSG = "This frontend requires OpenGL 3.2, and this machine's display driver did not provide it.\nUpdate or install a graphics driver with OpenGL 3.2 support.";
				if (HeadlessMode.Enabled)
				{
					Console.Error.WriteLine($"{GL_FATAL_MSG}\n{ex}");
				}
				else
				{
					new ExceptionBox(new Exception(GL_FATAL_MSG, ex)).ShowDialog();
				}
				return -1;
			}

			Sound globalSound = null;

			if (!OSTailoredCode.IsUnixHost)
			{
				// WHY do we have to do this? some intel graphics drivers (ig7icd64.dll 10.18.10.3304 on an unknown chip on win8.1) are calling SetDllDirectory() for the process, which ruins stuff.
				// The relevant initialization happened just before in "create IGL context".
				// It isn't clear whether we need the earlier SetDllDirectory(), but I think we do.
				if (!Win32Imports.SetDllDirectoryW(dllDir))
				{
					MessageBox.Show(
						$"SetDllDirectoryW failed with error code {Marshal.GetLastWin32Error()}, this is fatal. Chimera will now close.");
					return -1;
				}
			}

			if (!initialConfig.SkipSuperuserPrivsCheck
				&& OSTailoredCode.HostWindowsVersion is null or { Version: >= OSTailoredCode.WindowsVersion._10 }) // "windows isn't capable of being useful for non-administrators until windows 10" --zeromus
			{
				if (GuiUtil.CLRHostHasElevatedPrivileges)
				{
					var privsMsg = $"Chimera detected it {(OSTailoredCode.IsUnixHost ? "is running as root (Superuser)" : "has Administrator privileges")}.\n"
						+ $"Regularly using {(OSTailoredCode.IsUnixHost ? "Superuser" : "Administrator")} for things other than system administration makes it easier to hack you.\n"
						+ "If you're certain, you may continue anyway (and without support).\n"
						+ $"You'll find a flag \"{nameof(Config.SkipSuperuserPrivsCheck)}\" in the config file, which disables this warning.";
					if (HeadlessMode.Enabled)
					{
						HeadlessMode.LogSuppressedWarning(privsMsg);
					}
					else
					{
						using MsgBox dialog = new(
							title: "This Chimera is privileged",
							message: privsMsg,
							boxIcon: MessageBoxIcon.Warning);
						dialog.ShowDialog();
					}
				}
				else
				{
					Util.DebugWriteLine("running as unprivileged user");
				}
			}

			FPCtrl.FixFPCtrl();

			var exitCode = 0;
			try
			{
				MainForm mf = new(
					cliFlags,
					workingGL,
					() => configPath,
					() => initialConfig,
					newSound => globalSound = newSound,
					args,
					out var movieSession,
					out var exitEarly);
				if (exitEarly)
				{
					//TODO also use this for ArgParser failure
					mf.Dispose();
					return 0;
				}
				mf.LoadGlobalConfigFromFile = iniPath =>
				{
					initialConfig = ConfigService.Load<Config>(iniPath);
					initialConfig.ResolveDefaults();
					// ReSharper disable once AccessToDisposedClosure
					mf.Config = initialConfig;
				};
				mf.Show();
				try
				{
					exitCode = mf.ProgramRunLoop();
					if (!mf.IsDisposed)
						mf.Dispose();
				}
				catch (Exception e) when (movieSession.Movie.IsActive() && !Debugger.IsAttached)
				{
					if (HeadlessMode.Enabled)
					{
						Console.Error.WriteLine($"[headless] fatal exception (movie active, not saving): {e}");
						exitCode = 1;
					}
					else
					{
						var result = MessageBox.Show(
							"Chimera has thrown a fatal exception and is about to close.\nA movie has been detected. Would you like to try to save?\n(Note: Depending on what caused this error, this may or may not succeed)",
							$"Fatal error: {e.GetType().Name}",
							MessageBoxButtons.YesNo,
							MessageBoxIcon.Exclamation
						);
						if (result == DialogResult.Yes)
						{
							movieSession.Movie.Save();
						}
					}
				}
			}
			catch (Exception e) when (!Debugger.IsAttached)
			{
				if (HeadlessMode.Enabled)
				{
					Console.Error.WriteLine($"[headless] fatal exception: {e}");
					exitCode = 1;
				}
				else
				{
					new ExceptionBox(e).ShowDialog();
				}
			}
			finally
			{
				globalSound?.Dispose();
				workingGL.Dispose();
				Input.Instance?.Adapter?.DeInitAll();
			}

			// return 0 assuming things have gone well, non-zero values could be used as error codes or for scripting purposes
			return exitCode;
		}

		/// <remarks>http://www.codeproject.com/Articles/310675/AppDomain-AssemblyResolve-Event-Tips</remarks>
		private static Assembly CurrentDomain_AssemblyResolve(object sender, ResolveEventArgs args)
		{
			var requested = args.Name;

			lock (AppDomain.CurrentDomain)
			{
				var firstAsm = Array.Find(AppDomain.CurrentDomain.GetAssemblies(), asm => asm.FullName == requested);
				if (firstAsm != null)
				{
					return firstAsm;
				}

				// load missing assemblies by trying to find them in the dll directory
				var dllname = $"{new AssemblyName(requested).Name}.dll";
				var directory = Path.Combine(AppContext.BaseDirectory, "dll");
				var fname = Path.Combine(directory, dllname);
				// it is important that we use LoadFile here and not load from a byte array; otherwise mixed (managed/unmanaged) assemblies can't load
				return File.Exists(fname) ? Assembly.LoadFile(fname) : null;
			}
		}
	}
}

