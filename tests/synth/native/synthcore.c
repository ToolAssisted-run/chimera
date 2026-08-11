/* The Synth machine - native reference implementation.
 * Implements tests/synth/SPEC.md (v1) exactly; the spec, not this file, is the
 * source of truth. No dependencies, no floats, no undefined behavior on any
 * input: every access is bounds-defined by the spec's mod/clip rules.
 */

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#define SYNTH_EXPORT __declspec(dllexport)
#else
#define SYNTH_EXPORT __attribute__((visibility("default")))
#endif

#define RAM_SIZE 4096
#define FB_W 128
#define FB_H 120
#define FB_SIZE (FB_W * FB_H)
#define SAMPLES_PER_FRAME 735
#define INSN_BUDGET 65536
#define STATE_SIZE 20272

typedef struct
{
	/* rom (parsed views into a private copy) */
	uint8_t* romCopy;
	uint32_t entry;
	const uint8_t* code; uint32_t codeSize;
	const uint8_t* gfx;  uint32_t gfxSize;   /* palette + tiles */
	const uint8_t* snd;  uint32_t sndSize;
	const uint8_t* data; uint32_t dataSize;
	uint32_t tileCount;
	uint32_t jingleCount;

	/* machine state (serialized) */
	int32_t r[8];
	uint32_t frameCounter;
	uint32_t audioPhase;
	uint32_t audioIncrement;
	uint8_t audioVolume;
	uint8_t jingleActive;
	uint16_t jingleIndex;
	uint16_t jingleNotePos;
	uint8_t jingleNoteFramesLeft;
	uint8_t ram[RAM_SIZE];
	uint8_t fb[FB_SIZE];

	/* per-frame output (not state) */
	int16_t audioOut[SAMPLES_PER_FRAME];
	uint8_t inputRead; /* did INPUT execute during the last frame (lag tracking) */
} synth_t;

static uint16_t rd16(const uint8_t* p) { return (uint16_t)(p[0] | (p[1] << 8)); }
static uint32_t rd32(const uint8_t* p) { return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24); }
static void wr32(uint8_t* p, uint32_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24); }

/* Locates the directory entry of a jingle; returns 0 and *noteCount=0 if none. */
static const uint8_t* jingle_notes(const synth_t* s, uint32_t idx, uint16_t* noteCount)
{
	const uint8_t* p = s->snd + 2;
	const uint8_t* end = s->snd + s->sndSize;
	uint32_t i;
	*noteCount = 0;
	for (i = 0; i < s->jingleCount; i++)
	{
		uint16_t n;
		if (p + 2 > end) return 0;
		n = rd16(p);
		p += 2;
		if (p + (uint32_t)n * 4 > end) return 0;
		if (i == idx) { *noteCount = n; return p; }
		p += (uint32_t)n * 4;
	}
	return 0;
}

static void set_tone(synth_t* s, uint16_t freq, uint8_t vol)
{
	if (freq == 0) vol = 0;
	s->audioIncrement = (uint32_t)(((uint64_t)freq << 32) / 44100u);
	s->audioVolume = vol;
}

/* Per-frame jingle advance (SPEC "Per-frame execution"): the channel is only
 * written at note boundaries - a note load, or the final silence. */
static void jingle_tick(synth_t* s)
{
	uint16_t noteCount;
	const uint8_t* notes;
	if (!s->jingleActive) return;
	notes = jingle_notes(s, s->jingleIndex, &noteCount);
	if (s->jingleNoteFramesLeft == 0)
	{
		if (!notes || s->jingleNotePos >= noteCount)
		{
			s->jingleActive = 0;
			set_tone(s, 0, 0);
			return;
		}
		{
			const uint8_t* note = notes + (uint32_t)s->jingleNotePos * 4;
			set_tone(s, rd16(note), note[2]);
			s->jingleNoteFramesLeft = note[3] ? note[3] : 1;
		}
	}
	s->jingleNoteFramesLeft--;
	if (s->jingleNoteFramesLeft == 0) s->jingleNotePos++;
}

