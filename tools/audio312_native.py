"""Issue 312 bounded x86 witnesses. No original process, OS calls, or PCM output.

Machine executes allowlisted original PE/relocated bank instructions. Bank
Player and deletion callbacks are observation stops, not a simulated mixer.
"""
import struct
from unicorn import Uc, UC_ARCH_X86, UC_MODE_32, UC_HOOK_CODE
from unicorn import x86_const as x


def u32(data, at=0):
    return struct.unpack_from('<I', data, at)[0]


class Machine:
    def __init__(self, image, allowed):
        self.uc = Uc(UC_ARCH_X86, UC_MODE_32)
        # Explicit witness environment: normal x87 round-to-nearest control.
        # This does not establish every live thread's floating-point mode.
        self.uc.reg_write(x.UC_X86_REG_FPCW, 0x037F)
        self.uc.mem_map(0x400000, 0x400000)
        self.uc.mem_map(0x1000000, 0x200000)
        for start, size, raw in image.sections:
            self.uc.mem_write(start, image.data[raw:raw+size])
        self.allowed = list(allowed)
        self.stack, self.stop = 0x11F0000, 0x11FF000
        self.callbacks, self.visited = {}, set()
        self.stopped = False
        self.uc.hook_add(UC_HOOK_CODE, self.hook)

    def wi(self, at, value):
        self.uc.mem_write(at, struct.pack('<I', value & 0xFFFFFFFF))

    def wf(self, at, value):
        self.uc.mem_write(at, struct.pack('<f', value))

    def ri(self, at):
        return u32(self.uc.mem_read(at, 4))

    def rf(self, at):
        return struct.unpack('<f', self.uc.mem_read(at, 4))[0]

    def cstring(self, at):
        return bytes(self.uc.mem_read(at, 128)).split(b'\0', 1)[0].decode('ascii')

    def hook(self, cpu, address, size, _):
        if address == self.stop:
            self.finish()
            return
        if address in self.callbacks:
            self.callbacks[address](self)
            return
        if not any(a <= address and address+size <= b for a, b in self.allowed):
            raise RuntimeError('execution outside allowlist: %08X' % address)
        self.visited.add(address)

    def finish(self):
        self.stopped = True
        self.uc.emu_stop()

    def return_stub(self, result=0):
        sp = self.uc.reg_read(x.UC_X86_REG_ESP)
        self.uc.reg_write(x.UC_X86_REG_EAX, result)
        self.uc.reg_write(x.UC_X86_REG_EIP, self.ri(sp))
        self.uc.reg_write(x.UC_X86_REG_ESP, sp+4)

    def run(self, start, args=(), regs=None):
        self.stopped = False
        for i, value in enumerate((self.stop,)+tuple(args)):
            self.wi(self.stack+4*i, value)
        self.uc.reg_write(x.UC_X86_REG_ESP, self.stack)
        for reg, value in (regs or {}).items():
            self.uc.reg_write(reg, value)
        self.uc.emu_start(start, 0, count=200000)
        if not self.stopped:
            raise RuntimeError('instruction limit reached without expected stop')


def stock_bank(image, car_index=18, upgrade=0, event_type=12):
    if car_index != 18 or upgrade != 0 or event_type not in (12, 13):
        raise ValueError('witness is restricted to stock 240SX engine/exhaust')
    m = Machine(image, [(0x520320, 0x5203D3), (0x51E0F4, 0x51E17C),
                        (0x522020, 0x522049), (0x521F80, 0x522011),
                        (0x521C80, 0x521E0E)])
    m.run(0x520320, (0,), {x.UC_X86_REG_EAX: car_index})
    index = m.uc.reg_read(x.UC_X86_REG_EAX)
    if index >= 20:
        raise ValueError('car type out of supported table')
    group = m.ri(0x6F8100+index*4)
    owner, manager = 0x1100000, 0x1101000
    # Execute the event producer fragment: the stock upgrade is a named input,
    # not a claim about any live profile or intro-event tuned setup.
    end = 0x51E14D if event_type == 12 else 0x51E17C
    m.callbacks[end] = lambda cpu: cpu.finish()
    m.wi(m.stack+0x10, upgrade)
    m.run(0x51E0F4, regs={x.UC_X86_REG_EBP: owner,
                         x.UC_X86_REG_EAX: group, x.UC_X86_REG_EBX: 0})
    record_at = owner+0x1E8+(0 if event_type == 12 else 0x1C)
    record = [m.ri(record_at+4*i) for i in range(3)]
    m.wi(0x6F1DD8, 1)
    m.callbacks[0x521E0E] = lambda cpu: cpu.finish()
    m.run(0x522020, (record[1], 0, 1, 9),
          {x.UC_X86_REG_ESI: manager, x.UC_X86_REG_ECX: record[0],
           x.UC_X86_REG_EDX: record[2]})
    return dict(car_type=car_index, sound_index=index, group=group,
                upgrade=upgrade, event_record_type_upgrade_group=record,
                event_type=event_type, directory=m.cstring(m.ri(manager+0x30)),
                bank=m.cstring(m.ri(manager+0x2C)), instructions=len(m.visited))


