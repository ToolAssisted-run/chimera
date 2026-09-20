using System.ComponentModel;

using NLua;
using Chimera.Emulation.Common;

// ReSharper disable UnusedMember.Global
// ReSharper disable UnusedAutoPropertyAccessor.Local
namespace Chimera.Client.Common
{
	[Description("A library for registering lua functions to emulator events.\n All events support multiple registered methods.\nAll registered event methods can be named and return a Guid when registered")]
	public sealed class EventsLuaLibrary : LuaLibraryBase, IRegisterFunctions
	{
		internal static readonly string EMPTY_UUID_STR = Guid.Empty.ToString("D");

		public NLFAddCallback CreateAndRegisterNamedFunction { get; set; }

		public NLFRemoveCallback RemoveNamedFunctionMatching { get; set; }

		[OptionalService]
		private IInputPollable InputPollableCore { get; set; }

		[OptionalService]
		private IDebuggable DebuggableCore { get; set; }

		[RequiredService]
		private IEmulator Emulator { get; set; }

		[OptionalService]
		private IMemoryDomains Domains { get; set; }

		public EventsLuaLibrary(ILuaLibraries luaLibsImpl, ApiContainer apiContainer, Action<string> logOutputCallback)
			: base(luaLibsImpl, apiContainer, logOutputCallback) {}

		public override string Name => "event";

		private void AddMemCallbackOnCore(INamedLuaFunction nlf, MemoryCallbackType kind, string/*?*/ scope, uint? address)
		{
			var memCallbackImpl = DebuggableCore.MemoryCallbacks;
			MemoryCallbackDelegate memCallback = (addr, val, flags) =>
				nlf.Call(addr, val, flags) is [ long n ] ? unchecked((uint) n) : null;

			memCallbackImpl.Add(new MemoryCallback(
				ProcessScope(scope),
				kind,
				"Lua Hook",
				memCallback,
				address,
				null));
			nlf.OnRemove += () => memCallbackImpl.Remove(memCallback);
		}

		/* A registration that cannot be honoured raises a Lua error rather than
		 * handing the script an id. It used to log a line and return the empty
		 * GUID, which reads as success: the script carries on, the callback
		 * never fires, and the run looks like a bug in the script or in the
		 * game rather than a feature that was never there. A callback that
		 * never fires is worse than one that refuses, because nothing says so
		 * at the point the script is wrong (ToolAssisted-run/chimera#113).
		 *
		 * The sandbox catches this, shows it against the script and stops that
		 * script; the frontend and the emulation are unaffected. */
		private Exception MemoryCallbacksNotImplemented(bool isWildcard, bool execute)
			=> new InvalidOperationException(
				$"{Emulator.Attributes().CoreName} does not implement {(isWildcard ? "wildcard " : string.Empty)}memory {(execute ? "execute " : string.Empty)}callbacks. "
				+ "Memory callbacks are a core-by-core feature: a core has them when it carries the memory hook, which is the core "
				+ "watching its own addresses and calling out only when the machine touches one. A core that emulates its CPU by "
				+ (execute
					? "interpretation can see every instruction fetch; one that recompiles usually cannot, which is why it may have read and write callbacks but not execute ones. "
					: "interpretation can carry it; one that recompiles into host code usually cannot. ")
				+ "Remove the registration or guard it.");

		private Exception ScopeNotAvailable(string scope)
			=> new InvalidOperationException(
				$"{scope} is not an available scope for {Emulator.Attributes().CoreName}");

		[LuaMethod("can_use_callback_params", "Returns whether Chimera will pass arguments to callbacks. The current version passes arguments to \"memory\" callbacks (RAM/ROM/bus R/W), so this function will return true for that input. (It returns false for any other input.) This tells you whether it's necessary to enable workarounds/hacks because a script is running in a version without parameter support.")]
		[LuaMethodExample("local mem_callback = event.can_use_callback_params(\"memory\") and mem_callback or mem_callback_pre_29;")]
		public bool CanUseCallbackParams(string subset = null)
			=> subset is "memory";

		[LuaMethodExample("local steveonf = event.onframeend(\r\n\tfunction()\r\n\t\tconsole.log( \"Calls the given lua function at the end of each frame, after all emulation and drawing has completed. Note: this is the default behavior of lua scripts\" );\r\n\tend\r\n\t, \"Frame name\" );")]
		[LuaMethod("onframeend", "Calls the given lua function at the end of each frame, after all emulation and drawing has completed. Note: this is the default behavior of lua scripts")]
		public string OnFrameEnd(LuaFunction luaf, string name = null)
			=> CreateAndRegisterNamedFunction(luaf, NamedLuaFunction.EVENT_TYPE_POSTFRAME, name: name)
				.GuidStr;

