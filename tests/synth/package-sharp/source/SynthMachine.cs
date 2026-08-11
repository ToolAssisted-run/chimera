using System;

namespace MiniHawk.Cores.SynthSharp
{
	/// <summary>
	/// The Synth machine, pure-C# flavor (b): a from-scratch implementation of
	/// tests/synth/SPEC.md v1. The spec, not the native implementation, is the
	/// source of truth - this class deliberately shares no code with flavor (a),
	/// and must be bit-identical to it in memory, video, and audio for any
	/// (rom, input sequence). Everything is exact-width integer arithmetic in
	/// unchecked context; no floats, no host state, no side effects.
	/// </summary>
	public sealed class SynthMachine
	{
		public const int RamSize = 4096;
		public const int FbWidth = 128;
		public const int FbHeight = 120;
		public const int FbSize = FbWidth * FbHeight;
		public const int SamplesPerFrame = 735;
		public const int InsnBudget = 65536;
		public const int StateSize = 20272;

		// rom (parsed views into a private copy)
		private readonly byte[] _rom;
		private readonly uint _entry;
		private readonly int _codeOff; private readonly uint _codeSize;
		private readonly int _gfxOff; private readonly uint _gfxSize;
		private readonly int _sndOff; private readonly uint _sndSize;
		private readonly int _dataOff; private readonly uint _dataSize;
		private readonly uint _tileCount;
		private readonly uint _jingleCount;

		// machine state (serialized)
		private readonly int[] _r = new int[8];
		private uint _frameCounter;
		private uint _audioPhase;
		private uint _audioIncrement;
		private byte _audioVolume;
		private byte _jingleActive;
		private ushort _jingleIndex;
		private ushort _jingleNotePos;
		private byte _jingleNoteFramesLeft;
		private readonly byte[] _ram = new byte[RamSize];
		private readonly byte[] _fb = new byte[FbSize];

		// per-frame output (not state)
		private readonly short[] _audioOut = new short[SamplesPerFrame];
		private bool _inputRead;

		public byte[] Ram => _ram;
		public byte[] Framebuffer => _fb;
		public short[] AudioOut => _audioOut;
		public bool InputWasRead => _inputRead;
		public uint FrameCounter => _frameCounter;

		private static ushort Rd16(byte[] p, int off) => (ushort)(p[off] | (p[off + 1] << 8));
		private static uint Rd32(byte[] p, int off)
			=> (uint)(p[off] | (p[off + 1] << 8) | (p[off + 2] << 16) | (p[off + 3] << 24));
		private static void Wr32(byte[] p, int off, uint v)
		{
			p[off] = (byte)v; p[off + 1] = (byte)(v >> 8); p[off + 2] = (byte)(v >> 16); p[off + 3] = (byte)(v >> 24);
		}

		/// <exception cref="InvalidOperationException">rom rejected (bad magic/version/sections)</exception>
		public SynthMachine(byte[] rom)
		{
			if (rom.Length < 48
				|| rom[0] != (byte)'S' || rom[1] != (byte)'Y' || rom[2] != (byte)'N' || rom[3] != (byte)'T'
				|| rom[4] != (byte)'H' || rom[5] != (byte)'R' || rom[6] != (byte)'O' || rom[7] != (byte)'M')
			{
				throw new InvalidOperationException("not a valid .testrom (bad magic)");
			}
			if (Rd16(rom, 8) != 1) throw new InvalidOperationException("unsupported .testrom format version");

			_rom = (byte[])rom.Clone();
			_entry = Rd32(_rom, 12);
			_codeOff = (int)Rd32(_rom, 16); _codeSize = Rd32(_rom, 20);
			_gfxOff = (int)Rd32(_rom, 24); _gfxSize = Rd32(_rom, 28);
			_sndOff = (int)Rd32(_rom, 32); _sndSize = Rd32(_rom, 36);
			_dataOff = (int)Rd32(_rom, 40); _dataSize = Rd32(_rom, 44);

			if ((ulong)_codeOff + _codeSize > (ulong)_rom.Length || (_codeSize & 7) != 0)
				throw new InvalidOperationException("bad CODE section");
			if (_gfxSize != 0 && ((ulong)_gfxOff + _gfxSize > (ulong)_rom.Length || _gfxSize < 64 || (_gfxSize - 64) % 64 != 0))
				throw new InvalidOperationException("bad GFX section");
			if (_sndSize != 0 && ((ulong)_sndOff + _sndSize > (ulong)_rom.Length || _sndSize < 2))
				throw new InvalidOperationException("bad SND section");
			if (_dataSize != 0 && (ulong)_dataOff + _dataSize > (ulong)_rom.Length)
				throw new InvalidOperationException("bad DATA section");

			_tileCount = _gfxSize != 0 ? (_gfxSize - 64) / 64 : 0;
			_jingleCount = _sndSize != 0 ? Rd16(_rom, _sndOff) : 0u;
		}

