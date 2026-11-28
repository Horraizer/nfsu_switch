"""Observe authenticated original Race Over controller through explicit UI seams."""
import argparse
import hashlib
import json
import struct
import sys
from pathlib import Path

WORK = Path(__file__).resolve().parent.parent
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--root',type=Path,required=True)
parser.add_argument('--out',type=Path,required=True)
parser.add_argument('--write-fixture',action='store_true')
ARGS = parser.parse_args()
HERE = ARGS.out.resolve()
HERE.mkdir(parents=True,exist_ok=False)
sys.path.insert(0, str(WORK / 'tools'))
from world_body_trace import Image
from physics256_native import Machine
from unicorn.x86_const import UC_X86_REG_FPCW

ROOT = ARGS.root
raw = (ROOT / 'speed.exe').read_bytes()
assert hashlib.sha256(raw).hexdigest() == 'c0fad450912952f53809a54690d235aa8b4e25b9f46650693c7cbee1b4c074b7'
image = Image(raw)
lang = (ROOT / 'Languages/English.bin').read_bytes()
assert hashlib.sha256(lang).hexdigest() == '6cbab64235523149b95966b1844cdca9fb858744126f8d9ad10e72c921ab8790'
u32 = lambda b, p: struct.unpack_from('<I', b, p)[0]
count, table, strings = u32(lang, 12), 8 + u32(lang, 16), 8 + u32(lang, 20)
labels = {}
for i in range(count):
    key, offset = struct.unpack_from('<II', lang, table + 8*i)
    labels[key] = lang[strings+offset:].split(b'\0', 1)[0].decode('ascii', errors='backslashreplace')


def cstring(m, at):
    return bytes(m.uc.mem_read(at, 256)).split(b'\0', 1)[0].decode('ascii')


def run(status, place, drag, masked=True, online=False, online_win=False, repeated=False):
    m = Machine(image)
    component, context, racer, timing, message, backing = [0x1100000 + 0x1000*i for i in range(6)]
    m.put(component+0x10, 1)
    m.put(component+0x18, int(masked))
    m.put(component+0x20, message)
    m.put(component+0x24, backing)
    m.put(component+0x28, int(repeated), 'B')
    m.put(context+4, racer)
    m.put(context+0x10, 1)
    m.put(racer+0x1C, timing)
    m.put(timing+0xC, place, 'b')
    m.put(timing+0x10, status, 'h')
    m.put(timing+0x110, 493800)
    m.put(0x6FBF14, 1)
    m.put(0x78A344, drag, 'B')
    m.put(0x7361A8, 0)
    m.allowed += [(0x4A7D60,0x4A7F8E),(0x586090,0x586640),(0x674898,0x67490D)]
    m.put(0x6EE6F0,5)
    calls, texts = [], []
    allocated = {}

    def address(text):
        if text not in allocated:
            at = 0x1110000 + 0x200*len(allocated)
            allocated[text] = at
            m.uc.mem_write(at, text.encode('ascii') + b'\0')
        return allocated[text]

    def localize(x):
        key = x.reg('edx')
        calls.append({'kind':'localize', 'hash':'%08X' % key, 'text':labels.get(key)})
        x.ret(value=address(labels[key]) if key in labels else 0)

    def animate(x):
        sp = x.reg('esp')
        calls.append({'kind':'animate', 'object':'message' if x.reg('edi') == message else 'backing',
                      'script':cstring(x, x.get(sp+4)), 'arg':x.get(sp+8)})
        x.ret()

    def bind(x):
        key = x.reg('eax')
        texts.append(labels.get(key, '<MISSING:%08X>' % key))
        calls.append({'kind':'bind', 'hash':'%08X' % key, 'object':x.reg('esi')})
        x.ret()

    def formatted(x):
        sp = x.reg('esp')
        fmt = cstring(x, x.get(sp+4))
        n = fmt.count('%s')
        args = [cstring(x, x.get(sp+8+4*i)) for i in range(n)]
        texts.append(fmt % tuple(args))
        calls.append({'kind':'formatted', 'format':fmt, 'args':args, 'object':x.reg('eax')})
        x.ret()

    m.hook(0x59FB80, 'language_table_lookup', callback=localize)
    m.hook(0x4F6B70, 'animate_ui_capture', callback=animate)
    m.hook(0x4F66F0, 'bind_language_capture', callback=bind)
    m.hook(0x4F68A0, 'format_text_capture', callback=formatted)
    m.hook(0x43A2E0, 'online_mode_reply', value=int(online))
    m.hook(0x43A340, 'online_win_reply', value=int(online_win))
    m.run(0x4A7D60, (component,), edx=context)
    assert m.reg('esp') == 0x11F0008
    return dict(status=status, place=place, drag=drag, masked=masked, online=online,
                online_win=online_win, repeated=repeated, texts=texts, calls=calls,
                latched=m.get(component+0x28, 'B'), hooks=m.events)


rows = [run(status, place, drag) for status in (1,3,6,7,8) for drag in (0,1) for place in range(1,5)]
rows += [run(1,1,0,masked=False), run(1,1,0,repeated=True)]
rows += [run(status,1,0,online=True,online_win=win) for status in (9,10) for win in (False,True)]
assert rows[0]['texts'] == ['Race Over']
assert rows[-6]['texts'] == []