		[LuaMethodExample("local steveonf = event.onframestart(\r\n\tfunction()\r\n\t\tconsole.log( \"Calls the given lua function at the beginning of each frame before any emulation and drawing occurs\" );\r\n\tend\r\n\t, \"Frame name\" );")]
		[LuaMethod("onframestart", "Calls the given lua function at the beginning of each frame before any emulation and drawing occurs")]
		public string OnFrameStart(LuaFunction luaf, string name = null)
			=> CreateAndRegisterNamedFunction(luaf, NamedLuaFunction.EVENT_TYPE_PREFRAME, name: name)
				.GuidStr;

		[LuaMethodExample("local steveoni = event.oninputpoll(\r\n\tfunction()\r\n\t\tconsole.log( \"Calls the given lua function after each time the emulator core polls for input\" );\r\n\tend\r\n\t, \"Frame name\" );")]
		[LuaMethod("oninputpoll", "Calls the given lua function after each time the emulator core polls for input")]
		public string OnInputPoll(LuaFunction luaf, string name = null)
		{
			// The core is asked FIRST, and the function is registered only once
			// the answer is yes. The old order registered and then discovered it
			// could not hook anything, which left the script a function that was
			// never going to be called.
			IInputCallbackSystem inputCallbackImpl;
			try
			{
				if (InputPollableCore is null) throw InputPollCallbacksNotImplemented();
				inputCallbackImpl = InputPollableCore.InputCallbacks;
			}
			catch (NotImplementedException)
			{
				throw InputPollCallbacksNotImplemented();
			}

			var nlf = CreateAndRegisterNamedFunction(luaf, NamedLuaFunction.EVENT_TYPE_INPUTPOLL, ApiGroup.PROHIBITED_MID_FRAME, name: name);
			Action InputCallback = () => nlf.Call();
			inputCallbackImpl.Add(InputCallback);
			nlf.OnRemove += () => inputCallbackImpl.Remove(InputCallback);
			return nlf.GuidStr;
		}

		/// <summary>Same refusal as the memory callbacks above, for the same reason.</summary>
		private Exception InputPollCallbacksNotImplemented()
			=> new InvalidOperationException(
				$"{Emulator.Attributes().CoreName} does not implement input polling callbacks. "
				+ "No Chimera core does yet: unlike the memory hook, nothing tells a core to report the moment it reads a controller. "
				+ "Remove the registration or guard it.");

		[LuaDeprecatedMethod]
		[LuaMethod("onmemoryexecute", "Fires immediately before the given address is executed by the core. Your callback can have 3 parameters {{(addr, val, flags)}}. {{val}} is the value to be executed (or {{0}} always, if this feature is only partially implemented).")]
		public string OnMemoryExecute(
			LuaFunction luaf,
			uint address,
			string name = null,
			string scope = null)
		{
//			Log("Deprecated function event.onmemoryexecute() used, replace the call with event.on_bus_exec().");
			return OnBusExec(luaf, address, name: name, scope: scope);
		}

		[LuaMethodExample("local exec_cb_id = event.on_bus_exec(\r\n\tfunction(addr, val, flags)\r\n\t\tconsole.log( \"Fires immediately before the given address is executed by the core. {{val}} is the value to be executed (or {{0}} always, if this feature is only partially implemented).\" );\r\n\tend\r\n\t, 0x200, \"Frame name\", \"System Bus\" );")]
		[LuaMethod("on_bus_exec", "Fires immediately before the given address is executed by the core. Your callback can have 3 parameters {{(addr, val, flags)}}. {{val}} is the value to be executed (or {{0}} always, if this feature is only partially implemented).")]
		public string OnBusExec(
			LuaFunction luaf,
			uint address,
			string name = null,
			string scope = null)
		{
			try
			{
				if (DebuggableCore is not null
					&& DebuggableCore.MemoryCallbacksAvailable()
					&& DebuggableCore.MemoryCallbacks.ExecuteCallbacksAvailable)
				{
					if (!HasScope(scope)) throw ScopeNotAvailable(scope);

					var nlf = CreateAndRegisterNamedFunction(luaf, NamedLuaFunction.EVENT_TYPE_MEMEXEC, ApiGroup.PROHIBITED_MID_FRAME, name: name);
					AddMemCallbackOnCore(nlf, MemoryCallbackType.Execute, scope, address);
					return nlf.GuidStr;
				}
			}
			catch (NotImplementedException)
			{
				throw MemoryCallbacksNotImplemented(isWildcard: false, execute: true);
			}

			throw MemoryCallbacksNotImplemented(isWildcard: false, execute: true);
		}

