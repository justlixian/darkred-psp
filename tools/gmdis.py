import struct
from gmparse import GM
OPN = {0x07:'conv',0x08:'mul',0x09:'div',0x0A:'rem',0x0B:'mod',0x0C:'add',0x0D:'sub',0x0E:'and',0x0F:'or',0x10:'xor',
 0x11:'neg',0x12:'not',0x13:'shl',0x14:'shr',0x15:'cmp',0x45:'pop',0x84:'pushi',0x86:'dup',0x9C:'ret',0x9D:'exit',0x9E:'popz',
 0xB6:'b',0xB7:'bt',0xB8:'bf',0xBA:'pushenv',0xBB:'popenv',0xC0:'push',0xC1:'pushloc',0xC2:'pushglb',0xC3:'pushbltn',0xD9:'call',0x99:'callv',0xFF:'break'}
TN = {0:'d',1:'f',2:'i',3:'l',4:'b',5:'v',6:'s',0xF:'e',9:'u',7:'inst'}
CMP = {1:'LT',2:'LTE',3:'EQ',4:'NEQ',5:'GTE',6:'GT'}
def tsize(t):
    return {0:8,1:4,2:4,3:8,4:4,5:4,6:4,0xF:0,9:0}.get(t, None)

class Code:
    pass

def load_code(g):
    o, sz = g.c['CODE']
    n = g.u32(o)
    ents = []
    for i in range(n):
        p = g.u32(o+4+4*i)
        e = Code()
        e.name = g.sname(p)
        e.length = g.u32(p+4)
        e.locals = g.u16(p+8)
        e.args = g.u16(p+10)
        rel = g.i32(p+12)
        e.addr = p+12+rel
        e.offset = g.u32(p+16)
        ents.append(e)
    return ents

def decode(g, e):
    ins=[]; pos=e.addr; end=e.addr+e.length
    while pos<end:
        w = g.u32(pos)
        op = w>>24
        t1=(w>>16)&0xF; t2=(w>>20)&0xF
        extra = w&0xFFFF
        if extra>=0x8000: sx = extra-0x10000
        else: sx = extra
        name = OPN.get(op,'op%02x'%op)
        size=4; operand=None
        if op in (0xC0,0xC1,0xC2,0xC3):
            ts=tsize(t1)
            if ts is None: raise Exception('bad push type %x at %x in %s'%(t1,pos,e.name))
            size=4+ts
            if t1==0: operand=struct.unpack('<d',g.d[pos+4:pos+12])[0]
            elif t1==1: operand=struct.unpack('<f',g.d[pos+4:pos+8])[0]
            elif t1==2: operand=g.i32(pos+4)
            elif t1==3: operand=struct.unpack('<q',g.d[pos+4:pos+12])[0]
            elif t1==4: operand=g.i32(pos+4)
            elif t1==5: operand=('ref',pos+4)
            elif t1==6: operand=('str',g.u32(pos+4))
            elif t1==0xF: operand=sx
        elif op==0x84: operand=sx
        elif op==0x45:
            if t1==5 or True:
                # pop: dest var operand (4 bytes) unless t1 is Int16 (popswap)
                if t1==0xF: size=4; operand=sx
                else: size=8; operand=('ref',pos+4)
        elif op==0xD9: size=8; operand=('func',pos+4)
        elif op in (0xB6,0xB7,0xB8,0xBA,0xBB):
            off=w&0x7FFFFF
            if off&0x400000: off-=0x800000
            operand=('jmp',off, (w>>23)&1)
        ins.append(dict(addr=pos-e.addr,op=op,name=name,t1=t1,t2=t2,x=sx,w=w,size=size,operand=operand))
        pos+=size
    return ins

if __name__=='__main__':
    import sys
    g=GM('/home/claude/darkred/data.win')
    ents=load_code(g)
    print(len(ents),'code entries')
    from collections import Counter
    cnt=Counter(); bad=0
    for e in ents:
        try:
            ins=decode(g,e)
            for i in ins: cnt[i['name']]+=1
            last=ins[-1]['name']
            tot=sum(i['size'] for i in ins)
            if tot!=e.length or last not in ('ret','exit'): print('MISMATCH',e.name,tot,e.length,last)
        except Exception as ex:
            bad+=1; print('ERR',e.name,ex)
    print(cnt); print('bad',bad)
    for e in ents[:40]: print(e.name,e.length,e.locals,e.args,hex(e.offset))
