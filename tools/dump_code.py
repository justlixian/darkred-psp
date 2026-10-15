"""dump_code.py - print readable pseudo-GML for entries of a data.win.
usage: python3 dump_code.py data.win gml_Object_obj_menu_Step_0 [more entry names...]
(a debugging aid for understanding the game; not needed to build the port)"""
import sys
from gmlink import *
DATA = sys.argv[1] if len(sys.argv) > 1 and sys.argv[1].endswith('.win') else 'data.win'
g=GM(DATA)
ents=load_code(g); vars_,funcs,refs=build_links(g); rng=code_ranges(g,ents)
ITY={-1:'self',-2:'other',-3:'all',-4:'noone',-5:'global',-6:'bltn',-7:'loc',-9:'stack',-15:'arg'}
OBJ=[]
def objname(i): return OBJ[i] if 0<=i<len(OBJ) else 'obj%d'%i
def itn(x): return ITY.get(x, objname(x) if x>=0 else 'i%d'%x)
def show(name):
    s,e=rng[name]
    ins=decode_range(g,s,e)
    stack=[]; out=[]; targets=set()
    for i in ins:
        if i['name'] in ('b','bt','bf','pushenv'): targets.add(i['a']+i['operand']*4)
        if i['name']=='popenv': pass
    ind=1
    for i in ins:
        n=i['name']; a=i['a']
        if a in targets: out.append('  L%x:'%a)
        def P(x): out.append('    %04x  %s%s'%(a,'  '*(ind-1),x))
        if n in ('push','pushloc','pushglb','pushbltn'):
            t=i['t1']
            if t==5:
                vt=g.u32(i['operand'])>>24; vn=vars_[refs[i['operand']][1]]['name']; x=i['x']
                if vt==0x00 or vt==0x90:
                    idx=stack.pop(); inst=stack.pop(); stack.append(('REF(%s.%s)' if vt==0x90 else '%s.%s[%s]')%((itn(int(inst)) if inst.lstrip('-').isdigit() else inst),vn,idx) if vt==0 else 'REF(%s.%s)'%(inst,vn))
                elif vt==0x80:
                    inst=stack.pop(); stack.append('(%s).%s'%(inst,vn))
                elif vt==0x10: stack.append('REF(%s.%s)'%(itn(x),vn))
                else: stack.append('%s.%s'%(itn(x),vn))
            elif t==6: stack.append(repr(g.strings[i['operand']]))
            elif t in(0,1): stack.append('%g'%i['operand'])
            else:
                r=refs.get(i['abs']+4)
                stack.append(('<fn %s>'%funcs[r[1]]['name']) if (r and r[0]=='func') else str(i['operand']))
        elif n=='pushi': stack.append(str(i['operand']))
        elif n=='conv': pass
        elif n in ('mul','div','rem','mod','add','sub','and','or','xor','shl','shr'):
            sym={'mul':'*','div':'/','rem':'rem','mod':'%','add':'+','sub':'-','and':'&','or':'|','xor':'^','shl':'<<','shr':'>>'}[n]
            b=stack.pop(); a_=stack.pop(); stack.append('(%s %s %s)'%(a_,sym,b))
        elif n=='neg': stack.append('-%s'%stack.pop())
        elif n=='not': stack.append('!%s'%stack.pop())
        elif n=='cmp':
            k=(i['w']>>8)&0xFF
            b=stack.pop(); a_=stack.pop(); stack.append('(%s %s %s)'%(a_,{1:'<',2:'<=',3:'==',4:'!=',5:'>=',6:'>'}.get(k,'?%d'%k),b))
        elif n=='call':
            kind,idx=refs[i['operand']]; fn=funcs[idx]['name'].replace('gml_Script_','')
            argc=i['x']; args=[stack.pop() for _ in range(argc)]
            stack.append('%s(%s)'%(fn,', '.join(args)))
        elif n=='popz': P(stack.pop()+';')
        elif n=='pop':
            vt=g.u32(i['operand'])>>24; vn=vars_[refs[i['operand']][1]]['name']; x=i['x']
            if vt==0x00:
                idx=stack.pop(); inst=stack.pop(); val=stack.pop()
                inst=itn(int(inst)) if inst.lstrip('-').isdigit() else inst
                P('%s.%s[%s] = %s;'%(inst,vn,idx,val))
            elif vt==0x80:
                inst=stack.pop(); val=stack.pop(); P('(%s).%s = %s;'%(inst,vn,val))
            else:
                val=stack.pop(); P('%s.%s = %s;'%(itn(x),vn,val))
        elif n=='dup': stack.append(stack[-1])
        elif n=='b': P('goto L%x'%(a+i['operand']*4))
        elif n=='bt': P('if (%s) goto L%x'%(stack.pop(),a+i['operand']*4))
        elif n=='bf': P('if (!%s) goto L%x'%(stack.pop(),a+i['operand']*4))
        elif n=='pushenv':
            t=stack.pop()
            if t=='-9': t=stack.pop()
            P('with(%s) {  // until L%x'%(t,a+i['operand']*4)); ind+=1
        elif n=='popenv': ind=max(1,ind-1); P('}')
        elif n=='ret': P('return %s;'%stack.pop())
        elif n=='exit': P('exit;')
        elif n=='break':
            if i['x']==-2: idx=stack.pop(); arr=stack.pop(); stack.append('%s[%s]'%(arr,idx))
            elif i['x']==-3: idx=stack.pop(); arr=stack.pop(); val=stack.pop(); P('%s[%s] = %s;'%(arr,idx,val))
            elif i['x']==-5: stack.pop()
            else: P(';; break %d'%i['x'])
        else: P('?? %s'%n)
    return '\n'.join(out), stack
if __name__=='__main__':
    # object names
    from gmparse import GM as _G
    o,sz=g.c['OBJT']; n=g.u32(o)
    for k in range(n):
        p=g.u32(o+4+4*k); OBJ.append(g.sname(p))
    names = sys.argv[2:] if (len(sys.argv) > 1 and sys.argv[1].endswith('.win')) else sys.argv[1:]
    for nm in names:
        print('=====',nm)
        t,st=show(nm); print(t)
        if st: print('LEFTOVER STACK',st)
