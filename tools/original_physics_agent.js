// Diagnostic instrumentation; no game file or gameplay value is changed.
'use strict';
let enabled = true, active = 0, sequence = 0, acknowledgements = 0;
let sent = 0, completed = 0, errors = 0, pending = [];
const MAX_STEPS = 40000, MAX_IN_FLIGHT = 8;
let fixture = false;
const hooks = [];
const cwMemory = Memory.alloc(Process.pageSize);
Memory.protect(cwMemory, Process.pageSize, 'rwx');
// Read the x87 control word without changing it. Tested on a live x86 fixture.
Memory.patchCode(cwMemory, 16, code => {
    code.writeByteArray([0x83, 0xec, 4, 0xd9, 0x3c, 0x24, 0x58, 0xc3]);
});
const controlWord = new NativeFunction(cwMemory, 'uint32', []);
Memory.patchCode(cwMemory.add(16), 16, code => {
    code.writeByteArray([0x83, 0xec, 4, 0x0f, 0xae, 0x1c, 0x24, 0x58, 0xc3]);
});
const sseControl = new NativeFunction(cwMemory.add(16), 'uint32', []);
function hex(address, size) {
    return Array.from(new Uint8Array(address.readByteArray(size)),
                      x => x.toString(16).padStart(2, '0')).join('');
}
function fail(message) {
    errors++; enabled = false;
    send({kind: 'capture-error', message: String(message)});
}
function flush() {
    if (pending.length === 0) return;
    if (sent - acknowledgements >= MAX_IN_FLIGHT) {
        fail('Writer backpressure: capture stopped; no silently dropped steps');
        return;
    }
    send({kind: 'batch', batch: sent++, records: pending}); pending = [];
}
rpc.exports = {
    start(testFixture) {
        if (hooks.length) throw new Error('Already started');
        fixture = testFixture === true;
        if (Process.arch !== 'ia32') throw new Error('32-bit target required');
        const entry = ptr('0x46aa40');
        if (hex(entry, 6) !== '81ec34020000') throw new Error('Integration signature differs');
        hooks.push(Interceptor.attach(entry, {
            onEnter(args) {
                this.accepted = false;
                if (!enabled || sequence >= MAX_STEPS) return;
                // The human physics1 callback supplies the system at physics+8.
                if (!fixture && !this.returnAddress.equals(ptr('0x477df2'))) return;
                try {
                    const system = args[0], physics = system.sub(8);
                    const actor = physics.add(0x5c).readPointer();
                    const config = actor.add(0x14).readPointer();
                    if (config.add(4).readU32() !== 1 || actor.add(0xa).readS16() !== 0 ||
                        !actor.add(0x34).readPointer().equals(physics) ||
                        actor.add(0x28).readU32() !== 1) return;
                    const rigid = physics.add(0x20).readPointer();
                    const properties = rigid.add(0x14).readPointer();
                    const driver = actor.add(0x30).readPointer();
                    const race = ptr('0x73619c').readPointer();
                    this.record = {
                        seq: sequence++, thread: this.threadId,
                        frame: ptr('0x7364dc').readU32(), clock: ptr('0x73ad34').readU32(),
                        stage: race.isNull() ? null : race.readU32(),
                        track: ptr('0x78a2f0').readU32(), mode: ptr('0x78a30c').readU32(),
                        actor: actor.toUInt32(), physics: physics.toUInt32(),
                        rigid: rigid.toUInt32(), properties: properties.toUInt32(),
                        dt_bits: args[1].toUInt32(), global_dt_bits: ptr('0x6f0738').readU32(),
                        fpcw: controlWord() & 0xffff,
                        mxcsr: sseControl(),
                        car_type: config.add(0x10).readU32(),
                        controls_hex: hex(driver.add(0x1d0), 0x18),
                        pre_hex: hex(rigid, 0x200), properties_hex: hex(properties, 0x54),
                        globals: [[0x6b5b54, 0x38], [0x6f08c8, 0x40],
                                  [0x704478, 0x44], [0x6d4a00, 0x100],
                                  [0x7000ac, 4], [0x6f0738, 4]].map(
                                      pair => [pair[0], hex(ptr(pair[0]), pair[1])])
                    };
                    this.rigid = rigid; this.started = Date.now();
                    this.accepted = true; active++;
                } catch (error) { fail(error); }
            },
            onLeave() {
                if (!this.accepted) return;
                try {
                    this.record.post_hex = hex(this.rigid, 0x200);
                    this.record.wall_ms = Date.now() - this.started;
                    pending.push(this.record); completed++;
                    if (pending.length >= 16) flush();
                    if (completed >= MAX_STEPS) enabled = false;
                } catch (error) { fail(error); }
                finally { active--; }
            }
        }));
        return {arch: Process.arch, limit: MAX_STEPS};
    },
    acknowledge(number) {
        if (number !== acknowledgements) throw new Error('Out-of-order writer acknowledgement');
        acknowledgements++; return acknowledgements;
    },
    status() { flush(); return {enabled, active, sequence, completed, errors, sent, acknowledgements}; },
    stop() { enabled = false; flush(); return {active, sequence, completed, errors, sent, acknowledgements}; }
};
