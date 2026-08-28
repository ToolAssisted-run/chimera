namespace Chimera.Bizware.Graphics.Controls
{
	/// <summary>
	/// A factory for creating a GraphicsControl based on an IGL
	/// </summary>
	public static class GraphicsControlFactory
	{
		public static GraphicsControl CreateGraphicsControl(IGL gl)
		{
			GraphicsControl ret = new OpenGLControl(((IGL_OpenGL)gl).InitGLState);

			// IGLs need the window handle in order to do things, so best create the control immediately
			ret.CreateControl();
			return ret;
		}
	}
}