using System.Collections.Generic;
using System.Linq;
using System.Reflection;
using System.Text;
using Newtonsoft.Json;

namespace Chimera.Client.Common
{
	public class LuaDocumentation : List<LibraryFunction>
	{
		private class SublimeCompletions
		{
			public SublimeCompletions()
			{
				Scope = "source.lua - string";
			}

			[JsonProperty(PropertyName = "scope")]
			public string Scope { get; set; }

			[JsonProperty(PropertyName = "completions")]
			public List<Completion> Completions { get; set; } = new List<Completion>();

			public class Completion
			{
				[JsonProperty(PropertyName = "trigger")]
				public string Trigger { get; set; }

				[JsonProperty(PropertyName = "contents")]
				public string Contents { get; set; }
			}
		}

		public string ToSublime2CompletionList()
		{
			var sc = new SublimeCompletions();

			foreach (var f in this.OrderBy(lf => lf.Library).ThenBy(lf => lf.Name))
			{
				var completion = new SublimeCompletions.Completion
				{
					Trigger = $"{f.Library}.{f.Name}",
				};

				var sb = new StringBuilder();

				if (f.ParameterList.Length is not 0)
				{
					sb
						.Append($"{f.Library}.{f.Name}(");

					var parameters = f.Method.GetParameters()
						.ToList();

					for (int i = 0; i < parameters.Count; i++)
					{
						sb
							.Append("${")
							.Append(i + 1)
							.Append(':');

						sb.Append(parameters[i].IsOptional
							? $"[{parameters[i].Name}]"
							: parameters[i].Name);

						sb.Append('}');

						if (i < parameters.Count - 1)
						{
							sb.Append(',');
						}
					}

					sb.Append(')');
				}
				else
				{
					sb.Append($"{f.Library}.{f.Name}()");
				}

				completion.Contents = sb.ToString();
				sc.Completions.Add(completion);
			}

			return JsonConvert.SerializeObject(sc);
		}

		public string ToNotepadPlusPlusAutoComplete()
		{
			return ""; // TODO
		}
	}

	public class LibraryFunction
	{
		private readonly LuaMethodAttribute _luaAttributes;
		private readonly LuaMethodExampleAttribute _luaExampleAttribute;

		public readonly bool SuggestInREPL;

		public LibraryFunction(string library, string libraryDescription, MethodInfo method, bool suggestInREPL = true)
		{
			_luaAttributes = method.GetCustomAttribute<LuaMethodAttribute>(false);
			_luaExampleAttribute = method.GetCustomAttribute<LuaMethodExampleAttribute>(false);
			Method = method;
			SuggestInREPL = suggestInREPL;

			IsDeprecated = method.GetCustomAttribute<LuaDeprecatedMethodAttribute>(false) != null;
			Library = library;
			LibraryDescription = libraryDescription;
		}

		public string Library { get; }
		public string LibraryDescription { get; }

		public readonly bool IsDeprecated;

		public MethodInfo Method { get; }

		public string Name => _luaAttributes.Name;

		public string Description => _luaAttributes.Description;

		public string Example => _luaExampleAttribute?.Example;

		private string _parameterList;

		public string ParameterList
		{
			get
			{
				static string DisplayDefaultValue(object/*?*/ defaultValue)
					=> defaultValue is null
						? "nil"
						: defaultValue is string s
							? $"\"{defaultValue}\""
							: defaultValue.ToString();
				if (_parameterList == null)
				{
					var parameters = Method.GetParameters();

					var list = new StringBuilder();
					list.Append('(');
					foreach (var (i, pi) in parameters.Index())
					{
						var p = TypeCleanup(pi.ParameterType);
						if (pi.GetCustomAttribute<LuaColorParamAttribute>() is not null) p = p.Replace("object", "luacolor");
						if (pi.GetCustomAttribute<LuaZeroIndexedAttribute>() is not null) p = p.Replace("nluatable", "nluatable0Indexed");
						p += $" {pi.Name.ToLowerInvariant()}";
						list.Append(pi.IsOptional
							? $"[{p} = {DisplayDefaultValue(pi.DefaultValue)}]"
							: p);
						if (i < parameters.Length - 1)
						{
							list.Append(", ");
						}
					}

					list.Append(')');
					_parameterList = list.ToString();
				}

				return _parameterList;
			}
		}

		private static string TypeCleanup(Type type)
		{
			return type.ToString()
				.Replace("System", "")
				.Replace(" ", "")
				.Replace(".", "")
				.Replace("LuaInterface", "")
				.Replace("Object[]", "object[] ")
				.Replace("Object", "object ")
				.Replace("Nullable`1[Boolean]", "bool? ")
				.Replace("Boolean[]", "bool[] ")
				.Replace("Boolean", "bool ")
				.Replace("String", "string ")
				.Replace(/*"NLua."+*/"LuaTable", /*"nlua"+*/"table ")
				.Replace(/*"NLua."+*/"LuaFunction", /*"nlua"+*/"func ")
				.Replace("Nullable`1[Int32]", "int? ")
				.Replace("Nullable`1[UInt32]", "uint? ")
				.Replace("Byte[]", "string ")
				.Replace("Nullable`1[ReadOnlyMemory`1[Byte]]", "string? ")
				.Replace("Nullable`1[Memory`1[Byte]]", "string? ")
				.Replace("ReadOnlyMemory`1[Byte]", "string ")
				.Replace("Memory`1[Byte]", "string ")
				.Replace("Byte", "byte ")
				.Replace("Int16", "short ")
				.Replace("Int32", "int ")
				.Replace("Int64", "long ")
				.Replace("UInt32", "uint ")
				.Replace("UInt64", "ulong ")
				.Replace("Single", "float ")
				.Replace("Double", "double ")
				.Replace("Nullable`1[DrawingColor]", "Color? ")
				.Replace("DrawingColor", "Color ")
				.TrimEnd(' ')
				.ToLowerInvariant();
		}

		private string/*?*/ field = null;
		public string ReturnType
		{
			get
			{
				if (field is not null) return field;
				var returnType = TypeCleanup(Method.ReturnType).Trim();
				if (Method.ReturnTypeCustomAttributes.GetCustomAttributes(typeof(LuaZeroIndexedAttribute), inherit: false).Length is not 0)
				{
					returnType = returnType.Replace("nluatable", "nluatable0Indexed");
				}
				return field = returnType;
			}
		}
	}
}
