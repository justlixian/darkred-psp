/* vm.c - GameMaker (GMS2.3, bytecode 17) stack VM, values and garbage collection */
#include "gm.h"
#include <stdarg.h>

int vm_warn = 0;

void vm_log(const char *fmt, ...) {
    static int budget = 200;
    if (budget-- <= 0) return;
    va_list ap; va_start(ap, fmt);
    fputs("[vm] ", stderr); vfprintf(stderr, fmt, ap); fputc('\n', stderr);
    va_end(ap); vm_warn++;
}
static void vm_fatal(const char *m) { fprintf(stderr, "[vm] FATAL: %s\n", m); exit(3); }

/* ------------------------------------------------------------------ values */
Val vreal(double d) { Val v; v.t = T_REAL; v.u.d = d; return v; }
Val vundef(void)    { Val v; v.t = T_UNDEF; v.u.d = 0; return v; }
Val vstr(Str *s)    { Val v; v.t = T_STR; v.u.s = s; return v; }
Val vfn(int code)   { Val v; v.t = T_FN; v.u.fn = code; return v; }

static Str *str_head; static Arr *arr_head; static uint8_t epoch = 1;

Str *str_new(const char *s, int len) {
    Str *p = (Str *)malloc(sizeof(Str) + (size_t)len);
    p->len = len; memcpy(p->s, s, (size_t)len); p->s[len] = 0; p->mark = 0; p->perm = 0;
    p->next = str_head; str_head = p; return p;
}
Arr *arr_new(void) {
    Arr *a = (Arr *)calloc(1, sizeof(Arr));
    a->next = arr_head; arr_head = a; return a;
}
Val *arr_slot(Arr *a, int idx, int grow) {
    if (idx < 0 || idx > 100000) return NULL;
    if (idx >= a->n) {
        if (!grow) return NULL;
        if (idx >= a->cap) {
            int nc = a->cap ? a->cap : 8;
            while (nc <= idx) nc *= 2;
            a->v = (Val *)realloc(a->v, sizeof(Val) * (size_t)nc); a->cap = nc;
        }
        for (int i = a->n; i <= idx; i++) a->v[i] = vreal(0);
        a->n = idx + 1;
    }
    return &a->v[idx];
}

double num(Val v) {
    switch (v.t) {
    case T_REAL: return v.u.d;
    case T_STR:  return atof(v.u.s->s);
    case T_FN:   return (double)v.u.fn;
    default:     return 0;
    }
}
int truthy(Val v) { return num(v) > 0.5; }

const char *val_cstr(Val v, char *buf, int cap) {
    if (v.t == T_STR) return v.u.s->s;
    if (v.t == T_REAL) {
        double d = v.u.d;
        if (d == floor(d) && fabs(d) < 1e15) snprintf(buf, (size_t)cap, "%lld", (long long)d);
        else snprintf(buf, (size_t)cap, "%.15g", d);
        return buf;
    }
    if (v.t == T_UNDEF) return "undefined";
    snprintf(buf, (size_t)cap, "<%d>", v.t); return buf;
}

/* ------------------------------------------------------------------ gc */
void gc_begin(void) { epoch = (uint8_t)(epoch == 255 ? 1 : epoch + 1); }
void gc_mark(Val v) {
    if (v.t == T_STR) v.u.s->mark = epoch;
    else if (v.t == T_ARR) {
        Arr *a = v.u.a;
        if (a->mark == epoch) return;
        a->mark = epoch;
        for (int i = 0; i < a->n; i++) gc_mark(a->v[i]);
    }
}
void gc_sweep(void) {
    Str **sp = &str_head;
    while (*sp) { Str *s = *sp; if (!s->perm && s->mark != epoch) { *sp = s->next; free(s); } else sp = &s->next; }
    Arr **ap = &arr_head;
    while (*ap) { Arr *a = *ap; if (a->mark != epoch) { *ap = a->next; free(a->v); free(a); } else ap = &a->next; }
}

