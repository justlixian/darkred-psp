/* rt.c - runtime: instances, rooms, layers, sequences, software renderer, builtin library, mixer */
#include "gm.h"
#include <ctype.h>
#include <strings.h>
#include <time.h>

Val *globals;

/* ================================================================== state */
#define MAX_INST 512
static Inst *insts[MAX_INST];
static int ninst, next_id = 100100;

static int cur_room = -1, pending_room = -1;
static int mouse_x, mouse_y, mouse_pressed;
static int sym_elementID = -1;

typedef struct { int id; char name[48]; int depth, visible, bg_sprite; uint32_t bg_color; int is_bg; } Layer;
typedef struct { int id, seq, layer; double x, y, pos; int fired, done, alive; } SeqEl;
static Layer lay[32], play[16];
static SeqEl sel[16], psel[16];
static int nlay, nsel, nplay, npsel, pend_room = -1, target_room = -1;
static int next_layer_id = 1000, next_el_id = 5000000;

/* draw state */
static uint32_t *FB;
static int d_color = 0xFFFFFF, d_halign = 0, d_valign = 0, d_font = 0;

/* ================================================================== instances */
Inst *inst_by_id(int id) {
    for (int i = 0; i < ninst; i++) if (insts[i]->alive && insts[i]->id == id) return insts[i];
    return NULL;
}
Inst *inst_first(int obj) {
    for (int i = 0; i < ninst; i++) if (insts[i]->alive && insts[i]->obj == obj) return insts[i];
    return NULL;
}
static Inst *inst_alloc(int obj, int id) {
    if (ninst >= MAX_INST) { vm_log("instance limit reached"); return NULL; }
    Inst *in = (Inst *)calloc(1, sizeof(Inst));
    in->vars = (Val *)calloc((size_t)G.nsym + 1, sizeof(Val));
    in->id = id; in->obj = obj; in->alive = 1;
    Obj *o = &G.obj[obj];
    in->sprite = o->sprite; in->visible = o->visible; in->depth = o->depth; in->persistent = o->persistent;
    in->image_speed = 1; in->image_alpha = 1; in->layer = -1;
    insts[ninst++] = in;
    return in;
}
void inst_destroy(Inst *i) { if (i) i->alive = 0; }

static Layer *find_layer_in(Layer *arr, int n, Val v) {
    char bf[40];
    for (int i = 0; i < n; i++) {
        if (v.t == T_STR) { if (strcasecmp(arr[i].name, val_cstr(v, bf, 40)) == 0) return &arr[i]; }
        else if (v.t == T_REAL && arr[i].id == (int)v.u.d) return &arr[i];
    }
    return NULL;
}
static int use_pending(void) { return target_room >= 0 && target_room != cur_room; }

Inst *inst_create(int obj, double x, double y, int layer, int depth, int use_layer) {
    if (obj < 0 || obj >= G.nobj) return NULL;
    Inst *in = inst_alloc(obj, next_id++);
    if (!in) return NULL;
    in->x = x; in->y = y;
    if (use_layer) {
        Layer *l = NULL;
        for (int i = 0; i < nlay; i++) if (lay[i].id == layer) l = &lay[i];
        in->layer = layer; in->depth = l ? l->depth : 0;
    } else in->depth = depth;
    if (G.obj[obj].c_create >= 0) vm_call(G.obj[obj].c_create, in, NULL, NULL, 0);
    return in;
}

int inst_exists(Val v) {
    int n = (int)num(v);
    if (n >= 100000) return inst_by_id(n) != NULL;
    return inst_first(n) != NULL;
}

int inst_list(Val t, Inst *self, Inst *other, int **out) {
    int *ids = (int *)malloc(sizeof(int) * (MAX_INST + 2)); int n = 0;
    if (t.t == T_REAL) {
        int v = (int)t.u.d;
        if (v == -1) { if (self) ids[n++] = self->id; }
        else if (v == -2) { if (other) ids[n++] = other->id; }
        else if (v == -3) { for (int i = 0; i < ninst; i++) if (insts[i]->alive) ids[n++] = insts[i]->id; }
        else if (v >= 100000) { if (inst_by_id(v)) ids[n++] = v; }
        else if (v >= 0) { for (int i = 0; i < ninst; i++) if (insts[i]->alive && insts[i]->obj == v) ids[n++] = insts[i]->id; }
    }
    *out = ids; return n;
}

/* ------------------------------------------------------------------ variables */
Val global_get(int sym) {
    switch (G.bsym[sym]) {
    case BV_ROOM: return vreal(cur_room);
    case BV_MOUSE_X: return vreal(mouse_x);
    case BV_MOUSE_Y: return vreal(mouse_y);
    default: return globals[sym];
    }
}
void global_set(int sym, Val v) { globals[sym] = v; }

