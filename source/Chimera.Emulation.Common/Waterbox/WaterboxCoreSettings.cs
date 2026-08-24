using System;
using System.Collections;
using System.Collections.Generic;
using System.ComponentModel;
using System.Linq;

using Newtonsoft.Json;

namespace Chimera.Emulation.Common.Waterbox
{
	/// <summary>
	/// A bag of user settings for a waterbox core, and - the whole point of this
	/// class - a description of itself that a property grid can draw.
	///
	/// The frontend has no per-core settings dialogs: one adapter serves every
	/// package, so there is no class whose properties could be the settings. The
	/// package declares them instead (<see cref="WaterboxConfig.SettingDecl"/>) and
	/// this synthesizes a property per declaration through
	/// <see cref="ICustomTypeDescriptor"/>, which is what WinForms' PropertyGrid
	/// asks for names, types, descriptions and defaults. So the grid shows a
	/// labelled, typed, documented row per setting, and every word in it came from
	/// the core.
	///
	/// Only <see cref="Values"/> is serialized: the config file and movie headers
	/// store a plain name -&gt; value map, unchanged by any of this.
	/// </summary>
	public abstract class WaterboxSettingsBase : ICustomTypeDescriptor
	{
		public Dictionary<string, object> Values { get; set; } = new();

		/// <summary>
		/// The declarations these values belong to. Not serialized and not part of
		/// equality: it is the core talking about the values, not one of them.
		/// Null when the settings were deserialized without a core in hand (from the
		/// config file at startup, say), in which case the grid simply has nothing to
		/// draw - which is correct, since no core is loaded to draw it for.
		/// </summary>
		[JsonIgnore]
		public IReadOnlyList<WaterboxConfig.SettingDecl> Declarations { get; set; }

		/// <summary>True if <paramref name="other"/> holds the same values.</summary>
		public bool ValuesEqual(WaterboxSettingsBase other)
		{
			var a = Values ?? new Dictionary<string, object>();
			var b = other?.Values ?? new Dictionary<string, object>();
			if (a.Count != b.Count) return false;
			foreach (var (k, v) in a)
			{
				if (!b.TryGetValue(k, out var w)) return false;
				// JSON round-trips turn 8 into 8L and back; compare as text so a value
				// that survived a config file still equals the one that did not
				if (!string.Equals(Convert.ToString(v), Convert.ToString(w), StringComparison.Ordinal)) return false;
			}
			return true;
		}

		protected void CopyTo(WaterboxSettingsBase clone)
		{
			clone.Values = Values is null ? new Dictionary<string, object>() : new Dictionary<string, object>(Values);
			clone.Declarations = Declarations;
		}

		private IReadOnlyList<WaterboxConfig.SettingDecl> Decls
			=> Declarations ?? [ ];

		// ---- ICustomTypeDescriptor: everything but GetProperties is the default ----

		public AttributeCollection GetAttributes() => TypeDescriptor.GetAttributes(GetType(), noCustomTypeDesc: true);

		public string GetClassName() => TypeDescriptor.GetClassName(GetType(), noCustomTypeDesc: true);

		public string GetComponentName() => TypeDescriptor.GetComponentName(GetType(), noCustomTypeDesc: true);

		public TypeConverter GetConverter() => TypeDescriptor.GetConverter(GetType(), noCustomTypeDesc: true);

		public EventDescriptor GetDefaultEvent() => TypeDescriptor.GetDefaultEvent(GetType(), noCustomTypeDesc: true);

		public PropertyDescriptor GetDefaultProperty() => null;

		public object GetEditor(Type editorBaseType) => TypeDescriptor.GetEditor(GetType(), editorBaseType, noCustomTypeDesc: true);

		public EventDescriptorCollection GetEvents() => TypeDescriptor.GetEvents(GetType(), noCustomTypeDesc: true);

		public EventDescriptorCollection GetEvents(Attribute[] attributes) => TypeDescriptor.GetEvents(GetType(), attributes, noCustomTypeDesc: true);

		public object GetPropertyOwner(PropertyDescriptor pd) => this;