/* ------------------------------------------------------------------ comparison / arithmetic */
static int cmp_vals(Val a, Val b, int kind) {
    int eq;
    if (a.t == T_STR && b.t == T_STR) {
        int c = strcmp(a.u.s->s, b.u.s->s);
        switch (kind) { case 1: return c < 0; case 2: return c <= 0; case 3: return c == 0;
                        case 4: return c != 0; case 5: return c >= 0; default: return c > 0; }
    }
    if (a.t == T_FN || b.t == T_FN || a.t == T_ARR || b.t == T_ARR) {
        eq = (a.t == b.t) && ((a.t == T_FN && a.u.fn == b.u.fn) || (a.t == T_ARR && a.u.a == b.u.a));
        if (kind == 3) return eq;
        if (kind == 4) return !eq;
        return 0;
    }
    double x = num(a), y = num(b);
    switch (kind) {
    case 1: return x < y;
    case 2: return x <= y;
    case 3: return fabs(x - y) < 1e-5;
    case 4: return fabs(x - y) >= 1e-5;
    case 5: return x >= y;
    default: return x > y;
    }
}

/* ------------------------------------------------------------------ interpreter */
#define STK_MAX 32768
static Val stk[STK_MAX];
static int sp;
static int depth;

typedef struct { int sym; Val v; } Loc;
typedef struct { Val args[16]; int nargs; Loc loc[64]; int nloc; } Frame;
typedef struct { int *ids; int n, idx; Inst *save_self, *save_other; } Env;

static Val *loc_slot(Frame *f, int sym, int create) {
    for (int i = 0; i < f->nloc; i++) if (f->loc[i].sym == sym) return &f->loc[i].v;
    if (!create) return NULL;
    if (f->nloc >= 64) vm_fatal("too many locals");
    f->loc[f->nloc].sym = sym; f->loc[f->nloc].v = vundef();
    return &f->loc[f->nloc++].v;
}

/* resolve an "instance specifier" value. returns 0 = instance (may be NULL), 1 = global, 2 = ignore */
static int scope_of(Val v, Inst *self, Inst *other, Inst **out) {
    int n = (int)num(v); *out = NULL;
    if (v.t != T_REAL) return 2;
    if (n == -1) { *out = self; return 0; }
    if (n == -2) { *out = other; return 0; }
    if (n == -5) return 1;
    if (n == -6 || n == -4 || n == -3 || n == -9) return 2;
    if (n >= 100000) { *out = inst_by_id(n); return 0; }
    if (n >= 0) { *out = inst_first(n); return 0; }
    return 2;
}

/* storage for an array-capable variable; NULL when not addressable */
static Val *var_slot(Val instv, int sym, Inst *self, Inst *other) {
    Inst *in; int sc = scope_of(instv, self, other, &in);
    if (sc == 1) return &globals[sym];
    if (sc == 0 && in && !G.bsym[sym]) return &in->vars[sym];
    return NULL;
}
static Val elem_read(Val instv, int sym, int idx, int vivify, Inst *self, Inst *other) {
    Val *slot = var_slot(instv, sym, self, other);
    if (!slot) return vundef();
    if (slot->t != T_ARR) {
        if (!vivify) return vundef();
        slot->t = T_ARR; slot->u.a = arr_new();
    }
    Val *e = arr_slot(slot->u.a, idx, vivify);
    if (!e) return vundef();
    if (vivify && e->t != T_ARR) { e->t = T_ARR; e->u.a = arr_new(); }
    return *e;
}
static void elem_write(Val instv, int sym, int idx, Val v, Inst *self, Inst *other) {
    Val *slot = var_slot(instv, sym, self, other);
    if (!slot) return;                         /* e.g. view_camera[0] = ... : not needed */
    if (slot->t != T_ARR) { slot->t = T_ARR; slot->u.a = arr_new(); }
    Val *e = arr_slot(slot->u.a, idx, 1);
    if (e) *e = v;
}

static Val builtin_global_get(int sym) { return inst_get(NULL, sym); }

