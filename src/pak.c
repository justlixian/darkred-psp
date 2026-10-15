/* pak.c - loads darkred.pak (produced by tools/pack.py). Streams from the file so that peak memory stays small
 * (matters on the PSP). */
#include "gm.h"

Game G;

/* the packer writes 16-byte instruction records; make sure every target ABI (x86-64, ARM32 Vita, MIPS32 PSP) agrees */
_Static_assert(sizeof(Ins) == 16, "Ins must be 16 bytes");
_Static_assert(sizeof(int) == 4, "int must be 32-bit");

static FILE *F;

static void die(const char *m) { fprintf(stderr, "pak: %s\n", m); exit(2); }
static void rd(void *p, size_t n) { if (n && fread(p, 1, n, F) != n) die("truncated file"); }
static uint32_t u32(void) { uint32_t v; rd(&v, 4); return v; }       /* little-endian hosts only (x86, ARM, MIPS-LE) */
static int32_t  i32(void) { return (int32_t)u32(); }
static float    f32(void) { float v; rd(&v, 4); return v; }
static char *cstr(void) {
    uint32_t n = u32(); char *s = (char *)malloc((size_t)n + 1);
    rd(s, n); s[n] = 0;
    char z; rd(&z, 1);
    return s;
}
static void *blob(size_t n) { void *p = malloc(n ? n : 1); rd(p, n); return p; }

static const struct { const char *n; int id; } BUILTIN_VARS[] = {
    {"x", BV_X}, {"y", BV_Y}, {"id", BV_ID}, {"sprite_index", BV_SPRITE}, {"image_index", BV_IMAGE_INDEX},
    {"image_speed", BV_IMAGE_SPEED}, {"image_number", BV_IMAGE_NUMBER}, {"image_alpha", BV_IMAGE_ALPHA},
    {"depth", BV_DEPTH}, {"visible", BV_VISIBLE}, {"layer", BV_LAYER}, {"persistent", BV_PERSISTENT},
    {"object_index", BV_OBJECT_INDEX}, {"room", BV_ROOM}, {"mouse_x", BV_MOUSE_X}, {"mouse_y", BV_MOUSE_Y},
    {"view_camera", BV_VIEW_CAMERA}, {NULL, 0}
};

