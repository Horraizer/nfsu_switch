"""Execute original NOS time gates without substituting their comparisons.

The request probe starts at462C77, after the declared active-state query;
it stops at the road predicate or rejection boundary. The stop probe starts
after the declared difficulty lookup. Neither probe models road/physics.
"""
import argparse
import hashlib
import json
from pathlib import Path
from physics256_native import Machine, STACK
from world_body_trace import Image


class BoundaryMachine(Machine):
    boundaries = (0x462d05, 0x462dde)

    def instruction(self, uc, address, size, data):
        if address in self.boundaries:
            self.boundary = address
            self.finished = True
            uc.emu_stop()
        else:
            super().instruction(uc, address, size, data)


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    for name in ('root', 'output', 'evidence'):
        ap.add_argument('--'+name, type=Path, required=True)
    args = ap.parse_args()
    data = (args.root/'speed.exe').read_bytes()
    identity = 'c0fad450912952f53809a54690d235aa8b4e25b9f46650693c7cbee1b4c074b7'
    if hashlib.sha256(data).hexdigest() != identity:
        raise ValueError('unsupported executable')
    im = Image(data)
    text = ['// Native NOS timing boundaries; zero semantic hooks.',
            '// speed.exe SHA256 '+identity,
            'const NitrousTimingCase nitrous_timing_cases[]={']
    rows = []
    controller, actor = 0x1100000, 0x1101000
    cases = []
    for active in (False, True):
        for last, finish in ((False, False), (True, False), (True, True)):
            for elapsed in (0, 1, 3999, 4000, 4001, 7999, 8000, 8001, 12000):
                cases.append((active, last, finish, 20000, 20000-elapsed, 20000-elapsed))
    cases += [(False, False, False, 20000, 1, 20001),
              (False, False, False, 20000, 19999, 0),
              (False, False, False, 20000, 0, 19999),
              (True, False, False, 1, 0xffffe0c0, 0)]
    for active, last, finish, now, started, stopped in cases:
        m = BoundaryMachine(im)
        m.allowed = [(0x462c77, 0x462d05), (0x465430, 0x465487)]
        m.put(0x73ad34, now)
        m.put(controller+0x4e0, started)
        m.put(controller+0x4e4, stopped)
        # The resource remains active throughout the bounded comparison.
        m.put(actor+0x40+0x3bc, 10 if active else 0)
        m.put(actor+0x40+0x3b8, 100000)
        m.put(STACK+0x18, 2.0, 'f')
        m.put(STACK+0x2c, int(finish), 'B')
        m.put(0x77a02c, 2.0, 'f')
        m.put(0x77a030, 1.0, 'f')
        m.run(0x462c77, eax=int(active), ebx=int(last), ebp=controller, esi=0, edi=actor)
        if m.events:
            raise ValueError('unexpected semantic hook')
        accepted = m.boundary == 0x462d05
        row = dict(active=active, last=last, finish=finish, now=now,
                   started=started, stopped=stopped, accepted=accepted)
        rows.append(row)
        text.append('{%s,%s,%s,%dU,%dU,%dU,%s},' %
                    (str(active).lower(), str(last).lower(), str(finish).lower(),
                     now, started, stopped, str(accepted).lower()))
    text += ['};', 'const NitrousStopCase nitrous_stop_cases[]={']
    stops = []
    for elapsed in (0, 1, 3999, 4000, 4001, 8000, -1):
        m = Machine(im)
        m.allowed = [(0x462e0b, 0x462e4e)]
        m.put(0x73ad34, 20000)
        m.put(controller+0x4e0, 20000-elapsed)
        m.put(0x77a034, 1.0, 'f')
        # Entry is after push ecx/push esi, with the context already in EAX.
        m.put(STACK+8, 0x1000000)
        m.run(0x462e0b, eax=0, esi=controller)
        accepted = m.reg('eax') != 0
        stops.append(dict(elapsed=elapsed, accepted=accepted))
        text.append('{20000U,%dU,%s},' % (20000-elapsed, str(accepted).lower()))
    text += ['};']
    args.output.write_text('\n'.join(text)+'\n', encoding='ascii')
    args.evidence.write_text(json.dumps(dict(executable_sha256=identity,
        semantic_hooks=0, request_cases=rows, stop_cases=stops), indent=2)+'\n', encoding='ascii')
    print('PASS original NOS time gates:', len(rows), 'requests,', len(stops), 'stops; semantic hooks: 0')


if __name__ == '__main__':
    main()