Val vm_call(int ci, Inst *self, Inst *other, const Val *args, int nargs) {
    if (ci < 0 || ci >= G.ncode) return vundef();
    Code *c = &G.code[ci];
    if (depth > 48) vm_fatal("call depth exceeded");
    depth++;
    Frame *f = (Frame *)malloc(sizeof(Frame));
    f->nargs = nargs > 16 ? 16 : nargs; f->nloc = 0;
    for (int i = 0; i < f->nargs; i++) f->args[i] = args[i];
    Env env[8]; int envn = 0;
    int base = sp, pc = 0;
    Inst *s = self, *o = other;
    Val ret = vundef();

#define PUSH(v) do { if (sp >= STK_MAX) vm_fatal("stack overflow"); stk[sp++] = (v); } while (0)
#define POP()   (sp > base ? stk[--sp] : (vm_log("stack underflow in %s @%d", c->name, pc - 1), vundef()))
#define POP_INST(var) do { (var) = POP(); if ((var).t == T_REAL && (var).u.d == -9) (var) = POP(); } while (0)

    while (pc < c->n) {
        const Ins *in = &c->ins[pc++];
        switch (in->op) {
        /* ---------------- pushes ---------------- */
        case 0x84: PUSH(vreal(in->x)); break;                         /* pushi.e */
        case 0xFE: PUSH(vfn(G.func[in->u.i.a].code)); break;         /* script reference */
        case 0xC0: case 0xC1: case 0xC2: case 0xC3:
            switch (in->t1) {
            case 0: PUSH(vreal(in->u.d)); break;
            case 1: PUSH(vreal(in->u.d)); break;
            case 2: case 3: case 4: PUSH(vreal((double)in->u.l)); break;
            case 6: PUSH(vstr(G.str[in->u.i.a])); break;
            case 0xF: PUSH(vreal(in->x)); break;
            case 5: {
                int sym = G.var[in->u.i.a].sym, vt = in->vt, x = in->x;
                Val r = vundef();
                if ((vt & 0xE0) == 0xA0 || in->op == 0xC3) {
                    switch (x) {
                    case -1: r = inst_get(s, sym); break;
                    case -2: r = inst_get(o, sym); break;
                    case -5: r = global_get(sym); break;
                    case -6: r = builtin_global_get(sym); break;
                    case -7: { Val *l = loc_slot(f, sym, 0); if (l) r = *l; break; }
                    case -15: { int n = G.argidx[sym]; if (n >= 0 && n < f->nargs) r = f->args[n]; break; }
                    default: if (x >= 0) { Inst *t = inst_first(x); if (t) r = inst_get(t, sym); } break;
                    }
                } else if (vt == 0x80) {
                    Val iv; POP_INST(iv);
                    Inst *t; int sc = scope_of(iv, s, o, &t);
                    if (sc == 1) r = global_get(sym); else if (sc == 0 && t) r = inst_get(t, sym);
                } else { /* 0x00 / 0x10 / 0x90 : array element, [instance, index] on the stack */
                    Val iv, xv = POP(); POP_INST(iv);
                    r = elem_read(iv, sym, (int)num(xv), vt != 0x00, s, o);
                }
                PUSH(r); break;
            }
            default: vm_log("push type %d", in->t1); PUSH(vundef()); break;
            }
            break;
        /* ---------------- pops ---------------- */
        case 0x45: {
            int sym = G.var[in->u.i.a].sym, vt = in->vt, x = in->x;
            if ((vt & 0xE0) == 0xA0) {
                Val v = POP();
                switch (x) {
                case -1: if (s) inst_set(s, sym, v); break;
                case -2: if (o) inst_set(o, sym, v); break;
                case -5: global_set(sym, v); break;
                case -7: *loc_slot(f, sym, 1) = v; break;
                case -6: break;
                default:
                    if (x >= 0) {
                        int *ids; int n = inst_list(vreal(x), s, o, &ids);
                        for (int k = 0; k < n; k++) { Inst *t = inst_by_id(ids[k]); if (t) inst_set(t, sym, v); }
                        free(ids);
                    }
                    break;
                }
            } else if (vt == 0x80) {
                Val iv; POP_INST(iv);
                Val v = POP();
                Inst *t; int sc = scope_of(iv, s, o, &t);
                if (sc == 1) global_set(sym, v); else if (sc == 0 && t) inst_set(t, sym, v);
            } else {      /* array write. t1 == 5: [value, inst, index]; t1 == 2 (compound): [inst, index, value] */
                Val iv, xv, v;
                if (in->t1 == 5) { xv = POP(); POP_INST(iv); v = POP(); }
                else             { v = POP(); xv = POP(); POP_INST(iv); }
                elem_write(iv, sym, (int)num(xv), v, s, o);
            }
            break;
        }
        /* ---------------- arithmetic ---------------- */
        case 0x08: { Val b = POP(), a = POP(); PUSH(vreal(num(a) * num(b))); break; }
        case 0x09: { Val b = POP(), a = POP(); double d = num(b); PUSH(vreal(d == 0 ? 0 : num(a) / d)); break; }
        case 0x0A: { Val b = POP(), a = POP(); long long d = (long long)num(b); PUSH(vreal(d == 0 ? 0 : (double)((long long)num(a) / d))); break; }
        case 0x0B: { Val b = POP(), a = POP(); double d = num(b); PUSH(vreal(d == 0 ? 0 : fmod(num(a), d))); break; }
        case 0x0C: {
            Val b = POP(), a = POP();
            if (a.t == T_STR || b.t == T_STR) {
                char b1[40], b2[40]; const char *x = val_cstr(a, b1, 40), *y = val_cstr(b, b2, 40);
                int lx = (int)strlen(x), ly = (int)strlen(y);
                char *t = (char *)malloc((size_t)lx + (size_t)ly + 1); memcpy(t, x, (size_t)lx); memcpy(t + lx, y, (size_t)ly);
                PUSH(vstr(str_new(t, lx + ly))); free(t);
            } else PUSH(vreal(num(a) + num(b)));
            break;
        }
        case 0x0D: { Val b = POP(), a = POP(); PUSH(vreal(num(a) - num(b))); break; }
        case 0x0E: { Val b = POP(), a = POP(); PUSH(vreal((double)((long long)num(a) & (long long)num(b)))); break; }
        case 0x0F: { Val b = POP(), a = POP(); PUSH(vreal((double)((long long)num(a) | (long long)num(b)))); break; }
        case 0x10: { Val b = POP(), a = POP(); PUSH(vreal((double)((long long)num(a) ^ (long long)num(b)))); break; }
        case 0x11: { Val a = POP(); PUSH(vreal(-num(a))); break; }
        case 0x12: { Val a = POP(); PUSH(vreal(truthy(a) ? 0 : 1)); break; }
        case 0x13: { Val b = POP(), a = POP(); PUSH(vreal((double)((long long)num(a) << (int)num(b)))); break; }
        case 0x14: { Val b = POP(), a = POP(); PUSH(vreal((double)((long long)num(a) >> (int)num(b)))); break; }
        case 0x15: { Val b = POP(), a = POP(); PUSH(vreal(cmp_vals(a, b, in->x) ? 1 : 0)); break; }
        case 0x07: {                                                    /* conv */
            if (in->t2 == 6) { Val v = POP(); if (v.t != T_STR) { char bf[40]; const char *t = val_cstr(v, bf, 40); v = vstr(str_new(t, (int)strlen(t))); } PUSH(v); }
            else if (in->t2 == 2 || in->t2 == 3) { Val v = POP(); if (v.t == T_REAL || v.t == T_STR) v = vreal(trunc(num(v))); PUSH(v); }
            else if (in->t2 == 4) { Val v = POP(); PUSH(vreal(truthy(v) ? 1 : 0)); }
            break;
        }
        /* ---------------- stack ---------------- */
        case 0x86: {                                                    /* dup */
            if (in->x & 0x8000) {
                /* "dup swap" as emitted for compound array assignment (a[i]++): rotate the top 4 slots right by one */
                if ((in->x & 0xFFFF) != 0xB004) vm_log("unsupported dup-swap %04x in %s", in->x & 0xFFFF, c->name);
                if (sp - base >= 4) { Val t = stk[sp - 1]; stk[sp - 1] = stk[sp - 2]; stk[sp - 2] = stk[sp - 3]; stk[sp - 3] = stk[sp - 4]; stk[sp - 4] = t; }
            } else {
                int n = (in->x & 0xFF) + 1;
                if (sp - base < n) { vm_log("dup underflow"); break; }
                int src = sp - n;
                for (int k = 0; k < n; k++) { Val t = stk[src + k]; PUSH(t); }
            }
            break;
        }
        case 0x9E: (void)POP(); break;                                  /* popz */
        case 0xFF:                                                      /* break (array helpers) */
            switch (in->x) {
            case -2: { Val xv = POP(), av = POP(); Val r = vundef();
                       if (av.t == T_ARR) { Val *e = arr_slot(av.u.a, (int)num(xv), 0); r = e ? *e : vreal(0); }
                       PUSH(r); break; }
            case -3: { Val xv = POP(), av = POP(), v = POP();
                       if (av.t == T_ARR) { Val *e = arr_slot(av.u.a, (int)num(xv), 1); if (e) *e = v; }
                       break; }
            case -5: (void)POP(); break;                                /* setowner */
            default: vm_log("unsupported break %d in %s", in->x, c->name); break;
            }
            break;
        /* ---------------- control flow ---------------- */
        case 0xB6: pc = in->u.i.a; break;
        case 0xB7: { Val v = POP(); if (truthy(v)) pc = in->u.i.a; break; }
        case 0xB8: { Val v = POP(); if (!truthy(v)) pc = in->u.i.a; break; }
        case 0x9C: ret = POP(); pc = c->n; break;
        case 0x9D: pc = c->n; break;
        /* ---------------- with ---------------- */
        case 0xBA: {
            Val tv; POP_INST(tv);
            if (envn >= 8) vm_fatal("with nesting too deep");
            int *ids; int n = inst_list(tv, s, o, &ids);
            if (n == 0) { free(ids); pc = in->u.i.a + 1; break; }
            Env *e = &env[envn++];
            e->ids = ids; e->n = n; e->idx = 0; e->save_self = s; e->save_other = o;
            { Inst *t = inst_by_id(ids[0]); if (!t && tv.t == T_REAL && (int)num(tv) == -1) t = s; o = s; s = t; }
            break;
        }
        case 0xBB: {
            if (envn == 0) break;
            Env *e = &env[envn - 1];
            if (in->u.i.a >= 0) {
                Inst *t = NULL;
                while (++e->idx < e->n && !(t = inst_by_id(e->ids[e->idx]))) {}
                if (e->idx < e->n && t) { s = t; pc = in->u.i.a; break; }
            }
            s = e->save_self; o = e->save_other; free(e->ids); envn--;
            break;
        }
        /* ---------------- calls ---------------- */
        case 0xD9: {
            Func *fn = &G.func[in->u.i.a]; int argc = in->x;
            Val av[32]; if (argc > 32) argc = 32;
            for (int k = 0; k < argc; k++) av[k] = POP();
            Val r = vundef();
            if (fn->is_script) { if (fn->code >= 0) r = vm_call(fn->code, s, o, av, argc); }
            else if (fn->bfn) r = fn->bfn(s, av, argc);
            else vm_log("call to unimplemented builtin %s", fn->name);
            PUSH(r);
            break;
        }
        default:
            vm_log("unsupported opcode %02x in %s @%d", in->op, c->name, pc - 1);
            break;
        }
    }
    while (envn > 0) free(env[--envn].ids);
    sp = base;
    free(f);
    depth--;
    return ret;
}
