#nullable disable

using System.Linq;
using System.Reflection;

namespace Chimera.Emulation.Common
{
	/// <summary>
	/// This service lets the client hand a core its settings. There is exactly
	/// ONE kind: settings shape the emulated machine, so every one of them is
	/// sync-sensitive, recorded in the project, and delivered before the core
	/// boots. (The BizHawk lineage split "settings" from "sync settings"; that
	/// distinction never earned its complexity and does not exist here.)
	/// </summary>
	/// <typeparam name="TSettings">The type of the object that represents the core's settings</typeparam>
	public interface ISettable<TSettings> : IEmulatorService
		where TSettings : class, new()
	{
		// in addition to these methods, it's expected that the constructor or
		// Load() method will take a settings object to set the initial state of
		// the core (if null, default settings are to be used)

		/// <summary>
		/// get the current core settings. should be a clone of the active in-core
		/// object - changes to the returned object MUST NOT affect emulation
		/// (unless the object is later passed to PutSettings)
		/// </summary>
		/// <returns>a JSON serializable object</returns>
		TSettings GetSettings();

		/// <summary>
		/// changes the core settings. Settings shape the machine, so THIS SHOULD
		/// NEVER BE CALLED WHILE A MOVIE IS ACTIVE except through the structural
		/// change flow (which clears the greenzone and reboots).
		/// </summary>
		/// <param name="o">an object of the same type as the return for GetSettings</param>
		/// <returns>flags for what the change requires (e.g. a core reboot)</returns>
		PutSettingsDirtyBits PutSettings(TSettings o);
	}

	/// <summary>
	/// Place this attribute for TSettings classes which use System.ComponentModel.DefaultValue
	/// Classes with this attribute will have a Chimera.Common.SettingsUtil.SetDefaultValues(T) function generated
	/// </summary>
	[AttributeUsage(AttributeTargets.Class)]
	public class CoreSettingsAttribute : Attribute {}

	//note: this is a bit of a frail API. If a frontend wants a new flag, cores won't know to yea or nay it
	//this could be solved by adding a KnownSettingsDirtyBits on the settings interface
	//or, in a pinch, the same thing could be done with THESE flags, so that the interface doesn't
	//change but newly-aware cores can simply manifest that they know about more bits, in the same variable they return the bits in
	[Flags]
	public enum PutSettingsDirtyBits
	{
		None = 0,
		RebootCore = 1,
		ScreenLayoutChanged = 2,
	}

	public interface ISettingsAdapter
	{
		bool HasSettings { get; }

		/// <exception cref="InvalidOperationException">does not have settings</exception>
		object GetSettings();

		void PutCoreSettings(object s);
	}

	/// <summary>
	/// serves as a shim between strongly typed ISettable and consumers
	/// </summary>
	public sealed class SettingsAdapter : ISettingsAdapter
	{
		private readonly Action<PutSettingsDirtyBits> _handlePutCoreSettings;

		private readonly Func<bool> _mayPutCoreSettings;

		public SettingsAdapter(
			IEmulator emulator,
			Func<bool> mayPutCoreSettings,
			Action<PutSettingsDirtyBits> handlePutCoreSettings)
		{
			_handlePutCoreSettings = handlePutCoreSettings;
			_mayPutCoreSettings = mayPutCoreSettings;

			var settableType = emulator.ServiceProvider.AvailableServices
				.SingleOrDefault(t => t.IsGenericType && t.GetGenericTypeDefinition() == typeof(ISettable<>));
			if (settableType == null)
			{
				HasSettings = false;
			}
			else
			{
				var settingType = settableType.GetGenericArguments()[0];
				HasSettings = settingType != typeof(object); // object is used as a placeholder where an emu has none

				if (HasSettings)
				{
					_gets = settableType.GetMethod("GetSettings");
					_puts = settableType.GetMethod("PutSettings");
				}

				_settable = emulator.ServiceProvider.GetService(settableType);
			}
		}

		private readonly object _settable;

		public bool HasSettings { get; }

		private readonly object[] _tempObject = new object[1];
		private static readonly object[] Empty = Array.Empty<object>();

		private readonly MethodInfo _gets;
		private readonly MethodInfo _puts;

		public object GetSettings()
		{
			if (!HasSettings)
			{
				throw new InvalidOperationException();
			}

			return _gets.Invoke(_settable, Empty);
		}

		public void PutCoreSettings(object s)
		{
			if (HasSettings && _mayPutCoreSettings()) _handlePutCoreSettings(DoPutSettings(s));
		}

		private PutSettingsDirtyBits DoPutSettings(object o)
		{
			_tempObject[0] = o;
			return (PutSettingsDirtyBits)_puts.Invoke(_settable, _tempObject);
		}
	}
}
