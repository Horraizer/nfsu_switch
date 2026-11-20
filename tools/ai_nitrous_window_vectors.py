"""Execute original462D6A..462DC8 road-window admission on declared inputs.

The entry AL is the declared5A6EF0 road-chain predicate. This bounded probe
does not claim to execute the earlier tuning/gear/contact admission.
"""
import argparse
import hashlib
import json
from pathlib import Path
from physics256_native import Machine, STACK
from world_body_trace import Image


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    for key in ('root', 'output', 'evidence'):
        ap.add_argument('--'+key, type=Path, required=True)
    args = ap.parse_args()
    data = (args.root/'speed.exe').read_bytes()
    identity = 'c0fad450912952f53809a54690d235aa8b4e25b9f46650693c7cbee1b4c074b7'
    if hashlib.sha256(data).hexdigest() != identity:
        raise ValueError('unsupported executable')
    im = Image(data)
    cases = [(True, 20000, 0), (False, 20000, 1000),
             (True, 20000, 16001), (True, 20000, 16000),
             (True, 20000, 15999), (True, 20000, 20001),
             (True, 20000, 0x7fffffff), (True, 20000, 0xfffff000),
             (True, 0, 0), (True, 1, 0xfffffffe)]
    rows = []
    text = ['// Original462D6A..462DC8 with declared road predicate; no semantic hooks.',
            '// speed.exe SHA256 '+identity]
    for monotonic, now, since in cases:
        m = Machine(im)
        m.allowed = [(0x462d6a, 0x462dc8), (0x421a30, 0x421a46)]
        controller = 0x1100000
        m.put(controller+0x4e8, since)
        m.put(0x73ad34, now)
        m.put(STACK+0x2c, 0)
        m.run(0x462d6a, end=0x462dc8, eax=int(monotonic), ebp=controller)
        admitted = m.get(STACK+0x2c, 'B') != 0
        after = m.get(controller+0x4e8)
        if m.events:
            raise ValueError('unexpected semantic hook')
        rows.append(dict(monotonic=monotonic, clock=now, before=since,
                         after=after, admitted=admitted))
        text.append('{%s,%dU,%dU,%dU,%s},' %
                    (str(monotonic).lower(), now, since, after, str(admitted).lower()))
    args.output.write_text('\n'.join(text)+'\n', encoding='ascii')
    args.evidence.write_text(json.dumps(dict(executable_sha256=identity,
        semantic_hooks=0, cases=rows), indent=2)+'\n', encoding='ascii')
    print('PASS original nitrous road window:', len(rows), 'cases; semantic hooks: 0')


if __name__ == '__main__':
    main()
