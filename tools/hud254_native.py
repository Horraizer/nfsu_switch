"""Bounded native lap/time consumer oracle; loaded only by hud254_trace.

Unicorn executes the pinned PE bytes, never the game process. UI binding calls
are captured stubs, not evidence of runtime FEng rendering or visibility.
"""
import struct


class Machine:
    def __init__(self, image):
        from unicorn import Uc, UC_ARCH_X86, UC_MODE_32
        from unicorn.x86_const import UC_X86_REG_ESP
        self.uc = Uc(UC_ARCH_X86, UC_MODE_32)
        self.uc.mem_map(0x400000, 0x400000)
        self.uc.mem_map(0x1000000, 0x100000)
        for start, size, raw in image.sections:
            self.uc.mem_write(start, image.data[raw:raw+size])
        self.stack = 0x10F0000
        self.stop = 0x10FF000
        self.uc.reg_write(UC_X86_REG_ESP, self.stack)

    def write(self, at, value):
        self.uc.mem_write(at, struct.pack('<I', value & 0xFFFFFFFF))

    def integer(self, at):
        return struct.unpack('<I', self.uc.mem_read(at, 4))[0]

    def text(self, at):
        return bytes(self.uc.mem_read(at, 256)).split(b'\0', 1)[0].decode('ascii')

    def run(self, at, args=(), registers=None):
        from unicorn.x86_const import UC_X86_REG_ESP, UC_X86_REG_EIP
        for offset, value in enumerate((self.stop,) + tuple(args)):
            self.write(self.stack + 4 * offset, value)
        self.uc.reg_write(UC_X86_REG_ESP, self.stack)
        for register, value in (registers or {}).items():
            self.uc.reg_write(register, value)
        self.uc.emu_start(at, self.stop, count=200000)
        if self.uc.reg_read(UC_X86_REG_EIP) != self.stop:
            raise RuntimeError('native execution limit reached without return')


def format_native(image, ticks, locale=1):
    from unicorn.x86_const import UC_X86_REG_EAX, UC_X86_REG_EDI
    machine = Machine(image)
    machine.write(0x6EE6F0, locale)
    machine.write(0x1000100, ticks)
    machine.run(0x586580, (0x1000200,), {UC_X86_REG_EAX: 4, UC_X86_REG_EDI: 0x1000100})
    if machine.integer(0x1000100) != (ticks & 0xFFFFFFFF):
        raise RuntimeError('formatter did not restore its input')
    return machine.text(0x1000200)


def lap_native(image, *, now, start, lap=0, laps=2, best=0x7FFFFFFF, locale=5,
               pulse=False, completed=0, held_since=0, held_ticks=0, enabled=True):
    from unicorn import UC_HOOK_CODE
    from unicorn.x86_const import UC_X86_REG_EAX, UC_X86_REG_EBX, UC_X86_REG_EIP, UC_X86_REG_ESP, UC_X86_REG_ESI
    machine = Machine(image)
    component, argument, view, record, status = 0x1001000, 0x1002000, 0x1003000, 0x1004000, 0x1005000
    machine.write(argument + 4, view)
    machine.write(view + 0x1C, record)
    machine.write(0x73619C, status)
    machine.write(0x6FBF14, 1)  # Use supplied view record; bypass fallback car lookup.
    machine.write(0x78A30C, 1)
    machine.write(0x78A31C, laps)
    machine.write(0x6EE6F0, locale)
    machine.write(0x73AD34, now)
    machine.write(status + 0x530, laps)
    machine.uc.mem_write(status + 0x53E, b'\x00\x04')
    machine.uc.mem_write(record + 9, bytes((lap & 255,)))
    machine.uc.mem_write(record + 0xD, bytes((int(pulse),)))
    machine.write(record + 0x24, start)
    machine.write(record + 0x60 + 4 * 4, best)
    machine.write(record + 0x38 + 4 * 4, completed)
    machine.write(component + 8, 0x1006000)
    machine.write(component + 0x10, 0x400)
    machine.write(component + 0x18, 0x400 if enabled else 0)
    machine.write(component + 0x44, held_since)
    machine.write(component + 0x48, held_ticks)
    objects = {0x1007000: 'lap_number', 0x1007100: 'lap_total',
               0x1007200: 'best', 0x1007300: 'current'}
    for offset, object_at in zip((0x20, 0x24, 0x28, 0x2C), objects):
        machine.write(component + offset, object_at)
    calls = []

    def hook(uc, at, size, _):
        if at not in (0x4F68A0, 0x4F66F0, 0x4F6970):
            return
        esp = uc.reg_read(UC_X86_REG_ESP)
        if at == 0x4F68A0:
            label = objects.get(uc.reg_read(UC_X86_REG_EAX))
            if label is None:
                raise RuntimeError('unexpected FEng target')
            fmt = machine.text(machine.integer(esp + 4))
            value = machine.integer(esp + 8)
            if fmt == '%s':
                text = machine.text(value)
            elif fmt in ('%2d', '/%2d'):
                signed = struct.unpack('<i', struct.pack('<I', value))[0]
                text = fmt % signed
            else:
                raise RuntimeError('unexpected text format: ' + fmt)
            calls.append(dict(target=label, format=fmt, text=text))
        elif at == 0x4F66F0:
            calls.append(dict(target=objects[uc.reg_read(UC_X86_REG_ESI)],
                              localized_hash='%08X' % uc.reg_read(UC_X86_REG_EAX)))
        else:
            calls.append(dict(unhide=objects[machine.integer(esp + 4)]))
        uc.reg_write(UC_X86_REG_EIP, machine.integer(esp))
        uc.reg_write(UC_X86_REG_ESP, esp + 4)

    machine.uc.hook_add(UC_HOOK_CODE, hook)
    machine.run(0x4A1820, (argument,), {UC_X86_REG_EBX: component})
    return dict(calls=calls, held_since=machine.integer(component+0x44),
                held_ticks=machine.integer(component+0x48))