		public PropertyDescriptorCollection GetProperties() => GetProperties(null);

		public PropertyDescriptorCollection GetProperties(Attribute[] attributes)
			=> new(Decls.Select(static d => (PropertyDescriptor) new SettingPropertyDescriptor(d)).ToArray());

		/// <summary>One declared setting, presented to the grid as if it were a property.</summary>
		private sealed class SettingPropertyDescriptor : PropertyDescriptor
		{
			private readonly WaterboxConfig.SettingDecl _decl;

			public SettingPropertyDescriptor(WaterboxConfig.SettingDecl decl)
				: base(decl.Name, BuildAttributes(decl))
				=> _decl = decl;

			private static Attribute[] BuildAttributes(WaterboxConfig.SettingDecl decl)
			{
				List<Attribute> attrs = [ new DisplayNameAttribute(decl.DisplayName) ];
				if (!string.IsNullOrWhiteSpace(decl.Description)) attrs.Add(new DescriptionAttribute(decl.Description));
				if (decl.EffectiveType is "enum") attrs.Add(new TypeConverterAttribute(typeof(OptionsConverter)));
				return attrs.ToArray();
			}

			public override Type ComponentType => typeof(WaterboxSettingsBase);

			public override bool IsReadOnly => false;

			public override Type PropertyType => _decl.ClrType;

			public override TypeConverter Converter
				=> _decl.EffectiveType is "enum" ? new OptionsConverter(_decl) : base.Converter;

			public override object GetValue(object component)
			{
				var values = ((WaterboxSettingsBase) component).Values;
				return _decl.Coerce(values is not null && values.TryGetValue(_decl.Name, out var v) ? v : _decl.Default);
			}

			public override void SetValue(object component, object value)
			{
				var settings = (WaterboxSettingsBase) component;
				settings.Values ??= new Dictionary<string, object>();
				settings.Values[_decl.Name] = _decl.Coerce(value);
			}

			public override void ResetValue(object component) => SetValue(component, _decl.DefaultValue);

			public override bool CanResetValue(object component) => true;

			// "should serialize" drives the grid's bolding: a value that differs from
			// the core's default is shown bold, which is the only cue that a setting
			// has been changed from what the package ships with
			public override bool ShouldSerializeValue(object component)
				=> !Equals(GetValue(component), _decl.DefaultValue);
		}

		/// <summary>Turns an enum setting's declared options into a dropdown.</summary>
		private sealed class OptionsConverter : StringConverter
		{
			private readonly WaterboxConfig.SettingDecl _decl;

			public OptionsConverter()
			{
			}

			public OptionsConverter(WaterboxConfig.SettingDecl decl) => _decl = decl;

			public override bool GetStandardValuesSupported(ITypeDescriptorContext context) => _decl?.Options is { Count: > 0 };

			/// <summary>The list is the whole set of legal values, so free text is not.</summary>
			public override bool GetStandardValuesExclusive(ITypeDescriptorContext context) => true;

			public override StandardValuesCollection GetStandardValues(ITypeDescriptorContext context)
				=> new((ICollection) (_decl?.Options ?? [ ]));
		}
	}

	/// <summary>
	/// Settings that do not shape the emulated machine (presentation and the like).
	/// Not recorded in movies, and changing one does not invalidate a run.
	/// </summary>
	public sealed class WaterboxCoreSettings : WaterboxSettingsBase
	{
		public WaterboxCoreSettings Clone()
		{
			WaterboxCoreSettings clone = new();
			CopyTo(clone);
			return clone;
		}
	}

	/// <summary>
	/// Settings that shape the machine, so they are part of a movie's reproduction
	/// contract: merged over the package's declared defaults, delivered to the guest
	/// before Init, and recorded in movie headers.
	/// </summary>
	public sealed class WaterboxCoreSyncSettings : WaterboxSettingsBase
	{
		public WaterboxCoreSyncSettings Clone()
		{
			WaterboxCoreSyncSettings clone = new();
			CopyTo(clone);
			return clone;
		}
	}
}
