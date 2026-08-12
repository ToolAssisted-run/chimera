using BizHawk.Client.Common;

namespace BizHawk.Tests.Client.Common.Movie
{
	[TestClass]
	public class MovieConversionExtensionTests
	{
		[TestMethod]
		[DataRow(null, null)]
		[DataRow("", "")]
		[DataRow("C:\\Temp\\TestMovie.tas", "C:\\Temp\\TestMovie.tasproj")]
		[DataRow("C:\\Temp\\TestMovie.tasproj.tas", "C:\\Temp\\TestMovie.tasproj.tasproj")]
		[DataRow("C:\\Temp\\TestMovie.tasproj", "C:\\Temp\\TestMovie.tasproj")]
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