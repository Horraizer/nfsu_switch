"""Original ordinary action ownership with declared virtual-method witnesses.

This proves scheduler order, not the hooked geometry/factory implementations.
Retail code is read-only; no native instruction is patched.
"""
import argparse,hashlib,json
from pathlib import Path
from world_body_trace import Image
from physics256_native import Machine

CONTROLLER=0x1160000
ACTIONS=[0,0x1161000,0x1162000,0x1163000]
VTABLE=0x1170000
CALLS=[0x1180000+16*i for i in range(7)]

def run(image,mask,timed,request,priority,present,ready):
    m=Machine(image);events=[];created=0
    m.allowed=[(0x44b94e,0x44b9f1),(0x44f700,0x44f741),
        (0x44a0e0,0x44a103),(0x44bc30,0x44bca6),(0x44bbd0,0x44bc25)]
    m.put(CONTROLLER,VTABLE);m.put(VTABLE+0x84,CALLS[3]);m.put(VTABLE+0x88,0x44bbd0)
    m.put(CONTROLLER+0x3f8,ACTIONS[1]);m.put(CONTROLLER+0x3f4,request)
    for i,at in enumerate(ACTIONS[1:],1):
        vt=VTABLE+0x100*i;m.put(at,vt)
        for off,op in ((0,5),(8,6),(12,4),(0x24,0),(0x20,1),(0x1c,2)):m.put(vt+off,CALLS[op])
        m.put(at+0x1f8,10 if i==1 else 1 if i==2 else 4)
        m.put(at+0x1f4,100 if i==1 else priority if i==2 else 0)
        m.put(at+4,int(timed if i==1 else False),'B')
        m.put(at+0x1e0,int(ready if i<3 else True),'B')
    def callback(op):
        def f(machine):
            nonlocal created
            at=machine.reg('ecx');sp=machine.reg('esp')
            if op==3:
                kind=machine.get(sp+8,'i');current=machine.get(CONTROLLER+0x3f8)
                events.append([op,ACTIONS.index(current),kind])
                output=machine.get(sp+4);created+=1
                result=ACTIONS[3 if kind==4 else 2] if present or kind==4 else 0
                machine.put(output,result);machine.ret(8)
            else:
                handle=ACTIONS.index(at)
                events.append([op,handle,0])
                if op<3:machine.ret(4,int(bool(mask&(1<<op))))
                else:machine.ret(4)
        return f
    for op,address in enumerate(CALLS):m.hook(address,'declared-virtual-'+str(op),callback=callback(op))
    m.run(0x44b94e,end=0x44b9f1,ebx=CONTROLLER,edi=CONTROLLER+0x2c)
    current=m.get(CONTROLLER+0x3f8);handle=ACTIONS.index(current)
    result=[handle,m.get(current+0x1f8),m.get(current+0x1f4,'i'),bool(m.get(current+4,'B')),
        m.get(current+0x10),m.get(CONTROLLER+0x3f4,'i')]
    return [mask,timed,request,priority,present,ready,result,len(events),events+[[0,0,0]]*(16-len(events))],m.events

def lit(v):
    if isinstance(v,bool):return str(v).lower()
    if isinstance(v,list):return '{'+','.join(map(lit,v))+'}'
    return str(v)

def main():
    ap=argparse.ArgumentParser(description=__doc__)
    for k in ('root','output','evidence'):ap.add_argument('--'+k,type=Path,required=True)
    args=ap.parse_args();data=(args.root/'speed.exe').read_bytes();sha=hashlib.sha256(data).hexdigest()
    if sha!='c0fad450912952f53809a54690d235aa8b4e25b9f46650693c7cbee1b4c074b7':raise ValueError('unsupported executable')
    image=Image(data);rows=[];hooks=[]
    cases=[(mask,timed,20,p,True,True) for mask in (0,1,2,4,7) for timed in (False,True) for p in (50,100,200)]
    cases += [(0,False,10,50,True,True),(0,False,10,50,False,True),(0,False,20,50,False,True),
        (0,False,20,200,True,False),(0,False,20,50,True,False),(4,False,10,50,True,False)]
    for spec in cases:
        row,events=run(image,*spec);rows.append(row);hooks+=events
    args.output.write_text('// Original44B94E ownership; virtual methods are declared witnesses.\nconst OwnerCase owner_cases[]={\n'+
        '\n'.join(lit(r)+',' for r in rows)+'\n};\n',encoding='ascii')
    args.evidence.write_text(json.dumps(dict(executable_sha256=sha,cases=rows,
        virtual_witness_hooks=hooks,scope='Original scheduler and dispatch only; supplied predicates/factory/update/destructor'),indent=2)+'\n',encoding='ascii')
    print('Original ownership:',len(rows),'cases;',len(hooks),'declared virtual witness calls')
if __name__=='__main__':main()