Val inst_get(Inst *in, int sym) {
    switch (G.bsym[sym]) {
    case BV_NONE: return in ? in->vars[sym] : vundef();
    case BV_ROOM: return vreal(cur_room);
    case BV_MOUSE_X: return vreal(mouse_x);
    case BV_MOUSE_Y: return vreal(mouse_y);
    case BV_VIEW_CAMERA: return vundef();
    default: break;
    }
    if (!in) return vundef();
    switch (G.bsym[sym]) {
    case BV_X: return vreal(in->x);
    case BV_Y: return vreal(in->y);
    case BV_ID: return vreal(in->id);
    case BV_SPRITE: return vreal(in->sprite);
    case BV_IMAGE_INDEX: return vreal(in->image_index);
    case BV_IMAGE_SPEED: return vreal(in->image_speed);
    case BV_IMAGE_NUMBER: return vreal(in->sprite >= 0 ? G.spr[in->sprite].nframes : 0);
    case BV_IMAGE_ALPHA: return vreal(in->image_alpha);
    case BV_DEPTH: return vreal(in->depth);
    case BV_VISIBLE: return vreal(in->visible);
    case BV_LAYER: return vreal(in->layer);
    case BV_PERSISTENT: return vreal(in->persistent);
    case BV_OBJECT_INDEX: return vreal(in->obj);
    default: return vundef();
    }
}
void inst_set(Inst *in, int sym, Val v) {
    double d = num(v);
    switch (G.bsym[sym]) {
    case BV_NONE: in->vars[sym] = v; break;
    case BV_X: in->x = d; break;
    case BV_Y: in->y = d; break;
    case BV_SPRITE: in->sprite = (int)d; break;
    case BV_IMAGE_INDEX: in->image_index = d; break;
    case BV_IMAGE_SPEED: in->image_speed = d; break;
    case BV_IMAGE_ALPHA: in->image_alpha = d; break;
    case BV_DEPTH: in->depth = (int)d; break;
    case BV_VISIBLE: in->visible = truthy(v); break;
    case BV_LAYER: in->layer = (int)d; break;
    case BV_PERSISTENT: in->persistent = truthy(v); break;
    default: break;                      /* read-only / unsupported builtins */
    }
}

/* ================================================================== graphics */
static inline void blend(uint32_t *dst, uint32_t src_rgb, int a255) {
    if (a255 <= 0) return;
    uint32_t d = *dst;
    if (a255 >= 255) { *dst = 0xFF000000u | src_rgb; return; }
    int dr = (d >> 16) & 255, dg = (d >> 8) & 255, db = d & 255;
    int sr = (src_rgb >> 16) & 255, sg = (src_rgb >> 8) & 255, sb = src_rgb & 255;
    dr += ((sr - dr) * a255) / 255; dg += ((sg - dg) * a255) / 255; db += ((sb - db) * a255) / 255;
    *dst = 0xFF000000u | ((uint32_t)dr << 16) | ((uint32_t)dg << 8) | (uint32_t)db;
}

/* draw an atlas rectangle with colour tint (GM BGR int) and alpha */
static void blit(int sx, int sy, int sw, int sh, int dx, int dy, int tint, double alpha) {
    int tr = tint & 255, tg = (tint >> 8) & 255, tb = (tint >> 16) & 255;
    int ga = (int)(alpha * 256.0 + 0.5);
    for (int j = 0; j < sh; j++) {
        int y = dy + j; if (y < 0 || y >= GAME_H) continue;
        for (int i = 0; i < sw; i++) {
            int x = dx + i; if (x < 0 || x >= GAME_W) continue;
            uint32_t s = G.pal[G.atlas8[(size_t)(sy + j) * (size_t)G.aw + (size_t)(sx + i)]];
            int a = (int)(s >> 24); if (!a) continue;
            a = (a * ga) >> 8;
            int r = (((s >> 16) & 255) * tr) / 255, g = (((s >> 8) & 255) * tg) / 255, b = ((s & 255) * tb) / 255;
            blend(&FB[y * GAME_W + x], ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b, a);
        }
    }
}
static void fill_rect(double fx, double fy, double fw, double fh, int bgr, double alpha) {
    int x0 = (int)floor(fx + 0.5), y0 = (int)floor(fy + 0.5), x1 = (int)floor(fx + fw + 0.5), y1 = (int)floor(fy + fh + 0.5);
    uint32_t rgb = ((uint32_t)(bgr & 255) << 16) | ((uint32_t)((bgr >> 8) & 255) << 8) | (uint32_t)((bgr >> 16) & 255);
    int a = (int)(alpha * 255.0 + 0.5);
    for (int y = y0; y < y1; y++) {
        if (y < 0 || y >= GAME_H) continue;
        for (int x = x0; x < x1; x++) if (x >= 0 && x < GAME_W) blend(&FB[y * GAME_W + x], rgb, a);
    }
}
static void draw_sprite_frame(int spr, double sub, double x, double y, double alpha) {
    if (spr < 0 || spr >= G.nspr) return;
    Sprite *s = &G.spr[spr];
    if (s->nframes <= 0) return;
    int f = ((int)floor(sub)) % s->nframes; if (f < 0) f += s->nframes;
    Tpag *t = &G.tpag[s->frames[f]];
    blit(t->sx, t->sy, t->sw, t->sh, (int)floor(x + 0.5) - s->ox + t->tx, (int)floor(y + 0.5) - s->oy + t->ty, 0xFFFFFF, alpha);
}
static int text_width(const char *str) {
    if (d_font < 0 || d_font >= G.nfont) return 0;
    Font *f = &G.font[d_font]; int w = 0;
    for (const unsigned char *p = (const unsigned char *)str; *p; p++) { int k = f->map[*p]; if (k >= 0) w += f->g[k].shift; }
    return w;
}
static void draw_text(double x, double y, const char *str) {
    if (d_font < 0 || d_font >= G.nfont) return;
    Font *f = &G.font[d_font];
    int w = text_width(str);
    double x0 = x - (d_halign == 1 ? w * 0.5 : d_halign == 2 ? (double)w : 0);
    double y0 = y - (d_valign == 1 ? f->lineh * 0.5 : d_valign == 2 ? (double)f->lineh : 0);
    int cx = (int)floor(x0 + 0.5), cy = (int)floor(y0 + 0.5);
    for (const unsigned char *p = (const unsigned char *)str; *p; p++) {
        int k = f->map[*p]; if (k < 0) continue;
        Glyph *g = &f->g[k];
        if (g->w > 0 && g->h > 0) blit(f->ox + g->x, f->oy + g->y, g->w, g->h, cx + g->off, cy, d_color, 1.0);
        cx += g->shift;
    }
}