		[LuaDeprecatedMethod]
		[LuaMethod("onmemoryexecuteany", "Fires immediately before every instruction executed (in the specified scope) by the core (CPU-intensive). Your callback can have 3 parameters {{(addr, val, flags)}}. {{val}} is the value to be executed (or {{0}} always, if this feature is only partially implemented).")]
		public string OnMemoryExecuteAny(
			LuaFunction luaf,
			string name = null,
			string scope = null)
		{
//			Log("Deprecated function event.onmemoryexecuteany(...) used, replace the call with event.on_bus_exec_any(...).");
			return OnBusExecAny(luaf, name: name, scope: scope);
		}

		[LuaMethodExample("local exec_cb_id = event.on_bus_exec_any(\r\n\tfunction(addr, val, flags)\r\n\t\tconsole.log( \"Fires immediately before every instruction executed (in the specified scope) by the core (CPU-intensive). {{val}} is the value to be executed (or {{0}} always, if this feature is only partially implemented).\" );\r\n\tend\r\n\t, \"Frame name\", \"System Bus\" );")]
		[LuaMethod("on_bus_exec_any", "Fires immediately before every instruction executed (in the specified scope) by the core (CPU-intensive). Your callback can have 3 parameters {{(addr, val, flags)}}. {{val}} is the value to be executed (or {{0}} always, if this feature is only partially implemented).")]
		public string OnBusExecAny(
			LuaFunction luaf,
			string name = null,
			string scope = null)
		{
			try
			{
				if (DebuggableCore?.MemoryCallbacksAvailable() == true
					&& DebuggableCore.MemoryCallbacks.ExecuteCallbacksAvailable)
				{
					if (!HasScope(scope)) throw ScopeNotAvailable(scope);

					var nlf = CreateAndRegisterNamedFunction(luaf, NamedLuaFunction.EVENT_TYPE_MEMEXECANY, ApiGroup.PROHIBITED_MID_FRAME, name: name);
					AddMemCallbackOnCore(nlf, MemoryCallbackType.Execute, scope, address: null);
					return nlf.GuidStr;
				}
				// fall through
			}
			catch (NotImplementedException)
			{
				// fall through
			}
			throw MemoryCallbacksNotImplemented(isWildcard: true, execute: true);
		}

		[LuaDeprecatedMethod]
		[LuaMethod("onmemoryread", "Fires immediately before the given address is read by the core. Your callback can have 3 parameters {{(addr, val, flags)}}. {{val}} is the value read. If no address is given, it will fire on every memory read.")]
		public string OnMemoryRead(
			LuaFunction luaf,
			uint? address = null,
			string name = null,
			string scope = null)
		{
//			Log("Deprecated function event.onmemoryread(...) used, replace the call with event.on_bus_read(...).");
			return OnBusRead(luaf, address, name: name, scope: scope);
		}

		[LuaMethodExample("local exec_cb_id = event.on_bus_read(\r\n\tfunction(addr, val, flags)\r\n\t\tconsole.log( \"Fires immediately before the given address is read by the core. {{val}} is the value read. If no address is given, it will fire on every memory read.\" );\r\n\tend\r\n\t, 0x200, \"Frame name\" );")]
		[LuaMethod("on_bus_read", "Fires immediately before the given address is read by the core. Your callback can have 3 parameters {{(addr, val, flags)}}. {{val}} is the value read. If no address is given, it will fire on every memory read.")]
		public string OnBusRead(
			LuaFunction luaf,
			uint? address = null,
			string name = null,
			string scope = null)
		{
			try
			{
				if (DebuggableCore?.MemoryCallbacksAvailable() == true)
				{
					if (!HasScope(scope)) throw ScopeNotAvailable(scope);

					var nlf = CreateAndRegisterNamedFunction(luaf, NamedLuaFunction.EVENT_TYPE_MEMREAD, ApiGroup.PROHIBITED_MID_FRAME, name: name);
					AddMemCallbackOnCore(nlf, MemoryCallbackType.Read, scope, address);
					return nlf.GuidStr;
				}
			}
			catch (NotImplementedException)
			{
				throw MemoryCallbacksNotImplemented(isWildcard: address is null, execute: false);
			}

			throw MemoryCallbacksNotImplemented(isWildcard: address is null, execute: false);
		}

		[LuaDeprecatedMethod]
		[LuaMethod("onmemorywrite", "Fires immediately before the given address is written by the core. Your callback can have 3 parameters {{(addr, val, flags)}}. {{val}} is the value to be written (or {{0}} always, if this feature is only partially implemented). If no address is given, it will fire on every memory write.")]
		public string OnMemoryWrite(
			LuaFunction luaf,
			uint? address = null,
			string name = null,
			string scope = null)
		{
//			Log("Deprecated function event.onmemorywrite(...) used, replace the call with event.on_bus_write(...).");
			return OnBusWrite(luaf, address, name: name, scope: scope);
		}