		/// <summary>power-on: the whole machine state to zero</summary>
		public void Reset()
		{
			Array.Clear(_r, 0, 8);
			_frameCounter = 0;
			_audioPhase = 0;
			_audioIncrement = 0;
			_audioVolume = 0;
			_jingleActive = 0;
			_jingleIndex = 0;
			_jingleNotePos = 0;
			_jingleNoteFramesLeft = 0;
			Array.Clear(_ram, 0, RamSize);
			Array.Clear(_fb, 0, FbSize);
			Array.Clear(_audioOut, 0, SamplesPerFrame);
		}

		public void FrameAdvance(byte pad)
		{
			unchecked
			{
				_inputRead = false;
				JingleTick();
				RunFrameCode(pad);
				SynthesizeAudio();
				_frameCounter++;
			}
		}

		private void SetTone(ushort freq, byte vol)
		{
			if (freq == 0) vol = 0;
			_audioIncrement = (uint)(((ulong)freq << 32) / 44100u);
			_audioVolume = vol;
		}

		/// <returns>note table offset for jingle idx, or -1; noteCount out</returns>
		private int JingleNotes(uint idx, out ushort noteCount)
		{
			int p = _sndOff + 2;
			int end = _sndOff + (int)_sndSize;
			noteCount = 0;
			for (uint i = 0; i < _jingleCount; i++)
			{
				if (p + 2 > end) return -1;
				ushort n = Rd16(_rom, p);
				p += 2;
				if (p + n * 4 > end) return -1;
				if (i == idx) { noteCount = n; return p; }
				p += n * 4;
			}
			return -1;
		}

		/// <summary>per-frame jingle advance; the channel is only written at note boundaries</summary>
		private void JingleTick()
		{
			if (_jingleActive == 0) return;
			int notes = JingleNotes(_jingleIndex, out var noteCount);
			if (_jingleNoteFramesLeft == 0)
			{
				if (notes < 0 || _jingleNotePos >= noteCount)
				{
					_jingleActive = 0;
					SetTone(0, 0);
					return;
				}
				int note = notes + _jingleNotePos * 4;
				SetTone(Rd16(_rom, note), _rom[note + 2]);
				_jingleNoteFramesLeft = _rom[note + 3] != 0 ? _rom[note + 3] : (byte)1;
			}
			_jingleNoteFramesLeft--;
			if (_jingleNoteFramesLeft == 0) _jingleNotePos++;
		}

		private void DrawTile(uint x, uint y, uint tileIdx)
		{
			if (_tileCount == 0) return;
			tileIdx %= _tileCount;
			int tile = _gfxOff + 64 + (int)tileIdx * 64;
			x %= FbWidth;
			y %= FbHeight;
			for (uint py = 0; py < 8; py++)
			{
				uint fy = y + py;
				if (fy >= FbHeight) break;
				for (uint px = 0; px < 8; px++)
				{
					uint fx = x + px;
					if (fx >= FbWidth) break;
					byte c = (byte)(_rom[tile + (int)(py * 8 + px)] & 15);
					if (c != 0) _fb[fy * FbWidth + fx] = c;
				}
			}
		}

