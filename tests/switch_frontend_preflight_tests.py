"""Validate staged GPU diagnostic boundaries and compiled shader interfaces on PC."""
from pathlib import Path
import argparse,re,struct

def check(root,shaders,source=None):
    if source is None:source=(root/'switch/deko_renderer.cpp').read_text()
    begin=source.rindex('#if defined(NFSU_FRONTEND_PREFLIGHT)',0,source.index('if(diagnose && face==0 && !emitted)'))
    end=source.index('#endif',begin);block=source[begin:end]
    labels=['preflight procedural constant','preflight actual mesh constant','preflight actual mesh texture0']
    if not all('checkpoint("'+label+'")' in block for label in labels):raise ValueError('staged fence missing')
    if block.count('clear_probe();')!=3:raise ValueError('diagnostic pixels/depth not cleared before next real draw')
    if 'std::memcmp' not in block or 'instance%256' not in block:raise ValueError('address/copy contract missing')
    if 'cmd.bindVtxAttribState(std::array<DkVtxAttribState,0>{});' not in block or 'cmd.bindVtxBufferState(std::array<DkVtxBufferState,0>{});' not in block:raise ValueError('procedural vertex fetch not disabled by SDK defaults')
    if 'previous=nullptr;' not in block:raise ValueError('real draw shader state not restored')
    for name in ['frontend_probe.vert','frontend_probe.frag','frontend_probe_textured.frag']:
        src=(root/'switch/diagnostic_shaders'/(name+'.glsl')).read_text()
        blob=(shaders/(name+'.dksh')).read_bytes();h=struct.unpack_from('<6I',blob)
        if h[0]!=0x48534b44 or h[2]+h[3]!=len(blob):raise ValueError('invalid diagnostic DKSH')
        program=struct.unpack_from('<6I',blob,h[4])
        if program[0]!=(0 if name.endswith('.vert') else 1) or program[5]>2048:raise ValueError('stage/scratch mismatch')
        tgsi=(shaders/(name+'.tgsi')).read_text()
        if 'CONST[' in tgsi or re.search(r'\bDD[XY]\b',tgsi):raise ValueError('probe unexpectedly uses UBO/derivatives')
        if name=='frontend_probe.frag' and ('DCL IN[' in tgsi or 'SAMP[' in tgsi):raise ValueError('constant probe consumes resources')
        if name.endswith('.vert') and ('DCL IN[' in tgsi or 'gl_VertexID' not in src):raise ValueError('procedural vertex consumes buffers')
        if name=='frontend_probe_textured.frag' and set(re.findall(r'DCL SAMP\[(\d+)\]',tgsi))!={'0'}:raise ValueError('texture probe sampler interface changed')
    print('PASS compiled procedural/constant/texture0 interfaces and staged source fences/clears; GPU execution is hardware-only')
def main():
    p=argparse.ArgumentParser();p.add_argument('--root',type=Path,required=True);p.add_argument('--shaders',type=Path,required=True);a=p.parse_args();check(a.root,a.shaders)
    source=(a.root/'switch/deko_renderer.cpp').read_text()
    try:check(a.root,a.shaders,source.replace('clear_probe();','',1))
    except ValueError:print('PASS negative control: missing diagnostic clear rejected')
    else:raise ValueError('negative control accepted')
if __name__=='__main__':main()