static void draw_tile(synth_t* s, uint32_t x, uint32_t y, uint32_t tileIdx)
{
	const uint8_t* tile;
	uint32_t px, py;
	if (s->tileCount == 0) return;
	tileIdx %= s->tileCount;
	tile = s->gfx + 64 + tileIdx * 64;
	x %= FB_W;
	y %= FB_H;
	for (py = 0; py < 8; py++)
	{
		uint32_t fy = y + py;
		if (fy >= FB_H) break;
		for (px = 0; px < 8; px++)
		{
			uint32_t fx = x + px;
			uint8_t c;
			if (fx >= FB_W) break;
			c = tile[py * 8 + px] & 15;
			if (c != 0) s->fb[fy * FB_W + fx] = c;
		}
	}
}

static void run_frame_code(synth_t* s, uint8_t pad)
{
	uint32_t pc = s->entry;
	uint32_t executed = 0;
	if (s->codeSize == 0) return;
	pc %= s->codeSize;
	pc &= ~7u;
	while (executed < INSN_BUDGET && pc + 8 <= s->codeSize)
	{
		const uint8_t* insn = s->code + pc;
		uint8_t op = insn[0];
		int32_t* Ra = &s->r[insn[1] & 7];
		int32_t* Rb = &s->r[insn[2] & 7];
		int32_t* Rc = &s->r[insn[3] & 7];
		int32_t imm = (int32_t)rd32(insn + 4);
		int taken = 0;
		pc += 8;
		executed++;
		switch (op)
		{
			case 0x00: return; /* HALT */
			case 0x01: *Ra = imm; break;
			case 0x02: *Ra = *Rb; break;
			case 0x03: *Ra = (int32_t)((uint32_t)*Rb + (uint32_t)*Rc); break;
			case 0x04: *Ra = (int32_t)((uint32_t)*Rb - (uint32_t)*Rc); break;
			case 0x05: *Ra = (int32_t)((uint32_t)*Rb * (uint32_t)*Rc); break;
			case 0x06:
				if (*Rc == 0) *Ra = 0;
				else if (*Rb == INT32_MIN && *Rc == -1) *Ra = INT32_MIN;
				else *Ra = *Rb / *Rc;
				break;
			case 0x07: *Ra = *Rb & *Rc; break;
			case 0x08: *Ra = *Rb | *Rc; break;
			case 0x09: *Ra = *Rb ^ *Rc; break;
			case 0x0A: *Ra = (int32_t)((uint32_t)*Rb << ((uint32_t)*Rc & 31)); break;
			case 0x0B: *Ra = (int32_t)((uint32_t)*Rb >> ((uint32_t)*Rc & 31)); break;
			case 0x0C: *Ra = (int32_t)((uint32_t)*Rb + (uint32_t)imm); break;
			case 0x10: *Ra = s->ram[(uint32_t)*Rb % RAM_SIZE]; break;
			case 0x11: s->ram[(uint32_t)*Rb % RAM_SIZE] = (uint8_t)*Ra; break;
			case 0x12: *Ra = (int32_t)rd32(&s->ram[(uint32_t)*Rb % (RAM_SIZE - 3)]); break;
			case 0x13: wr32(&s->ram[(uint32_t)*Rb % (RAM_SIZE - 3)], (uint32_t)*Ra); break;
			case 0x14: *Ra = s->dataSize ? s->data[(uint32_t)*Rb % s->dataSize] : 0; break;
			case 0x20: taken = 1; break;
			case 0x21: taken = (*Ra == *Rb); break;
			case 0x22: taken = (*Ra != *Rb); break;
			case 0x23: taken = (*Ra < *Rb); break;
			case 0x24: taken = (*Ra >= *Rb); break;
			case 0x30: *Ra = pad; s->inputRead = 1; break;
			case 0x31: *Ra = (int32_t)s->frameCounter; break;
			case 0x40: memset(s->fb, (uint8_t)(*Ra & 15), FB_SIZE); break;
			case 0x41: s->fb[((uint32_t)*Rb % FB_H) * FB_W + (uint32_t)*Ra % FB_W] = (uint8_t)(*Rc & 15); break;
			case 0x42:
			{
				uint32_t x = (uint32_t)*Ra % FB_W, y = (uint32_t)*Rb % FB_H;
				uint32_t w = (uint32_t)imm & 0xFF, h = ((uint32_t)imm >> 8) & 0xFF;
				uint8_t color = (uint8_t)(*Rc & 15);
				uint32_t px, py;
				for (py = 0; py < h && y + py < FB_H; py++)
					for (px = 0; px < w && x + px < FB_W; px++)
						s->fb[(y + py) * FB_W + (x + px)] = color;
				break;
			}
			case 0x43: draw_tile(s, (uint32_t)*Ra, (uint32_t)*Rb, (uint32_t)*Rc); break;
			case 0x50: set_tone(s, (uint16_t)((uint32_t)*Ra & 0xFFFF), (uint8_t)((uint32_t)*Rb & 0xFF)); break;
			case 0x51: s->audioVolume = 0; s->audioIncrement = 0; break;
			case 0x52:
				if (s->jingleCount != 0)
				{
					s->jingleActive = 1;
					s->jingleIndex = (uint16_t)((uint32_t)*Ra % s->jingleCount);
					s->jingleNotePos = 0;
					s->jingleNoteFramesLeft = 0; /* first note loads on the next frame's advance */
				}
				break;
			default: break; /* unknown opcode: no-op */
		}
		if (taken && (op >= 0x20 && op <= 0x24))
		{
			pc = ((uint32_t)imm % s->codeSize) & ~7u;
		}
	}
}