int pak_load(const char *path) {
    F = fopen(path, "rb");
    if (!F) { fprintf(stderr, "cannot open %s\n", path); return -1; }
    char magic[4]; rd(magic, 4);
    if (memcmp(magic, "DRPK", 4) != 0) { fprintf(stderr, "bad pak magic\n"); fclose(F); return -1; }
    if (u32() != 2) { fprintf(stderr, "bad pak version (re-run tools/pack.py)\n"); fclose(F); return -1; }

    memset(&G, 0, sizeof G);
    G.fps = f32(); G.first_room = i32(); G.title = cstr();

    G.nstr = (int)u32(); G.str = (Str **)calloc((size_t)G.nstr + 1, sizeof(Str *));
    for (int i = 0; i < G.nstr; i++) {
        char *s = cstr(); G.str[i] = str_new(s, (int)strlen(s)); G.str[i]->perm = 1; free(s);
    }

    G.nsym = (int)u32(); G.sym = (char **)calloc((size_t)G.nsym + 1, sizeof(char *));
    for (int i = 0; i < G.nsym; i++) G.sym[i] = cstr();
    G.nvar = (int)u32(); G.var = (VarRef *)calloc((size_t)G.nvar + 1, sizeof(VarRef));
    for (int i = 0; i < G.nvar; i++) { G.var[i].sym = i32(); G.var[i].itype = i32(); }
    G.nfunc = (int)u32(); G.func = (Func *)calloc((size_t)G.nfunc + 1, sizeof(Func));
    for (int i = 0; i < G.nfunc; i++) G.func[i].name = cstr();

    G.ncode = (int)u32(); G.code = (Code *)calloc((size_t)G.ncode + 1, sizeof(Code));
    for (int i = 0; i < G.ncode; i++) {
        Code *c = &G.code[i];
        c->name = cstr(); c->nlocals = (int)u32(); c->nargs = (int)u32(); c->n = (int)u32();
        c->ins = (Ins *)blob((size_t)c->n * sizeof(Ins));
    }
    /* link functions: scripts -> code index, others -> builtin table */
    for (int i = 0; i < G.nfunc; i++) {
        Func *fn = &G.func[i]; fn->code = -1; fn->bfn = NULL;
        if (strncmp(fn->name, "gml_Script_", 11) == 0) {
            for (int c = 0; c < G.ncode; c++) if (strcmp(G.code[c].name, fn->name) == 0) { fn->code = c; break; }
            fn->is_script = 1;
            if (fn->code < 0) fprintf(stderr, "warning: script %s has no code\n", fn->name);
        } else {
            fn->is_script = 0;
            fn->bfn = lib_lookup(fn->name);
            if (!fn->bfn) fprintf(stderr, "warning: builtin '%s' not implemented\n", fn->name);
        }
    }

    G.nobj = (int)u32(); G.obj = (Obj *)calloc((size_t)G.nobj + 1, sizeof(Obj));
    for (int i = 0; i < G.nobj; i++) {
        Obj *o = &G.obj[i];
        o->name = cstr(); o->sprite = i32(); o->visible = (int)u32(); o->solid = (int)u32(); o->depth = i32();
        o->persistent = (int)u32(); o->parent = i32(); o->mask = i32();
        o->c_pre = i32(); o->c_create = i32(); o->c_step = i32(); o->c_draw = i32(); o->c_roomend = i32();
    }

    G.ntpag = (int)u32(); G.tpag = (Tpag *)calloc((size_t)G.ntpag + 1, sizeof(Tpag));
    for (int i = 0; i < G.ntpag; i++) {
        Tpag *t = &G.tpag[i];
        t->sx = i32(); t->sy = i32(); t->sw = i32(); t->sh = i32(); t->tx = i32(); t->ty = i32();
        t->tw = i32(); t->th = i32(); t->bw = i32(); t->bh = i32(); t->page = i32();
    }

    G.nspr = (int)u32(); G.spr = (Sprite *)calloc((size_t)G.nspr + 1, sizeof(Sprite));
    for (int i = 0; i < G.nspr; i++) {
        Sprite *s = &G.spr[i];
        s->name = cstr(); s->w = (int)u32(); s->h = (int)u32(); s->ox = i32(); s->oy = i32();
        s->bl = i32(); s->br = i32(); s->bt = i32(); s->bb = i32();
        s->speed = f32(); s->stype = (int)u32(); s->nframes = (int)u32();
        s->frames = (int *)malloc(sizeof(int) * (size_t)(s->nframes + 1));
        for (int k = 0; k < s->nframes; k++) s->frames[k] = i32();
    }

    G.aw = (int)u32(); G.ah = (int)u32();
    {
        uint32_t np = u32(); if (np > 256) die("palette too large");
        for (uint32_t i = 0; i < np; i++) G.pal[i] = u32();
        size_t n = (size_t)G.aw * (size_t)G.ah;
        G.atlas8 = (uint8_t *)malloc(n); rd(G.atlas8, n);
    }

    G.nfont = (int)u32(); G.font = (Font *)calloc((size_t)G.nfont + 1, sizeof(Font));
    for (int i = 0; i < G.nfont; i++) {
        Font *ft = &G.font[i];
        ft->name = cstr(); ft->ox = i32(); ft->oy = i32(); ft->asc = i32(); ft->n = (int)u32();
        ft->g = (Glyph *)calloc((size_t)ft->n + 1, sizeof(Glyph));
        for (int k = 0; k < 256; k++) ft->map[k] = -1;
        for (int k = 0; k < ft->n; k++) {
            Glyph *g = &ft->g[k];
            g->ch = i32(); g->x = i32(); g->y = i32(); g->w = i32(); g->h = i32(); g->shift = i32(); g->off = i32();
            if (g->ch >= 0 && g->ch < 256) ft->map[g->ch] = (short)k;
            if (g->h > ft->lineh) ft->lineh = g->h;
        }
    }

    G.nsnd = (int)u32(); G.snd = (Sound *)calloc((size_t)G.nsnd + 1, sizeof(Sound));
    for (int i = 0; i < G.nsnd; i++) {
        Sound *s = &G.snd[i];
        s->name = cstr(); s->vol = f32(); s->ch = (int)u32(); s->rate = (int)u32(); s->n = (int)u32();
        s->pcm = (int16_t *)blob((size_t)s->n * 2 * (size_t)s->ch);
    }

    G.nroom = (int)u32(); G.room = (Room *)calloc((size_t)G.nroom + 1, sizeof(Room));
    for (int i = 0; i < G.nroom; i++) {
        Room *r = &G.room[i];
        r->name = cstr(); r->w = (int)u32(); r->h = (int)u32(); r->color = u32(); r->nlayers = (int)u32();
        r->layers = (RLayer *)calloc((size_t)r->nlayers + 1, sizeof(RLayer));
        for (int k = 0; k < r->nlayers; k++) {
            RLayer *l = &r->layers[k];
            l->name = cstr(); l->id = (int)u32(); l->type = (int)u32(); l->depth = i32(); l->visible = (int)u32();
            l->ninst = (int)u32(); l->inst = (RInst *)calloc((size_t)l->ninst + 1, sizeof(RInst));
            for (int j = 0; j < l->ninst; j++) {
                l->inst[j].iid = (int)u32(); l->inst[j].obj = i32(); l->inst[j].x = i32(); l->inst[j].y = i32();
            }
            l->bg_sprite = i32(); l->bg_color = u32();
        }
    }

    G.nseq = (int)u32(); G.seq = (SeqDef *)calloc((size_t)G.nseq + 1, sizeof(SeqDef));
    for (int i = 0; i < G.nseq; i++) {
        SeqDef *q = &G.seq[i];
        q->name = cstr(); q->fps = f32(); q->len = f32(); q->ox = i32(); q->oy = i32(); q->sprite = i32();
        q->px = f32(); q->py = f32(); q->sx = f32(); q->sy = f32();
        q->t0 = f32(); q->c0 = u32(); q->t1 = f32(); q->c1 = u32(); q->mom_t = f32(); q->mom_script = cstr();
        q->mom_code = -1;
        for (int c = 0; c < G.ncode; c++) if (strcmp(G.code[c].name, q->mom_script) == 0) q->mom_code = c;
    }
    { char extra; if (fread(&extra, 1, 1, F) == 1) fprintf(stderr, "warning: trailing bytes in pak\n"); }
    fclose(F); F = NULL;

    /* symbol side tables */
    G.bsym = (unsigned char *)calloc((size_t)G.nsym + 1, 1);
    G.argidx = (short *)malloc(sizeof(short) * (size_t)(G.nsym + 1));
    for (int i = 0; i < G.nsym; i++) {
        G.argidx[i] = -1;
        for (int k = 0; BUILTIN_VARS[k].n; k++) if (strcmp(G.sym[i], BUILTIN_VARS[k].n) == 0) G.bsym[i] = (unsigned char)BUILTIN_VARS[k].id;
        if (strncmp(G.sym[i], "argument", 8) == 0 && G.sym[i][8] >= '0' && G.sym[i][8] <= '9') G.argidx[i] = (short)atoi(G.sym[i] + 8);
    }
    return 0;
}
