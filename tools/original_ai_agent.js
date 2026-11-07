// Bounded diagnostic AI capture. Never modifies retail files or AI inputs.
'use strict';
let enabled = true, sequence = 0, completed = 0, errors = 0;
let sent = 0, acknowledgements = 0, active = 0, invocation = 0;
let current = null, fixture = false;
const hooks = [], listeners = [], keepAlive = [], last = new Map();
const producers = [0x44b1b0,0x45b150,0x45ae10,0x44bd10,0x44c010,0x4216b0,0x4216f0];
const LIMIT = 300, INTERVAL = 8000, DEADLINE = 10000;
const kernel = Process.getModuleByName('kernel32.dll');
const createEvent = new NativeFunction(kernel.getExportByName('CreateEventW'), 'pointer', ['pointer','int','int','pointer']);
const setEvent = new NativeFunction(kernel.getExportByName('SetEvent'), 'int', ['pointer']);
const resetEvent = new NativeFunction(kernel.getExportByName('ResetEvent'), 'int', ['pointer']);
const waitEvent = new NativeFunction(kernel.getExportByName('WaitForSingleObject'), 'uint32', ['pointer','uint32']);
const closeHandle = new NativeFunction(kernel.getExportByName('CloseHandle'), 'int', ['pointer']);
const event = createEvent(ptr(0), 1, 0, ptr(0));
if (event.isNull()) throw new Error('Cannot create capture watchdog event');
const helpers = Memory.alloc(Process.pageSize);
Memory.protect(helpers, Process.pageSize, 'rwx');
Memory.patchCode(helpers, 8, p => p.writeByteArray([0x64,0xa1,0x18,0,0,0,0xc3]));
const teb = new NativeFunction(helpers, 'pointer', []);

function hex(p, n) {
    return Array.from(new Uint8Array(p.readByteArray(n)), x => x.toString(16).padStart(2,'0')).join('');
}
function fail(message) {
    enabled = false; errors++;
    send({kind:'capture-error', message:String(message)});
}
function state() { return {enabled, active, sequence, completed, errors, sent, acknowledgements, invocation}; }
function registers(frame, entry) {
    const at = frame.add(0x220), result = {};
    ['edi','esi','ebp','saved_esp','ebx','edx','ecx','eax','eflags'].forEach((name,i) => result[name] = at.add(i*4).readU32());
    delete result.saved_esp;
    result.esp = frame.add(0x244).toUInt32();
    // The exit shim reserves a return slot before saving the actual CPU state.
    if (!entry) result.esp += 4;
    return result;
}
function fxsave(frame) { return hex(frame.add(15).and(ptr('0xfffffff0')), 512); }
function restoreSeh(address, value, base, head) {
    // The diagnostic callback has its own SEH frame. Its saved native value
    // is captured by the assembly shim before calling JavaScript.
    let result = value;
    for (let i=0;i<4;i++) {
        const offset = base+i-address;
        if (offset>=0 && offset<value.length/2) result = result.slice(0,offset*2)+
            ((head>>>(i*8))&255).toString(16).padStart(2,'0')+result.slice(offset*2+2);
    }
    return result;
}
function eligible(kind, regs) {
    const object = ptr(regs.ecx);
    const actor = object.add(kind === 'controller' ? 4 : 0x1f0).readPointer();
    const config = actor.add(0x14).readPointer();
    if (config.add(4).readU32() !== 2 || actor.add(0xa).readS16() < 100 ||
        actor.add(0xa).readS16() > 102) return null;
    const link = actor.add(kind === 'controller' ? 0x2c : 0x30).readPointer();
    if (!object.equals(link)) return null;
    if (!fixture && kind === 'controller' && actor.add(0x24).readU32() !== 3) return null;
    const race = ptr('0x73619c').readPointer();
    if (!fixture && (race.isNull() || race.readU32() !== 4)) return null;
    return actor;
}