/* ================================================================== sequences */
static void seq_update(void) {
    for (int i = 0; i < nsel; i++) {
        SeqEl *e = &sel[i]; if (!e->alive || e->done) continue;
        SeqDef *q = &G.seq[e->seq];
        e->pos += q->fps / G.fps;
        if (!e->fired && e->pos >= q->mom_t) {
            e->fired = 1;
            if (q->mom_code >= 0) {
                Inst pi; memset(&pi, 0, sizeof pi);
                pi.vars = (Val *)calloc((size_t)G.nsym + 1, sizeof(Val)); pi.id = -1; pi.alive = 1; pi.obj = -1;
                if (sym_elementID >= 0) pi.vars[sym_elementID] = vreal(e->id);
                vm_call(q->mom_code, &pi, NULL, NULL, 0);
                free(pi.vars);
            }
        }
        if (e->pos >= q->len) { e->pos = q->len; e->done = 1; }
    }
}
static void draw_seq(const SeqEl *e) {
    const SeqDef *q = &G.seq[e->seq];
    double t = e->pos; int a0 = (int)(q->c0 >> 24), a1 = (int)(q->c1 >> 24); double a; int rgb;
    if (t <= q->t0) { a = a0; rgb = (int)(q->c0 & 0xFFFFFF); }
    else if (t >= q->t1) { a = a1; rgb = (int)(q->c1 & 0xFFFFFF); }
    else { double f = (t - q->t0) / (q->t1 - q->t0); a = a0 + (a1 - a0) * f; rgb = (int)(q->c0 & 0xFFFFFF); }
    fill_rect(e->x + (q->px - q->ox), e->y + (q->py - q->oy), q->sx, q->sy, rgb, a / 255.0);
}

/* ================================================================== rooms */
static void load_room(int r) {
    cur_room = r; nlay = 0; nsel = 0;
    Room *rm = &G.room[r];
    for (int i = 0; i < rm->nlayers && nlay < 32; i++) {
        RLayer *l = &rm->layers[i]; Layer *d = &lay[nlay++];
        memset(d, 0, sizeof *d);
        snprintf(d->name, sizeof d->name, "%s", l->name);
        d->id = l->id; d->depth = l->depth; d->visible = l->visible;
        d->is_bg = (l->type == 1); d->bg_sprite = l->bg_sprite; d->bg_color = l->bg_color;
    }
    if (pend_room == r) {
        for (int i = 0; i < nplay && nlay < 32; i++) lay[nlay++] = play[i];
        for (int i = 0; i < npsel && nsel < 16; i++) { sel[nsel] = psel[i]; sel[nsel].pos = 0; nsel++; }
    }
    pend_room = -1; nplay = 0; npsel = 0;

    int first = ninst;
    for (int i = 0; i < rm->nlayers; i++) {
        RLayer *l = &rm->layers[i];
        for (int k = 0; k < l->ninst; k++) {
            Inst *in = inst_alloc(l->inst[k].obj, l->inst[k].iid);
            if (!in) continue;
            in->x = l->inst[k].x; in->y = l->inst[k].y; in->layer = l->id; in->depth = l->depth;
        }
    }
    int last = ninst;
    for (int i = first; i < last; i++) {
        Inst *in = insts[i];
        if (in->alive && G.obj[in->obj].c_create >= 0) vm_call(G.obj[in->obj].c_create, in, NULL, NULL, 0);
    }
}

static void compact_instances(void) {
    int j = 0;
    for (int i = 0; i < ninst; i++) {
        if (insts[i]->alive) insts[j++] = insts[i];
        else { free(insts[i]->vars); free(insts[i]); }
    }
    ninst = j;
}