		private void RunFrameCode(byte pad)
		{
			if (_codeSize == 0) return;
			uint pc = (_entry % _codeSize) & ~7u;
			uint executed = 0;
			while (executed < InsnBudget && pc + 8 <= _codeSize)
			{
				int insn = _codeOff + (int)pc;
				byte op = _rom[insn];
				int ra = _rom[insn + 1] & 7;
				int rb = _rom[insn + 2] & 7;
				int rc = _rom[insn + 3] & 7;
				int imm = (int)Rd32(_rom, insn + 4);
				bool taken = false;
				pc += 8;
				executed++;
				switch (op)
				{
					case 0x00: return; // HALT
					case 0x01: _r[ra] = imm; break;
					case 0x02: _r[ra] = _r[rb]; break;
					case 0x03: _r[ra] = _r[rb] + _r[rc]; break;
					case 0x04: _r[ra] = _r[rb] - _r[rc]; break;
					case 0x05: _r[ra] = _r[rb] * _r[rc]; break;
					case 0x06:
						_r[ra] = _r[rc] == 0 ? 0
							: _r[rb] == int.MinValue && _r[rc] == -1 ? int.MinValue
							: _r[rb] / _r[rc];
						break;
					case 0x07: _r[ra] = _r[rb] & _r[rc]; break;
					case 0x08: _r[ra] = _r[rb] | _r[rc]; break;
					case 0x09: _r[ra] = _r[rb] ^ _r[rc]; break;
					case 0x0A: _r[ra] = _r[rb] << (_r[rc] & 31); break;
					case 0x0B: _r[ra] = (int)((uint)_r[rb] >> (_r[rc] & 31)); break;
					case 0x0C: _r[ra] = _r[rb] + imm; break;
					case 0x10: _r[ra] = _ram[(uint)_r[rb] % RamSize]; break;
					case 0x11: _ram[(uint)_r[rb] % RamSize] = (byte)_r[ra]; break;
					case 0x12: _r[ra] = (int)Rd32(_ram, (int)((uint)_r[rb] % (RamSize - 3))); break;
					case 0x13: Wr32(_ram, (int)((uint)_r[rb] % (RamSize - 3)), (uint)_r[ra]); break;
					case 0x14: _r[ra] = _dataSize != 0 ? _rom[_dataOff + (int)((uint)_r[rb] % _dataSize)] : 0; break;
					case 0x20: taken = true; break;
					case 0x21: taken = _r[ra] == _r[rb]; break;
					case 0x22: taken = _r[ra] != _r[rb]; break;
					case 0x23: taken = _r[ra] < _r[rb]; break;
					case 0x24: taken = _r[ra] >= _r[rb]; break;
					case 0x30: _r[ra] = pad; _inputRead = true; break;
					case 0x31: _r[ra] = (int)_frameCounter; break;
					case 0x40:
						for (int i = 0; i < FbSize; i++) _fb[i] = (byte)(_r[ra] & 15);
						break;
					case 0x41: _fb[(uint)_r[rb] % FbHeight * FbWidth + (uint)_r[ra] % FbWidth] = (byte)(_r[rc] & 15); break;
					case 0x42:
					{
						uint x = (uint)_r[ra] % FbWidth, y = (uint)_r[rb] % FbHeight;
						uint w = (uint)imm & 0xFF, h = ((uint)imm >> 8) & 0xFF;
						byte color = (byte)(_r[rc] & 15);
						for (uint py = 0; py < h && y + py < FbHeight; py++)
							for (uint px = 0; px < w && x + px < FbWidth; px++)
								_fb[(y + py) * FbWidth + (x + px)] = color;
						break;
					}
					case 0x43: DrawTile((uint)_r[ra], (uint)_r[rb], (uint)_r[rc]); break;
					case 0x50: SetTone((ushort)((uint)_r[ra] & 0xFFFF), (byte)((uint)_r[rb] & 0xFF)); break;
					case 0x51: _audioVolume = 0; _audioIncrement = 0; break;
					case 0x52:
						if (_jingleCount != 0)
						{
							_jingleActive = 1;
							_jingleIndex = (ushort)((uint)_r[ra] % _jingleCount);
							_jingleNotePos = 0;
							_jingleNoteFramesLeft = 0; // first note loads on the next frame's advance
						}
						break;
					default: break; // unknown opcode: no-op
				}
				if (taken && op >= 0x20 && op <= 0x24)
				{
					pc = ((uint)imm % _codeSize) & ~7u;
				}
			}
		}

