/* synth-run: the native Synth tester (Level A of the synthetic witness).
 * Replays a movie against a .testrom on libsynthcore and reports the complete
 * observable output: final RAM, final framebuffer, and SHA1-style hashes of
 * the full per-frame video and audio streams. With --rerecord it also does a
 * serialize/deserialize round-trip around every frame (the TAS-critical
 * property) - results must be identical to the straight run.
 *
 * Movie format: one frame per line, "|UDLRABST|" - a character per button in
 * bitmask order (Up Down Left Right A B Select Start), '.' = unpressed. Lines
 * not starting with '|' are ignored (comments).
 *
 * usage: synth-run <rom.testrom> <movie.txt> [--rerecord]
 *          [--dump-ram <f>] [--dump-video <f>] [--dump-audio <f>]
 * output (stdout, one per line):
 *   frames=<n> ramSha1=<hex> videoSha1=<hex> audioSha1=<hex> status=<ram[0]>
 * where videoSha1/audioSha1 hash the concatenation of every frame's
 * framebuffer index bytes / mono int16 samples (little-endian).
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---- minimal SHA1 (public-domain-style reference implementation) ---- */
typedef struct { uint32_t h[5]; uint64_t len; uint8_t buf[64]; size_t fill; } sha1_t;
static uint32_t rol(uint32_t v, int s) { return (v << s) | (v >> (32 - s)); }
static void sha1_block(sha1_t* c, const uint8_t* p)
{
	uint32_t w[80], a, b, d, e, f, k, t, cc;
	int i;
	for (i = 0; i < 16; i++) w[i] = ((uint32_t)p[i*4] << 24) | ((uint32_t)p[i*4+1] << 16) | ((uint32_t)p[i*4+2] << 8) | p[i*4+3];
	for (; i < 80; i++) w[i] = rol(w[i-3] ^ w[i-8] ^ w[i-14] ^ w[i-16], 1);
	a = c->h[0]; b = c->h[1]; cc = c->h[2]; d = c->h[3]; e = c->h[4];
	for (i = 0; i < 80; i++)
	{
		if (i < 20) { f = (b & cc) | (~b & d); k = 0x5A827999; }
		else if (i < 40) { f = b ^ cc ^ d; k = 0x6ED9EBA1; }
		else if (i < 60) { f = (b & cc) | (b & d) | (cc & d); k = 0x8F1BBCDC; }
		else { f = b ^ cc ^ d; k = 0xCA62C1D6; }
		t = rol(a, 5) + f + e + k + w[i];
		e = d; d = cc; cc = rol(b, 30); b = a; a = t;
	}
	c->h[0] += a; c->h[1] += b; c->h[2] += cc; c->h[3] += d; c->h[4] += e;
}
static void sha1_init(sha1_t* c)
{
	c->h[0] = 0x67452301; c->h[1] = 0xEFCDAB89; c->h[2] = 0x98BADCFE;
	c->h[3] = 0x10325476; c->h[4] = 0xC3D2E1F0; c->len = 0; c->fill = 0;
}
static void sha1_update(sha1_t* c, const void* data, size_t n)
{
	const uint8_t* p = (const uint8_t*)data;
	c->len += n;
	while (n)
	{
		size_t take = 64 - c->fill;
		if (take > n) take = n;
		memcpy(c->buf + c->fill, p, take);
		c->fill += take; p += take; n -= take;
		if (c->fill == 64) { sha1_block(c, c->buf); c->fill = 0; }
	}
}
static void sha1_final(sha1_t* c, char out[41])
{
	uint64_t bits = c->len * 8;
	uint8_t pad = 0x80;
	uint8_t lenb[8];
	int i;
	sha1_update(c, &pad, 1);
	while (c->fill != 56) { uint8_t z = 0; sha1_update(c, &z, 1); c->len--; }
	for (i = 0; i < 8; i++) lenb[i] = (uint8_t)(bits >> (56 - 8 * i));
	c->len -= 8;
	sha1_update(c, lenb, 8);
	for (i = 0; i < 5; i++) sprintf(out + i * 8, "%08X", c->h[i]);
	out[40] = 0;
}

/* ---- libsynthcore surface (linked directly against synthcore.c) ---- */
typedef struct synth synth;
extern synth* synth_create(const uint8_t* rom, uint32_t romSize);
extern void synth_destroy(synth* s);
extern void synth_reset(synth* s);
extern void synth_frame(synth* s, uint8_t pad);
extern uint8_t* synth_get_ram(synth* s);
extern const uint8_t* synth_get_framebuffer(synth* s);
extern const int16_t* synth_get_audio(synth* s);
extern uint32_t synth_state_size(void);
extern void synth_serialize(synth* s, uint8_t* out);
extern void synth_deserialize(synth* s, const uint8_t* in);

