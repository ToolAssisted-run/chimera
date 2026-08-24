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

		private readonly Type _typeSS;

		public bool HasSettings { get; }

		public bool HasSyncSettings { get; }

		public ConfigSettingsAdapter(Config config)
		{
			_config = config;
			var settableType = typeof(T).GetInterfaces()
				.SingleOrDefault(t => t.IsGenericType && t.GetGenericTypeDefinition() == typeof(ISettable<,>));
			if (settableType == null)
			{
				_typeS = typeof(object);
				_typeSS = typeof(object);
			}
			else
			{
				var tt = settableType.GetGenericArguments();
				_typeS = tt[0];
				_typeSS = tt[1];
			}
			HasSettings = _typeS != typeof(object);
			HasSyncSettings = _typeSS != typeof(object);
		}

		public object GetSettings()
			=> _config.GetCoreSettings(typeof(T), _typeS)
				?? Activator.CreateInstance(_typeS);

		public object GetSyncSettings()
			=> _config.GetCoreSyncSettings(typeof(T), _typeSS)
				?? Activator.CreateInstance(_typeSS);

		public void PutCoreSettings(object s)
			=> _config.PutCoreSettings(s, typeof(T));

		public void PutCoreSyncSettings(object ss)
			=> _config.PutCoreSyncSettings(ss, typeof(T));
	}

	/// <summary>Like <see cref="ConfigSettingsAdapter{T}"/>, for core types only known at runtime (e.g. from core packages).</summary>
	public sealed class ConfigSettingsAdapterUntyped : ISettingsAdapter
	{
		private readonly Config _config;

		private readonly Type _coreType;

		private readonly Type _typeS;

		private readonly Type _typeSS;

		public bool HasSettings { get; }

		public bool HasSyncSettings { get; }

		public ConfigSettingsAdapterUntyped(Config config, Type coreType)
		{
			_config = config;
			_coreType = coreType;
			var settableType = coreType.GetInterfaces()
				.SingleOrDefault(t => t.IsGenericType && t.GetGenericTypeDefinition() == typeof(ISettable<,>));
			if (settableType == null)
			{
				_typeS = typeof(object);
				_typeSS = typeof(object);
			}
			else
			{
				var tt = settableType.GetGenericArguments();
				_typeS = tt[0];
				_typeSS = tt[1];
			}
			HasSettings = _typeS != typeof(object);
			HasSyncSettings = _typeSS != typeof(object);
		}

		public object GetSettings()
			=> _config.GetCoreSettings(_coreType, _typeS)
				?? Activator.CreateInstance(_typeS);

		public object GetSyncSettings()
			=> _config.GetCoreSyncSettings(_coreType, _typeSS)
				?? Activator.CreateInstance(_typeSS);

		public void PutCoreSettings(object s)
			=> _config.PutCoreSettings(s, _coreType);

		public void PutCoreSyncSettings(object ss)
			=> _config.PutCoreSyncSettings(ss, _coreType);
	}
}
