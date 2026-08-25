using Chimera.Client.Common;

namespace Chimera.Tests.Client.Common.Movie
{
	[TestClass]
	public class MovieConversionExtensionTests
	{
		[TestMethod]
		[DataRow(null, null)]
		[DataRow("", "")]
		[DataRow("C:\\Temp\\TestMovie.chimeraMovie", "C:\\Temp\\TestMovie.chimeraProject")]
		[DataRow("C:\\Temp\\TestMovie.chimeraProject.chimeraMovie", "C:\\Temp\\TestMovie.chimeraProject.chimeraProject")]
		[DataRow("C:\\Temp\\TestMovie.chimeraProject", "C:\\Temp\\TestMovie.chimeraProject")]
		public void ConvertFileNameToTasMovie(string original, string expected)
		{
			PlatformTestUtils.OnlyRunOnWindows();

			var actual = MovieConversionExtensions.ConvertFileNameToTasMovie(original);
#pragma warning disable BHI1600 // wants message argument
			Assert.AreEqual(expected, actual);
#pragma warning restore BHI1600
		}
	}
}