function install(kind, address, signature) {
    const target = ptr(address);
    if (hex(target, signature.length/2) !== signature) throw new Error('AI entry signature differs: '+kind);
    const trampoline = Memory.alloc(Process.pageSize);
    Memory.protect(trampoline, Process.pageSize, 'rwx');
    // Build the original relay before installing the replacement: no null relay
    // window while the running game might enter the function.
    Memory.patchCode(trampoline, 128, code => {
        const writer = new X86Writer(code, {pc:trampoline});
        const relocator = new X86Relocator(target, writer);
        let length = 0;
        while (length < 24) { length = relocator.readOne(); if (!length) throw new Error('Cannot relocate AI entry'); }
        relocator.writeAll(); writer.putJmpAddress(target.add(length)); writer.flush();
    });
    const exit = Memory.alloc(Process.pageSize), enter = Memory.alloc(Process.pageSize);
    Memory.protect(exit, Process.pageSize, 'rwx'); Memory.protect(enter, Process.pageSize, 'rwx');
    const after = new NativeCallback(frame => {
        const item = current;
        if (!item || item.thread !== Process.getCurrentThreadId()) { fail('Lost AI return owner'); return; }
        // Always supply the saved return, including failed reads, so diagnostics
        // cannot turn an ordinary decision into an invalid return.
        frame.add(0x244).writePointer(ptr(item.return_address));
        try {
            item.post_regs = registers(frame, false); item.post_fxsave = fxsave(frame);
            item.post = item.watch.map(pair => [pair[0], hex(ptr(pair[0]), pair[1])]);
            const head = frame.add(0x210).readU32();
            item.post = item.post.map(pair => [pair[0], restoreSeh(pair[0],pair[1],item.teb,head)]);
            item.end_frame = ptr('0x7364dc').readU32(); item.end_clock = ptr('0x73ad34').readU32();
            delete item.watch;
            send({kind:'batch', batch:sent++, records:[item]}); completed++;
            if (completed >= LIMIT) enabled = false;
        } catch (error) { fail(error); }
        finally { current = null; active = 0; }
    }, 'void', ['pointer']);
    const before = new NativeCallback(frame => {
        if (active) {
            if (current && current.phase === 'native' && current.thread === Process.getCurrentThreadId() && kind === 'driver') current.native_calls.push(0x45e970);
            return;
        }
        if (!enabled || sequence >= LIMIT) return;
        try {
            const regs = registers(frame, true), actor = eligible(kind, regs);
            if (actor === null) return;
            const order = invocation++, clock = ptr('0x73ad34').readU32();
            const key = kind+':'+actor.toString(), previous = last.get(key);
            if (!fixture && previous !== undefined && ((clock-previous)>>>0) < INTERVAL) return;
            if (sent - acknowledgements >= 8) { fail('AI writer backpressure'); return; }
            last.set(key, clock);
            const environment = teb();
            const item = {seq:sequence++, invocation:order, kind, entry:target.toUInt32(),
                thread:Process.getCurrentThreadId(), actor:actor.toUInt32(), actor_id:actor.add(0xa).readS16(),
                frame:ptr('0x7364dc').readU32(), clock, track:ptr('0x78a2f0').readU32(),
                mode:ptr('0x78a30c').readU32(), regs, fxsave:fxsave(frame), teb:environment.toUInt32(),
                stack_base:environment.add(4).readU32(), stack_limit:environment.add(8).readU32(),
                seh_head:frame.add(0x210).readU32(),
                return_address:ptr(regs.esp).readU32(), watch:[]};
            active = 1; current = item; resetEvent(event);
            send({kind:'begin', record:item});
            const waited = waitEvent(event, DEADLINE);
            if (waited !== 0 || !item.ready) {
                fail('AI shadow timeout/cancel; original decision continues unchanged');
                current = null; active = 0; return;
            }
            delete item.ready;
            item.phase = 'native'; item.native_calls = kind === 'driver' ? [0x45e970] : [];
            ptr(regs.esp).writePointer(exit);
        } catch (error) { current = null; active = 0; fail(error); }
    }, 'void', ['pointer']);
    function shim(location, callback, returning) {
        Memory.patchCode(location, 128, code => {
            const w = new X86Writer(code, {pc:location});
            if (returning) w.putBytes([0x8d,0x64,0x24,0xfc]); // reserve return, preserve flags
            w.putBytes([0x9c,0x60,0x81,0xec,0x20,0x02,0,0]);
            w.putBytes([0x89,0xe0,0x83,0xc0,0x0f,0x83,0xe0,0xf0,0x0f,0xae,0x00]);
            w.putBytes([0x64,0xa1,0,0,0,0,0x89,0x84,0x24,0x10,0x02,0,0]);
            w.putBytes([0x54]); w.putCallAddress(callback); w.putBytes([0x83,0xc4,0x04]);
            w.putBytes([0x89,0xe0,0x83,0xc0,0x0f,0x83,0xe0,0xf0,0x0f,0xae,0x08]);
            w.putBytes([0x81,0xc4,0x20,0x02,0,0,0x61,0x9d]);
            if (returning) w.putRet(); else w.putJmpAddress(trampoline);
            w.flush();
        });
    }
    shim(exit, after, true); shim(enter, before, false);
    keepAlive.push(trampoline, exit, enter, before, after);
    Interceptor.replaceFast(target, enter); hooks.push(target);
}

