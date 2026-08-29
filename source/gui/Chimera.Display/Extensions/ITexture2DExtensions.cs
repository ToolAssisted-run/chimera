using System.Drawing;

namespace Chimera.Display
{
	public static class ITexture2DExtensions
	{
		public static Rectangle GetRectangle(this ITexture2D texture2D)
			=> new(0, 0, texture2D.Width, texture2D.Height);

		public static Size GetSize(this ITexture2D texture2D)
			=> new(texture2D.Width, texture2D.Height);
	}
}
