"""Resolve runtime shader paths through the actual embedded NRO RomFS tables."""
from pathlib import Path
import argparse,re,struct,sys

INVALID=0xffffffff
class Romfs:
    def __init__(self,nro):
        if len(nro)<0x20 or nro[0x10:0x14]!=b'NRO0':raise ValueError('invalid NRO header')
        image=struct.unpack_from('<I',nro,0x18)[0]
        if image+56>len(nro) or nro[image:image+4]!=b'ASET':raise ValueError('missing bounded ASET')
        offset,size=struct.unpack_from('<QQ',nro,image+40)
        if offset<56 or size<80 or image+offset+size>len(nro):raise ValueError('invalid RomFS bounds')
        self.start=image+offset;self.data=nro[self.start:self.start+size]
        self.header=struct.unpack_from('<10Q',self.data)
        if self.header[0]!=80:raise ValueError('invalid RomFS header')
        for at,size in zip(self.header[1:9:2],self.header[2:9:2]):
            if not size or at+size>len(self.data):raise ValueError('invalid metadata bounds')
        self.dirs=self.records(self.header[3],self.header[4],False)
        self.files=self.records(self.header[7],self.header[8],True)
        if self.dirs.get(0,(None,))[-1]!='':raise ValueError('missing root directory')
    def records(self,offset,size,file):
        result={};at=0;fixed=32 if file else 24
        while at<size:
            if at+fixed>size:raise ValueError('truncated metadata')
            fields=struct.unpack_from('<IIQQII' if file else '<6I',self.data,offset+at)
            length=fields[-1]
            if at+fixed+length>size:raise ValueError('truncated name')
            name=self.data[offset+at+fixed:offset+at+fixed+length].decode('ascii')
            if '/' in name or '\0' in name:raise ValueError('invalid component')
            result[at]=fields[:-1]+(name,);at=(at+fixed+length+3)&~3
        if at!=size:raise ValueError('invalid metadata alignment')
        return result
    @staticmethod
    def hash(parent,name):
        value=parent^123456789
        for byte in name.encode('ascii'):value=(((value>>5)|(value<<27))&0xffffffff)^byte
        return value
    def lookup(self,parent,name,file):
        index=5 if file else 1;offset,size=self.header[index:index+2]
        if size%4:raise ValueError('invalid hash table')
        bucket=self.hash(parent,name)%(size//4)
        cursor=struct.unpack_from('<I',self.data,offset+4*bucket)[0];seen=set()
        records=self.files if file else self.dirs
        while cursor!=INVALID:
            if cursor in seen or cursor not in records:raise ValueError('invalid hash chain')
            seen.add(cursor);record=records[cursor]
            if record[0]==parent and record[-1]==name:return cursor,record
            cursor=record[4]
        raise ValueError('runtime path missing: '+name)
    def entry(self,path):
        parent=0;parts=path.split('/')
        for name in parts[:-1]:parent,_=self.lookup(parent,name,False)
        offset,record=self.lookup(parent,parts[-1],True)
        start=self.header[9]+record[2];size=record[3]
        if start+size>len(self.data):raise ValueError('invalid file data bounds')
        return offset,start,self.data[start:start+size]

def check(nro,shaders,stages):
    romfs=Romfs(nro)
    for stage in stages:
        _,_,actual=romfs.entry('shaders/'+stage+'.dksh')
        expected=(shaders/(stage+'.dksh')).read_bytes()
        if len(expected)<24 or expected[:4]!=b'DKSH' or actual!=expected:
            raise ValueError('runtime shader content differs: '+stage)
    return romfs

def mount_contract(source):
    main=source.index('int main(int argc, char** argv)')
    init=source.index('romfsInit()');exit=source.index('romfsExit()')
    first_renderer=source.index('frontend_renderer->open_frontend',main)
    if source.count('romfsInit()')!=1 or source.count('romfsExit()')!=1 or not main<init<exit<first_renderer:
        raise ValueError('RomFS mount must span application menu/race/reentry')
    if not re.search(r'~RomfsGuard\(\)\s*\{\s*romfsExit\(\);\s*\}',source):
        raise ValueError('application mount cleanup must be scoped')

def main():
    parser=argparse.ArgumentParser();parser.add_argument('--nro',type=Path,required=True)
    parser.add_argument('--shaders',type=Path,required=True);parser.add_argument('--source',type=Path,required=True)
    parser.add_argument('--renderer',type=Path,required=True);args=parser.parse_args()
    source=args.source.read_text(encoding='ascii');mount_contract(source)
    stages=sorted(set(re.findall(r'shader\("([a-z_]+\.(?:vert|frag))"',args.renderer.read_text(encoding='ascii'))))
    if not {'frontend_car.vert','frontend_car.frag','frontend_ui.vert','frontend_ui.frag'}<=set(stages):
        raise ValueError('missing frontend runtime shader consumers')
    raw=args.nro.read_bytes();romfs=check(raw,args.shaders,stages)
    print('PASS actual NRO runtime hash/path lookup and exact shader bytes:',', '.join(stages))
    offset,start,_=romfs.entry('shaders/frontend_car.vert.dksh')
    controls={}
    rename=bytearray(raw);rename[romfs.start+romfs.header[7]+offset+32]=ord('x');controls['renamed file with unchanged shader bytes']=rename
    broken=bytearray(raw);bucket=romfs.hash(romfs.files[offset][0],'frontend_car.vert.dksh')%(romfs.header[6]//4)
    struct.pack_into('<I',broken,romfs.start+romfs.header[5]+4*bucket,INVALID);controls['unreachable hash entry with unchanged name/blob']=broken
    corrupt=bytearray(raw);corrupt[romfs.start+start+24]^=1;controls['wrong embedded shader bytes']=corrupt
    for name,negative in controls.items():
        try:check(negative,args.shaders,stages)
        except ValueError:print('PASS negative control rejected:',name)
        else:raise ValueError('negative control was accepted: '+name)
    old=source.replace('romfsInit()','oldMount()',1)
    old='romfsInit();romfsExit();\n'+old.replace('romfsExit()','oldUnmount()',1)
    try:mount_contract(old)
    except ValueError:print('PASS negative control rejected: race-only mount before application entry')
    else:raise ValueError('race-only mount negative control accepted')
    print('PASS application lifetime RomFS mount source contract; hardware execution still required')
if __name__=='__main__':
    try:main()
    except (ValueError,OSError,struct.error) as error:print('FAIL:',error,file=sys.stderr);sys.exit(1)
