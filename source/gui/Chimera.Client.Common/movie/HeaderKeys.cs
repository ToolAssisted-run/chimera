using System.Collections.Frozen;
using System.Collections.Generic;
using System.Linq;

namespace Chimera.Client.Common
{
	public static class HeaderKeys
	{
		public const string EmulatorVersion = "emuVersion";
		public const string OriginalEmulatorVersion = "OriginalEmuVersion";
		public const string MovieVersion = "MovieVersion";
		public const string Platform = "Platform";
		public const string GameName = "GameName";
		public const string Author = "Author";
		public const string Rerecords = "rerecordCount";
		public const string StartsFromSavestate = "StartsFromSavestate";
		public const string SavestateBinaryBase64Blob = "SavestateBinaryBase64Blob"; // this string will not contain base64: ; it's implicit (this is to avoid another big string op to dice off the base64: substring)
		public const string Sha1 = "SHA1"; // misleading name; either CRC32, MD5, or SHA1, hex-encoded, unprefixed
		public const string Sha256 = "SHA256";
		public const string Md5 = "MD5";
		public const string Crc32 = "CRC32";
		public const string Pal = "PAL";
		public const string BoardName = "BoardName";
		public const string CycleCount = "CycleCount";
		public const string ClockRate = "ClockRate";
		public const string VsyncAttoseconds = "VsyncAttoseconds"; // used for Arcade due to it representing thousands of different systems with different vsync rates

		// The rate the machine actually ran at, as the core reports it. Chimera
		// keeps no per-system rate table - the exact rate is the core's, and a
		// core is a package this frontend knows nothing about - so a movie that
		// did not write it down cannot be turned into a duration by anything
		// except the core that recorded it.
		public const string VsyncNumerator = "VsyncNumerator";
		public const string VsyncDenominator = "VsyncDenominator";

		// The last frame anything is pressed on. Derived from the input log, and
		// written on every save because it changes with the log: a reader that
		// wants where the run's input stops should not have to know what a
		// neutral entry looks like for this core's controller.
		public const string LastInputFrame = "LastInputFrame";

		// A GPU outside the sandbox drew this run's pictures - much faster than
		// the software rasteriser and NOT deterministic: the GPU is outside the
		// savestate and differs between machines. Recorded so that a replay
		// which desyncs somewhere else can be understood rather than merely
		// suffered. The value is what the driver called itself.
		public const string GpuRenderer = "GpuRenderer";

		/// <summary>
		/// "1" when the core that made this can load its own states into a
		/// session with another GL context (see IGpuRendered). Written only
		/// alongside GpuRenderer, and read when a project's cached states are
		/// opened - by which time there is no core to ask.
		/// </summary>
		public const string GpuStatesSurvive = "GpuStatesSurvive";
		public const string Core = "Core";
		public const string WaterboxHost = "WaterboxHost"; // the sandbox the core ran inside, by its own build info
		public const string Firmware = "Firmware"; // "<id>=<sha1>" per file the core was given; a BIOS changes what runs
		public const string CoreVersion = "CoreVersion"; // the core's authoritative version: the commit its published build was made from
		public const string CorePackageSha1 = "CorePackageSHA1"; // SHA1 of the package file; the same version can be built twice, this says which build

		private static FrozenSet<string> field;

		private static ISet<string> AllValues
			=> field ??= typeof(HeaderKeys).GetFields()
				.Select(static fi => fi.GetValue(null).ToString())
				.ToFrozenSet();

		public static bool Contains(string val)
			=> AllValues.Contains(val);
	}
}
