#nullable enable

using System.Collections.Generic;

using BizHawk.Emulation.Common;

namespace BizHawk.Client.Common
{
	public interface IEmulationApi : IExternalApi
	{
		void DisplayVsync(bool enabled);
		int FrameCount();

		/// <returns>disassembly and opcode width, or <c>(string.Empty, 0)</c> on failure</returns>
		(string Disasm, int Length) Disassemble(uint pc, string? name = null);

		ulong? GetRegister(string name);
		IReadOnlyDictionary<string, ulong> GetRegisters();
		void SetRegister(string register, int value);
		long TotalExecutedCycles();
		string GetSystemId();

		/// <summary>
		/// Which core is running. More than one package can claim a system, so a script (or a test)
		/// that cares what it is talking to has to be able to ask.
		/// </summary>
		string GetCoreName();
		bool IsLagged();
		void SetIsLagged(bool value = true);
		int LagCount();
		void SetLagCount(int count);
		void LimitFramerate(bool enabled);
		void MinimizeFrameskip(bool enabled);
		string GetDisplayType();
		string GetBoardName();

		IGameInfo? GetGameInfo();

		IReadOnlyDictionary<string, string?> GetGameOptions();

		object? GetSettings();
		PutSettingsDirtyBits PutSettings(object settings);
		void SetRenderPlanes(params bool[] args);
	}
}
