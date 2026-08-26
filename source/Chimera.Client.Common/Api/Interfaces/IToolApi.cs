using System.Collections.Generic;

namespace Chimera.Client.Common
{
	public interface IToolApi : IExternalApi
	{
		IEnumerable<Type> AvailableTools { get; }

		object CreateInstance(string name);

		IToolForm GetTool(string name);


		void OpenHexEditor();

		void OpenRamSearch();

		void OpenRamWatch();

		void OpenTasStudio();

	}
}