static uint8_t* read_file(const char* path, long* size)
{
	FILE* f = fopen(path, "rb");
	uint8_t* data;
	if (!f) return 0;
	fseek(f, 0, SEEK_END); *size = ftell(f); fseek(f, 0, SEEK_SET);
	data = (uint8_t*)malloc(*size ? (size_t)*size : 1);
	if (fread(data, 1, (size_t)*size, f) != (size_t)*size) { fclose(f); free(data); return 0; }
	fclose(f);
	return data;
}

static void write_file(const char* path, const void* data, size_t n)
{
	FILE* f = fopen(path, "wb");
	if (!f) { fprintf(stderr, "cannot write %s\n", path); exit(1); }
	fwrite(data, 1, n, f);
	fclose(f);
}

int main(int argc, char** argv)
{
	const char* romPath = 0;
	const char* moviePath = 0;
	const char* dumpRam = 0;
	const char* dumpVram = 0;
	const char* dumpVideo = 0;
	const char* dumpAudio = 0;
	int rerecord = 0;
	int i;
	long romSize = 0, movieSize = 0;
	uint8_t* rom;
	uint8_t* movie;
	synth* s;
	uint8_t* state = 0;
	uint32_t stateSize;
	sha1_t vh, ah, rh;
	char vhex[41], ahex[41], rhex[41];
	uint32_t frames = 0;
	char* line;
	FILE* audioDump = 0;
	FILE* videoDump = 0;

	for (i = 1; i < argc; i++)
	{
		if (!strcmp(argv[i], "--rerecord")) rerecord = 1;
		else if (!strcmp(argv[i], "--dump-ram") && i + 1 < argc) dumpRam = argv[++i];
		else if (!strcmp(argv[i], "--dump-vram") && i + 1 < argc) dumpVram = argv[++i];
		else if (!strcmp(argv[i], "--dump-video") && i + 1 < argc) dumpVideo = argv[++i];
		else if (!strcmp(argv[i], "--dump-audio") && i + 1 < argc) dumpAudio = argv[++i];
		else if (!romPath) romPath = argv[i];
		else if (!moviePath) moviePath = argv[i];
		else { fprintf(stderr, "unexpected arg %s\n", argv[i]); return 2; }
	}
	if (!romPath || !moviePath)
	{
		fprintf(stderr, "usage: synth-run <rom.testrom> <movie.txt> [--rerecord] [--dump-ram f] [--dump-video f] [--dump-audio f]\n");
		return 2;
	}

	rom = read_file(romPath, &romSize);
	if (!rom) { fprintf(stderr, "cannot read %s\n", romPath); return 1; }
	movie = read_file(moviePath, &movieSize);
	if (!movie) { fprintf(stderr, "cannot read %s\n", moviePath); return 1; }
	movie = (uint8_t*)realloc(movie, (size_t)movieSize + 1);
	movie[movieSize] = 0;

	s = synth_create(rom, (uint32_t)romSize);
	if (!s) { fprintf(stderr, "rom rejected: %s\n", romPath); return 1; }
	synth_reset(s);
	stateSize = synth_state_size();
	state = (uint8_t*)malloc(stateSize);
	sha1_init(&vh);
	sha1_init(&ah);
	if (dumpVideo) videoDump = fopen(dumpVideo, "wb");
	if (dumpAudio) audioDump = fopen(dumpAudio, "wb");

	for (line = strtok((char*)movie, "\n"); line; line = strtok(0, "\n"))
	{
		uint8_t pad = 0;
		static const char keys[8] = { 'U', 'D', 'L', 'R', 'A', 'B', 'S', 'T' };
		if (line[0] != '|') continue;
		for (i = 0; i < 8 && line[1 + i] && line[1 + i] != '|'; i++)
		{
			if (line[1 + i] != '.' && line[1 + i] == keys[i]) pad |= (uint8_t)(1u << i);
		}
		if (rerecord)
		{
			synth_serialize(s, state);
			synth_deserialize(s, state);
		}
		synth_frame(s, pad);
		frames++;
		sha1_update(&vh, synth_get_framebuffer(s), 128 * 120);
		sha1_update(&ah, synth_get_audio(s), 735 * 2);
		if (videoDump) fwrite(synth_get_framebuffer(s), 1, 128 * 120, videoDump);
		if (audioDump) fwrite(synth_get_audio(s), 2, 735, audioDump);
	}

	sha1_final(&vh, vhex);
	sha1_final(&ah, ahex);
	sha1_init(&rh);
	sha1_update(&rh, synth_get_ram(s), 4096);
	sha1_final(&rh, rhex);

	if (dumpRam) write_file(dumpRam, synth_get_ram(s), 4096);
	if (dumpVram) write_file(dumpVram, synth_get_framebuffer(s), 128 * 120);
	if (videoDump) fclose(videoDump);
	if (audioDump) fclose(audioDump);

	printf("frames=%u\n", frames);
	printf("ramSha1=%s\n", rhex);
	printf("videoSha1=%s\n", vhex);
	printf("audioSha1=%s\n", ahex);
	printf("status=%u\n", synth_get_ram(s)[0]);

	free(state);
	synth_destroy(s);
	free(rom);
	return 0;
}