def engine_parameters(image, rpm, load, gain=32767, crossfade=0, group=2, game_producer=False, pan=32768):
    m = Machine(image, [(0x5236D0, 0x523888), (0x674898, 0x674910)])
    if game_producer:
        m.uc.reg_write(x.UC_X86_REG_FPCW, 0x007F)
    owner, event, tune = 0x1100000, 0x1103000, 0x1104000
    m.wi(owner+0x1760, event)
    m.wi(owner+0x1F0C, tune)
    m.wi(owner+0x238, gain)
    m.wi(owner+0x23C, gain)
    m.wf(owner+0x1F1C, rpm)
    m.wf(owner+0x1F8C, load)
    m.wi(owner+0x6C, pan)
    m.wi(owner+0x2AC, crossfade)
    m.wi(owner+0x1FD4, 16383)
    # Event handle zero deliberately excludes the OS/event dispatch in 64BC30.
    m.wi(event+8, group)
    m.run(0x5236D0, regs={x.UC_X86_REG_ECX: owner})
    return [m.ri(event+4+4*i) for i in range(19)]


class BankGraph:
    def __init__(self, image, bank):
        self.base = 0x1000000
        self.bank = bank
        module = u32(bank, 0x1C)
        self.start, self.template = u32(bank, module+40), u32(bank, module+44)
        self.m = Machine(image, [(self.base+self.start, self.base+self.template),
                                (0x64C650, 0x64DBD0), (0x6527A0, 0x6527F3)])
        self.m.uc.mem_write(self.base, bank)
        # Retail 64DF82 relocates callback displacement fields; 64DFC0 adds
        # the bank base to pointer fields. Only in-memory copies are modified.
        for table, mode in ((u32(bank, 0x30), 'call'), (u32(bank, 0x34), 'pointer')):
            for i in range(u32(bank, table)):
                off = u32(bank, table+4+4*i)
                value = self.m.ri(self.base+off)
                if mode == 'call':
                    if not 0 <= value < 40 or bank[off-1] != 0xE8:
                        raise ValueError('invalid callback relocation')
                    value = u32(image.read(0x6E9AE8+4*value, 4))-(self.base+off+4)
                else:
                    value += self.base
                self.m.wi(self.base+off, value)
        self.players, self.deletions = [], []
        self.m.callbacks[0x64D860] = self.player
        self.m.callbacks[0x64C690] = self.deletion
        self.m.wf(0x713CA0, 1000/60)
        # Keep the retail template's zero stop request. This is NOT a start
        # trigger: forcing +0x28 to one asks callback 64C690 to delete it.

    def player(self, m):
        node = m.ri(m.uc.reg_read(x.UC_X86_REG_ESP)+4)
        table = m.ri(node+4)
        index = max(0, min(m.ri(node+0x14), m.ri(table)-1))
        self.players.append(dict(node=hex(node-self.base),
            sample=m.ri(table+8+12*index), state=m.ri(node+0x18),
            pitch_q12=m.ri(node+0x24), gain_q15=m.ri(node+0x30),
            pan=m.ri(node+0x3C)))
        # ABK ignores each Player return; observe intent before backend.
        m.return_stub()

    def deletion(self, m):
        node = m.ri(m.uc.reg_read(x.UC_X86_REG_ESP)+4)
        self.deletions.append(m.ri(node+0xC))
        if self.deletions[-1]:
            raise RuntimeError('bank requested deletion; graph fixture invalid')
        m.return_stub()

    def tick(self, params):
        if len(params) != 19:
            raise ValueError('expected 19 engine event parameters')
        for i, value in enumerate(params):
            self.m.wi(self.base+self.template+0x40+i*4, value)
        self.players.clear()
        self.deletions.clear()
        self.m.run(self.base+self.start, (self.base+self.template+0x18,))
        if len(self.players) != 4:
            raise RuntimeError('expected all four Player observations')
        return sorted(self.players, key=lambda row: row['sample'])