static void synthesize_audio(synth_t* s)
{
	int i;
	for (i = 0; i < SAMPLES_PER_FRAME; i++)
	{
		s->audioPhase += s->audioIncrement;
		if (s->audioVolume == 0)
			s->audioOut[i] = 0;
		else
			s->audioOut[i] = (int16_t)((s->audioPhase & 0x80000000u) ? (s->audioVolume << 6) : -(s->audioVolume << 6));
	}
}

/* ---------------- public API ---------------- */

SYNTH_EXPORT synth_t* synth_create(const uint8_t* rom, uint32_t romSize)
{
	synth_t* s;
	uint32_t codeOff, gfxOff, sndOff, dataOff;
	if (romSize < 48 || memcmp(rom, "SYNTHROM", 8) != 0) return 0;
	if (rd16(rom + 8) != 1) return 0;
	s = (synth_t*)calloc(1, sizeof(synth_t));
	if (!s) return 0;
	s->romCopy = (uint8_t*)malloc(romSize ? romSize : 1);
	if (!s->romCopy) { free(s); return 0; }
	memcpy(s->romCopy, rom, romSize);

	s->entry = rd32(rom + 12);
	codeOff = rd32(rom + 16); s->codeSize = rd32(rom + 20);
	gfxOff = rd32(rom + 24); s->gfxSize = rd32(rom + 28);
	sndOff = rd32(rom + 32); s->sndSize = rd32(rom + 36);
	dataOff = rd32(rom + 40); s->dataSize = rd32(rom + 44);

	/* section bounds and invariants */
	if ((uint64_t)codeOff + s->codeSize > romSize || (s->codeSize & 7) != 0) goto bad;
	if (s->gfxSize != 0 && ((uint64_t)gfxOff + s->gfxSize > romSize || s->gfxSize < 64 || ((s->gfxSize - 64) % 64) != 0)) goto bad;
	if (s->sndSize != 0 && ((uint64_t)sndOff + s->sndSize > romSize || s->sndSize < 2)) goto bad;
	if (s->dataSize != 0 && (uint64_t)dataOff + s->dataSize > romSize) goto bad;

	s->code = s->romCopy + codeOff;
	s->gfx = s->gfxSize ? s->romCopy + gfxOff : 0;
	s->snd = s->sndSize ? s->romCopy + sndOff : 0;
	s->data = s->dataSize ? s->romCopy + dataOff : 0;
	s->tileCount = s->gfxSize ? (s->gfxSize - 64) / 64 : 0;
	s->jingleCount = s->sndSize ? rd16(s->snd) : 0;
	return s;
bad:
	free(s->romCopy);
	free(s);
	return 0;
}

