"""Check original16-bit velocity angles and sine against portable player helpers."""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import subprocess
import sys
sys.path.insert(0,str(Path.cwd()/'tools'))
from world_body_trace import Image,manifest
from physics256_native import Machine,bits
from handling_brake_compare import EXE_SHA


def run(root,out):
    data=(root/'speed.exe').read_bytes()
    if hashlib.sha256(data).hexdigest()!=EXE_SHA:raise ValueError('unsupported executable')
    image=Image(data);m=Machine(image)
    m.allowed=[(0x564CD0,0x564DA8),(0x564DB0,0x564EF6),(0x564B10,0x564BA6),
               (0x674898,0x67490D),(0x1001000,0x1001007)]
    output=0x1110000
    m.uc.mem_write(0x1001000,b'\xd9\x1d'+struct.pack('<I',output)+b'\xc3')
    pairs=[(x,y) for x in (-100,-8.3165,-1,-0.001,-0.000001,0,0.000001,0.001,1,8.3165,100)
           for y in (-100,-4,-1,-0.001,-0.000001,0,0.000001,0.001,1,4,100)]
    cases=[(x,y,angle) for x,y in pairs for angle in (0,1,8191,8192,16383,16384,32767,32768,49151,49152,65534,65535)]
    cases += [(8.316501835256739,4.0,angle) for angle in range(65536)]
    original=[]
    for x,y,angle in cases:
        m.run(0x564DB0,(bits(x),bits(y)))
        native_angle=m.reg('eax')&65535
        m.run(0x564B10,eax=angle);m.run(0x1001000)
        original.append([native_angle,m.get(output,'f')])
    if m.events:raise RuntimeError('unexpected replacement hook')
    binary=out/'handling251-player-angle-compare.exe'
    sources=['tools/handling_player_angle_compare.cpp','src/player_powertrain.cpp','src/vehicle.cpp','src/sha256.cpp']
    command=['g++','-std=c++17','-O2','-ffp-contract=off','-fno-fast-math','-I','include',*sources,'-o',str(binary)]
    build=subprocess.run(command,capture_output=True,text=True)
    (out/'build.log').write_text(build.stdout+build.stderr,encoding='ascii')
    if build.returncode:raise RuntimeError('portable build failed')
    inputs=''.join('%.9g %.9g %d\n'%case for case in cases)
    port=subprocess.run([str(binary)],input=inputs,capture_output=True,text=True)
    (out/'portable.csv').write_text(port.stdout,encoding='ascii')
    if port.returncode or port.stderr or len(port.stdout.splitlines())!=len(cases):raise RuntimeError('fresh portable rows missing')
    differences=[]
    for i,(expected,line) in enumerate(zip(original,port.stdout.splitlines())):
        actual=[float(v) for v in line.split(',')]
        if actual!=expected:differences.append([i,cases[i],expected,actual])
    result=dict(executable_sha256=EXE_SHA,source_head=subprocess.check_output(['git','rev-parse','HEAD']).decode('ascii').strip(),
                source_tool_sha256=hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
                sources={str(p):hashlib.sha256(p.read_bytes()).hexdigest() for p in map(Path,sources+['include/nfsu/player_angle_data.h'])},
                binary_sha256=hashlib.sha256(binary.read_bytes()).hexdigest(),cases=len(cases),differences=differences,
                native_rows_sha256=hashlib.sha256(json.dumps(original,separators=(',',':')).encode('ascii')).hexdigest(),
                x87_control='0x007F synthetic',external_hooks=[],
                boundary='Isolated authenticated564DB0/564CD0 angles and564B10 polynomial; all65536 sine words, signed/zero/small vector cases; no full tyre/body/cadence acceptance.')
    (out/'report.json').write_text(json.dumps(result,ensure_ascii=True,indent=2)+'\n',encoding='ascii')
    if differences:raise RuntimeError('angle comparison failed: see report.json')
    print('PASS: original/portable%d exact velocity-angle and sine rows'%len(cases))


def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--root',type=Path,required=True);p.add_argument('--out',type=Path,required=True)
    a=p.parse_args();root=a.root.resolve();out=a.out.resolve();relative=out.relative_to(Path.cwd())
    if not relative.parts[0].startswith('build') or not str(relative).isascii() or root in out.parents:
        p.error('fresh ASCII workspace build output outside retail required')
    out.mkdir(parents=True,exist_ok=False);before=manifest(root);(out/'before.json').write_bytes(before)
    try:run(root,out)
    finally:
        after=manifest(root);(out/'after.json').write_bytes(after)
        if before!=after:raise RuntimeError('retail manifest changed')
        print('PASS: complete retail content/metadata manifest unchanged')


if __name__=='__main__':main()