		private void SynthesizeAudio()
		{
			for (int i = 0; i < SamplesPerFrame; i++)
			{
				_audioPhase += _audioIncrement;
				_audioOut[i] = _audioVolume == 0
					? (short)0
					: (_audioPhase & 0x80000000u) != 0
						? (short)(_audioVolume << 6)
						: (short)(-(_audioVolume << 6));
			}
		}

		/// <summary>palette-resolved BGRA presentation of the framebuffer</summary>
		public void GetVideoBgra(int[] dest)
		{
			var palette = new uint[16];
			if (_gfxSize != 0)
			{
				for (int i = 0; i < 16; i++)
				{
					int e = _gfxOff + i * 4;
					palette[i] = 0xFF000000u | ((uint)_rom[e] << 16) | ((uint)_rom[e + 1] << 8) | _rom[e + 2];
				}
			}
			else
			{
				palette[0] = 0xFF000000u;
				for (int i = 1; i < 16; i++) palette[i] = 0xFF000000u | (0x111111u * (uint)i);
			}
			for (int i = 0; i < FbSize; i++) dest[i] = (int)palette[_fb[i] & 15];
		}

		public void Serialize(byte[] dest)
		{
			int p = 0;
			for (int i = 0; i < 8; i++) { Wr32(dest, p, (uint)_r[i]); p += 4; }
			Wr32(dest, p, _frameCounter); p += 4;
			Wr32(dest, p, _audioPhase); p += 4;
			Wr32(dest, p, _audioIncrement); p += 4;
			dest[p++] = _audioVolume;
			dest[p++] = _jingleActive;
			dest[p++] = (byte)_jingleIndex; dest[p++] = (byte)(_jingleIndex >> 8);
			dest[p++] = (byte)_jingleNotePos; dest[p++] = (byte)(_jingleNotePos >> 8);
			dest[p++] = _jingleNoteFramesLeft;
			dest[p++] = 0; dest[p++] = 0; dest[p++] = 0;
			Array.Copy(_ram, 0, dest, p, RamSize); p += RamSize;
			Array.Copy(_fb, 0, dest, p, FbSize);
		}

		public void Deserialize(byte[] src)
		{
			int p = 0;
			for (int i = 0; i < 8; i++) { _r[i] = (int)Rd32(src, p); p += 4; }
			_frameCounter = Rd32(src, p); p += 4;
			_audioPhase = Rd32(src, p); p += 4;
			_audioIncrement = Rd32(src, p); p += 4;
			_audioVolume = src[p++];
			_jingleActive = src[p++];
			_jingleIndex = (ushort)(src[p] | (src[p + 1] << 8)); p += 2;
			_jingleNotePos = (ushort)(src[p] | (src[p + 1] << 8)); p += 2;
			_jingleNoteFramesLeft = src[p++];
			p += 3;
			Array.Copy(src, p, _ram, 0, RamSize); p += RamSize;
			Array.Copy(src, p, _fb, 0, FbSize);
		}
	}
}
