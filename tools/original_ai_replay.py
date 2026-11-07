"""Fail-closed replay of isolated original AI decisions on captured memory."""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import time

from original_recorder import EXE_SHA256, digest, write_json

REG_NAMES = ('eax','ebx','ecx','edx','esi','edi','ebp','esp','eflags')
ENTRIES = {'controller': 0x462430, 'driver': 0x45e970}
MAX_PAGES = 2048
MAX_INSTRUCTIONS = 1000000
WATCH_ENTRIES = {0x44b1b0:'scheduler', 0x45b150:'path-choice', 0x45ae10:'path-build',
                 0x44bd10:'catchup', 0x44c010:'catchup-scale', 0x45e970:'driver',
                 0x4216b0:'world-rng-integer', 0x4216f0:'world-rng-scalar'}


class RetailCode:
    def __init__(self, path):
        if digest(path) != EXE_SHA256:
            raise ValueError('Unsupported original executable')
        data = Path(path).read_bytes()
        pe = struct.unpack_from('<I', data, 0x3c)[0]
        count, optional = struct.unpack_from('<H', data, pe+6)[0], struct.unpack_from('<H', data, pe+20)[0]
        base = struct.unpack_from('<I', data, pe+52)[0]
        self.pages = {}
        for i in range(count):
            at = pe+24+optional+40*i
            virtual, rva, size, raw = struct.unpack_from('<4I', data, at+8)
            flags = struct.unpack_from('<I', data, at+36)[0]
            if not flags & 0x20000000:
                continue
            content = data[raw:raw+size].ljust((max(virtual,size)+4095)&~4095, b'\0')
            for offset in range(0, len(content), 4096):
                self.pages[base+rva+offset] = content[offset:offset+4096]


def validate_entry(record):
    if record.get('kind') not in ENTRIES or record.get('entry') != ENTRIES[record['kind']]:
        raise ValueError('Unsupported AI boundary')
    for name in ('seq','invocation','thread','actor','actor_id','return_address','teb'):
        if not isinstance(record.get(name), int) or record[name] < 0:
            raise ValueError('Invalid AI identity: '+name)
    if record['actor_id'] not in (100,101,102):
        raise ValueError('Non-rival actor')
    regs = record.get('regs', {})
    if set(regs) != set(REG_NAMES) or any(type(v) is not int or not 0 <= v <= 0xffffffff for v in regs.values()):
        raise ValueError('Incomplete CPU entry')
    if not 0x10000 <= regs['esp'] < 0x7fff0000 or len(bytes.fromhex(record['fxsave'])) != 512:
        raise ValueError('Invalid stack/FPU entry')
    if not 0x10000 <= record.get('stack_limit',0) <= regs['esp'] < record.get('stack_base',0) < 0x7fff0000:
        raise ValueError('Missing/invalid actual thread stack bounds')


def ranges(addresses):
    result = []
    for address in sorted(addresses):
        if result and result[-1][0]+result[-1][1] == address and result[-1][1] < 4096:
            result[-1][1] += 1
        else:
            result.append([address,1])
    return result


