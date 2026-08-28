using Chimera.Client.Common;

namespace Chimera.Client.GUI
{
	/// <summary>
	/// Defines a <see cref="IToolForm"/> as a specialized tool that is for a specific system or core
	/// </summary>
	[AttributeUsage(AttributeTargets.Class)]
	public class SpecializedToolAttribute : Attribute
	{
		public SpecializedToolAttribute(string displayName)
		{
			DisplayName = displayName;
		}

		public string DisplayName { get; }
	}
}
