'use strict';
// D3D9 COM indices are from the installed d3d9.h. Frida ABI/CModule API:
// https://frida.re/docs/javascript-api/
const ps = Process.pointerSize;
const abi = ps === 4 ? 'stdcall' : 'win64';
let config, device, presentHook, blurHook, cornerHook, cornerModule;
let timer, complete = false, armed = false, selected = false;
let epoch = 0, traceCount = 0, captureThread = 0, current = null;
const listeners = [], functions = new Map();
const corners = Memory.alloc(16 + 384);
function emit(type, data) { send(Object.assign({type:type}, data)); }
function com(object, index, types, result) {
  const fn = object.readPointer().add(index * ps).readPointer();
  const key = fn.toString() + ':' + types.join(',') + ':' + (result || 'int');
  if (!functions.has(key)) functions.set(key, new NativeFunction(fn,
    result || 'int', ['pointer'].concat(types), {abi:abi}));
  return functions.get(key);
}
function checked(hr, operation) {
  if (hr < 0) throw new Error(operation + ' HRESULT=0x' + (hr >>> 0).toString(16));
}
function release(object) {
  if (object && !object.isNull()) com(object, 2, [], 'uint')(object);
}
function scalar(address, type) {
  try { return type === 'float' ? ptr(address).readFloat() : ptr(address).readU32(); }
  catch (_) { return null; }
}
function vector(address) {
  try { return [ptr(address).readFloat(), ptr(address).add(4).readFloat(),
    ptr(address).add(8).readFloat()]; } catch (_) { return null; }
}
function hexBytes(address, count) {
  try { return Array.from(new Uint8Array(ptr(address).readByteArray(count)),
    x => x.toString(16).padStart(2, '0')).join(''); } catch (_) { return null; }
}
function player() {
  if (config.synthetic) return {speed_mps:22, rpm:5000, synthetic:true};
  try {
    const world = ptr('0x7361f8').readPointer();
    let actor = world.readPointer(); const seen = new Set();
    for (let n = 0; n < 128 && !actor.isNull() && !actor.equals(world); n++) {
      if (seen.has(actor.toString())) break; seen.add(actor.toString());
      const cfg = actor.add(0x14).readPointer();
      if (!cfg.isNull() && cfg.add(4).readU32() === 1) return {
        address:actor.toString(), speed_mps:actor.add(0x3d4).readFloat(),
        rpm:actor.add(0x3e0).readFloat(), gear_slot_raw:actor.add(0x3d8).readS32(),
        position:vector(actor.add(0x60)), velocity:vector(actor.add(0x70))};
      actor = actor.readPointer();
    }
  } catch (_) {}
  return null;
}
function sample(full) {
  const result = {epoch:epoch, thread:Process.getCurrentThreadId(),
    frame:scalar(config.frame), target_raw:scalar(config.target),
    envelope:scalar(config.envelope, 'float'), wall_ms:Date.now(), player:player()};
  if (!config.synthetic) {
    try {
      const view = ptr('0x740500').readPointer(); result.view = view.toString();
      result.view_velocity = vector(view.add(0x1e0));
      if (full) result.view_raw_hex = hexBytes(view, 0x240);
      result.world_clock = scalar('0x73ad34');
    } catch (_) { result.view_unavailable = true; }
  }
  return result;
}
function capture(stage, backbuffer, metadata) {
  let source = null, resolved = null, system = null, locked = false;
  try {
    const output = Memory.alloc(ps); output.writePointer(ptr(0));
    if (backbuffer) checked(com(device, 18, ['uint','uint','uint','pointer'])(
      device, 0, 0, 0, output), 'GetBackBuffer');
    else checked(com(device, 38, ['uint','pointer'])(device, 0, output), 'GetRenderTarget');
    source = output.readPointer();
    const desc = Memory.alloc(32);
    checked(com(source, 12, ['pointer'])(source, desc), 'GetDesc');
    const format = desc.readU32(), msaa = desc.add(16).readU32();
    const width = desc.add(24).readU32(), height = desc.add(28).readU32();
    if (![21,22].includes(format) || width < 32 || width > 4096 ||
        height < 32 || height > 2160) throw new Error('Unsupported surface description');
    let readable = source;
    if (msaa !== 0) {
      if (!backbuffer) throw new Error('MSAA world capture requires resolve outside BeginScene');
      output.writePointer(ptr(0));
      checked(com(device, 28, ['uint','uint','uint','uint','uint','int','pointer','pointer'])(
        device, width, height, format, 0, 0, 0, output, ptr(0)), 'CreateRenderTarget');
      resolved = output.readPointer();
      checked(com(device, 34, ['pointer','pointer','pointer','pointer','uint'])(
        device, source, ptr(0), resolved, ptr(0), 0), 'StretchRect');
      readable = resolved;
    }
    output.writePointer(ptr(0));
    checked(com(device, 36, ['uint','uint','uint','uint','pointer','pointer'])(
      device, width, height, format, 2, output, ptr(0)), 'CreateOffscreenPlainSurface');
    system = output.readPointer();
    checked(com(device, 32, ['pointer','pointer'])(device, readable, system), 'GetRenderTargetData');
    const rect = Memory.alloc(ps === 4 ? 8 : 16);
    checked(com(system, 13, ['pointer','pointer','uint'])(system, rect, ptr(0), 0x10), 'LockRect');
    locked = true;
    const pitch = rect.readS32(), bits = rect.add(ps === 4 ? 4 : 8).readPointer();
    if (bits.isNull() || pitch < width * 4 || pitch > 65536 || pitch * height > 67108864)
      throw new Error('Invalid locked surface bounds');
    send({type:'image', stage:stage, width:width, height:height, pitch:pitch,
      format:format, msaa:msaa, metadata:metadata}, bits.readByteArray(pitch * height));
    return true;
  } catch (error) {
    emit('capture_error', {stage:stage, error:String(error), epoch:epoch});
    return false;
  } finally {
    if (locked) com(system, 14, [])(system);
    release(system); release(resolved); release(source);
  }
}
function renderState(id) {
  try { const output = Memory.alloc(4);
    checked(com(device, 58, ['uint','pointer'])(device, id, output), 'GetRenderState');
    return output.readU32(); } catch (_) { return null; }
}
function installPresent() {
  const next = ptr(config.device).readPointer();
  if (next.isNull()) return;
  device = next;
  if (presentHook) return;
  const address = device.readPointer().add(17 * ps).readPointer();
  presentHook = Interceptor.attach(address, {
    onEnter(args) {
      this.finishCapture = false;
      if (!armed || complete || !selected || !args[0].equals(device) ||
          Process.getCurrentThreadId() !== captureThread) return;
      const metadata = sample(true);
      metadata.blur = current; metadata.corner_count = corners.add(4).readU32();
      metadata.vertices_u32 = Array.from(new Uint32Array(corners.add(16).readByteArray(
        Math.min(metadata.corner_count, 2) * 192)));
      metadata.source_thread_matches_present = metadata.thread === captureThread;
      capture('final', true, metadata);
      this.finishCapture = true;
    },
    onLeave(result) {
      if (!this.finishCapture) return;
      complete = true; armed = false; corners.writeU32(0);
      emit('complete', {epoch:epoch, present_hresult:result.toInt32()});
    }
  }); listeners.push(presentHook);
  emit('device_ready', {device:device.toString(), present:address.toString(), arch:Process.arch});
}
rpc.exports = {
  start(value) {
    config = value;
    if (!config.synthetic && Process.arch !== 'ia32') throw new Error('Original must be ia32');
    for (const pin of config.pins || []) {
      if (hexBytes(pin.address, pin.bytes.length / 2) !== pin.bytes)
        throw new Error('Live instruction mismatch at ' + pin.address);
    }
    if (!config.synthetic) {
      cornerModule = new CModule(`
#include <gum/guminterceptor.h>
#include <stdint.h>
extern volatile uint32_t capture_state[];
void on_corner(GumInvocationContext *ic) {
  if (!capture_state[0] || capture_state[1] >= 2) return;
  const uint32_t *input = (const uint32_t *)(ic->cpu_context->esp + 0x2c);
  volatile uint32_t *output = capture_state + 4 + capture_state[1] * 48;
  for (uint32_t i = 0; i < 48; ++i) output[i] = input[i];
  ++capture_state[1];
}
`, {capture_state:corners});
      cornerHook = Interceptor.attach(ptr('0x40d1af'), cornerModule.on_corner);
      listeners.push(cornerHook);
    }
    blurHook = Interceptor.attach(ptr(config.blur), {
      onEnter() {
        if (!armed || complete) return;
        epoch++; const state = sample(false);
        if (state.target_raw || ++traceCount % 6 === 0) emit('trace', state);
        this.captureFrame = !selected && device && state.target_raw !== null &&
          state.target_raw !== 0 && Number.isFinite(state.envelope) && state.envelope >= 0.99;
        if (!this.captureFrame) return;
        selected = true; captureThread = Process.getCurrentThreadId();
        current = sample(true); corners.add(4).writeU32(0); corners.writeU32(1);
        capture('before', false, current);
      },
      onLeave() {
        if (!this.captureFrame) return;
        corners.writeU32(0);
        const metadata = sample(true);
        metadata.blend = {source:renderState(19), destination:renderState(20),
          factor:renderState(193), alpha_enable:renderState(27)};
        capture('after', false, metadata);
      }
    }); listeners.push(blurHook);
    armed = true;
    timer = setInterval(() => { try { installPresent(); }
      catch (error) { emit('device_error', {error:String(error)}); } }, 250);
    installPresent(); emit('armed', {synthetic:!!config.synthetic});
    return true;
  },
  stop() {
    armed = false; corners.writeU32(0); if (timer) clearInterval(timer);
    for (const listener of listeners) listener.detach();
    return {complete:complete, selected:selected, epoch:epoch};
  }
};