def shadow(code, record, read_page, seconds=4, observer=None):
    """Execute authentic code without semantic stubs; fault pages are inputs.

    read_page is called while the original game thread waits at entry. An
    offline call serves exactly the saved preimages and cannot invent pages.
    """
    from unicorn import Uc, UcError, UC_ARCH_X86, UC_MODE_32, UC_HOOK_CODE, UC_HOOK_MEM_UNMAPPED, UC_HOOK_MEM_WRITE
    from unicorn import x86_const as x
    validate_entry(record)
    uc = Uc(UC_ARCH_X86, UC_MODE_32)
    pages, written, calls = {}, set(), []
    count, done, error = 0, False, None
    fault, dependency_error = None, None
    started = time.monotonic()
    # Private emulator workspace, never copied to or allocated inside the game.
    scratch = 0xfff00000
    uc.mem_map(scratch, 0x4000)
    uc.mem_write(scratch, b'\x0f\xae\x0d'+struct.pack('<I',scratch+0x1000))
    uc.mem_write(scratch+0x1000, bytes.fromhex(record['fxsave']))
    uc.emu_start(scratch, scratch+7, count=1)
    # A real FS base is required by original SEH; NULL is never a fake TEB.
    base, limit = record['teb'], 0xfffff
    descriptor = (limit & 0xffff) | ((base & 0xffffff) << 16) | (0x93 << 40) | (((limit >> 16)&15) << 48) | (0xc << 52) | ((base >> 24) << 56)
    uc.mem_write(scratch+0x2000+24, struct.pack('<Q', descriptor))
    uc.mem_write(scratch+0x2000+8,struct.pack('<Q',0x00cf9b000000ffff))
    uc.mem_write(scratch+0x2000+16,struct.pack('<Q',0x00cf93000000ffff))
    uc.reg_write(x.UC_X86_REG_GDTR, (0,scratch+0x2000,0x100,0))
    for name,value in (('CS',8),('DS',16),('ES',16),('SS',16)):
        uc.reg_write(getattr(x,'UC_X86_REG_'+name),value)
    uc.reg_write(x.UC_X86_REG_FS, 24)
    for name in REG_NAMES:
        uc.reg_write(getattr(x,'UC_X86_REG_'+name.upper()), record['regs'][name])

    def load_page(uc, access, address, size, value, user):
        nonlocal fault
        fault = [access,address,size,uc.reg_read(x.UC_X86_REG_EIP)]
        for p in range(address&~4095,(address+size+4095)&~4095,4096):
            if any(a <= p <= b for a,b,_ in uc.mem_regions()):
                continue
            if p in code.pages:
                blob = code.pages[p]
            else:
                if not 0x10000 <= p < 0x7fff0000 or len(pages) >= MAX_PAGES:
                    raise RuntimeError('Invalid/missing dependency or memory ceiling at %08X'%p)
                blob = read_page(p)
                if len(blob) != 4096:
                    raise RuntimeError('Short dependency page at %08X'%p)
                pages[p] = bytes(blob)
            uc.mem_map(p,4096); uc.mem_write(p,blob)
        return True

    def page(*args):
        nonlocal dependency_error
        try:
            return load_page(*args)
        except Exception as exc:
            dependency_error = str(exc)
            return False

    def instruction(uc,address,size,user):
        nonlocal count,done
        if observer is not None:
            observer(uc, address)
        if address == record['return_address']:
            done = True; uc.emu_stop(); return
        if address&~4095 not in code.pages:
            raise RuntimeError('External/non-retail execution at %08X; no substituted result'%address)
        count += 1
        if count % 2048 == 0 and time.monotonic()-started > seconds:
            raise RuntimeError('AI shadow time ceiling')
        op = bytes(uc.mem_read(address,min(size,3)))
        if op[:2] in (b'\x0f\x31',b'\x0f\x34',b'\x0f\x05',b'\xcd\x2e',b'\x0f\xa2'):
            raise RuntimeError('External CPU/time input at %08X'%address)
        if address in WATCH_ENTRIES:
            calls.append([count,address,WATCH_ENTRIES[address]])

    def write(uc,access,address,size,value,user):
        if address&~4095 in code.pages:
            raise RuntimeError('AI writes executable memory')
        # Exit instrumentation overwrites dead local-stack bytes; caller-owned
        # output pointers above entry ESP are retained and checked normally.
        if record['stack_limit'] <= address < record['regs']['esp']:
            return
        if len(written)+size > 262144:
            raise RuntimeError('AI output ceiling')
        written.update(range(address,address+size))

    uc.hook_add(UC_HOOK_MEM_UNMAPPED,page)
    uc.hook_add(UC_HOOK_CODE,instruction)
    uc.hook_add(UC_HOOK_MEM_WRITE,write)
    try:
        uc.emu_start(record['entry'],0,count=MAX_INSTRUCTIONS)
        if not done:
            raise RuntimeError('AI instruction ceiling or missing return')
    except (RuntimeError,UcError,ValueError) as exc:
        error = (dependency_error or str(exc))+'; eip=%08X fault=%s'%(uc.reg_read(x.UC_X86_REG_EIP),fault)
    watch = ranges(written) if not error else []
    if len(watch) > 8192:
        error = 'AI output range ceiling'; watch = []
    outputs = [[a,bytes(uc.mem_read(a,n)).hex()] for a,n in watch]
    return {'supported':error is None,'error':error,'instructions':count,
            'pages':[[a,b.hex()] for a,b in sorted(pages.items())],
            'watch':watch,'expected':outputs,'calls':calls,
            'post_regs':{name:uc.reg_read(getattr(x,'UC_X86_REG_'+name.upper())) for name in REG_NAMES},
            'wall_seconds':time.monotonic()-started}


