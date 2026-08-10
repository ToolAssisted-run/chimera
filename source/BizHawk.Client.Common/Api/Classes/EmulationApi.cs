#nullable enable

using System.Collections.Generic;
using System.ComponentModel;
using System.Linq;

using BizHawk.Emulation.Common;

namespace BizHawk.Client.Common
{
	[Description("A library for interacting with the currently loaded emulator core")]
	public sealed class EmulationApi : IEmulationApi
	{
		[RequiredService]
		private IEmulator? Emulator { get; set; }

		[OptionalService]
		private IBoardInfo? BoardInfo { get; set; }

		[OptionalService]
		private IDebuggable? DebuggableCore { get; set; }

		[OptionalService]
		private IDisassemblable? DisassemblableCore { get; set; }

		[OptionalService]
		private IInputPollable? InputPollableCore { get; set; }

		[OptionalService]
		private IMemoryDomains? MemoryDomains { get; set; }

		[OptionalService]
		private IRegionable? RegionableCore { get; set; }

		private readonly Config _config;

		private readonly IGameInfo? _game;

		private readonly Action<string> LogCallback;

		/// <summary>Using this property to get a reference to the global <see cref="Config"/> instance is a terrible, horrible, no good, very bad idea. That's why it's not in the <see cref="IEmulationApi">interface</see>.</summary>
		public Config ForbiddenConfigReference
		{
			get
			{
				ForbiddenConfigReferenceUsed = true;
				return _config;
			}
		}

		public bool ForbiddenConfigReferenceUsed { get; private set; }

		public EmulationApi(Action<string> logCallback, Config config, IGameInfo? game)
		{
			_config = config;
			_game = game;
			LogCallback = logCallback;
		}

		public void DisplayVsync(bool enabled) => _config.VSync = enabled;

		public int FrameCount()
			=> Emulator!.Frame;

		public (string Disasm, int Length) Disassemble(uint pc, string? name = null)
		{
			try
			{
				if (DisassemblableCore != null)
				{
					var disasm = DisassemblableCore.Disassemble(
						string.IsNullOrEmpty(name) ? MemoryDomains!.SystemBus : MemoryDomains![name!]!,
						pc,
						out var l
					);
					return (disasm, l);
				}
			}
			catch (NotImplementedException) {}
			LogCallback($"Error: {Emulator.Attributes().CoreName} does not yet implement {nameof(IDisassemblable.Disassemble)}()");
			return (string.Empty, 0);
		}

		public ulong? GetRegister(string name)
		{
			try
			{
				if (DebuggableCore != null)
				{
					var registers = DebuggableCore.GetCpuFlagsAndRegisters();
					return registers.TryGetValue(name, out var rv) ? rv.Value : null;
				}
			}
			catch (NotImplementedException) {}
			LogCallback($"Error: {Emulator.Attributes().CoreName} does not yet implement {nameof(IDebuggable.GetCpuFlagsAndRegisters)}()");
			return null;
		}

		public IReadOnlyDictionary<string, ulong> GetRegisters()
		{
			try
			{
				if (DebuggableCore != null)
				{
					var table = new Dictionary<string, ulong>();
					foreach (var (name, rv) in DebuggableCore.GetCpuFlagsAndRegisters()) table[name] = rv.Value;
					return table;
				}
			}
			catch (NotImplementedException) {}
			LogCallback($"Error: {Emulator.Attributes().CoreName} does not yet implement {nameof(IDebuggable.GetCpuFlagsAndRegisters)}()");
			return new Dictionary<string, ulong>();
		}

		public void SetRegister(string register, int value)
		{
			try
			{
				if (DebuggableCore != null)
				{
					DebuggableCore.SetCpuRegister(register, value);
					return;
				}
			}
			catch (NotImplementedException) {}
			LogCallback($"Error: {Emulator.Attributes().CoreName} does not yet implement {nameof(IDebuggable.SetCpuRegister)}()");
		}

		public long TotalExecutedCycles()
		{
			try
			{
				if (DebuggableCore != null) return DebuggableCore.TotalExecutedCycles;
			}
			catch (NotImplementedException) {}
			LogCallback($"Error: {Emulator.Attributes().CoreName} does not yet implement {nameof(IDebuggable.TotalExecutedCycles)}()");
			return default;
		}

		public string GetSystemId()
			=> _game!.System;

		public bool IsLagged()
		{
			if (InputPollableCore != null) return InputPollableCore.IsLagFrame;
			LogCallback($"Can not get lag information, {Emulator.Attributes().CoreName} does not implement {nameof(IInputPollable)}");
			return false;
		}

		public void SetIsLagged(bool value = true)
		{
			if (InputPollableCore != null) InputPollableCore.IsLagFrame = value;
			else LogCallback($"Can not set lag information, {Emulator.Attributes().CoreName} does not implement {nameof(IInputPollable)}");
		}

		public int LagCount()
		{
			if (InputPollableCore != null) return InputPollableCore.LagCount;
			LogCallback($"Can not get lag information, {Emulator.Attributes().CoreName} does not implement {nameof(IInputPollable)}");
			return default;
		}

		public void SetLagCount(int count)
		{
			if (InputPollableCore != null) InputPollableCore.LagCount = count;
			else LogCallback($"Can not set lag information, {Emulator.Attributes().CoreName} does not implement {nameof(IInputPollable)}");
		}

		public void LimitFramerate(bool enabled) => _config.ClockThrottle = enabled;

		public void MinimizeFrameskip(bool enabled) => _config.AutoMinimizeSkipping = enabled;

		public string GetDisplayType() => (RegionableCore?.Region)?.ToString() ?? "";

		public string GetBoardName() => BoardInfo?.BoardName ?? "";

		public IGameInfo? GetGameInfo()
			=> _game;

		public IReadOnlyDictionary<string, string?> GetGameOptions()
			=> _game == null
				? new Dictionary<string, string?>()
				: ((GameInfo) _game).GetOptions().ToDictionary(static kvp => kvp.Key, static kvp => (string?) kvp.Value);

		private SettingsAdapter? MakeSettingsAdapter()
			=> Emulator is null
				? null
				: new(
					Emulator,
					mayPutCoreSettings: static () => true,
					handlePutCoreSettings: static _ => { },
					mayPutCoreSyncSettings: static () => false,
					handlePutCoreSyncSettings: static _ => { });

		public object? GetSettings()
		{
			var adapter = MakeSettingsAdapter();
			return adapter is { HasSettings: true } ? adapter.GetSettings() : null;
		}

		public PutSettingsDirtyBits PutSettings(object settings)
		{
			var adapter = MakeSettingsAdapter();
			if (adapter is not { HasSettings: true }) return PutSettingsDirtyBits.None;
			adapter.PutCoreSettings(settings);
			return PutSettingsDirtyBits.None; //TODO the adapter routes the core's dirty bits to the handler callback; surface them here if a caller ever needs them
		}

		public void SetRenderPlanes(params bool[] args)
		{
			// with cores externalized there is no core to special-case here.
			// TODO: consider promoting render-plane toggling into the core contract.
			LogCallback($"{nameof(SetRenderPlanes)} is not supported by this build (no core-specific handling available)");
		}
	}
}

