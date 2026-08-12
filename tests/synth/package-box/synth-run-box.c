/* Level A tester for the waterboxed Synth flavor (c). Runs synth.wbx through the
 * miniBox host over a movie and reports the same metrics as native/synth-run and
 * the C# synth-run-sharp, so all three flavors verify against the SAME goldens.
 *
 * Notably: no serialize/deserialize here - --rerecord uses the waterbox host's
 * whole-machine wbx_save_state/wbx_load_state around each frame, so the entire
 * guest is round-tripped. That is the flavor's promise exercised directly.
 *
 * usage: synth-run-box <synth.wbx> <rom.testrom> <movie.txt> [--rerecord]
 */
#include "minibox.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* embedded SHA1 (same as native/synth-run.c) */
typedef struct { uint32_t h[5]; uint64_t len; uint8_t buf[64]; size_t fill; } sha1_t;
static uint32_t rol(uint32_t v, int s) { return (v << s) | (v >> (32 - s)); }
static void sha1_block(sha1_t *c, const uint8_t *p) {
	uint32_t w[80], a, b, d, e, f, k, t, cc; int i;
	for (i = 0; i < 16; i++) w[i] = ((uint32_t)p[i*4]<<24)|((uint32_t)p[i*4+1]<<16)|((uint32_t)p[i*4+2]<<8)|p[i*4+3];
	for (; i < 80; i++) w[i] = rol(w[i-3]^w[i-8]^w[i-14]^w[i-16], 1);
	a=c->h[0]; b=c->h[1]; cc=c->h[2]; d=c->h[3]; e=c->h[4];
	for (i = 0; i < 80; i++) {
		if (i<20){f=(b&cc)|(~b&d);k=0x5A827999;} else if (i<40){f=b^cc^d;k=0x6ED9EBA1;}
		else if (i<60){f=(b&cc)|(b&d)|(cc&d);k=0x8F1BBCDC;} else {f=b^cc^d;k=0xCA62C1D6;}
		t=rol(a,5)+f+e+k+w[i]; e=d; d=cc; cc=rol(b,30); b=a; a=t;
	}
	c->h[0]+=a; c->h[1]+=b; c->h[2]+=cc; c->h[3]+=d; c->h[4]+=e;
}
static void sha1_init(sha1_t *c){c->h[0]=0x67452301;c->h[1]=0xEFCDAB89;c->h[2]=0x98BADCFE;c->h[3]=0x10325476;c->h[4]=0xC3D2E1F0;c->len=0;c->fill=0;}
static void sha1_update(sha1_t *c, const void *data, size_t n){const uint8_t*p=data;c->len+=n;while(n){size_t t=64-c->fill;if(t>n)t=n;memcpy(c->buf+c->fill,p,t);c->fill+=t;p+=t;n-=t;if(c->fill==64){sha1_block(c,c->buf);c->fill=0;}}}
static void sha1_final(sha1_t *c, char out[41]){uint64_t bits=c->len*8;uint8_t pad=0x80,lenb[8];int i;sha1_update(c,&pad,1);while(c->fill!=56){uint8_t z=0;sha1_update(c,&z,1);c->len--;}for(i=0;i<8;i++)lenb[i]=(uint8_t)(bits>>(56-8*i));c->len-=8;sha1_update(c,lenb,8);for(i=0;i<5;i++)sprintf(out+i*8,"%08X",c->h[i]);out[40]=0;}

typedef struct { FILE *f; } fr_t;
static intptr_t file_read(uintptr_t ud, uint8_t *d, uintptr_t s){return (intptr_t)fread(d,1,s,((fr_t*)ud)->f);}
typedef struct { const uint8_t *p; size_t n, pos; } mr_t;
static intptr_t mem_reader(uintptr_t ud, uint8_t *d, uintptr_t s){mr_t*m=(mr_t*)ud;size_t t=s<(m->n-m->pos)?s:(m->n-m->pos);memcpy(d,m->p+m->pos,t);m->pos+=t;return (intptr_t)t;}
typedef struct { uint8_t *b; size_t len, cap, pos; } mb_t;
static int32_t mem_write(uintptr_t ud, const uint8_t *d, uintptr_t n){mb_t*m=(mb_t*)ud;if(m->len+n>m->cap){m->cap=(m->len+n)*2+64;m->b=realloc(m->b,m->cap);}memcpy(m->b+m->len,d,n);m->len+=n;return 0;}
static intptr_t mem_read(uintptr_t ud, uint8_t *d, uintptr_t n){mb_t*m=(mb_t*)ud;uintptr_t a=m->len-m->pos;if(n>a)n=a;memcpy(d,m->b+m->pos,n);m->pos+=n;return (intptr_t)n;}

typedef int (*init_fn)(void);
typedef void (*frame_fn)(uint64_t);
typedef uintptr_t (*getptr_fn)(void);

static uintptr_t proc(mb_host *h, const char *n){mb_return r;wbx_get_proc_addr(h,n,&r);if(r.error_message[0]){fprintf(stderr,"proc %s: %s\n",n,r.error_message);exit(2);}return r.data;}