static void change_room(void) {
    int r = pending_room; pending_room = -1;
    if (r < 0 || r >= G.nroom) return;
    for (int i = 0; i < ninst; i++) {
        Inst *in = insts[i];
        if (in->alive && G.obj[in->obj].c_roomend >= 0) vm_call(G.obj[in->obj].c_roomend, in, NULL, NULL, 0);
    }
    for (int i = 0; i < ninst; i++) if (!insts[i]->persistent) insts[i]->alive = 0;
    compact_instances();
    load_room(r);
}
void rt_goto_room(int r) { pending_room = r; }
int rt_room(void) { return cur_room; }

/* ================================================================== frame */
void gc_collect(void) {
    gc_begin();
    for (int i = 0; i < G.nsym; i++) gc_mark(globals[i]);
    for (int i = 0; i < ninst; i++) for (int k = 0; k < G.nsym; k++) gc_mark(insts[i]->vars[k]);
    gc_sweep();
}

void rt_init(void) {
    globals = (Val *)calloc((size_t)G.nsym + 1, sizeof(Val));
    for (int i = 0; i < G.nsym; i++) if (strcmp(G.sym[i], "elementID") == 0) sym_elementID = i;
    srand((unsigned)time(NULL));
    load_room(G.first_room);
}

void rt_frame(const PlatIn *in) {
    mouse_x = in->x; mouse_y = in->y; mouse_pressed = in->pressed;
    int cnt = ninst;
    for (int i = 0; i < cnt && i < ninst; i++) {
        Inst *p = insts[i];
        if (p->alive && G.obj[p->obj].c_step >= 0) vm_call(G.obj[p->obj].c_step, p, NULL, NULL, 0);
    }
    for (int i = 0; i < ninst; i++) {                      /* sprite animation */
        Inst *p = insts[i];
        if (!p->alive || p->sprite < 0) continue;
        Sprite *s = &G.spr[p->sprite];
        if (s->nframes > 1 && p->image_speed != 0) {
            double step = s->stype == 0 ? (double)s->speed / G.fps : (double)s->speed;
            p->image_index += p->image_speed * step;
            while (p->image_index >= s->nframes) p->image_index -= s->nframes;
        }
    }
    seq_update();
    if (pending_room >= 0) change_room();
    else compact_instances();
    gc_collect();
}

typedef struct { int depth, order, kind, ref; } DrawItem;
static int draw_cmp(const void *a, const void *b) {
    const DrawItem *x = (const DrawItem *)a, *y = (const DrawItem *)b;
    if (x->depth != y->depth) return y->depth - x->depth;       /* larger depth = further back = first */
    return x->order - y->order;
}

void rt_render(uint32_t *fb) {
    FB = fb; d_color = 0xFFFFFF; d_halign = 0; d_valign = 0; d_font = 0;
    for (int i = 0; i < GAME_W * GAME_H; i++) fb[i] = 0xFF000000u;
    DrawItem items[MAX_INST + 64]; int n = 0, ord = 0;
    for (int i = 0; i < nlay; i++) if (lay[i].is_bg && lay[i].visible && n < MAX_INST + 60) { items[n].depth = lay[i].depth; items[n].order = ord++; items[n].kind = 0; items[n].ref = i; n++; }
    for (int i = 0; i < ninst && n < MAX_INST + 60; i++) if (insts[i]->alive && insts[i]->visible) { items[n].depth = insts[i]->depth; items[n].order = ord++; items[n].kind = 1; items[n].ref = i; n++; }
    for (int i = 0; i < nsel && n < MAX_INST + 60; i++) if (sel[i].alive) {
        int d = 0; for (int k = 0; k < nlay; k++) if (lay[k].id == sel[i].layer) d = lay[k].depth;
        items[n].depth = d; items[n].order = ord++; items[n].kind = 2; items[n].ref = i; n++;
    }
    qsort(items, (size_t)n, sizeof(DrawItem), draw_cmp);
    for (int k = 0; k < n; k++) {
        DrawItem *it = &items[k];
        if (it->kind == 0) {
            Layer *l = &lay[it->ref];
            if (l->bg_sprite >= 0) draw_sprite_frame(l->bg_sprite, 0, 0, 0, 1.0);
            else fill_rect(0, 0, GAME_W, GAME_H, (int)(l->bg_color & 0xFFFFFF), 1.0);
        } else if (it->kind == 1) {
            Inst *p = insts[it->ref];
            if (G.obj[p->obj].c_draw >= 0) vm_call(G.obj[p->obj].c_draw, p, NULL, NULL, 0);
            else draw_sprite_frame(p->sprite, p->image_index, p->x, p->y, p->image_alpha);
        } else draw_seq(&sel[it->ref]);
    }
}