		[LuaMethodExample("local exec_cb_id = event.on_bus_write(\r\n\tfunction(addr, val, flags)\r\n\t\tconsole.log( \"Fires immediately before the given address is written by the core. {{val}} is the value to be written (or {{0}} always, if this feature is only partially implemented). If no address is given, it will fire on every memory write.\" );\r\n\tend\r\n\t, 0x200, \"Frame name\" );")]
		[LuaMethod("on_bus_write", "Fires immediately before the given address is written by the core. Your callback can have 3 parameters {{(addr, val, flags)}}. {{val}} is the value to be written (or {{0}} always, if this feature is only partially implemented). If no address is given, it will fire on every memory write.")]
		public string OnBusWrite(
			LuaFunction luaf,
			uint? address = null,
			string name = null,
			string scope = null)
		{
			try
			{
				if (DebuggableCore?.MemoryCallbacksAvailable() == true)
				{
					if (!HasScope(scope)) throw ScopeNotAvailable(scope);

					var nlf = CreateAndRegisterNamedFunction(luaf, NamedLuaFunction.EVENT_TYPE_MEMWRITE, ApiGroup.PROHIBITED_MID_FRAME, name: name);
					AddMemCallbackOnCore(nlf, MemoryCallbackType.Write, scope, address);
					return nlf.GuidStr;
				}
			}
			catch (NotImplementedException)
			{
				throw MemoryCallbacksNotImplemented(isWildcard: address is null, execute: false);
			}

			throw MemoryCallbacksNotImplemented(isWildcard: address is null, execute: false);
		}

		[LuaMethodExample("local steveone = event.onexit(\r\n\tfunction()\r\n\t\tconsole.log( \"Fires after the calling script has stopped\" );\r\n\tend\r\n\t, \"Frame name\" );")]
		[LuaMethod("onexit", "Fires after the calling script has stopped")]
		public string OnExit(LuaFunction luaf, string name = null)
			=> CreateAndRegisterNamedFunction(luaf, NamedLuaFunction.EVENT_TYPE_ENGINESTOP, name: name)
				.GuidStr;

		[LuaMethodExample("local closeGuid = event.onconsoleclose(\r\n\tfunction()\r\n\t\tconsole.log( \"Fires when the Lua Console closes\" );\r\n\tend\r\n\t, \"Frame name\" );")]
		[LuaMethod("onconsoleclose", "Fires when the Lua Console closes")]
		public string OnConsoleClose(LuaFunction luaf, string name = null)
			=> CreateAndRegisterNamedFunction(luaf, NamedLuaFunction.EVENT_TYPE_CONSOLECLOSE, name: name)
				.GuidStr;

		[LuaMethodExample("if ( event.unregisterbyid( \"4d1810b7 - 0d28 - 4acb - 9d8b - d87721641551\" ) ) then\r\n\tconsole.log( \"Removes the registered function that matches the guid. If a function is found and remove the function will return true. If unable to find a match, the function will return false.\" );\r\nend;")]
		[LuaMethod("unregisterbyid", "Removes the registered function that matches the guid. If a function is found and remove the function will return true. If unable to find a match, the function will return false.")]
		public bool UnregisterById(string guid)
		{
			Guid parsed = new(guid);
			return RemoveNamedFunctionMatching(nlf => nlf.Guid == parsed);
		}

		[LuaMethodExample("if ( event.unregisterbyname( \"Function name\" ) ) then\r\n\tconsole.log( \"Removes the first registered function that matches Name. If a function is found and remove the function will return true. If unable to find a match, the function will return false.\" );\r\nend;")]
		[LuaMethod("unregisterbyname", "Removes the first registered function that matches Name. If a function is found and remove the function will return true. If unable to find a match, the function will return false.")]
		public bool UnregisterByName(string name)
			=> RemoveNamedFunctionMatching(nlf => nlf.Name == name);

		[LuaMethodExample("local scopes = event.availableScopes();")]
		[LuaMethod("availableScopes", "Lists the available scopes that can be specified for on_bus_* events")]
		[return: LuaZeroIndexed]
		public LuaTable AvailableScopes()
		{
			return DebuggableCore?.MemoryCallbacksAvailable() == true
				? _th.ListToTable(DebuggableCore.MemoryCallbacks.AvailableScopes, indexFrom: 0)
				: _th.CreateTable();
		}

		private string ProcessScope(string scope)
		{
			if (string.IsNullOrWhiteSpace(scope))
			{
				if (Domains != null && Domains.HasSystemBus)
				{
					scope = Domains.SystemBus.Name;
				}
				else
				{
					scope = DebuggableCore.MemoryCallbacks.AvailableScopes[0];
				}
			}

			return scope;
		}

		private bool HasScope(string scope)
			=> string.IsNullOrWhiteSpace(scope)
				|| DebuggableCore.MemoryCallbacks.AvailableScopes.AsSpan().Contains(scope);
	}
}