def contact_age(image, age):
    # Execute both independently: physics record cleanup and audio expiry.
    results = {}
    for label, start, keep, expire, context_reg in (
        ('physics', 0x420AB0, 0x420B20, 0x420AD7, x.UC_X86_REG_ECX),
        ('audio', 0x530A90, 0x530AC5, 0x530ABC, x.UC_X86_REG_EDI)):
        m = Machine(image, [(start, max(keep, expire))])
        record, audio = 0x1100000, 0x1101000
        m.wi(0x73AD34, 5000)
        m.wi(record+0xC, 5000-age)
        m.wi(audio+0x21C, record)
        result = []
        def end(value):
            def stop(cpu):
                result.append(value)
                cpu.finish()
            return stop
        m.callbacks[keep] = end(False)
        m.callbacks[expire] = end(True)
        m.run(start, regs={context_reg: record if label == 'physics' else audio})
        results[label+'_expired'] = result[0]
    results['native_clock_units'] = age
    return results


def scrape_fade(image):
    m = Machine(image, [(0x5307E0, 0x530838), (0x532640, 0x5326EB)])
    record = 0x1100000
    m.uc.mem_write(record+0x350, b'\x01')
    m.run(0x5307E0, regs={x.UC_X86_REG_EAX: record})
    return dict(expired=bool(m.uc.mem_read(record+0x351, 1)[0]),
                duration_seconds=m.rf(record+0x254),
                from_value=m.rf(record+0x26C), to_value=m.rf(record+0x284))

def collision_intensities(image, impulse, subtype, slide):
    m = Machine(image, [(0x530340, 0x530570), (0x530F80, 0x53102A),
                        (0x531030, 0x53107E), (0x532840, 0x5328AC),
                        (0x674898, 0x67490D)])
    m.run(0x530340, (0,))
    audio, record = 0x1100000, 0x1101000
    m.wi(audio+0x21C, record)
    m.wi(audio+0x2F8, 0x7367A4)
    m.wi(audio+0x2FC, 0x7367C0)
    m.wi(audio+0x340, subtype)
    m.wf(record+0x60, impulse)
    m.wf(record+0x40, slide)
    m.run(0x530F80, regs={x.UC_X86_REG_ECX: audio})
    m.run(0x531030, regs={x.UC_X86_REG_ECX: audio})
    return dict(impulse=impulse, subtype=subtype, slide=slide,
                native_impact_index=m.ri(audio+0x248),
                native_scrape_index=m.ri(audio+0x24C))


def spatial_gain(image, gain, attenuation, category):
    m = Machine(image, [(0x530740, 0x5307E0)])
    audio, value, manager, listener = 0x1100000, 0x1101000, 0x1102000, 0x1103000
    m.wi(audio+0x50, attenuation)
    m.wi(value, gain)
    m.wi(0x7356A8, manager)
    m.wi(0x7356AC, listener)
    m.wi(0x7356B0, 0)
    m.wi(listener+0x858, category)
    m.run(0x530740, regs={x.UC_X86_REG_EAX: audio, x.UC_X86_REG_ESI: value})
    return dict(input_gain=gain, attenuation=attenuation, category=category,
                native_gain=m.ri(value))


def random_sequence(image):
    # Synthetic six-word state, not the original game's unrecovered seed.
    m = Machine(image, [(0x6511B0, 0x651255)])
    seed = [1, 2, 3, 4, 5, 6]
    for k, value in enumerate(seed):
        m.wi(0x713FCC+4*k, value)
    values = []
    for _ in range(16):
        m.run(0x6511B0)
        values.append(m.uc.reg_read(x.UC_X86_REG_EAX))
    return dict(synthetic_seed=seed, values=values,
                final_state=[m.ri(0x713FCC+4*k) for k in range(6)])