/* ---- gamepad support: hotspots are the clickable objects (map markers and menu buttons) */
static int is_hotspot_obj(int obj) {
    const char *n = G.obj[obj].name;
    return !strcmp(n, "obj_city") || !strcmp(n, "obj_scr") || !strcmp(n, "obj_menu");
}
static int hotspots(int *xs, int *ys, int max) {
    int n = 0;
    for (int i = 0; i < ninst; i++) if (insts[i]->alive) if (!strcmp(G.obj[insts[i]->obj].name, "obj_textbox")) return 0;
    for (int i = 0; i < nsel; i++) if (sel[i].alive && !sel[i].done) return 0;     /* transition running */
    for (int i = 0; i < ninst && n < max; i++) {
        Inst *p = insts[i];
        if (!p->alive || !p->visible || !is_hotspot_obj(p->obj) || p->sprite < 0) continue;
        Sprite *sp = &G.spr[p->sprite];
        if (sp->w <= 1 && sp->h <= 1) continue;
        xs[n] = (int)floor(p->x - sp->ox + (sp->bl + sp->br) / 2.0 + 0.5);
        ys[n] = (int)floor(p->y - sp->oy + (sp->bt + sp->bb) / 2.0 + 0.5);
        n++;
    }
    return n;
}
/* move the pointer to the next hotspot in direction (dx,dy). returns 1 if moved, 0 if nothing to move to */
int rt_nav(int cx, int cy, int dx, int dy, int *nx, int *ny) {
    int xs[64], ys[64]; int n = hotspots(xs, ys, 64);
    if (n == 0) return 0;
    int best = -1; double bs = 1e18; int on = -1;
    for (int i = 0; i < n; i++) if (abs(xs[i] - cx) <= 3 && abs(ys[i] - cy) <= 3) on = i;
    for (int i = 0; i < n; i++) {
        if (i == on) continue;
        double vx = xs[i] - cx, vy = ys[i] - cy, along = vx * dx + vy * dy, side = fabs(vx * dy) + fabs(vy * dx);
        if (on >= 0 && along <= 0.5) continue;                       /* must lie ahead of the current hotspot */
        double score = on >= 0 ? along + 2.0 * side : vx * vx + vy * vy;
        if (score < bs) { bs = score; best = i; }
    }
    if (best < 0) return 0;
    *nx = xs[best]; *ny = ys[best]; return 1;
}
int rt_has_hotspots(void) { int xs[64], ys[64]; return hotspots(xs, ys, 64) > 0; }

void rt_debug_set_global(const char *name, double v) {
    for (int i = 0; i < G.nsym; i++) if (!strcmp(G.sym[i], name)) { globals[i] = vreal(v); return; }
    fprintf(stderr, "no such global: %s\n", name);
}
double rt_debug_get_global(const char *name) {
    for (int i = 0; i < G.nsym; i++) if (!strcmp(G.sym[i], name)) return num(globals[i]);
    return -12345;
}
void rt_debug_dump(void) {
    fprintf(stderr, "-- room %d (%s), %d instances\n", cur_room, G.room[cur_room].name, ninst);
    for (int i = 0; i < ninst; i++) {
        Inst *p = insts[i];
        fprintf(stderr, "   id=%d %s at (%g,%g) spr=%d depth=%d alive=%d\n", p->id, G.obj[p->obj].name, p->x, p->y, p->sprite, p->depth, p->alive);
    }
}

void rt_draw_cursor(uint32_t *fb, int cx, int cy) {
    static const char *shape[] = { "X.......", "XX......", "XWX.....", "XWWX....", "XWWWX...", "XWWWWX..", "XWWWWWX.", "XWWXXXXX", "XWXX....", "XX......" };
    for (int j = 0; j < 10; j++) for (int i = 0; i < 8; i++) {
        char c = shape[j][i]; int x = cx + i, y = cy + j;
        if (c == '.' || x < 0 || x >= GAME_W || y < 0 || y >= GAME_H) continue;
        fb[y * GAME_W + x] = c == 'X' ? 0xFF000000u : 0xFFFFFFFFu;
    }
}

/* ================================================================== audio mixer */
typedef struct { int snd, loop, active; double pos; } Voice;
static Voice voices[16];

int snd_play(int sound, int loop) {
    if (sound < 0 || sound >= G.nsnd) return -1;
    plat_audio_lock(1);
    int slot = -1;
    for (int i = 0; i < 16; i++) if (!voices[i].active) { slot = i; break; }
    if (slot < 0) slot = 0;
    voices[slot].snd = sound; voices[slot].loop = loop; voices[slot].pos = 0; voices[slot].active = 1;
    plat_audio_lock(0);
    return slot;
}
void snd_mix(int16_t *out, int frames) {
    enum { CH = 1024 };                                   /* fixed chunk: no allocation on the audio thread */
    while (frames > 0) {
        int n = frames < CH ? frames : CH;
        int32_t acc[CH * 2]; memset(acc, 0, sizeof(int32_t) * (size_t)n * 2);
        for (int v = 0; v < 16; v++) {
            Voice *vo = &voices[v]; if (!vo->active) continue;
            Sound *s = &G.snd[vo->snd]; double step = (double)s->rate / 44100.0;
            for (int i = 0; i < n; i++) {
                int p = (int)vo->pos;
                if (p >= s->n) { if (vo->loop) { vo->pos -= s->n; p = (int)vo->pos; } else { vo->active = 0; break; } }
                int l = s->pcm[p * s->ch], r = s->pcm[p * s->ch + (s->ch > 1 ? 1 : 0)];
                acc[i * 2] += (int)(l * s->vol); acc[i * 2 + 1] += (int)(r * s->vol);
                vo->pos += step;
            }
        }
        for (int i = 0; i < n * 2; i++) { int32_t a = acc[i]; out[i] = (int16_t)(a > 32767 ? 32767 : a < -32768 ? -32768 : a); }
        out += n * 2; frames -= n;
    }
}

