using System.Collections.Generic;
using System.IO;
using System.Text;

using BizHawk.Emulation.Common;

namespace BizHawk.Client.Common
{
	public sealed class MemorySaveStateApi : IMemorySaveStateApi
	{
		[RequiredService]
		private IStatable StatableCore { get; set; }

		private readonly Action<string> LogCallback;

		/// <remarks>
		/// A pooled buffer may be larger than the state stored in it, so the length
		/// travels with it. Scripts that save and load every frame - a bot, or a
		/// rerecord-style replay - would otherwise allocate a fresh
		/// multi-hundred-kilobyte array per frame and hand it straight to the GC.
		/// </remarks>
		private readonly Dictionary<Guid, (byte[] Buffer, int Length)> _memorySavestates = new();

		/// <summary>Buffers freed by <see cref="DeleteState"/>, waiting to be filled again.</summary>
		private readonly Stack<byte[]> _bufferPool = new();

		private const int MAX_POOLED_BUFFERS = 8;

		private MemoryStream _scratch;

		public MemorySaveStateApi(Action<string> logCallback) => LogCallback = logCallback;

		public string SaveCoreStateToMemory()
		{
#pragma warning disable RS0030 // this is to ensure no collisions
			var guid = Guid.NewGuid();
#pragma warning restore RS0030

			// Deliberately not IStatable.CloneSavestate(): it allocates a stream AND a
			// copy of the whole state on every call, and says so in its own summary.
			_scratch ??= new MemoryStream();
			_scratch.SetLength(0);
			using (var bw = new BinaryWriter(_scratch, Encoding.UTF8, leaveOpen: true))
			{
				StatableCore.SaveStateBinary(bw);
				bw.Flush();
			}

			var length = (int)_scratch.Length;
			var buffer = Rent(length);
			Buffer.BlockCopy(_scratch.GetBuffer(), 0, buffer, 0, length);
			_memorySavestates.Add(guid, (buffer, length));
			return guid.ToString("D");
		}

		public void LoadCoreStateFromMemory(string identifier)
		{
			var guid = new Guid(identifier);
			if (!_memorySavestates.TryGetValue(guid, out var state))
			{
				LogCallback("Unable to find the given savestate in memory");
				return;
			}

			using var ms = new MemoryStream(state.Buffer, 0, state.Length, writable: false);
			using var br = new BinaryReader(ms);
			StatableCore.LoadStateBinary(br);
		}

		public void DeleteState(string identifier)
		{
			var guid = new Guid(identifier);
			if (_memorySavestates.TryGetValue(guid, out var state))
			{
				_memorySavestates.Remove(guid);
				Return(state.Buffer);
			}
		}

		public void ClearInMemoryStates()
		{
			foreach (var state in _memorySavestates.Values) Return(state.Buffer);
			_memorySavestates.Clear();
		}

		private byte[] Rent(int length)
		{
			while (_bufferPool.Count is not 0)
			{
				var buffer = _bufferPool.Pop();
				// too small for this core's states: drop it rather than re-test it forever
				if (buffer.Length >= length) return buffer;
			}
			return new byte[length];
		}

		private void Return(byte[] buffer)
		{
			if (_bufferPool.Count < MAX_POOLED_BUFFERS) _bufferPool.Push(buffer);
		}
	}
}
