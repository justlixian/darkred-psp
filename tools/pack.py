#!/usr/bin/env python3
"""
pack.py - convert a GameMaker (GMS2.3, bytecode 17) data.win into darkred.pak

The C runtime does not parse data.win. This tool resolves all variable/function
references in the GML bytecode, pre-decodes every instruction into a fixed 16-byte
record, and converts sprites / atlas / font / audio / rooms / sequences into one
flat little-endian file.

usage: python3 pack.py path/to/data.win out.pak
requires: Python 3 + Pillow
"""
import io
import struct
import sys
import os

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from PIL import Image
from gmparse import GM
from gmlink import build_links, code_ranges, decode_range, load_code

OP_PUSHFN = 0xFE          # custom: push script reference (a = func index)
MAGIC = b"DRPK"
VERSION = 2


class W:
    def __init__(self):
        self.b = bytearray()

    def u32(self, v): self.b += struct.pack('<I', v & 0xFFFFFFFF)
    def i32(self, v): self.b += struct.pack('<i', v)
    def f32(self, v): self.b += struct.pack('<f', v)
    def raw(self, d): self.b += d

    def s(self, text):
        d = text.encode('utf-8')
        self.u32(len(d))
        self.b += d + b'\0'


def build(path):
    g = GM(path)
    d = g.d
    out = W()
    out.raw(MAGIC)
    out.u32(VERSION)

    # ---------------------------------------------------------------- GEN8
    go, gs = g.c['GEN8']
    fps = struct.unpack('<f', d[go + 188:go + 192])[0]
    assert fps in (30.0, 60.0), "unexpected fps %r" % fps
    room_count = g.u32(go + 128)
    room_order = [g.u32(go + 132 + 4 * i) for i in range(room_count)]
    out.f32(fps)
    out.i32(room_order[0])
    out.s(g.sname(go + 100) or 'GAME')

    # ---------------------------------------------------------------- strings
    out.u32(len(g.strings))
    for t in g.strings:
        out.s(t)

    # ---------------------------------------------------------------- symbols / vars / funcs
    vars_, funcs, refs = build_links(g)
    sym_of = {}
    syms = []
    for v in vars_:
        if v['name'] not in sym_of:
            sym_of[v['name']] = len(syms)
            syms.append(v['name'])
    out.u32(len(syms))
    for n in syms:
        out.s(n)
    out.u32(len(vars_))
    for v in vars_:
        out.i32(sym_of[v['name']])
        out.i32(v['itype'])
    out.u32(len(funcs))
    for f in funcs:
        out.s(f['name'])

    # ---------------------------------------------------------------- code
    ents = load_code(g)
    rng = code_ranges(g, ents)
    out.u32(len(ents))
    stats = {}
    for e in ents:
        s, en = rng[e.name]
        ins = decode_range(g, s, en) if (en > s and not e.name.startswith('gml_GlobalScript_')) else []
        index_of = {i['a']: k for k, i in enumerate(ins)}
        index_of[(en - s)] = len(ins)
        out.s(e.name)
        out.u32(e.locals)
        out.u32(e.args & 0x7FFF)
        out.u32(len(ins))
        for i in ins:
            op, t1, t2, x = i['op'], i['t1'], i['t2'], i['x']
            vt = 0
            payload = b'\0' * 8
            stats[i['name']] = stats.get(i['name'], 0) + 1
            if op in (0xC0, 0xC1, 0xC2, 0xC3):
                if t1 == 0:
                    payload = struct.pack('<d', i['operand'])
                elif t1 == 1:
                    payload = struct.pack('<d', float(i['operand']))
                elif t1 in (2, 3, 4):
                    r = refs.get(i['abs'] + 4)
                    if t1 == 2 and r and r[0] == 'func':
                        op = OP_PUSHFN
                        payload = struct.pack('<ii', r[1], 0)
                    else:
                        payload = struct.pack('<q', i['operand'])
                elif t1 == 5:
                    r = refs[i['operand']]
                    assert r[0] == 'var'
                    vt = g.u32(i['operand']) >> 24
                    payload = struct.pack('<ii', r[1], 0)
                elif t1 == 6:
                    payload = struct.pack('<ii', i['operand'], 0)
                elif t1 == 0xF:
                    pass
                else:
                    raise Exception('push type %x in %s' % (t1, e.name))
            elif op == 0x84:
                pass
            elif op == 0x45:
                assert t1 != 0xF, 'popswap not supported (%s)' % e.name
                r = refs[i['operand']]
                assert r[0] == 'var'
                vt = g.u32(i['operand']) >> 24
                payload = struct.pack('<ii', r[1], 0)
            elif op == 0xD9:
                r = refs[i['operand']]
                assert r[0] == 'func'
                payload = struct.pack('<ii', r[1], 0)
            elif op in (0xB6, 0xB7, 0xB8, 0xBA, 0xBB):
                off = i['operand']
                if op == 0xBB and (i['w'] & 0xFFFFFF) == 0xF00000:
                    tgt = -1
                else:
                    tb = i['a'] + off * 4
                    tgt = index_of.get(tb, len(ins))   # out-of-range = unreachable boilerplate
                payload = struct.pack('<ii', tgt, 0)
            elif op == 0x15:
                x = (i['w'] >> 8) & 0xFF
            out.raw(struct.pack('<BBBBi', op, t1, t2, vt, x) + payload)
    print('instruction histogram:', dict(sorted(stats.items(), key=lambda kv: -kv[1])), file=sys.stderr)

    # ---------------------------------------------------------------- objects
    oo, _ = g.c['OBJT']
    n = g.u32(oo)
    out.u32(n)
    for k in range(n):
        p = g.u32(oo + 4 + 4 * k)
        name = g.sname(p)
        vc = g.u32(p + 64)
        q = p + 80 + vc * 8
        ev = {}
        for t in range(g.u32(q)):
            lp = g.u32(q + 4 + 4 * t)
            for e in range(g.u32(lp)):
                ep = g.u32(lp + 4 + 4 * e)
                sub = g.u32(ep)
                ac = g.u32(ep + 4)
                codes = [g.i32(g.u32(ep + 8 + 4 * a) + 32) for a in range(ac)]
                ev[(t, sub)] = codes[0] if codes else -1
        out.s(name)
        out.i32(g.i32(p + 4))        # sprite
        out.u32(g.u32(p + 8))        # visible
        out.u32(g.u32(p + 12))       # solid
        out.i32(g.i32(p + 16))       # depth
        out.u32(g.u32(p + 20))       # persistent
        out.i32(g.i32(p + 24))       # parent
        out.i32(g.i32(p + 28))       # mask
        out.i32(ev.get((14, 0), -1))  # precreate
        out.i32(ev.get((0, 0), -1))   # create
        out.i32(ev.get((3, 0), -1))   # step
        out.i32(ev.get((8, 0), -1))   # draw
        out.i32(ev.get((7, 5), -1))   # room end
        unknown = [k2 for k2 in ev if k2 not in ((14, 0), (0, 0), (3, 0), (8, 0), (7, 5))]
        assert not unknown, 'unhandled events on %s: %s' % (name, unknown)

    # ---------------------------------------------------------------- sprites + tpag
    to, _ = g.c['TPAG']
    tn = g.u32(to)
    tpag_ptrs = [g.u32(to + 4 + 4 * i) for i in range(tn)]
    tpag_idx = {p: i for i, p in enumerate(tpag_ptrs)}
    out.u32(tn)
    for p in tpag_ptrs:
        v = [g.u16(p + 2 * k) for k in range(11)]
        for k in range(10):
            out.i32(v[k])
        out.i32(v[10])

    so, _ = g.c['SPRT']
    sn = g.u32(so)
    out.u32(sn)
    for i in range(sn):
        p = g.u32(so + 4 + 4 * i)
        q = p + 56
        speed, stype = 30.0, 0
        if g.i32(q) == -1:
            ver = g.u32(q + 4)
            speed = g.f32(q + 12)
            stype = g.u32(q + 16)
            q += 20
            if ver >= 2:
                q += 4
            if ver >= 3:
                q += 4
        fc = g.u32(q)
        out.s(g.sname(p))
        out.u32(g.u32(p + 4)); out.u32(g.u32(p + 8))
        out.i32(g.i32(p + 48)); out.i32(g.i32(p + 52))
        out.i32(g.i32(p + 12)); out.i32(g.i32(p + 16))   # left right
        out.i32(g.i32(p + 24)); out.i32(g.i32(p + 20))   # top bottom
        out.f32(speed)
        out.u32(stype)
        out.u32(fc)
        for k in range(fc):
            out.i32(tpag_idx[g.u32(q + 4 + 4 * k)])

    # ---------------------------------------------------------------- atlas
    i0 = d.find(b'\x89PNG\r\n\x1a\n')
    j0 = d.find(b'IEND', i0) + 8
    im = Image.open(io.BytesIO(d[i0:j0])).convert('RGBA')
    from array import array
    px = array('I'); px.frombytes(im.tobytes())          # little-endian RGBA bytes -> 0xAABBGGRR
    def argb(v):
        a = v >> 24
        return 0 if a == 0 else (a << 24) | ((v & 0xFF) << 16) | (v & 0xFF00) | ((v >> 16) & 0xFF)
    uniq = sorted(set(argb(v) for v in set(px)))
    assert len(uniq) <= 256, 'atlas has %d colours; the 8-bit palette format needs <= 256' % len(uniq)
    index = {c: k for k, c in enumerate(uniq)}
    lut = {v: index[argb(v)] for v in set(px)}
    out.u32(im.size[0]); out.u32(im.size[1])
    out.u32(len(uniq))
    for c in uniq:
        out.u32(c)
    out.raw(bytes(map(lut.__getitem__, px)))

    # ---------------------------------------------------------------- font
    fo, _ = g.c['FONT']
    fcount = g.u32(fo)
    out.u32(fcount)
    for fi in range(fcount):
        fp = g.u32(fo + 4 + 4 * fi)
        tp = g.u32(fp + 28)
        ox, oy = g.u16(tp), g.u16(tp + 2)
        asc = g.u32(fp + 40)
        gn = g.u32(fp + 44)
        out.s(g.sname(fp))
        out.i32(ox); out.i32(oy)
        out.i32(asc)
        out.u32(gn)
        for k in range(gn):
            gp = g.u32(fp + 48 + 4 * k)
            ch, gx, gy, gw, gh = [g.u16(gp + 2 * j) for j in range(5)]
            shift, off = g.i16(gp + 10), g.i16(gp + 12)
            out.i32(ch); out.i32(gx); out.i32(gy); out.i32(gw); out.i32(gh)
            out.i32(shift); out.i32(off)

    # ---------------------------------------------------------------- sounds
    so2, _ = g.c['SOND']
    ao, _ = g.c['AUDO']
    n = g.u32(so2)
    out.u32(n)
    for i in range(n):
        p = g.u32(so2 + 4 + 4 * i)
        vol = g.f32(p + 20)
        aid = g.i32(p + 32)
        ap = g.u32(ao + 4 + 4 * aid)
        ln = g.u32(ap)
        wav = d[ap + 4:ap + 4 + ln]
        fmt = struct.unpack('<HHIIHH', wav[20:36])
        assert fmt[0] == 1 and fmt[5] == 16, 'only PCM16 supported'
        ch, rate = fmt[1], fmt[2]
        # locate 'data' chunk
        pos = 12
        data = None
        while pos + 8 <= len(wav):
            cid = wav[pos:pos + 4]
            csz = struct.unpack('<I', wav[pos + 4:pos + 8])[0]
            if cid == b'data':
                data = wav[pos + 8:pos + 8 + csz]
                break
            pos += 8 + csz + (csz & 1)
        assert data is not None
        out.s(g.sname(p))
        out.f32(vol)
        out.u32(ch); out.u32(rate)
        out.u32(len(data) // (2 * ch))
        out.raw(data[:(len(data) // (2 * ch)) * 2 * ch])

    # ---------------------------------------------------------------- rooms
    ro, _ = g.c['ROOM']
    n = g.u32(ro)
    out.u32(n)
    for r in range(n):
        p = g.u32(ro + 4 + 4 * r)
        out.s(g.sname(p))
        out.u32(g.u32(p + 8)); out.u32(g.u32(p + 12))
        out.u32(g.u32(p + 24))               # bg colour (BGR)
        ip = g.u32(p + 48)
        inst = {}
        for k in range(g.u32(ip)):
            q = g.u32(ip + 4 + 4 * k)
            inst[g.u32(q + 12)] = (g.i32(q + 8), g.i32(q), g.i32(q + 4))
        lp = g.u32(p + 88)
        ln = g.u32(lp)
        out.u32(ln)
        for i in range(ln):
            q = g.u32(lp + 4 + 4 * i)
            t = g.u32(q + 8)
            out.s(g.sname(q))
            out.u32(g.u32(q + 4)); out.u32(t); out.i32(g.i32(q + 12)); out.u32(g.u32(q + 32))
            if t == 2:
                c = g.u32(q + 36)
                out.u32(c)
                for j in range(c):
                    iid = g.u32(q + 40 + 4 * j)
                    o_, x_, y_ = inst[iid]
                    out.u32(iid); out.i32(o_); out.i32(x_); out.i32(y_)
                out.i32(-1); out.u32(0)
            elif t == 1:
                spr = g.u32(q + 36 + 8)
                col = g.u32(q + 36 + 24)
                out.u32(0)
                out.i32(-1 if spr == 0xFFFFFFFF else spr)
                out.u32(col)
            else:
                raise Exception('unsupported layer type %d' % t)

    # ---------------------------------------------------------------- sequences
    qo, _ = g.c['SEQN']
    n = g.u32(qo + 4)
    seq_ptrs = [g.u32(qo + 8 + 4 * i) for i in range(n)]
    out.u32(n)
    for sp_ in seq_ptrs:
        def w(off): return g.u32(sp_ + off)
        def f(off): return g.f32(sp_ + off)
        def sw(off): return g.sname(sp_ + off)
        assert sw(40) == 'GMGraphicTrack' and sw(72) == 'GMColourTrack', 'unexpected sequence layout'
        assert sw(188) == 'origin' and sw(280) == 'position' and sw(448) == 'scale'
        out.s(sw(0))
        out.f32(f(8))                  # speed (fps)
        out.f32(f(16))                 # length (frames)
        out.i32(g.i32(sp_ + 20)); out.i32(g.i32(sp_ + 24))   # origin
        spr_name = sw(44)
        spr_idx = None
        # sprite index by name
        sso, _ = g.c['SPRT']
        for k in range(g.u32(sso)):
            if g.sname(g.u32(sso + 4 + 4 * k)) == spr_name:
                spr_idx = k
        assert spr_idx is not None
        out.i32(spr_idx)
        out.f32(f(340)); out.f32(f(356))     # position
        out.f32(f(508)); out.f32(f(524))     # scale
        out.f32(f(112)); out.u32(w(136))     # colour key 0
        out.f32(f(148)); out.u32(w(172))     # colour key 1
        out.f32(f(576))                      # moment time
        out.s(sw(604))                       # moment script

    return bytes(out.b)


def main():
    if len(sys.argv) != 3:
        print(__doc__)
        sys.exit(1)
    blob = build(sys.argv[1])
    with open(sys.argv[2], 'wb') as fh:
        fh.write(blob)
    print('wrote %s (%d bytes)' % (sys.argv[2], len(blob)))


if __name__ == '__main__':
    main()