SYNTH_EXPORT void synth_destroy(synth_t* s)
{
	if (!s) return;
	free(s->romCopy);
	free(s);
}

SYNTH_EXPORT void synth_reset(synth_t* s)
{
	memset(s->r, 0, sizeof(s->r));
	s->frameCounter = 0;
	s->audioPhase = 0;
	s->audioIncrement = 0;
	s->audioVolume = 0;
	s->jingleActive = 0;
	s->jingleIndex = 0;
	s->jingleNotePos = 0;
	s->jingleNoteFramesLeft = 0;
	memset(s->ram, 0, RAM_SIZE);
	memset(s->fb, 0, FB_SIZE);
	memset(s->audioOut, 0, sizeof(s->audioOut));
}

SYNTH_EXPORT void synth_frame(synth_t* s, uint8_t pad)
{
	s->inputRead = 0;
	jingle_tick(s);
	run_frame_code(s, pad);
	synthesize_audio(s);
	s->frameCounter++;
}

SYNTH_EXPORT uint8_t synth_input_was_read(synth_t* s) { return s->inputRead; }

SYNTH_EXPORT uint8_t* synth_get_ram(synth_t* s) { return s->ram; }
SYNTH_EXPORT const uint8_t* synth_get_framebuffer(synth_t* s) { return s->fb; }
SYNTH_EXPORT const int16_t* synth_get_audio(synth_t* s) { return s->audioOut; }
SYNTH_EXPORT uint32_t synth_state_size(void) { return STATE_SIZE; }

SYNTH_EXPORT void synth_get_video_bgra(synth_t* s, uint32_t* out)
{
	uint32_t palette[16];
	int i;
	if (s->gfx)
	{
		for (i = 0; i < 16; i++)
		{
			const uint8_t* e = s->gfx + i * 4;
			palette[i] = 0xFF000000u | ((uint32_t)e[0] << 16) | ((uint32_t)e[1] << 8) | e[2];
		}
	}
	else
	{
		palette[0] = 0xFF000000u;
		for (i = 1; i < 16; i++) palette[i] = 0xFF000000u | (0x111111u * (uint32_t)i);
	}
	for (i = 0; i < FB_SIZE; i++) out[i] = palette[s->fb[i] & 15];
}

SYNTH_EXPORT void synth_serialize(synth_t* s, uint8_t* out)
{
	uint8_t* p = out;
	int i;
	for (i = 0; i < 8; i++) { wr32(p, (uint32_t)s->r[i]); p += 4; }
	wr32(p, s->frameCounter); p += 4;
	wr32(p, s->audioPhase); p += 4;
	wr32(p, s->audioIncrement); p += 4;
	*p++ = s->audioVolume;
	*p++ = s->jingleActive;
	*p++ = (uint8_t)s->jingleIndex; *p++ = (uint8_t)(s->jingleIndex >> 8);
	*p++ = (uint8_t)s->jingleNotePos; *p++ = (uint8_t)(s->jingleNotePos >> 8);
	*p++ = s->jingleNoteFramesLeft;
	*p++ = 0; *p++ = 0; *p++ = 0;
	memcpy(p, s->ram, RAM_SIZE); p += RAM_SIZE;
	memcpy(p, s->fb, FB_SIZE);
}

SYNTH_EXPORT void synth_deserialize(synth_t* s, const uint8_t* in)
{
	const uint8_t* p = in;
	int i;
	for (i = 0; i < 8; i++) { s->r[i] = (int32_t)rd32(p); p += 4; }
	s->frameCounter = rd32(p); p += 4;
	s->audioPhase = rd32(p); p += 4;
	s->audioIncrement = rd32(p); p += 4;
	s->audioVolume = *p++;
	s->jingleActive = *p++;
	s->jingleIndex = (uint16_t)(p[0] | (p[1] << 8)); p += 2;
	s->jingleNotePos = (uint16_t)(p[0] | (p[1] << 8)); p += 2;
	s->jingleNoteFramesLeft = *p++;
	p += 3;
	memcpy(s->ram, p, RAM_SIZE); p += RAM_SIZE;
	memcpy(s->fb, p, FB_SIZE);
}
