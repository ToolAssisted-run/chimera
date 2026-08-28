#nullable enable

using System.Linq;

using Chimera.Emulation.Common;

namespace Chimera.Client.Common
{
	public sealed class ConfigSettingsAdapter<T> : ISettingsAdapter
		where T : IEmulator
	{
		private readonly Config _config;

		private readonly Type _typeS;

		public bool HasSettings { get; }

		public ConfigSettingsAdapter(Config config)
		{
			_config = config;
			var settableType = typeof(T).GetInterfaces()
				.SingleOrDefault(t => t.IsGenericType && t.GetGenericTypeDefinition() == typeof(ISettable<>));
			_typeS = settableType == null ? typeof(object) : settableType.GetGenericArguments()[0];
			HasSettings = _typeS != typeof(object);
		}

		public object GetSettings()
			=> _config.GetCoreSettings(typeof(T), _typeS)
				?? Activator.CreateInstance(_typeS);

		public void PutCoreSettings(object s)
			=> _config.PutCoreSettings(s, typeof(T));
	}

	/// <summary>Like <see cref="ConfigSettingsAdapter{T}"/>, for core types only known at runtime (e.g. from core packages).</summary>
	public sealed class ConfigSettingsAdapterUntyped : ISettingsAdapter
	{
		private readonly Config _config;

		private readonly Type _coreType;

		private readonly Type _typeS;

		public bool HasSettings { get; }

		public ConfigSettingsAdapterUntyped(Config config, Type coreType)
		{
			_config = config;
			_coreType = coreType;
			var settableType = coreType.GetInterfaces()
				.SingleOrDefault(t => t.IsGenericType && t.GetGenericTypeDefinition() == typeof(ISettable<>));
			_typeS = settableType == null ? typeof(object) : settableType.GetGenericArguments()[0];
			HasSettings = _typeS != typeof(object);
		}

		public object GetSettings()
			=> _config.GetCoreSettings(_coreType, _typeS)
				?? Activator.CreateInstance(_typeS);

		public void PutCoreSettings(object s)
			=> _config.PutCoreSettings(s, _coreType);
	}
}