def compare(result, record):
    if not result['supported']:
        return {'passed':False,'unsupported':result['error']}
    expected, actual = result['expected'], record.get('post')
    if not expected:
        return {'passed':False,'unsupported':'No non-stack decision outputs; not an acceptance case'}
    if not isinstance(actual,list) or [(a,len(bytes.fromhex(v))) for a,v in actual] != [(a,len(bytes.fromhex(v))) for a,v in expected]:
        raise ValueError('Missing/reordered native output ranges')
    differences = []
    for (address,want),(_,got) in zip(expected,actual):
        if want != got:
            differences.append({'address':address,'bytes':len(bytes.fromhex(want)),
                                'expected_sha256':hashlib.sha256(bytes.fromhex(want)).hexdigest(),
                                'actual_sha256':hashlib.sha256(bytes.fromhex(got)).hexdigest()})
    # Volatile registers/flags can include CPU-undefined arithmetic flags. The
    # saved callee registers and ESP must nevertheless return identically.
    regs = {name:[result['post_regs'][name],record.get('post_regs',{}).get(name)]
            for name in ('ebx','esi','edi','ebp','esp')
            if result['post_regs'][name] != record.get('post_regs',{}).get(name)}
    call_match = [c[1] for c in result['calls']] == record.get('native_calls')
    return {'passed':not differences and not regs and call_match,'memory_differences':differences,
            'register_differences':regs,'producer_order_matches':call_match}


def replay(code,record):
    entries = record.get('input_pages',[])
    pages = {}
    for a,value in entries:
        if type(a) is not int or a%4096 or a in pages or len(bytes.fromhex(value)) != 4096 or a in code.pages:
            raise ValueError('Malformed/duplicate input page')
        pages[a] = bytes.fromhex(value)
    def read(address):
        if address not in pages:
            raise RuntimeError('Uncaptured dependency at %08X'%address)
        return pages[address]
    result = shadow(code,record,read)
    return {'seq':record['seq'],'kind':record['kind'],'actor_id':record['actor_id'],
            'instructions':result['instructions'],'calls':result['calls'],**compare(result,record)}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('session',type=Path)
    parser.add_argument('--retail',type=Path,required=True)
    parser.add_argument('--out',type=Path,required=True)
    args = parser.parse_args()
    if args.out.exists() or args.out.resolve().is_relative_to(args.retail.resolve()):
        raise ValueError('Fresh non-retail output directory required')
    manifest = json.loads((args.session/'manifest.json').read_text('ascii'))
    for name,pin in manifest.items():
        if Path(name).name != name or name not in ('metadata.json','steps.jsonl','events.jsonl','summary.json'):
            raise ValueError('Unexpected capture manifest path')
        p = args.session/name
        if p.stat().st_size != pin['bytes'] or digest(p) != pin['sha256']:
            raise ValueError('Capture integrity failure: '+name)
    if not {'metadata.json','steps.jsonl','events.jsonl','summary.json'} <= manifest.keys():
        raise ValueError('Incomplete AI manifest')
    metadata = json.loads((args.session/'metadata.json').read_text('ascii'))
    if metadata.get('schema') != 'nfsu-ai-decisions-1' or metadata.get('source',{}).get('sha256') != EXE_SHA256:
        raise ValueError('Unsupported AI capture identity/schema')
    from original_ai_recorder import validate_record
    records = []
    with (args.session/'steps.jsonl').open(encoding='ascii') as stream:
        for line in stream:
            item = json.loads(line); validate_record(item,len(records)); records.append(item)
    if not records:
        raise ValueError('Empty AI capture')
    code = RetailCode(args.retail/'speed.exe')
    results = [replay(code,r) for r in records]
    args.out.mkdir(parents=True)
    report = {'capture_manifest_sha256':digest(args.session/'manifest.json'),
              'synthetic_fixture':bool(metadata.get('synthetic_fixture')),
              'records':len(results),'passed':sum(r['passed'] for r in results),
              'results':results,'port_comparison':'Not performed; native reproducibility is a separate prerequisite.',
              'scope':'Isolated calls on captured inputs, sampled with gaps. Not a chained race or performance benchmark.'}
    write_json(args.out/'report.json',report)
    print(json.dumps({k:v for k,v in report.items() if k != 'results'},ensure_ascii=True))
    return 0 if report['passed'] == len(results) else 1


if __name__ == '__main__':
    raise SystemExit(main())