/* ================================================================== builtin library */
#define ARG(i) ((i) < n ? a[i] : vundef())
#define NUM(i) num(ARG(i))

static Val bi_method(Inst *s, Val *a, int n) { (void)s; return n >= 2 ? a[1] : vundef(); }
static Val bi_event_inherited(Inst *s, Val *a, int n) { (void)s; (void)a; (void)n; return vundef(); }
static Val bi_camera_create(Inst *s, Val *a, int n) { (void)s; (void)a; (void)n; return vreal(0); }
static Val bi_script_execute(Inst *s, Val *a, int n) {
    if (n < 1 || a[0].t != T_FN) return vundef();
    return vm_call(a[0].u.fn, s, NULL, a + 1, n - 1);
}

static Val bi_instance_position(Inst *s, Val *a, int n) {
    (void)s;
    double px = NUM(0), py = NUM(1); int target = (int)NUM(2);
    for (int i = 0; i < ninst; i++) {
        Inst *p = insts[i]; if (!p->alive) continue;
        if (target >= 100000 ? p->id != target : p->obj != target) continue;
        int spr = p->sprite >= 0 ? p->sprite : G.obj[p->obj].sprite; if (spr < 0) continue;
        Sprite *sp = &G.spr[spr];
        double l = p->x - sp->ox + sp->bl, r = p->x - sp->ox + sp->br, t = p->y - sp->oy + sp->bt, b = p->y - sp->oy + sp->bb;
        if (px >= l && px < r + 1 && py >= t && py < b + 1) return vreal(p->id);
    }
    return vreal(-4);
}
static Val bi_instance_exists(Inst *s, Val *a, int n) { (void)s; return vreal(inst_exists(ARG(0)) ? 1 : 0); }
static Val bi_instance_destroy(Inst *s, Val *a, int n) {
    if (n < 1) { inst_destroy(s); return vundef(); }
    int *ids; int c = inst_list(a[0], s, NULL, &ids);
    for (int k = 0; k < c; k++) inst_destroy(inst_by_id(ids[k]));
    free(ids); return vundef();
}
static Val bi_instance_create_layer(Inst *s, Val *a, int n) {
    (void)s;
    Layer *l = find_layer_in(lay, nlay, ARG(2));
    Inst *p = inst_create((int)NUM(3), NUM(0), NUM(1), l ? l->id : -1, 0, 1);
    if (p && !l) p->depth = 0;
    return vreal(p ? p->id : -4);
}
static Val bi_instance_create_depth(Inst *s, Val *a, int n) {
    (void)s;
    Inst *p = inst_create((int)NUM(3), NUM(0), NUM(1), -1, (int)NUM(2), 0);
    return vreal(p ? p->id : -4);
}
static Val bi_mouse_check_button_pressed(Inst *s, Val *a, int n) { (void)s; (void)a; (void)n; return vreal(mouse_pressed ? 1 : 0); }
static Val bi_room_goto(Inst *s, Val *a, int n) { (void)s; rt_goto_room((int)NUM(0)); return vundef(); }

/* ---- layers / sequences */
static Layer *tlayers(int *cnt) { if (use_pending()) { *cnt = nplay; return play; } *cnt = nlay; return lay; }
static Val bi_layer_exists(Inst *s, Val *a, int n) {
    (void)s; int c; Layer *arr = tlayers(&c);
    return vreal(find_layer_in(arr, c, ARG(0)) ? 1 : 0);
}
static Val bi_layer_create(Inst *s, Val *a, int n) {
    (void)s; int pend = use_pending();
    if (pend && pend_room != target_room) { pend_room = target_room; nplay = 0; npsel = 0; }
    Layer *arr = pend ? play : lay; int *cnt = pend ? &nplay : &nlay; int cap = pend ? 16 : 32;
    if (*cnt >= cap) return vreal(-1);
    Layer *l = &arr[(*cnt)++]; memset(l, 0, sizeof *l);
    char bf[40]; snprintf(l->name, sizeof l->name, "%s", val_cstr(ARG(1), bf, 40));
    l->id = next_layer_id++; l->depth = (int)NUM(0); l->visible = 1; l->bg_sprite = -1;
    return vreal(l->id);
}
static void remove_el_array(SeqEl *arr, int *cnt, int layer_id, int el_id) {
    for (int i = 0; i < *cnt; i++)
        if ((layer_id >= 0 && arr[i].layer == layer_id) || (el_id >= 0 && arr[i].id == el_id)) arr[i].alive = 0;
}
static Val bi_layer_destroy(Inst *s, Val *a, int n) {
    (void)s; int pend = use_pending();
    Layer *arr = pend ? play : lay; int *cnt = pend ? &nplay : &nlay;
    Layer *l = find_layer_in(arr, *cnt, ARG(0)); if (!l) return vundef();
    int id = l->id;
    remove_el_array(pend ? psel : sel, pend ? &npsel : &nsel, id, -1);
    *l = arr[--(*cnt)];
    return vundef();
}
static Val bi_layer_sequence_create(Inst *s, Val *a, int n) {
    (void)s; int pend = use_pending();
    Layer *arr = pend ? play : lay; int cnt = pend ? nplay : nlay;
    Layer *l = find_layer_in(arr, cnt, ARG(0));
    SeqEl *els = pend ? psel : sel; int *ec = pend ? &npsel : &nsel;
    int seq = (int)NUM(3);
    if (!l || seq < 0 || seq >= G.nseq || *ec >= 16) return vreal(-1);
    SeqEl *e = &els[(*ec)++]; memset(e, 0, sizeof *e);
    e->id = next_el_id++; e->seq = seq; e->layer = l->id; e->x = NUM(1); e->y = NUM(2); e->alive = 1;
    return vreal(e->id);
}
static Val bi_layer_sequence_destroy(Inst *s, Val *a, int n) {
    (void)s; int id = (int)NUM(0);
    remove_el_array(sel, &nsel, -1, id); remove_el_array(psel, &npsel, -1, id);
    return vundef();
}
static Val bi_layer_set_target_room(Inst *s, Val *a, int n) { (void)s; target_room = (int)NUM(0); return vundef(); }
static Val bi_layer_reset_target_room(Inst *s, Val *a, int n) { (void)s; (void)a; (void)n; target_room = -1; return vundef(); }