static uint8_t *read_file(const char *p, long *n){FILE*f=fopen(p,"rb");if(!f)return 0;fseek(f,0,SEEK_END);*n=ftell(f);fseek(f,0,SEEK_SET);uint8_t*b=malloc(*n?*n:1);if(fread(b,1,*n,f)!=(size_t)*n){free(b);fclose(f);return 0;}fclose(f);return b;}

int main(int argc, char **argv) {
	const char *wbxPath = 0, *romPath = 0, *moviePath = 0; int rerecord = 0;
	for (int i = 1; i < argc; i++) {
		if (!strcmp(argv[i], "--rerecord")) rerecord = 1;
		else if (!wbxPath) wbxPath = argv[i];
		else if (!romPath) romPath = argv[i];
		else if (!moviePath) moviePath = argv[i];
	}
	if (!wbxPath || !romPath || !moviePath) { fprintf(stderr, "usage: synth-run-box <synth.wbx> <rom> <movie> [--rerecord]\n"); return 2; }

	long romLen = 0, movLen = 0;
	uint8_t *rom = read_file(romPath, &romLen);
	uint8_t *movie = read_file(moviePath, &movLen);
	if (!rom || !movie) { fprintf(stderr, "cannot read rom/movie\n"); return 1; }
	movie = realloc(movie, movLen + 1); movie[movLen] = 0;

	FILE *wf = fopen(wbxPath, "rb");
	if (!wf) { fprintf(stderr, "cannot open %s\n", wbxPath); return 1; }
	mb_memory_layout_template layout = { 16u<<20, 16u<<20, 16u<<20, 16u<<20, 32u<<20 };
	fr_t fr = { wf };
	mb_return r;
	wbx_create_host(&layout, "synth.wbx", file_read, (uintptr_t)&fr, &r);
	fclose(wf);
	if (r.error_message[0]) { fprintf(stderr, "create: %s\n", r.error_message); return 1; }
	mb_host *h = (mb_host *)r.data;

	/* mount the rom (readonly, stable across states) */
	mr_t mr = { rom, (size_t)romLen, 0 };
	wbx_mount_file(h, "rom", mem_reader, (uintptr_t)&mr, false, &r);
	if (r.error_message[0]) { fprintf(stderr, "mount: %s\n", r.error_message); return 1; }

	wbx_activate_host(h, &r);
	init_fn Init = (init_fn)proc(h, "Init");
	frame_fn FrameAdvance = (frame_fn)proc(h, "FrameAdvance");
	getptr_fn GetRam = (getptr_fn)proc(h, "GetRam");
	getptr_fn GetFramebuffer = (getptr_fn)proc(h, "GetFramebuffer");
	getptr_fn GetAudio = (getptr_fn)proc(h, "GetAudio");
	if (Init() != 1) { fprintf(stderr, "Init failed\n"); return 1; }
	wbx_deactivate_host(h, &r);
	wbx_seal(h, &r);
	if (r.error_message[0]) { fprintf(stderr, "seal: %s\n", r.error_message); return 1; }
	wbx_activate_host(h, &r);

	sha1_t vh, ah; sha1_init(&vh); sha1_init(&ah);
	uint32_t frames = 0;
	mb_t state = {0};
	for (char *line = strtok((char *)movie, "\n"); line; line = strtok(0, "\n")) {
		if (line[0] != '|') continue;
		uint8_t pad = 0;
		static const char keys[8] = { 'U','D','L','R','A','B','S','T' };
		for (int i = 0; i < 8 && line[1+i] && line[1+i] != '|'; i++)
			if (line[1+i] != '.' && line[1+i] == keys[i]) pad |= (uint8_t)(1u << i);
		if (rerecord) {
			state.len = 0;
			wbx_deactivate_host(h, &r); wbx_save_state(h, mem_write, (uintptr_t)&state, &r); wbx_activate_host(h, &r);
			state.pos = 0;
			wbx_deactivate_host(h, &r); wbx_load_state(h, mem_read, (uintptr_t)&state, &r); wbx_activate_host(h, &r);
			if (r.error_message[0]) { fprintf(stderr, "rerecord: %s\n", r.error_message); return 1; }
		}
		FrameAdvance((uint64_t)pad);
		frames++;
		sha1_update(&vh, (const void *)GetFramebuffer(), 128 * 120);
		sha1_update(&ah, (const void *)GetAudio(), 735 * 2);
	}

	char vhex[41], ahex[41], rhex[41];
	sha1_final(&vh, vhex); sha1_final(&ah, ahex);
	{ sha1_t rh; sha1_init(&rh); sha1_update(&rh, (const void *)GetRam(), 4096); sha1_final(&rh, rhex); }
	uint8_t status = *(const uint8_t *)GetRam();

	printf("frames=%u\n", frames);
	printf("ramSha1=%s\n", rhex);
	printf("videoSha1=%s\n", vhex);
	printf("audioSha1=%s\n", ahex);
	printf("status=%u\n", status);

	wbx_deactivate_host(h, &r); wbx_destroy_host(h, &r);
	free(rom); free(movie); free(state.b);
	return 0;
}
