using System;
using System.Collections.Generic;
using System.IO;

using Chimera.Client.Common;
using Chimera.Client.GUI;
using Chimera.Emulation.Common.Waterbox;

namespace Chimera.Tests.Client.GUI
{
	/// <summary>
	/// The compile step: a core that translates a game's code before running it
	/// says so here, lists what it compiled, and the project cannot be created
	/// with any of it missing (docs/compile-cache.md).
	/// </summary>
	[TestClass]
	public class WizardPrecompileTests
	{
		private static WaterboxConfig CfgThatCompiles()
			=> WaterboxConfig.FromJson("""
				{
				  "coreName": "compiles",
				  "systemId": "PS3",
				  "version": "1",
				  "precompile": true,
				  "video": { "width": 640, "height": 480 },
				  "audio": { "samplesPerFrame": 1024 },
				  "input": { "buttons": [] }
				}
				""");

		private static WaterboxConfig CfgThatDoesNot()
			=> WaterboxConfig.FromJson("""
				{
				  "coreName": "plain",
				  "systemId": "NES",
				  "video": { "width": 256, "height": 240 },
				  "audio": { "samplesPerFrame": 1024 },
				  "input": { "buttons": [] }
				}
				""");

		private static string TempDir()
		{
			var dir = Path.Combine(Path.GetTempPath(), "chimera-precompile-" + Guid.NewGuid().ToString("N"));
			Directory.CreateDirectory(dir);
			return dir;
		}

		private static string MakeRom(string dir)
		{
			var path = Path.Combine(dir, "game.iso");
			File.WriteAllText(path, "not a real disc, but it hashes");
			return path;
		}

		/// <summary>Writes an object and the manifest that says the game needs it.</summary>
		private static void Compiled(string cacheRoot, WaterboxConfig cfg, string romPath, string name, string contents)
		{
			var dir = WaterboxCore.CoreCacheDirectoryFor(cacheRoot, cfg.CoreName, cfg.Version);
			var file = Path.Combine(dir, name);
			Directory.CreateDirectory(Path.GetDirectoryName(file)!);
			File.WriteAllText(file, contents);
			var romSha1 = Sha1Of(romPath);
			CoreCacheManifest manifest = new()
			{
				RomName = Path.GetFileName(romPath),
				RomSha1 = romSha1,
				Files = [ new CoreCacheFile { Name = name, Sha1 = CoreCacheManifest.HashOf(dir, name) } ],
			};
			manifest.Save(dir, romSha1);
		}

		private static string Sha1Of(string path)
		{
			using var stream = File.OpenRead(path);
			using var sha1 = System.Security.Cryptography.SHA1.Create();
			return BitConverter.ToString(sha1.ComputeHash(stream)).Replace("-", "");
		}

		[TestMethod]
		public void ACoreThatCompilesNothingIsNeverAsked()
		{
			var dir = TempDir();
			try
			{
				using NewProjectWizard form = new([ ], static _ => [ ]);
				form.Show();
				form.UsePrecompileFrom(CfgThatDoesNot(), dir, MakeRom(dir));
				Assert.AreEqual(0, form.PrecompileEntries.Count, "a core that compiles nothing has nothing to list");
			}
			finally
			{
				Directory.Delete(dir, recursive: true);
			}
		}

		/// <summary>
		/// The sessions are CHILD PROCESSES: they read the config off disk, and
		/// what it remembers is their only source of firmware. So whatever the
		/// firmware page settled has to be written there BEFORE they start - the
		/// owner remembering it after this wizard returns is too late, and a core
		/// that needs firmware then compiles nothing at all, which is what a PS3
		/// project did.
		/// </summary>
		[TestMethod]
		public void TheFirmwareIsRememberedBeforeTheSessionsStart()
		{
			var dir = TempDir();
			try
			{
				// a core has to be CHOSEN for a compile to start at all: the
				// sessions are spawned for a package, and RunPrecompile leaves
				// immediately without one
				var pkg = new DiscoveredCorePackage { Path = Path.Combine(dir, "fake.chimeraCore"), Name = "compiles" };
				var order = new List<string>();
				using NewProjectWizard form = new(
					[ pkg ],
					static _ => [ ],
					rememberFirmwareNow: (_, _) => order.Add("remembered"));
				form.Show();
				form.UsePrecompileFrom(CfgThatCompiles(), dir, MakeRom(dir));
				form.RunPrecompileForTest();
				CollectionAssert.Contains(order, "remembered",
					"the firmware must reach the config before a session is spawned");
			}
			finally
			{
				Directory.Delete(dir, recursive: true);
			}
		}

		[TestMethod]
		public void AGameNotCompiledYetCannotBeCreated()
		{
			var dir = TempDir();
			try
			{
				using NewProjectWizard form = new([ ], static _ => [ ]);
				form.Show();
				form.UsePrecompileFrom(CfgThatCompiles(), dir, MakeRom(dir));
				Assert.AreEqual(0, form.PrecompileEntries.Count);
				Assert.IsFalse(form.PrecompileReady, "nothing is compiled, so there is nothing to create a project against");
				Assert.IsFalse(form.CreateEnabled);
			}
			finally
			{
				Directory.Delete(dir, recursive: true);
			}
		}

		[TestMethod]
		public void ACompiledGameListsItsModulesInGreenAndCanBeCreated()
		{
			var dir = TempDir();
			try
			{
				var cfg = CfgThatCompiles();
				var rom = MakeRom(dir);
				Compiled(dir, cfg, rom, "cache/ppu-abc/module.obj.gz", "compiled bytes");
				using NewProjectWizard form = new([ ], static _ => [ ]);
				form.Show();
				form.UsePrecompileFrom(cfg, dir, rom);
				var entries = form.PrecompileEntries;
				Assert.AreEqual(1, entries.Count);
				Assert.AreEqual("cache/ppu-abc/module.obj.gz", entries[0].Name);
				Assert.AreEqual(40, entries[0].Sha1.Length, "the hash is shown, not just the name");
				Assert.IsTrue(entries[0].Present, "an object that is there and unchanged is green");
				Assert.IsTrue(form.PrecompileReady);
				Assert.IsTrue(form.CreateEnabled);
			}
			finally
			{
				Directory.Delete(dir, recursive: true);
			}
		}

		[TestMethod]
		public void AModuleThatChangedUnderneathIsNotAccepted()
		{
			var dir = TempDir();
			try
			{
				var cfg = CfgThatCompiles();
				var rom = MakeRom(dir);
				Compiled(dir, cfg, rom, "cache/ppu-abc/module.obj.gz", "compiled bytes");
				// something else wrote over it: same name, other content
				var file = Path.Combine(WaterboxCore.CoreCacheDirectoryFor(dir, cfg.CoreName, cfg.Version), "cache/ppu-abc/module.obj.gz");
				File.WriteAllText(file, "not what was compiled");
				using NewProjectWizard form = new([ ], static _ => [ ]);
				form.Show();
				form.UsePrecompileFrom(cfg, dir, rom);
				Assert.AreEqual(1, form.PrecompileEntries.Count);
				Assert.IsFalse(form.PrecompileEntries[0].Present, "a module that is not what it was is not compiled");
				Assert.IsFalse(form.PrecompileReady, "and the project cannot be created against it");
			}
			finally
			{
				Directory.Delete(dir, recursive: true);
			}
		}
	}
}