/* ---- drawing */
static Val bi_draw_sprite(Inst *s, Val *a, int n) { (void)s; draw_sprite_frame((int)NUM(0), NUM(1), NUM(2), NUM(3), 1.0); return vundef(); }
static Val bi_draw_self(Inst *s, Val *a, int n) {
    (void)a; (void)n; if (s) draw_sprite_frame(s->sprite, s->image_index, s->x, s->y, s->image_alpha); return vundef();
}
static Val bi_draw_set_halign(Inst *s, Val *a, int n) { (void)s; d_halign = (int)NUM(0); return vundef(); }
static Val bi_draw_set_valign(Inst *s, Val *a, int n) { (void)s; d_valign = (int)NUM(0); return vundef(); }
static Val bi_draw_set_font(Inst *s, Val *a, int n) { (void)s; d_font = (int)NUM(0); return vundef(); }
static Val bi_draw_set_color(Inst *s, Val *a, int n) { (void)s; d_color = (int)NUM(0); return vundef(); }
static Val bi_draw_text(Inst *s, Val *a, int n) { (void)s; char bf[40]; draw_text(NUM(0), NUM(1), val_cstr(ARG(2), bf, 40)); return vundef(); }
static Val bi_make_color_rgb(Inst *s, Val *a, int n) { (void)s; return vreal((int)NUM(0) | ((int)NUM(1) << 8) | ((int)NUM(2) << 16)); }
static Val bi_sprite_get_speed(Inst *s, Val *a, int n) {
    (void)s; int i = (int)NUM(0);
    return vreal(i >= 0 && i < G.nspr ? (G.spr[i].stype == 0 ? G.spr[i].speed : G.spr[i].speed * G.fps) : 0);
}
static Val bi_sprite_get_number(Inst *s, Val *a, int n) { (void)s; int i = (int)NUM(0); return vreal(i >= 0 && i < G.nspr ? G.spr[i].nframes : 0); }

/* ---- strings / math */
static Val bi_string_length(Inst *s, Val *a, int n) { (void)s; char bf[40]; return vreal((double)strlen(val_cstr(ARG(0), bf, 40))); }
static Val bi_string_char_at(Inst *s, Val *a, int n) {
    (void)s; char bf[40]; const char *t = val_cstr(ARG(0), bf, 40); int i = (int)NUM(1), l = (int)strlen(t);
    if (i < 1 || i > l) return vstr(str_new("", 0));
    return vstr(str_new(t + i - 1, 1));
}
static Val bi_string_copy(Inst *s, Val *a, int n) {
    (void)s; char bf[40]; const char *t = val_cstr(ARG(0), bf, 40); int i = (int)NUM(1), c = (int)NUM(2), l = (int)strlen(t);
    if (i < 1) i = 1;
    if (c < 0 || i > l) return vstr(str_new("", 0));
    if (i - 1 + c > l) c = l - (i - 1);
    return vstr(str_new(t + i - 1, c));
}
static Val bi_string_width(Inst *s, Val *a, int n) { (void)s; char bf[40]; return vreal(text_width(val_cstr(ARG(0), bf, 40))); }
static Val bi_clamp(Inst *s, Val *a, int n) { (void)s; double v = NUM(0), lo = NUM(1), hi = NUM(2); return vreal(v < lo ? lo : v > hi ? hi : v); }
static Val bi_max(Inst *s, Val *a, int n) { (void)s; double m = NUM(0); for (int i = 1; i < n; i++) if (num(a[i]) > m) m = num(a[i]); return vreal(m); }
static Val bi_floor(Inst *s, Val *a, int n) { (void)s; return vreal(floor(NUM(0))); }
static Val bi_irandom_range(Inst *s, Val *a, int n) {
    (void)s; int lo = (int)floor(NUM(0)), hi = (int)floor(NUM(1)); if (hi < lo) { int t = lo; lo = hi; hi = t; }
    return vreal(lo + rand() % (hi - lo + 1));
}

