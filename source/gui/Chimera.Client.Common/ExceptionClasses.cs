namespace Chimera.Client.Common
{
	public class MoviePlatformMismatchException : InvalidOperationException
	{
		public MoviePlatformMismatchException(string message) : base(message)
		{
		}
	}
}
