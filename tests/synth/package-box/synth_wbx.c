/* synth.wbx - the waterboxed flavor (c) of the Synth machine.
 *
 * The machine itself is the SAME reference implementation as flavors (a)/(b):
 * native/synthcore.c, compiled unchanged for the guest. This file is only the
 * thin waterbox ABI layer over it. The whole machine state lives in guest
 * memory, so the miniBox host savestates it automatically - there is no
 * explicit serialize/deserialize here, unlike the native and C# adapters.
 * That is the point of the waterbox flavor: reproducibility by construction.
 *
 * The rom arrives as a mounted file "rom" (read at Init, per the side-effect
 * rule - all data through the host interface, never a host path).
 */
#include <emulibc.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include <waterbox_settings.h>  /* miniBox guest kit: read the host settings channel */

/* synthcore.c public API (see tests/synth/SPEC.md). */
typedef struct synth synth_t;
extern synth_t *synth_create(const uint8_t *rom, uint32_t romSize);
extern void synth_reset(synth_t *s);
extern void synth_frame(synth_t *s, uint8_t pad);
extern uint8_t *synth_get_ram(synth_t *s);
extern const uint8_t *synth_get_framebuffer(synth_t *s);
extern const int16_t *synth_get_audio(synth_t *s);
extern uint8_t synth_input_was_read(synth_t *s);
extern void synth_get_video_bgra(synth_t *s, uint32_t *out);  /* palette-resolved 128x120 BGRA */

#define FB_W 128
#define FB_H 120

static synth_t *g_synth;
static uint32_t g_video[FB_W * FB_H];

/* Reads the whole mounted "rom" file into a buffer (caller frees). */
static uint8_t *read_rom(uint32_t *out_len) {
	FILE *f = fopen("rom", "rb");
	if (!f) return 0;
	fseek(f, 0, SEEK_END);
	long n = ftell(f);
	fseek(f, 0, SEEK_SET);
	uint8_t *buf = (uint8_t *)malloc(n > 0 ? (size_t)n : 1);
	if (fread(buf, 1, (size_t)n, f) != (size_t)n) { free(buf); fclose(f); return 0; }
	fclose(f);
	*out_len = (uint32_t)n;
	return buf;
}

ECL_EXPORT int Init(void) {
	uint32_t len = 0;
	uint8_t *rom = read_rom(&len);
	if (!rom) return 0;
	g_synth = synth_create(rom, len);   /* copies the rom internally */
	free(rom);
	if (!g_synth) return 0;
	synth_reset(g_synth);

	/* Demonstration setting: pre-fill RAM with a byte before the program runs.
	 * Default 0 leaves the machine identical to flavors a/b (goldens hold); a
	 * non-zero value proves the setting reached the guest (observable in RAM). */
	long fill = wbx_setting_long("initFillByte", 0);
	if (fill != 0) {
		uint8_t *ram = synth_get_ram(g_synth);
		for (int i = 0; i < 4096; i++) ram[i] = (uint8_t)fill;
	}
	return 1;
}

ECL_EXPORT void FrameAdvance(uint32_t pad) { synth_frame(g_synth, (uint8_t)pad); }

/* Guest-memory pointers the host reads while active (RAM/VRAM/audio domains). */
ECL_EXPORT uint8_t *GetRam(void)         { return synth_get_ram(g_synth); }
ECL_EXPORT uint8_t *GetFramebuffer(void) { return (uint8_t *)synth_get_framebuffer(g_synth); }
ECL_EXPORT int16_t *GetAudio(void)       { return (int16_t *)synth_get_audio(g_synth); }
ECL_EXPORT int      InputWasRead(void)   { return synth_input_was_read(g_synth); }

/* Palette-resolved presentation for the frontend's IVideoProvider (the raw
 * palette-index framebuffer is what the witness hashes; this is display only). */
ECL_EXPORT uint32_t *GetVideoBgra(void)  { synth_get_video_bgra(g_synth, g_video); return g_video; }

/* --- self-described memory domains (guest ABI v1) ---
 * The generic miniHawk adapter queries these AFTER Init, because a core's domain
 * sizes/count can depend on runtime settings (synth's are fixed, but the ABI is
 * uniform). Domain 0 is RAM (writable), domain 1 is VRAM (the palette-index
 * framebuffer, read-only). */
#define MD_COUNT 2
static const char *const md_names[MD_COUNT] = { "RAM", "VRAM" };

ECL_EXPORT int GetMemoryDomainCount(void) { return MD_COUNT; }

ECL_EXPORT const char *GetMemoryDomainName(int i) { return (i >= 0 && i < MD_COUNT) ? md_names[i] : 0; }

ECL_EXPORT uint8_t *GetMemoryDomainPtr(int i) {
	if (i == 0) return synth_get_ram(g_synth);
	if (i == 1) return (uint8_t *)synth_get_framebuffer(g_synth);
	return 0;
}

ECL_EXPORT int64_t GetMemoryDomainSize(int i) {
	if (i == 0) return 4096;
	if (i == 1) return FB_W * FB_H;
	return 0;
}

ECL_EXPORT int GetMemoryDomainWritable(int i) { return i == 0 ? 1 : 0; }

int main(void) { return 0; }
