/* gm.h - shared definitions for the DARKRED runtime (GameMaker bytecode VM port) */
#ifndef GM_H
#define GM_H

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#define GAME_W 124
#define GAME_H 175

/* ------------------------------------------------------------------ values */
typedef struct Val Val;
typedef struct Inst Inst;
typedef struct Str { struct Str *next; uint8_t mark, perm; int len; char s[1]; } Str;
typedef struct Arr { struct Arr *next; uint8_t mark; int n, cap; Val *v; } Arr;
enum { T_UNDEF = 0, T_REAL, T_STR, T_ARR, T_FN };
struct Val { int t; union { double d; Str *s; Arr *a; int fn; } u; };

/* ------------------------------------------------------------------ pak data */
typedef struct { uint8_t op, t1, t2, vt; int32_t x; union { double d; int64_t l; struct { int32_t a, b; } i; } u; } Ins;
typedef struct { char *name; int nlocals, nargs, n; Ins *ins; } Code;
typedef Val (*BFn)(Inst *self, Val *a, int n);
typedef struct { char *name; int is_script; int code; BFn bfn; } Func;
typedef struct { int sym, itype; } VarRef;
typedef struct { char *name; int sprite, visible, solid, depth, persistent, parent, mask;
                 int c_pre, c_create, c_step, c_draw, c_roomend; } Obj;
typedef struct { int sx, sy, sw, sh, tx, ty, tw, th, bw, bh, page; } Tpag;
typedef struct { char *name; int w, h, ox, oy, bl, br, bt, bb; float speed; int stype, nframes; int *frames; } Sprite;
typedef struct { int ch, x, y, w, h, shift, off; } Glyph;
typedef struct { char *name; int ox, oy, asc, n; Glyph *g; int lineh; short map[256]; } Font;
typedef struct { char *name; float vol; int ch, rate, n; int16_t *pcm; } Sound;
typedef struct { int iid, obj, x, y; } RInst;
typedef struct { char *name; int id, type, depth, visible, ninst; RInst *inst; int bg_sprite; uint32_t bg_color; } RLayer;
typedef struct { char *name; int w, h; uint32_t color; int nlayers; RLayer *layers; } Room;
typedef struct { char *name; float fps, len; int ox, oy, sprite; float px, py, sx, sy, t0, t1; uint32_t c0, c1;
                 float mom_t; char *mom_script; int mom_code; } SeqDef;

typedef struct {
    float fps; int first_room; char *title;
    int nstr; Str **str;
    int nsym; char **sym; int nvar; VarRef *var; int nfunc; Func *func;
    int ncode; Code *code; int nobj; Obj *obj; int ntpag; Tpag *tpag; int nspr; Sprite *spr;
    int aw, ah; uint8_t *atlas8; uint32_t pal[256];     /* 8-bit palettised atlas, palette entries are 0xAARRGGBB */
    int nfont; Font *font; int nsnd; Sound *snd; int nroom; Room *room; int nseq; SeqDef *seq;
    unsigned char *bsym;        /* sym -> builtin variable id (BV_*) */
    short *argidx;              /* sym -> N for argumentN, else -1 */
} Game;
extern Game G;

enum { BV_NONE = 0, BV_X, BV_Y, BV_ID, BV_SPRITE, BV_IMAGE_INDEX, BV_IMAGE_SPEED, BV_IMAGE_NUMBER,
       BV_IMAGE_ALPHA, BV_DEPTH, BV_VISIBLE, BV_LAYER, BV_PERSISTENT, BV_OBJECT_INDEX,
       BV_ROOM, BV_MOUSE_X, BV_MOUSE_Y, BV_VIEW_CAMERA };

int pak_load(const char *path);

/* ------------------------------------------------------------------ instances / runtime */
struct Inst {
    int id, obj, alive, depth, layer, visible, persistent, sprite;
    double x, y, image_index, image_speed, image_alpha;
    Val *vars;                  /* nsym entries, T_UNDEF when unset */
};

/* values */
Val vreal(double d);
Val vundef(void);
Val vstr(Str *s);
Val vfn(int code);
double num(Val v);
int truthy(Val v);
Str *str_new(const char *s, int len);
Arr *arr_new(void);
Val *arr_slot(Arr *a, int idx, int grow);
const char *val_cstr(Val v, char *buf, int cap);   /* string view of a value */
void gc_collect(void);
void gc_begin(void);
void gc_mark(Val v);
void gc_sweep(void);

/* vm */
Val vm_call(int code, Inst *self, Inst *other, const Val *args, int nargs);
extern int vm_warn;
void vm_log(const char *fmt, ...);

/* runtime (rt.c) */
typedef struct { int x, y, pressed, cursor, quit; } PlatIn;
void rt_init(void);
void rt_frame(const PlatIn *in);
void rt_render(uint32_t *fb);
void rt_draw_cursor(uint32_t *fb, int cx, int cy);
void rt_debug_dump(void);
void rt_debug_set_global(const char *name, double v);
double rt_debug_get_global(const char *name);
int  rt_nav(int cx, int cy, int dx, int dy, int *nx, int *ny);
int  rt_has_hotspots(void);
int  rt_room(void);
Inst *inst_by_id(int id);
Inst *inst_first(int obj);
Inst *inst_create(int obj, double x, double y, int layer, int depth, int use_layer);
void inst_destroy(Inst *i);
int  inst_exists(Val v);
Val  inst_get(Inst *i, int sym);
void inst_set(Inst *i, int sym, Val v);
int  inst_list(Val target, Inst *self, Inst *other, int **ids);   /* for with() */
Val  global_get(int sym);
void global_set(int sym, Val v);
BFn  lib_lookup(const char *name);
void rt_goto_room(int r);
extern Val *globals;

/* ------------------------------------------------------------------ platform (plat_*.c) */
int  plat_init(const char *title, int w, int h);
void plat_poll(PlatIn *in);
void plat_present(const uint32_t *fb);
void plat_wait(void);
void plat_shutdown(void);
void plat_fatal(const char *msg);       /* show an error to the user (screen on PSP, stderr elsewhere) */
const char *plat_data_dir(void);
void plat_audio_lock(int lock);
int  plat_audio_open(int rate);          /* stereo s16; calls snd_mix from its thread */
void snd_mix(int16_t *out, int frames);  /* rt.c */
int  snd_play(int sound, int loop);

#endif