def mask_native(image, stage, permission, view_state=3, view_mode=0, finished=False):
    """Execute actual mask body with explicitly stubbed non-lap predicates."""
    from unicorn import UC_HOOK_CODE
    from unicorn.x86_const import UC_X86_REG_EAX, UC_X86_REG_EDX, UC_X86_REG_EIP, UC_X86_REG_ESP
    machine = Machine(image)
    view, scene, status = 0x1001000, 0x1002000, 0x1003000
    machine.write(0x7361F0, scene)
    machine.write(0x7796A8, 0)
    machine.write(scene, 4)
    machine.write(0x73619C, status)
    machine.write(status, stage)
    machine.write(0x7361C4, 1)
    machine.write(0x78A30C, 1)
    machine.write(0x78A31C, 2)
    machine.write(0x78A320, 1)
    machine.write(0x78A324, 3)
    machine.write(0x78A350, 0)
    machine.write(0x78A368, 0)
    machine.write(view + 0x30, permission)
    machine.write(view + 4, 0x1004000)
    machine.write(view + 0x60, view_mode)
    machine.write(view + 0x58, int(finished))
    machine.uc.mem_write(view + 0x20, bytes((view_state,)))
    stubs = (0x42AC10, 0x42AC30, 0x421A50, 0x41F600, 0x432A10,
             0x421A30, 0x42AB30, 0x42EBB0, 0x43A2E0)
    calls = []

    def hook(uc, at, size, _):
        if at not in stubs:
            return
        calls.append('%08X' % at)
        esp = uc.reg_read(UC_X86_REG_ESP)
        uc.reg_write(UC_X86_REG_EAX, 0)
        uc.reg_write(UC_X86_REG_EDX, 0)
        uc.reg_write(UC_X86_REG_EIP, machine.integer(esp))
        uc.reg_write(UC_X86_REG_ESP, esp + 4)

    machine.uc.hook_add(UC_HOOK_CODE, hook)
    machine.run(0x42E620, (view,))
    low, high = machine.uc.reg_read(UC_X86_REG_EAX), machine.uc.reg_read(UC_X86_REG_EDX)
    return dict(low='%08X' % low, high='%08X' % high,
                lap_enabled=bool(low & 0x400), stub_calls=calls)


def resource_native(image, index):
    from unicorn.x86_const import UC_X86_REG_ECX
    machine = Machine(image)
    obj, context, indirect, table = 0x1001000, 0x1002000, 0x1003000, 0x1004000
    machine.write(context + 8, indirect)
    machine.write(indirect, table)
    machine.write(obj + 0x18, 2)
    machine.uc.mem_write(obj + 0x22, struct.pack('<H', index))
    if index != 0xFFFF:
        machine.write(table + index*24 + 0x10, 0x11220000 + index)
        machine.write(table + index*24 + 0x14, 0x33440000 + index)
    machine.run(0x5003C0, (obj,), {UC_X86_REG_ECX: context})
    return dict(index=index, resolved_hash=machine.integer(obj+0x24),
                resolved_handle=machine.integer(obj+0x28))


def measure(image):
    ticks = (0, 1, 39, 40, 3999, 4000, 239999, 240000, 3599999,
             3600000, 14399999, 14400000, 0x7FFFFFFF, -40, -240000)
    formats = [dict(ticks=value, locale=locale, text=format_native(image, value, locale))
               for locale in (1, 2, 3, 4, 5, 6, 0) for value in ticks]
    cases = (
        ('before_start', dict(now=4000, start=0)),
        ('invalid_start_sentinel', dict(now=4000, start=0x7FFFFFFF)),
        ('go', dict(now=4000, start=4000)),
        ('running', dict(now=64321, start=4000)),
        ('rollover', dict(now=244000, start=244000, lap=1, best=240000,
                          pulse=True, completed=240000)),
        ('hold_before_3s', dict(now=255999, start=244000, lap=1, best=240000,
                               held_since=244000, held_ticks=240000)),
        ('hold_exact_3s', dict(now=256000, start=244000, lap=1, best=240000,
                              held_since=244000, held_ticks=240000)),
        ('hold_after_3s', dict(now=256001, start=244000, lap=1, best=240000,
                              held_since=244000, held_ticks=240000)),
        ('finished_record', dict(now=484000, start=484000, lap=2, best=240000,
                                 pulse=True, completed=240000)),
        ('best_zero', dict(now=64321, start=4000, best=0)),
        ('paused_same_snapshot', dict(now=64321, start=4000)),
        ('restart_fresh', dict(now=4000, start=0)),
        ('component_disabled', dict(now=64321, start=4000, enabled=False)),
    )
    masks = [dict(stage=stage, permission=permission, view_state=state,
                  output=mask_native(image, stage, permission, state))
             for stage in (1, 2, 3, 4) for permission in (0, 0x100)
             for state in (0, 1, 2, 3)]
    masks.append(dict(stage=4, permission=0x100, view_state=3, finished=True,
                      output=mask_native(image, 4, 0x100, finished=True)))
    masks.append(dict(stage=4, permission=0x100, view_state=0, finished=True,
                      output=mask_native(image, 4, 0x100, view_state=0, finished=True)))
    return dict(format_cases=formats, mask_cases=masks,
                resource_cases=[resource_native(image, index) for index in (0, 3, 5, 0xFFFF)],
                lap_cases=[dict(name=name, input=values, output=lap_native(image, **values))
                           for name, values in cases],
                limits='Synthetic records; UI text/unhide stubs; no runtime visibility/render/actor/lifecycle proof')
