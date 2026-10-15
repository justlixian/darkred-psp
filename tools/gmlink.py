import struct
from gmparse import GM
from gmdis import *

def build_links(g):
    """map operand-word address -> ('var',idx) / ('func',idx); also return tables"""
    vo, vsz = g.c['VARI']
    nv = (vsz-12)//20
    vars_ = []
    refs = {}
    for i in range(nv):
        p = vo+12+20*i
        name = g.sname(p); itype = g.i32(p+4); vid = g.i32(p+8); occ = g.u32(p+12); first = g.u32(p+16)
        vars_.append(dict(name=name, itype=itype, vid=vid, occ=occ))
        a = first
        for k in range(0 if first==0xFFFFFFFF else occ):
            refs[a+4] = ('var', i)
            w = g.u32(a+4)
            a += (w & 0x07FFFFFF)
    fo, fsz = g.c['FUNC']
    nf = g.u32(fo)
    funcs = []
    for i in range(nf):
        p = fo+4+12*i
        name = g.sname(p); occ = g.u32(p+4); first = g.u32(p+8)
        funcs.append(dict(name=name, occ=occ))
        a = first
        for k in range(0 if first==0xFFFFFFFF else occ):
            refs[a] = ('func', i)
            w = g.u32(a)
            a += (w & 0x07FFFFFF)
    return vars_, funcs, refs

def code_ranges(g, ents):
    """compute (start_abs, end_abs) per entry, handling children sharing parent's blob"""
    groups = {}
    for e in ents: groups.setdefault(e.addr, []).append(e)
    rng = {}
    for addr, lst in groups.items():
        offs = sorted(set(e.offset for e in lst))
        for e in lst:
            nxt = [o for o in offs if o > e.offset]
            end = addr + (nxt[0] if nxt else e.length)
            rng[e.name] = (addr + e.offset, end)
    return rng

def decode_range(g, start, end):
    ins=[]; pos=start
    while pos<end:
        w = g.u32(pos); op = w>>24; t1=(w>>16)&0xF; t2=(w>>20)&0xF
        extra = w&0xFFFF; sx = extra-0x10000 if extra>=0x8000 else extra
        size=4; operand=None
        if op in (0xC0,0xC1,0xC2,0xC3):
            ts=tsize(t1)
            size=4+ts
            if t1==0: operand=struct.unpack('<d',g.d[pos+4:pos+12])[0]
            elif t1==1: operand=struct.unpack('<f',g.d[pos+4:pos+8])[0]
            elif t1==2: operand=g.i32(pos+4)
            elif t1==3: operand=struct.unpack('<q',g.d[pos+4:pos+12])[0]
            elif t1==4: operand=g.i32(pos+4)
            elif t1==5: operand=pos+4
            elif t1==6: operand=g.u32(pos+4)
            elif t1==0xF: operand=sx
        elif op==0x84: operand=sx
        elif op==0x45:
            if t1==0xF: size=4; operand=sx
            else: size=8; operand=pos+4
        elif op==0xD9: size=8; operand=pos+4
        elif op in (0xB6,0xB7,0xB8,0xBA,0xBB):
            off=w&0x7FFFFF
            if off&0x400000: off-=0x800000
            operand=off
        ins.append(dict(a=pos-start,abs=pos,op=op,name=OPN.get(op,'op%02x'%op),t1=t1,t2=t2,x=sx,w=w,size=size,operand=operand))
        pos+=size
    return ins
