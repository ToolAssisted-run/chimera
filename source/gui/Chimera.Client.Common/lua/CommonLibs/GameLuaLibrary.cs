using System.ComponentModel;
using System.Linq;

using Chimera.Emulation.Common;

using NLua;

// ReSharper disable UnusedMember.Global
// ReSharper disable UnusedAutoPropertyAccessor.Local
namespace Chimera.Client.Common
{
	[Description("A game core's properties by name (docs/game-cores.md): the Kid's position, a level's name, an array of guards - what the core's property table names, read and written by the engine's rules. An array's element is named by the number the game calls it, from 0 unless the core says otherwise (\"Guards.X[2]\"); the array by its own name is a table, from 1 as Lua counts. An emulator core has none, and list() is empty.")]
	public sealed class GameLuaLibrary : LuaLibraryBase
	{
		[OptionalService]
		private IGameProperties Properties { get; set; }

		public GameLuaLibrary(ILuaLibraries luaLibsImpl, ApiContainer apiContainer, Action<string> logOutputCallback)
			: base(luaLibsImpl, apiContainer, logOutputCallback) {}

		public override string Name => "game";

		[LuaMethodExample("for _, name in ipairs(game.list()) do console.log(name .. \" = \" .. tostring(game.get(name))); end;")]
		[LuaMethod("list", "Returns the names of the loaded core's game properties, in the order the core lists them (an array once, by its own name); empty for a core without any. A core whose properties come and go while it runs - a Flash movie's variables - is asked for them as they are now")]
		public LuaTable List()
		{
			// a dynamic table is asked for as it is now: what a movie keeps changes as it runs
			if (Properties is { IsDynamic: true }) Properties.Refresh();
			return _th.ListToTable((Properties?.Properties ?? [ ]).Where(static p => p.Listed).Select(static p => p.Name).ToList());
		}

		[LuaMethodExample("local x = game.get(\"Kid.X\"); local second = game.get(\"Guards.X[1]\"); local all = game.get(\"Guards.X\");")]
		[LuaMethod("get", "Returns a game property's value: an integer (a u64 as the integer with the same 64 bits), a float for f32/f64, a boolean for bool, a string, or a table of byte values for bytes. An array by its own name is a table of its elements. A name the core does not have returns nil and says so in the console")]
		public object Get(string name)
		{
			if (Find("get", name) is not { } element) return null;
			if (element.Property.IsArray && !name.TrimEnd().EndsWith("]"))
			{
				return _th.ListToTable(element.Property.Elements.Select(ToLua).ToList());
			}
			return ToLua(element);
		}

		[LuaMethodExample("game.set(\"Kid.HP\", 3); game.set(\"Level Name\", \"Dungeon\"); game.set(\"Guards.X[1]\", -40);")]
		[LuaMethod("set", "Sets a game property; the game's next step sees it. Takes a number, a boolean, a string, or a table of byte values for bytes; an array by its own name takes a table of its elements. Returns whether it was set: not for a name the core does not have, a value it cannot hold, or a property the game works out afresh every step - and the console says why")]
		public bool Set(string name, object value)
		{
			if (Find("set", name) is not { } element) return false;
			if (element.Property.IsArray && !name.TrimEnd().EndsWith("]"))
			{
				if (value is not LuaTable table)
				{
					Log($"game.set: \"{element.Property.Name}\" is an array of {element.Property.Count}, so it takes a table of them");
					return false;
				}
				var all = true;
				foreach (var e in element.Property.Elements)
				{
					if (table[(long)e.Index + 1] is { } item) all &= SetOne(e, item);
				}
				return all;
			}
			return SetOne(element, value);
		}

		[LuaMethodExample("local info = game.describe(\"Guards.X\"); console.log(info.type .. \"[\" .. info.count .. \"] at \" .. info.domain .. \":\" .. info.offset);")]
		[LuaMethod("describe", "Returns a table describing a game property: name, domain, offset (of the element named, or the first; where it is now, for a property that moves, and -1 while it is not there), type, size (bytes in one element), count, first (the number the first element is called by), stride, endian, encoding, bit, bits, group, writable, description, and label (the value as the core names it, when it has names for its values); nil for a name the core does not have")]
		public LuaTable Describe(string name)
		{
			if (Find("describe", name) is not { } element) return null;
			var p = element.Property;
			var table = _th.CreateTable();
			table["name"] = element.Name;
			table["domain"] = p.Domain;
			table["offset"] = Properties.AddressNow(element);
			table["type"] = p.TypeName;
			table["size"] = (long)p.Size;
			table["count"] = (long)p.Count;
			table["first"] = (long)p.First;
			table["stride"] = (long)p.Stride;
			table["endian"] = p.BigEndian ? "big" : "little";
			table["encoding"] = p.Encoding;
			table["bit"] = (long)p.Bit;
			table["bits"] = (long)p.Bits;
			table["group"] = p.Group;
			table["writable"] = p.Writable;
			table["description"] = p.Description;
			table["label"] = Properties.Text(element);
			return table;
		}

		private object ToLua(GamePropertyElement element)
			=> Properties.Get(element) switch
			{
				ulong u => unchecked((long)u), // Lua's integers are 64 bits wide and signed
				byte[] bytes => _th.ListToTable(bytes.Select(static b => (long)b).ToList()),
				var other => other,
			};

		private bool SetOne(GamePropertyElement element, object value)
		{
			object given = value;
			if (value is LuaTable table)
			{
				// a table of byte values, for a bytes property: all of them, as the engine takes
				var count = element.Property.Size;
				var entries = table.Keys.Cast<object>().Count();
				if (entries != count || table[(long)count] is null)
				{
					Log($"game.set: {element.Name} is {count} bytes, so it takes a table of {count}, not {entries}");
					return false;
				}
				var bytes = new byte[count];
				for (var k = 0; k < count; k++) bytes[k] = table[(long)k + 1] is long b ? unchecked((byte)b) : table[(long)k + 1] is double d ? unchecked((byte)d) : (byte)0;
				given = bytes;
			}
			if (Properties.Set(element, given) is { } refused)
			{
				Log($"game.set: {element.Name}: {refused}");
				return false;
			}
			return true;
		}

		/// <summary>
		/// The element, or null with the reason in the console. Not an exception: one thrown
		/// back through Lua after the script has yielded a frame takes the whole process down
		/// under Mono, even inside a pcall - the memory library's way (say it, and carry on)
		/// is the one that is safe.
		/// </summary>
		private GamePropertyElement Find(string function, string name)
		{
			if (Properties?.Find(name ?? "") is { } element) return element;
			Log(Properties is null
				? $"game.{function}: the loaded core has no game properties (no \"{name}\")"
				: $"game.{function}: the loaded core has no game property \"{name}\"; game.list() says which it has");
			return null;
		}
	}
}