def loading(phase_ticks,busy,flag=1,loaded=0,fpcw=0x007F):
    results=[]
    for phase,begin,end in [('entry',0x422F84,0x422FA5),('poll',0x422FE9,0x42302A),('exit',0x422FB2,0x422FDC)]:
        m=Machine(image);m.uc.reg_write(UC_X86_REG_FPCW,fpcw);state,package,controller=0x1100000,0x1101000,0x1102000
        m.put(0x6FB988,flag,'B');m.put(0x73AD3C,phase_ticks);m.put(state+0x14,8)
        m.put(package+0x14,controller)
        m.allowed += [(begin,end),(0x4DE130,0x4DE152)]
        calls=[]
        def capture(name,pop=0,value=0,package_arg=False):
            def sink(x):
                calls.append(dict(name=name,package=cstring(x,x.reg('eax')) if package_arg else None))
                x.ret(pop,value)
            return sink
        m.hook(0x4F6650,'query_package',callback=capture('query_package',value=loaded,package_arg=True))
        m.hook(0x4F61F0,'open_package',callback=capture('open_package',package_arg=True))
        m.hook(0x4F6240,'close_package',callback=capture('close_package',package_arg=True))
        m.hook(0x445100,'ending_busy',callback=capture('ending_busy',4,busy))
        m.hook(0x4F3F90,'loading_controller',callback=capture('loading_controller',4,package,True))
        m.hook(0x421AE0,'advance_state',callback=capture('advance_state',4))
        m.hook(0x434B50,'notify_race',callback=capture('notify_race',8))
        m.run(begin,end=end,esi=state)
        results.append(dict(phase=phase,calls=calls,loading_controller_complete=m.get(controller+0x40)))
    return dict(ticks=phase_ticks,busy=busy,flag=flag,loaded=loaded,fpcw=fpcw,phases=results)


loading_rows=[loading(t,b) for t in (0,7999,8000,8001,12000) for b in (0,1)]
loading_rows += [loading(8000,0,0),loading(8000,0,1,1)]
for row in loading_rows:
    poll=row['phases'][1]
    advanced=any(c['name']=='advance_state' for c in poll['calls'])
    assert advanced == (row['ticks']>8000 and not row['busy'])
    assert poll['loading_controller_complete']==int(advanced)


driver_delays=[]
for event in range(8):
    m=Machine(image);view=0x1100000;m.put(view+0x60,event)
    m.allowed=[(0x42C2FB,0x42C325),(0x674898,0x67490D)]
    m.run(0x42C2FB,end=0x42C325,esi=view)
    frames=m.get(view+0x5C)
    assert frames==(360 if event in (6,7) else 180)
    driver_delays.append(dict(event=event,frames60=frames,hooks=m.events))
loading_extended=[loading(t,b,fpcw=0x037F) for t in (7999,8000,8001) for b in (0,1)]
for row in rows:
    animations=[c for c in row['calls'] if c['kind']=='animate']
    assert len(animations)==(0 if not row['masked'] or row['repeated'] else 2)
    if animations:assert [c['object'] for c in animations]==['message','backing']


def escaped(text):
    return json.dumps(text,ensure_ascii=True)

fixture='// Original4A7D60 controller, with actual586580 and language table.\n'
fixture+='struct NativeFinishMessage {unsigned status,place,drag;int ticks;const char* message;};\n'
fixture+='static constexpr NativeFinishMessage kNativeFinishMessages[]={\n'
for row in rows:
    if not row['masked'] or row['online'] or row['repeated']:continue
    fixture+='    {%d,%d,%d,493800,%s},\n'%(row['status'],row['place'],row['drag'],escaped(row['texts'][-1] if row['texts'] else ''))
fixture+='};\nstruct NativeFinishLoading {unsigned ticks;bool resources_ready,advance;};\n'
fixture+='static constexpr NativeFinishLoading kNativeFinishLoading[]={\n'
for row in loading_rows[:10]:
    advanced=any(c['name']=='advance_state' for c in row['phases'][1]['calls'])
    fixture+='    {%d,%s,%s},\n'%(row['ticks'],str(not row['busy']).lower(),str(advanced).lower())
fixture+='};\n'
fixture+='struct NativeFinishDriverDelay {unsigned event,frames60;};\nstatic constexpr NativeFinishDriverDelay kNativeFinishDriverDelays[]={\n'
fixture+=''.join('    {%d,%d},\n'%(r['event'],r['frames60']) for r in driver_delays)
fixture+='};\n'
target=WORK/'tests/race_finish_flow_native.inc'
if ARGS.write_fixture:target.write_text(fixture,encoding='ascii')
else:assert target.read_text(encoding='ascii')==fixture,'finish flow fixture stale'
report=dict(original_sha256=hashlib.sha256(raw).hexdigest(),language_sha256=hashlib.sha256(lang).hexdigest(),
            controller_range='004A7D60..004A7F8E',messages=rows,loading=loading_rows,loading_extended=loading_extended,driver_delays=driver_delays,
            loading_fpcw='007F selected from static D3D main-thread context;037F recorded separately. Strict floating comparison is original code.',limits='Original x86 on supplied records; UI lookup/submission and readiness callbacks are explicit external seams. No live process or original saves modified.')
(HERE/'oracle.json').write_text(json.dumps(report,indent=2,ensure_ascii=True)+'\n',encoding='ascii')
print('PASS original terminal message,one-shot animation,loading entry/readiness/exit')
