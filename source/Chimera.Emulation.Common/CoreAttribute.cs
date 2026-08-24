namespace Chimera.Emulation.Common
{
	[AttributeUsage(AttributeTargets.Class)]
	public class CoreAttribute : Attribute
	{
		public readonly string Author;

		public readonly string CoreName;

		public readonly bool Released;

		public readonly bool SingleInstance;

		public CoreAttribute(string name, string author, bool singleInstance = false, bool isReleased = true)
		{
			Author = author;
			CoreName = name;
			Released = isReleased;
			SingleInstance = singleInstance;
		}
	}

	[AttributeUsage(AttributeTargets.Class)]
	public sealed class PortedCoreAttribute : CoreAttribute
	{
		public readonly string PortedUrl;

		public readonly string PortedVersion;

		public PortedCoreAttribute(
			string name,
			string author,
			string portedVersion = "",
			string portedUrl = "",
			bool singleInstance = false,
			bool isReleased = true)
				: base(name, author, singleInstance, isReleased)
		{
			PortedUrl = portedUrl;
			PortedVersion = portedVersion;
		}
	}

	/// <summary>
	/// Implemented by a core (or core factory) whose identity is not known at build
	/// time, and which therefore cannot be described by a <see cref="CoreAttribute"/>
	/// on its class. One generic adapter type serves every waterbox package, so the
	/// name, author and version belong to the PACKAGE, not the type; without this
	/// every such core would introduce itself with the adapter's name.
	/// </summary>
	public interface ICoreIdentity
	{
		CoreAttribute CoreIdentity { get; }
	}
}
