/* plat_null.c - headless test backend: scripted clicks, PPM frame dumps, no audio device */
#include "gm.h"

extern int g_argc; extern char **g_argv;
static int frame, max_frames = 600;
typedef struct { int f, x, y; } Click;
typedef struct { int f, dx, dy; } Nav;
static Nav navs[256]; static int nnavs;
static int press_frames[256], npress;
typedef struct { int f; char name[64]; double v; int print; } GOp;
static GOp gops[64]; static int ngops;
typedef struct { int f; char path[256]; } Dump;
static Click clicks[256]; static int nclicks;
static Dump dumps[256]; static int ndumps;
static int dumpinst[16], ndumpinst;
static const char *data_dir = "./";
static int mx = -1, my = -1;
static const char *wav_path; static int16_t *wavbuf; static size_t wavn, wavcap;

int plat_init(const char *title, int w, int h) {
    (void)title; (void)w; (void)h;
    for (int i = 1; i < g_argc; i++) {
        if (!strcmp(g_argv[i], "--frames") && i + 1 < g_argc) max_frames = atoi(g_argv[++i]);
        else if (!strcmp(g_argv[i], "--click") && i + 1 < g_argc && nclicks < 256) {
            Click *c = &clicks[nclicks++]; sscanf(g_argv[++i], "%d:%d,%d", &c->f, &c->x, &c->y);
        } else if (!strcmp(g_argv[i], "--dump") && i + 1 < g_argc && ndumps < 256) {
            Dump *d = &dumps[ndumps++]; char *s = g_argv[++i];
            d->f = atoi(s); char *c = strchr(s, ':'); snprintf(d->path, sizeof d->path, "%s", c ? c + 1 : "frame.ppm");
        } else if (!strcmp(g_argv[i], "--nav") && i + 1 < g_argc && nnavs < 256) {
            Nav *v = &navs[nnavs++]; sscanf(g_argv[++i], "%d:%d,%d", &v->f, &v->dx, &v->dy);
        } else if (!strcmp(g_argv[i], "--global") && i + 1 < g_argc && ngops < 64) {   /* F:NAME=VALUE */
            GOp *o = &gops[ngops++]; char *s = g_argv[++i], *c = strchr(s, ':'), *e = c ? strchr(c, '=') : NULL;
            if (c && e) { o->f = atoi(s); snprintf(o->name, sizeof o->name, "%.*s", (int)(e - c - 1), c + 1); o->v = atof(e + 1); } else ngops--;
        } else if (!strcmp(g_argv[i], "--print") && i + 1 < g_argc && ngops < 64) {    /* F:NAME */
            GOp *o = &gops[ngops++]; char *s = g_argv[++i], *c = strchr(s, ':'); o->print = 1;
            if (c) { o->f = atoi(s); snprintf(o->name, sizeof o->name, "%s", c + 1); } else ngops--;
        } else if (!strcmp(g_argv[i], "--press") && i + 1 < g_argc && npress < 256) { press_frames[npress++] = atoi(g_argv[++i]);
        } else if (!strcmp(g_argv[i], "--inst") && i + 1 < g_argc && ndumpinst < 16) { dumpinst[ndumpinst++] = atoi(g_argv[++i]);
        } else if (!strcmp(g_argv[i], "--wav") && i + 1 < g_argc) { wav_path = g_argv[++i];
        } else if (!strcmp(g_argv[i], "--data") && i + 1 < g_argc) data_dir = g_argv[++i];
    }
    return 0;
}
void plat_poll(PlatIn *in) {
    in->pressed = 0; in->cursor = 0; in->quit = frame >= max_frames;
    for (int i = 0; i < nclicks; i++) if (clicks[i].f == frame) { mx = clicks[i].x; my = clicks[i].y; in->pressed = 1; }
    for (int i = 0; i < ngops; i++) if (gops[i].f == frame) {
        if (gops[i].print) fprintf(stderr, "[global] frame %d %s = %g\n", frame, gops[i].name, rt_debug_get_global(gops[i].name));
        else rt_debug_set_global(gops[i].name, gops[i].v);
    }
    in->cursor = 0;
    for (int i = 0; i < nnavs; i++) if (navs[i].f == frame) {
        int nx, ny; in->cursor = 1;
        if (rt_nav(mx < 0 ? 62 : mx, my < 0 ? 87 : my, navs[i].dx, navs[i].dy, &nx, &ny)) { mx = nx; my = ny; fprintf(stderr, "[nav] frame %d -> (%d,%d)\n", frame, mx, my); }
        else fprintf(stderr, "[nav] frame %d: no target\n", frame);
    }
    for (int i = 0; i < npress; i++) if (press_frames[i] == frame) in->pressed = 1;
    in->x = mx; in->y = my;
}
void plat_present(const uint32_t *fb) {
    for (int i = 0; i < ndumpinst; i++) if (dumpinst[i] == frame) rt_debug_dump();
    for (int i = 0; i < ndumps; i++) if (dumps[i].f == frame) {
        FILE *f = fopen(dumps[i].path, "wb"); if (!f) continue;
        fprintf(f, "P6\n%d %d\n255\n", GAME_W, GAME_H);
        for (int p = 0; p < GAME_W * GAME_H; p++) { uint8_t c[3] = { (uint8_t)(fb[p] >> 16), (uint8_t)(fb[p] >> 8), (uint8_t)fb[p] }; fwrite(c, 1, 3, f); }
        fclose(f);
    }
    frame++;
}
void plat_wait(void) {
    if (!wav_path) return;
    enum { N = 735 };                       /* 44100 / 60 */
    if (wavn + N * 2 > wavcap) { wavcap = wavcap ? wavcap * 2 : 1 << 20; wavbuf = (int16_t *)realloc(wavbuf, wavcap * 2); }
    snd_mix(wavbuf + wavn, N); wavn += N * 2;
}
void plat_shutdown(void) {
    if (!wav_path) return;
    FILE *f = fopen(wav_path, "wb"); if (!f) return;
    uint32_t bytes = (uint32_t)(wavn * 2), rate = 44100, br = rate * 4, riff = 36 + bytes, fmt = 16;
    uint16_t pcm = 1, ch = 2, ba = 4, bits = 16;
    fwrite("RIFF", 1, 4, f); fwrite(&riff, 4, 1, f); fwrite("WAVEfmt ", 1, 8, f); fwrite(&fmt, 4, 1, f);
    fwrite(&pcm, 2, 1, f); fwrite(&ch, 2, 1, f); fwrite(&rate, 4, 1, f); fwrite(&br, 4, 1, f); fwrite(&ba, 2, 1, f); fwrite(&bits, 2, 1, f);
    fwrite("data", 1, 4, f); fwrite(&bytes, 4, 1, f); fwrite(wavbuf, 2, wavn, f); fclose(f);
}
void plat_fatal(const char *msg) { fprintf(stderr, "%s\n", msg); }

const char *plat_data_dir(void) { return data_dir; }
void plat_audio_lock(int lock) { (void)lock; }
int plat_audio_open(int rate) { (void)rate; return 0; }
