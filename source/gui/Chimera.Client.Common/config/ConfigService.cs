using System.IO;
using System.Reflection;

using Chimera.Common;

using Newtonsoft.Json;
using Newtonsoft.Json.Linq;
using Newtonsoft.Json.Serialization;

#pragma warning disable 618

namespace Chimera.Client.Common
{
	public static class ConfigService
	{
		internal static readonly JsonSerializer Serializer;

		/// <summary>
		/// Configs and movies written before the Chimera rename carry embedded
		/// "$type" names like "BizHawk.Client.Common.X, BizHawk.Client.Common".
		/// Renaming the code must not orphan a decade of files: legacy names are
		/// rewritten to their new homes at bind time, forever.
		/// </summary>
		private sealed class LegacyNameBinder : Newtonsoft.Json.Serialization.DefaultSerializationBinder
		{
			private static string Rewrite(string name)
			{
				if (name == null) return null;
				if (name.StartsWith("BizHawk.", StringComparison.Ordinal)) name = "Chimera." + name.Substring("BizHawk.".Length);
				if (name.StartsWith("Chimera.Client.EmuHawk", StringComparison.Ordinal)) name = "Chimera.Client.GUI" + name.Substring("Chimera.Client.EmuHawk".Length);
				if (name == "EmuHawk") name = "Chimera.Client.GUI"; // the frontend assembly's pre-rename name
				return name;
			}

			public override Type BindToType(string assemblyName, string typeName)
				=> base.BindToType(Rewrite(assemblyName), Rewrite(typeName));
		}

		static ConfigService()
		{
			Serializer = new JsonSerializer
			{
				MissingMemberHandling = MissingMemberHandling.Ignore,
				TypeNameHandling = TypeNameHandling.Auto,
				ConstructorHandling = ConstructorHandling.Default,
				SerializationBinder = new LegacyNameBinder(),

				// because of the peculiar setup of Binding.cs and PathEntry.cs
				ObjectCreationHandling = ObjectCreationHandling.Replace,

				ContractResolver = new DefaultContractResolver
				{
					DefaultMembersSearchFlags = BindingFlags.Public | BindingFlags.Instance | BindingFlags.NonPublic,
				},
			};
		}


		/// <exception cref="InvalidOperationException">internal error</exception>
		public static T Load<T>(string filepath) where T : new()
		{
			T config = default(T);

			try
			{
				var file = new FileInfo(filepath);
				if (file.Exists)
				{
					using var reader = file.OpenText();
					var r = new JsonTextReader(reader);
					config = (T)Serializer.Deserialize(r, typeof(T));
				}
			}
			catch (Exception ex)
			{
				throw new InvalidOperationException("Config Error", ex);
			}

			return config ?? new T();
		}

		public static FileWriteResult Save(string filepath, object config)
		{
			return FileWriter.Write(filepath, (fs) =>
			{
				using var writer = new StreamWriter(fs);
				var w = new JsonTextWriter(writer) { Formatting = Formatting.Indented };
				Serializer.Serialize(w, config);
			});
		}

		// movie 1.0 header stuff
		private class TypeNameEncapsulator
		{
			public object o;
		}

		public static object LoadWithType(string serialized)
		{
			using var tr = new StringReader(serialized);
			using var jr = new JsonTextReader(tr);
			var tne = (TypeNameEncapsulator)Serializer.Deserialize(jr, typeof(TypeNameEncapsulator));

			// in the case of trying to deserialize nothing, tne will be nothing
			// we want to return nothing
			return tne?.o;
		}

		public static string SaveWithType(object o)
		{
			using var sw = new StringWriter();
			using var jw = new JsonTextWriter(sw) { Formatting = Formatting.None };
			var tne = new TypeNameEncapsulator { o = o };
			Serializer.Serialize(jw, tne, typeof(TypeNameEncapsulator));
			sw.Flush();
			return sw.ToString();
		}
	}
}