rpc.exports = {
    start(testFixture) {
        if (hooks.length || Process.arch !== 'ia32') throw new Error('Fresh 32-bit AI recorder required');
        fixture = testFixture === true;
        install('controller', '0x462430', '53568bf18b4604');
        install('driver', '0x45e970', '83ec3cd9442440');
        producers.forEach(address => listeners.push(Interceptor.attach(ptr(address),{
            onEnter() {
                if (current && current.phase === 'native' && current.thread === this.threadId) {
                    if (current.native_calls.length >= 20000) { fail('AI producer trace ceiling'); return; }
                    current.native_calls.push(address);
                }
            }
        })));
        return {arch:Process.arch, limit:LIMIT, interval_ticks:INTERVAL, deadline_ms:DEADLINE};
    },
    pages(seq, addresses) {
        if (!current || current.seq !== seq || current.ready || addresses.length > 32) throw new Error('No paused AI boundary');
        return addresses.map(address => {
            if (!Number.isInteger(address) || address < 0x10000 || address >= 0x7fff0000 || address % 4096) throw new Error('Invalid page');
            return [address, restoreSeh(address,hex(ptr(address),4096),current.teb,current.seh_head)];
        });
    },
    resume(seq, watch) {
        if (!current || current.seq !== seq || current.ready || watch.length > 8192) throw new Error('No matching paused AI boundary');
        for (const pair of watch) if (!Array.isArray(pair) || pair.length !== 2 ||
            !Number.isInteger(pair[0]) || !Number.isInteger(pair[1]) || pair[0] < 0x10000 ||
            pair[1] < 1 || pair[1] > 4096 || pair[0]+pair[1] >= 0x7fff0000) throw new Error('Invalid output watch');
        current.watch = watch; current.ready = true; setEvent(event);
    },
    acknowledge(number) {
        if (number !== acknowledgements) throw new Error('Out-of-order AI acknowledgement');
        return ++acknowledgements;
    },
    status() { return state(); },
    stop() { enabled = false; return state(); },
    cancel() { enabled = false; if (current && !current.ready) setEvent(event); return state(); },
    cleanup() {
        if (active) throw new Error('AI return still active');
        hooks.forEach(p => Interceptor.revert(p)); hooks.length = 0; closeHandle(event);
        listeners.forEach(listener => listener.detach()); listeners.length = 0;
    }
};