/* ---- audio */
static Val bi_audio_play_sound(Inst *s, Val *a, int n) { (void)s; return vreal(snd_play((int)NUM(0), truthy(ARG(2)))); }

/* ---- files (the original writes "save.save" but reads "save.sav": both names map to one file) */
static FILE *files[16];
static void fpath(const char *name, char *out, size_t cap) {
    if (strcmp(name, "save.save") == 0) name = "save.sav";
    snprintf(out, cap, "%s%s", plat_data_dir(), name);
}
static Val bi_file_exists(Inst *s, Val *a, int n) { (void)s; char bf[40], p[512]; fpath(val_cstr(ARG(0), bf, 40), p, sizeof p); FILE *f = fopen(p, "rb"); if (f) fclose(f); return vreal(f ? 1 : 0); }
static Val bi_file_delete(Inst *s, Val *a, int n) { (void)s; char bf[40], p[512]; fpath(val_cstr(ARG(0), bf, 40), p, sizeof p); return vreal(remove(p) == 0); }
static Val file_open(Val v, const char *mode) {
    char bf[40], p[512]; fpath(val_cstr(v, bf, 40), p, sizeof p);
    for (int i = 1; i < 16; i++) if (!files[i]) { files[i] = fopen(p, mode); return vreal(files[i] ? i : -1); }
    return vreal(-1);
}
static Val bi_file_text_open_write(Inst *s, Val *a, int n) { (void)s; return file_open(ARG(0), "w"); }
static Val bi_file_text_open_read(Inst *s, Val *a, int n) { (void)s; return file_open(ARG(0), "r"); }
static Val bi_file_text_write_real(Inst *s, Val *a, int n) {
    (void)s; int h = (int)NUM(0); if (h > 0 && h < 16 && files[h]) fprintf(files[h], "%.15g ", NUM(1)); return vundef();
}
static Val bi_file_text_read_real(Inst *s, Val *a, int n) {
    (void)s; int h = (int)NUM(0); double v = 0; if (h > 0 && h < 16 && files[h] && fscanf(files[h], "%lf", &v) != 1) v = 0; return vreal(v);
}
static Val bi_file_text_close(Inst *s, Val *a, int n) {
    (void)s; int h = (int)NUM(0); if (h > 0 && h < 16 && files[h]) { fclose(files[h]); files[h] = NULL; } return vundef();
}

static const struct { const char *n; BFn f; } LIB[] = {
    {"method", bi_method}, {"event_inherited", bi_event_inherited}, {"camera_create", bi_camera_create},
    {"script_execute", bi_script_execute}, {"instance_position", bi_instance_position},
    {"instance_exists", bi_instance_exists}, {"instance_destroy", bi_instance_destroy},
    {"instance_create_layer", bi_instance_create_layer}, {"instance_create_depth", bi_instance_create_depth},
    {"mouse_check_button_pressed", bi_mouse_check_button_pressed}, {"room_goto", bi_room_goto},
    {"layer_exists", bi_layer_exists}, {"layer_create", bi_layer_create}, {"layer_destroy", bi_layer_destroy},
    {"layer_sequence_create", bi_layer_sequence_create}, {"layer_sequence_destroy", bi_layer_sequence_destroy},
    {"layer_set_target_room", bi_layer_set_target_room}, {"layer_reset_target_room", bi_layer_reset_target_room},
    {"draw_sprite", bi_draw_sprite}, {"draw_self", bi_draw_self}, {"draw_set_halign", bi_draw_set_halign},
    {"draw_set_valign", bi_draw_set_valign}, {"draw_set_font", bi_draw_set_font}, {"draw_set_color", bi_draw_set_color},
    {"draw_text", bi_draw_text}, {"make_color_rgb", bi_make_color_rgb}, {"sprite_get_speed", bi_sprite_get_speed},
    {"sprite_get_number", bi_sprite_get_number}, {"string_length", bi_string_length},
    {"string_char_at", bi_string_char_at}, {"string_copy", bi_string_copy}, {"string_width", bi_string_width},
    {"clamp", bi_clamp}, {"max", bi_max}, {"floor", bi_floor}, {"irandom_range", bi_irandom_range},
    {"audio_play_sound", bi_audio_play_sound}, {"file_exists", bi_file_exists}, {"file_delete", bi_file_delete},
    {"file_text_open_write", bi_file_text_open_write}, {"file_text_open_read", bi_file_text_open_read},
    {"file_text_write_real", bi_file_text_write_real}, {"file_text_read_real", bi_file_text_read_real},
    {"file_text_close", bi_file_text_close}, {NULL, NULL}
};
BFn lib_lookup(const char *name) {
    for (int i = 0; LIB[i].n; i++) if (strcmp(LIB[i].n, name) == 0) return LIB[i].f;
    return NULL;
}
