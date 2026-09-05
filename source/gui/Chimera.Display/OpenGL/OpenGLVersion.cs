using System.Collections.Generic;

using Silk.NET.OpenGL;

using Chimera.Common.CollectionExtensions;

namespace Chimera.Display
{
	/// <summary>
	/// Wraps checking OpenGL versions
	/// </summary>
	public static class OpenGLVersion
	{
		private static readonly IDictionary<int, bool> _glSupport = new Dictionary<int, bool>();

		/// <summary>
		/// What the last probe found, in words a person can act on: the driver's
		/// version, renderer and vendor strings when a context came up, or the
		/// SDL/GL error when one did not. The probe used to keep this to itself
		/// on standard error, which a Windows user never sees - so "requires
		/// OpenGL 3.2" reached people whose driver says 4.6, with nothing to go
		/// on (issue #39).
		/// </summary>
		public static string LastProbe { get; private set; } = "no probe has run";

		private static int PackGLVersion(int major, int minor)
			=> major * 10 + minor;

		private static bool CheckVersion(int requestedMajor, int requestedMinor)
		{
			using (new SavedOpenGLContext())
			{
				try
				{
					using (new SDL2OpenGLContext(requestedMajor, requestedMinor, true))
					{
						using var gl = GL.GetApi(SDL2OpenGLContext.GetGLProcAddress);
						var versionString = gl.GetStringS(StringName.Version);
						LastProbe = $"the driver answered a {requestedMajor}.{requestedMinor} request with OpenGL \"{versionString}\""
							+ $" on \"{gl.GetStringS(StringName.Renderer)}\" by \"{gl.GetStringS(StringName.Vendor)}\"";
						var versionParts = versionString!.Split('.');
						var major = int.Parse(versionParts[0]);
						var minor = int.Parse(versionParts[1][0].ToString());
						return PackGLVersion(major, minor) >= PackGLVersion(requestedMajor, requestedMinor);
					}
				}
				catch (Exception ex)
				{
					LastProbe = $"no {requestedMajor}.{requestedMinor} context could be made: {ex.Message}";
					Console.Error.WriteLine($"OpenGL check for version {requestedMajor}.{requestedMinor} failed, underlying exception: {ex}");
					return false;
				}
			}
		}

		public static bool SupportsVersion(int major, int minor)
			=> _glSupport.GetValueOrPut(PackGLVersion(major, minor),
				static version => CheckVersion(version / 10, version % 10));
	}
}
