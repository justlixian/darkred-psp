import struct, sys
from collections import OrderedDict
class GM:
    def __init__(s, path):
        s.d = open(path,'rb').read()
        s.c = {}
        pos = 8
        while pos < len(s.d):
            n = s.d[pos:pos+4].decode('latin1'); sz = struct.unpack('<I', s.d[pos+4:pos+8])[0]
            s.c[n] = (pos+8, sz); pos += 8+sz
        s._strs = {}
        o, sz = s.c['STRG']
        cnt = s.u32(o)
        s.strings = []
        for i in range(cnt):
            p = s.u32(o+4+4*i)
            ln = s.u32(p)
            t = s.d[p+4:p+4+ln].decode('utf-8','replace')
            s.strings.append(t)
            s._strs[p] = i; s._strs[p+4] = i
    def u32(s,p): return struct.unpack('<I', s.d[p:p+4])[0]
    def i32(s,p): return struct.unpack('<i', s.d[p:p+4])[0]
    def u16(s,p): return struct.unpack('<H', s.d[p:p+2])[0]
    def i16(s,p): return struct.unpack('<h', s.d[p:p+2])[0]
    def f32(s,p): return struct.unpack('<f', s.d[p:p+4])[0]
    def sidx(s,p):  # string pointer -> index
        return s._strs.get(s.u32(p), -1)
    def sname(s,p):
        i = s.sidx(p); return s.strings[i] if i>=0 else None
    def ptrlist(s, chunk):
        o, sz = s.c[chunk]; n = s.u32(o)
        return [s.u32(o+4+4*i) for i in range(n)]
if __name__ == '__main__':
    g = GM(sys.argv[1])
    # FUNC
    o,sz = g.c['FUNC']; n = g.u32(o)
    print('FUNC count', n)
    names=[]
    for i in range(n):
        p = o+4+12*i
        names.append(g.sname(p))
    print(' '.join(names))
    o,sz = g.c['VARI']
    nv = (sz-12)//20
    print('VARI hdr', g.u32(o), g.u32(o+4), g.u32(o+8), 'entries', nv)
    out=[]
    for i in range(nv):
        p=o+12+20*i
        out.append('%s:%d'%(g.sname(p), g.i32(p+4)))
    print(' '.join(out))
