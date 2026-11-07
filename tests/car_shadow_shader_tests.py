"""Evaluate the actual UAM TGSI shadow vertex program for all terrain taps.
This is a bounded compiler regression, not Maxwell hardware execution.
Unknown instructions and uninitialized reads fail rather than compare empty output.
"""
import argparse,math,re,struct
from pathlib import Path


def f32(x):
    return struct.unpack('<f',struct.pack('<f',x))[0]


def run(text,vertex,heights):
    registers={('IN',0):[0]*4,('IN',1):[1]*4,('IN',2):[0]*4,
               ('SV',0):[vertex]*4}
    constants={1:[[0,0,0,0],[1,0,0,0],[0,1,0,0],[0,0,1,0],[1,1,.1,100],[0]*4,[0]*4],
               2:[[0]*4,[0]*4,[0]*4,[0,-10,0,1]],
               3:[heights[:4],[0]*4,heights[4:8],heights[8:12],heights[12:]]}
    instructions=[]
    for line in text.splitlines():
        imm=re.fullmatch(r'IMM\[(\d+)\] (?:UINT32|INT32|FLT32) \{([^}]+)\}',line.strip())
        if imm:registers[('IMM',int(imm[1]))]=[float(x) if '.' in x else int(x) for x in imm[2].split(',')]
        instruction=re.fullmatch(r'\s*\d+:\s*(\w+)(?:\s+(.*))?',line)
        if instruction:instructions.append((instruction[1],instruction[2] or ''))
    if not instructions or instructions[-1][0]!='END':raise ValueError('missing complete TGSI program')
    address=0

    def read(operand):
        negative=operand.startswith('-');operand=operand.lstrip('-')
        c=re.fullmatch(r'CONST\[(\d+)\]\[(\d+|ADDR\[0\]\.x(?:\+\d+)?)\](?:\.([xyzw]+))?',operand)
        if c:
            index=int(c[2]) if c[2].isdigit() else address+int(c[2].split('+')[1] if '+' in c[2] else 0)
            values=constants[int(c[1])][index];swizzle=c[3] or 'xyzw'
        else:
            r=re.fullmatch(r'(IN|SV|IMM|TEMP|OUT)\[(\d+)\](?:\.([xyzw]+))?',operand)
            if not r:raise ValueError('unsupported operand: '+operand)
            values=registers[(r[1],int(r[2]))];swizzle=r[3] or 'xyzw'
        swizzle=(swizzle*4)[:4]
        values=[values['xyzw'.index(x)] for x in swizzle]
        if any(x is None for x in values):raise ValueError('uninitialized TGSI operand: '+operand)
        return [-x for x in values] if negative else values

    stack=[];active=True
    for op,args in instructions:
        if op=='UIF':
            condition=bool(read(args)[0]) if active else False
            stack.append((active,condition));active=active and condition;continue
        if op=='ELSE':
            parent,condition=stack[-1];active=parent and not condition;continue
        if op=='ENDIF':active=stack.pop()[0];continue
        if not active:continue
        if op=='END':break
        fields=args.split(', ');destination=fields[0];values=[read(x) for x in fields[1:]]
        result=[]
        if op in ('MOV','UARL'):result=values[0]
        elif op=='DP3':result=[f32(sum(values[0][k]*values[1][k] for k in range(3)))]*4
        else:
            for k in range(4):
                a=values[0][k];b=values[1][k] if len(values)>1 else 0
                if op=='ADD':v=f32(a+b)
                elif op=='MUL':v=f32(a*b)
                elif op=='MAD':v=f32(a*b+values[2][k])
                elif op=='RCP':v=f32(1/a)
                elif op=='ISLT':v=-1 if int(a)<int(b) else 0
                elif op=='USEQ':v=-1 if int(a)==int(b) else 0
                elif op=='AND':v=int(a)&int(b)
                elif op=='UADD':v=(int(a)+int(b))&0xffffffff
                elif op=='UMUL':v=(int(a)*int(b))&0xffffffff
                elif op=='USHR':v=(int(a)&0xffffffff)>>int(b)
                else:raise ValueError('unsupported TGSI instruction: '+op)
                result.append(v)
        if destination=='ADDR[0].x':address=int(result[0]);continue
        d=re.fullmatch(r'(TEMP|OUT)\[(\d+)\](?:\.([xyzw]+))?',destination)
        if not d:raise ValueError('unsupported destination: '+destination)
        key=(d[1],int(d[2]));registers.setdefault(key,[None]*4)
        for component in d[3] or 'xyzw':
            k='xyzw'.index(component);registers[key][k]=result[k]
    if stack:raise ValueError('unterminated TGSI branch')
    position=registers[('OUT',0)]
    if any(x is None or not math.isfinite(x) for x in position):raise ValueError('invalid output position')
    return position[1]


def verify(text):
    cases=[list(range(1001,1017)),
           [f32(56.1-.1*(i%4)+(.08 if i in (5,6,9,10) else 0)+.04) for i in range(16)],
           [f32(-12.5+.13*(i%4)-.07*(i//4)) for i in range(16)]]
    errors=[]
    for case,heights in enumerate(cases):
        for vertex,expected in enumerate(heights):
            actual=run(text,vertex,heights)
            if actual!=expected:errors.append((case,vertex,expected,actual))
    if errors:raise ValueError('terrain tap component differs: '+repr(errors))
    return len(cases)*16


def main():
    ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('--tgsi',type=Path,required=True)
    text=ap.parse_args().tgsi.read_text('ascii');count=verify(text)
    # The original failure reads X for every column. Reject that behavior in
    # the same executed compiler IR, not a separate formula or source matcher.
    bad=re.sub(r'(MOV TEMP\[\d+\]\.x, TEMP\[\d+\]\.)[yzw]{4}',r'\1xxxx',text)
    if bad==text:raise ValueError('negative control stale: no column selection')
    try:verify(bad)
    except ValueError as error:
        if not str(error).startswith('terrain tap component differs:'):raise
        print('PASS negative control: collapsed row heights rejected')
    else:raise ValueError('collapsed row heights accepted')
    print('PASS actual UAM TGSI projected heights for',count,'terrain taps; not GPU execution')


if __name__=='__main__':
    try:main()
    except (ValueError,KeyError,IndexError) as error:
        print('FAIL shadow shader:',error);raise SystemExit(1)
