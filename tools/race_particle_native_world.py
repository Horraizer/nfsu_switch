"""Bounded original-PC particle memory fixture; retail images remain read-only."""
import struct
from unicorn import Uc,UC_ARCH_X86,UC_MODE_32
from unicorn.x86_const import UC_X86_REG_FPCW,UC_X86_REG_EIP
import unicorn.x86_const as registers

class World:
    base=0x09000000
    stack=base+0xe000
    done=base+0xff00

    def __init__(self,image,xbox=False):
        assert not xbox
        self.uc=Uc(UC_ARCH_X86,UC_MODE_32)
        self.uc.mem_map(0x400000,0x400000)
        for start,length,offset in image.sections:self.uc.mem_write(start,image.data[offset:offset+length])
        self.uc.mem_map(self.base,0x10000)
        self.uc.reg_write(UC_X86_REG_FPCW,0x007f)
        self.system,self.sources,self.presets,self.nodes,self.particles,self.node_arena,self.pool=(self.base+i*0x1000 for i in range(1,8))
        for head in (self.system,self.system+8):self.empty(head)
        self.put(0x73652c,self.pool);self.put(0x736530,self.node_arena)
        self.put(self.pool+0x2c,1000);self.put(self.node_arena+0x2c,16);self.put(0x77a920,0)
        self.put(0x6d4ac8,0x5ac449);self.put(0x6d4acc,0x5ac6a2)
        for i in range(12):self.put(self.nodes+i*0x100,self.nodes+(i+1)*0x100 if i<11 else 0)
        self.put(self.node_arena+0x10,self.nodes)

    def put(self,address,value,fmt='I'):
        self.uc.mem_write(address,struct.pack('<'+fmt,value))

    def get(self,address):
        return struct.unpack('<I',self.uc.mem_read(address,4))[0]

    def empty(self,head):
        self.put(head,head);self.put(head+4,head)

    def reset(self,entry,stop,**values):
        for name,value in values.items():self.uc.reg_write(getattr(registers,'UC_X86_REG_'+name),value)
        self.uc.emu_start(entry,stop,count=5000)
        assert self.uc.reg_read(UC_X86_REG_EIP)==stop

    def source(self,index,rate,seed=121):
        source=self.sources+index*0x100;preset=self.presets+index*0x220
        self.put(source+0x10,1);self.put(source+0x1c,preset);self.put(source+0x24,seed)
        for i in range(3):self.put(source+0x30+i*0x14,1.,'f')
        self.put(preset+0x19,2,'B');self.put(preset+0x1a,2,'B')
        self.put(preset+0x30,rate,'f');self.put(preset+0x38,2.,'f');self.put(preset+0x80,10.,'f')
        self.reset(0x402cb0,0x402cc3,EDI=self.system,ESI=source)
        return source

    def provide_particles(self,claimed):
        available=1000-claimed;assert 0<=available<=16
        for i in range(available):self.put(self.particles+i*0x40,self.particles+(i+1)*0x40 if i+1<available else 0)
        self.put(self.pool+0x10,self.particles if available else 0);self.put(self.pool+0x18,claimed)

