"""Compare port components with authenticated AI calls on real capture inputs.

Intermediates are observed in an offline execution of the original code. Only
records whose complete native output reproduces are admitted. No output from
the capture is supplied as an input to that execution, and no native function
is replaced. This is component comparison, not a chained whole-race replay.
"""
import argparse
import collections
import json
import math
from pathlib import Path
import struct
import subprocess

from original_ai_replay import RetailCode, shadow, compare
from original_ai_recorder import validate_record
from original_recorder import EXE_SHA256, digest, write_json


class View:
    def __init__(self, uc, pages, code):
        self.uc, self.pages, self.code = uc, pages, code

    def read(self, address, size):
        # Reading an unmapped observer-only dependency must not map it or
        # modify the shadow. It is available only if the real capture saved it.
        result = bytearray()
        while size:
            n = min(size, 4096 - (address & 4095))
            try:
                part = bytes(self.uc.mem_read(address, n))
            except Exception:
                page = self.pages.get(address & ~4095, self.code.pages.get(address & ~4095))
                if page is None:
                    raise ValueError('Uncaptured observer dependency at %08X' % address)
                part = page[address & 4095:(address & 4095) + n]
            if len(part) != n:
                raise ValueError('Short observer read')
            result.extend(part); address += n; size -= n
        return bytes(result)

    def get(self, address, fmt='I'):
        values = struct.unpack('<' + fmt, self.read(address, struct.calcsize('<' + fmt)))
        return values[0] if len(values) == 1 else list(values)


def path_state(m, address):
    count = m.get(address, 'B')
    if count > 20:
        raise ValueError('Nonordinary path count')
    return dict(count=count, points=[m.get(address + 16 + 16*i, 'fff') for i in range(count)],
                route=[m.get(address + 0x150 + i, 'B') for i in range(count)],
                index=[m.get(address + 0x164 + 2*i, 'H') for i in range(count)],
                category=[m.get(address + 0x18c + i, 'B') for i in range(count)],
                length=m.get(address + 0x1ac, 'f'))


def history_error(m, address):
    count = m.get(address + 0x2e, 'B')
    bound = m.get(address + 0x78, 'f')
    if bound != 30 or count > 5:
        raise ValueError('Unsupported history layout')
    # Preserve original stores at single precision, including the integral
    # numerator/denominator. No new or reset history is substituted.
    f32 = lambda x: struct.unpack('<f', struct.pack('<f', x))[0]
    integral = (f32(m.get(address + 0x30, 'f') * count) /
                f32(m.get(address + 8, 'f') * bound)) if count else 0
    return [m.get(address + 0x7c, 'f'), f32(integral), m.get(address + 0x5c, 'f')]


def history_snapshot(m, address):
    samples = m.get(address+5,'B')
    if samples not in (4,5) or m.get(address+0x78,'f') != 30:
        raise ValueError('History snapshot layout')
    result = [samples,m.get(address+0x7c,'f')]
    for offset in (0,0x28,0x50):
        ring = address+offset
        if m.get(ring+4,'B') != 4 or m.get(ring+5,'B') != samples:
            raise ValueError('History ring layout')
        count,nxt = m.get(ring+6,'BB')
        if count>samples or nxt>=samples:
            raise ValueError('History ring bounds')
        result += [count,nxt,m.get(ring+8,'f'),m.get(ring+12,'f'),
                   *m.get(m.get(ring+16),'f'*samples), *([0]*(5-samples))]
    return result


def driver_input(m, record):
    actor,driver = record['actor'],record['regs']['ecx']
    selector = m.get(actor+0x28)
    if m.get(driver+0x1f0)!=actor or m.get(actor+0x30)!=driver or selector not in (1,3,4) or m.get(actor+0x24)!=3:
        raise ValueError('Driver links/family')
    if any(m.get(driver+o,'f')>0 for o in (0x210,0x21c)) or any(m.get(driver+o,'B') for o in (0x220,0x224,0x228)):
        raise ValueError('Active driver override/hold/reset outside ordinary API')
    if selector==1:
        engine=m.get(m.get(actor+0x34)+0x48)
        if m.get(engine+0x8c,'f')>=m.get(0x6ffff0,'f'):
            raise ValueError('Driver engine failure override')
    status=m.get(actor+0x1c)
    if status and m.get(status+0x10,'h')!=0:
        raise ValueError('Finished driver slip gate outside ordinary API')
    count=m.get(driver+0x10,'B')
    if count>25:
        raise ValueError('Driver path capacity')
    race=m.get(0x73619c);start=m.get(race+4) if race else 0
    delta=(m.get(0x73ad34)-start)&0xffffffff
    if delta>=0x80000000:delta-=0x100000000
    race_seconds=struct.unpack('<f',struct.pack('<f',delta*.0002500000118743628))[0] if start not in (0,0x7fffffff) else -1
    result=[selector,m.get(record['regs']['esp']+4,'f'),count,
            *sum((m.get(driver+0x20+16*i,'fff') for i in range(count)),[]),
            *m.get(actor+0x60,'fff'),*m.get(actor+0x70,'fff'),m.get(actor+0x3b4,'H'),
            *m.get(actor+0x3d0,'ff'),m.get(driver+0x200,'f'),m.get(driver+0x1fc,'f'),
            m.get(m.get(actor+0x40)+0x1c0,'f'),*m.get(actor+0x350,'BBBB'),
            *[m.get(actor+0x374+8*i,'f') for i in range(4)],m.get(actor+0x3b8,'f'),
            m.get(m.get(actor+0x10)+0x424,'b'),race_seconds,m.get(driver+0x20c,'B'),
            m.get(driver+0x1d0,'H'),m.get(driver+0x1dc,'f'),m.get(driver+0x1e4,'f'),
            m.get(driver+0x218,'B')]
    for offset in (0x230,0x238,0x23c,0x240):
        result+=history_snapshot(m,m.get(driver+offset))
    return result


def driver_output(m, driver):
    result=[m.get(driver+0x1d0,'H'),m.get(driver+0x1d4,'i'),m.get(driver+0x1dc,'f'),
            m.get(driver+0x1e4,'f'),m.get(driver+0x218,'B'),*m.get(driver+0x1f4,'ff')]
    for offset in (0x230,0x238,0x23c,0x240):
        result+=history_snapshot(m,m.get(driver+offset))
    return result


def st0_float(uc):
    from unicorn.x86_const import UC_X86_REG_FP0, UC_X86_REG_FPSW
    top=(uc.reg_read(UC_X86_REG_FPSW)>>11)&7
    mantissa,exponent=uc.reg_read(UC_X86_REG_FP0+top)
    sign=-1 if exponent&0x8000 else 1
    exponent&=0x7fff
    if exponent==0x7fff:
        raise ValueError('Nonfinite native floating return')
    value=sign*math.ldexp(mantissa,(exponent or 1)-16383-63)
    return struct.unpack('<f',struct.pack('<f',value))[0]


def speed_input(m, controller):
    descriptor=m.get(controller+0x3d8)
    route,point,category=m.get(descriptor,'III')
    if category>=23:
        raise ValueError('Speed category outside table')
    actor=m.get(controller+4)
    context=m.get(m.get(actor+0x14)+0x735,'b')
    if context>=10:
        raise ValueError('Speed context outside selected table')
    action=m.get(controller+0x3f8);world=m.get(0x7361f8)
    marker=m.get(controller+0x478,'i')
    marker_speeds=[]
    if marker>=0:
        event=m.get(0x736150)+32*marker
        raw=m.get(0x77af30+4*m.get(event+9,'B'))+0x27c+m.get(event+4,'H')*0x88
        marker_speeds=list(m.read(raw+0x6a,23))
    count=m.get(route+0x10,'h') if route else 0
    index=(point-route-0x27c)//0x88 if route else 0
    if route and (not 0<count<=4096 or point!=route+0x27c+index*0x88 or not 0<=index<count):
        raise ValueError('Speed route/point layout')
    row=0x779ff8+max(0,context)*0x54
    return dict(rows=[list(m.read(route+0x27c+j*0x88+0x6a,23)) for j in range(count)],
        marker=marker_speeds,point=index,category=category,direction=m.get(0x78a2fc),context=context,
        tick=m.get(0x73ad34),interval=m.get(0x77a498,'f'),scale=m.get(row+0x14,'f'),
        catchup=m.get(controller+0x4b8,'f'),amplitude=m.get(row+0x50,'B'),
        offset_tick=m.get(controller+0x400),offset=m.get(controller+0x404,'i'),rng=m.get(world+0x2d0),
        route_speed=m.get(action+0x1f0,'B'),enabled=m.get(action+0x1f1,'B'),
        action_speed=m.get(action+0x1e4,'f'),duration=m.get(action+0x1e8,'f'),auxiliary=m.get(action+0x1ec,'f'),
        debug_constant=int(bool(m.get(0x7360b8))),debug_route=int(bool(m.get(0x736090))),
        debug_auxiliary=m.get(0x6b77f8,'f'),queued_override=0)


class Observer:
    ENTRIES = {0x45fb30:'target', 0x45e370:'errors', 0x45fc90:'pedals',
               0x460100:'slip-gate', 0x45c370:'continue', 0x45e970:'driver-step',
               0x459b30:'fitness', 0x44c550:'speed-publication',0x459980:'publish'}

    def __init__(self, record, pages, code, speed_publication=False):
        self.record, self.pages, self.code = record, pages, code
        self.pending, self.cases, self.unsupported = [], [], []
        self.target = None
        self.cadence=None
        self.speed_publication=speed_publication

    def __call__(self, uc, address):
        from unicorn import x86_const as x
        m = View(uc, self.pages, self.code)
        reg = lambda name: uc.reg_read(getattr(x, 'UC_X86_REG_' + name))
        sp = reg('ESP')
        if address==0x44b56d:
            try:
                controller=reg('EBX');actor=m.get(controller+4);world=m.get(0x7361f8)
                first,last=m.get(world+0x1c,'II');count=last-first
                if actor!=self.record['actor'] or not 0<count<=3:
                    raise ValueError('Cadence actor/world range')
                slots=[i for i in range(count) if m.get(world+0x28+4*(first+i))==actor]
                if len(slots)!=1:
                    raise ValueError('Cadence rival slot identity')
                self.cadence=dict(kind='cadence',input=[m.get(0x6f08a4),m.get(0x73ad34),m.get(controller+0x4c0),
                                                      slots[0],count,int(bool(m.get(controller+0x3f8)))])
            except (ValueError,struct.error) as exc:
                self.unsupported.append(dict(kind='cadence',reason=str(exc)));self.cadence=None
        if address in (0x44b5fc,0x44b715) and self.cadence is not None:
            self.cadence['expected']=[int(address==0x44b5fc)]
            self.cases.append(self.cadence);self.cadence=None
        if address==0x44c578:
            for parent in reversed(self.pending):
                if parent['kind']=='speed-publication':
                    try:parent['input']=speed_input(m,parent['controller'])
                    except (ValueError,struct.error) as exc:parent['mapping_error']=str(exc)
                    break
        if address == 0x45c640:
            for parent in reversed(self.pending):
                if parent['kind'] == 'continue':
                    # 45C370 resolves sentinel99999 through45C550. The
                    # lower-level port API consumes the resolved value.
                    step = m.get(sp + 4, 'f')
                    if 'resolved_step' in parent and parent['resolved_step'] != step:
                        raise ValueError('Offset step changed within continuation')
                    parent['resolved_step'] = step
                    parent['input']['params'][0] = step
                    break
        # Return PC and restored caller stack distinguish recursive calls at
        # the same return address. Observe, never alter registers or memory.
        while self.pending and self.pending[-1]['return'] == address and sp > self.pending[-1]['sp']:
            call = self.pending.pop()
            try:
                kind = call['kind']
                if kind == 'target':
                    call['expected'] = m.get(call['driver'] + 0x1f4, 'ff')
                    self.target = call
                    self.cases.append(call)
                elif kind == 'errors':
                    if self.target is None:
                        raise ValueError('Missing target producer')
                    call['input'] = self.target['input']
                    call['expected'] = [m.get(p, 'f') for p in call['outputs'][:2]]
                    self.cases.append(call)
                elif kind == 'slip-gate':
                    for parent in reversed(self.pending):
                        if parent['kind'] == 'pedals':
                            value = reg('EAX') & 255
                            if value not in (0, 1):
                                raise ValueError('Invalid slip gate')
                            parent['gates'].append(value)
                            break
                elif kind == 'pedals':
                    if len(set(call['gates'])) > 1:
                        raise ValueError('Slip gate changed inside one calculation')
                    call['input'][-1] = call['gates'][0] if call['gates'] else 0
                    call['expected'] = [m.get(p, 'f') for p in call['outputs']]
                    self.cases.append(call)
                elif kind == 'continue':
                    if 'resolved_step' not in call:
                        raise ValueError('No path emission; continuation not an acceptance case')
                    call['expected'] = path_state(m, call['path'])
                    self.cases.append(call)
                elif kind=='publish':
                    p=call['path']
                    call['expected']=[*flatten_path(path_state(m,p)),m.get(p+0x1a0,'B'),m.get(p+0x1a4),m.get(p+0x1a8)]
                    self.cases.append(call)
                elif kind == 'driver-step':
                    call['expected']=driver_output(m,call['driver'])
                    self.cases.append(call)
                elif kind == 'fitness':
                    call['expected']=[st0_float(uc),m.get(call['path']+0x1a0,'B'),m.get(call['path']+0x1a4)]
                    self.cases.append(call)
                elif kind=='speed-cap':
                    call['parent']['cap']=st0_float(uc)
                elif kind=='override-gate':
                    call['parent']['input']['queued_override']=reg('EAX')&255
                elif kind=='speed-publication':
                    if call.get('mapping_error'):
                        raise ValueError(call['mapping_error'])
                    if 'input' not in call or 'cap' not in call:
                        raise ValueError('Missing speed input/cap observation')
                    call['input']['cap']=call['cap']
                    driver=call['driver'];c=call['controller'];world=m.get(0x7361f8)
                    call['expected']=[*m.get(driver+0x200,'fff'),m.get(driver+0x20c,'B'),
                                      m.get(c+0x400),m.get(c+0x404,'i'),m.get(world+0x2d0)]
                    self.cases.append(call)
            except (ValueError, struct.error, ZeroDivisionError, OverflowError) as exc:
                self.unsupported.append(dict(kind=call['kind'], reason=str(exc)))
        for parent in reversed(self.pending):
            if parent['kind']=='speed-publication':
                if address==parent['cap_entry'] or (address==0x421a30 and 'input' in parent):
                    self.pending.append(dict(kind='speed-cap' if address==parent['cap_entry'] else 'override-gate',
                                             sp=sp,**{'return':m.get(sp)},parent=parent))
                    return
                break
        if address not in self.ENTRIES:
            return
        kind = self.ENTRIES[address]
        if kind=='speed-publication' and not self.speed_publication:
            return
        try:
            call = dict(kind=kind, sp=sp, return_address=address)
            call['return'] = m.get(sp)
            if kind in ('target', 'errors'):
                driver = reg('EAX') if kind == 'target' else reg('ESI')
                call['driver'] = driver
                actor = m.get(driver + 0x1f0)
                if actor != self.record['actor']:
                    raise ValueError('Driver actor differs from capture identity')
                if kind == 'target':
                    physics, controller = m.get(actor + 0x28), m.get(actor + 0x24)
                    if physics not in (1, 3, 4) or controller == 4:
                        raise ValueError('Nonordinary target family')
                    count = m.get(driver + 0x10, 'B')
                    if count > 25:
                        raise ValueError('Target path ceiling')
                    call['input'] = [count, *sum((m.get(driver + 0x20 + 16*i, 'fff') for i in range(count)), []),
                                     *m.get(actor + 0x60, 'fff'), *m.get(actor + 0x70, 'fff'),
                                     m.get(actor + 0x3b4, 'H'), *m.get(actor + 0x3d0, 'ff'),
                                     m.get(m.get(actor + 0x10) + 0x424, 'b')]
                else:
                    call['outputs'] = m.get(sp + 4, 'III')
            elif kind == 'pedals':
                driver = reg('EDI'); actor = m.get(driver + 0x1f0)
                selector = m.get(actor + 0x28)
                if actor != self.record['actor'] or selector not in (1,3,4):
                    raise ValueError('Pedals require captured ordinary actor')
                if selector == 1:
                    engine = m.get(m.get(actor + 0x34) + 0x48)
                    if m.get(engine + 0x8c,'f') >= m.get(0x6ffff0,'f'):
                        raise ValueError('Engine failure override outside pedal API')
                call['input'] = [m.get(driver + 0x200, 'f'), m.get(actor + 0x3d4, 'f'),
                                 m.get(driver + 0x1fc, 'f'),
                                 *history_error(m, m.get(driver + 0x23c)),
                                 *history_error(m, m.get(driver + 0x240)),
                                 m.get(driver + 0x1dc, 'f'), m.get(driver + 0x1e4, 'f'),
                                 reg('EDX'), m.get(driver + 0x220, 'B'), 0]
                call['outputs'] = m.get(sp + 4, 'II'); call['gates'] = []
            elif kind == 'continue':
                path, ctx, margin_bits, mode, target_bits, step_bits = m.get(sp + 4, 'IIIIII')
                f = lambda bits: struct.unpack('<f', struct.pack('<I', bits))[0]
                actor = m.get(ctx)
                if actor != self.record['actor']:
                    raise ValueError('Path actor identity')
                category = m.get(ctx + 0x394, 'i'); selected = m.get(ctx + 0x388, 'b')
                if category not in range(5) or selected not in range(16):
                    raise ValueError('Nonordinary path category/context')
                entry = ctx + 8 + selected*0x38
                chain = m.get(ctx + 0x3a8); count = m.get(chain + 4)
                if not 0 < count <= 64:
                    raise ValueError('Forward chain ceiling')
                roads = []
                for i in range(count):
                    cell = m.get(chain + 8 + 4*i)
                    if m.get(cell) != 0x6c8f90:
                        raise ValueError('Nonforward compact cell')
                    route, index = m.get(cell + 0x5c, 'b'), m.get(cell + 0x5e, 'h')
                    if route < 0 or index < 0:
                        raise ValueError('Negative road identity')
                    raw = m.get(0x77af30 + 4*route) + 0x27c + index*0x88
                    roads.append([route, index, *[m.get(raw + a, 'f') for a in (0,4,8,12,16,0x2c,0x30)],
                                  m.get(raw + 0x3c + 2*category, 'h')/256,
                                  *[m.get(cell + a, 'f') for a in (4,0x20,0x3c)],
                                  m.get(raw + 0x24, 'f'), m.get(raw + 0x28, 'f'), m.get(cell + 0x58, 'f')])
                call.update(path=path, input=dict(category=category, mode=mode,
                    params=[f(step_bits), f(target_bits), m.get(entry + 8, 'f'), f(margin_bits)],
                    actor=[*m.get(actor+0x60,'fff'), *m.get(actor+0x70,'fff'),
                           *m.get(actor+0x90,'fff'), m.get(actor+0x3d4,'f')],
                    selected=m.get(entry+0x30), initial_offset=m.get(entry+0x14,'f'),
                    roads=roads, before=path_state(m,path)))
            elif kind == 'driver-step':
                if self.record['kind']!='driver':
                    raise ValueError('Nested driver outside captured entry')
                call.update(driver=reg('ECX'),input=driver_input(m,self.record))
            elif kind == 'fitness':
                path,actor=m.get(sp+4,'II')
                if actor!=self.record['actor']:
                    raise ValueError('Fitness actor identity')
                p=path_state(m,path)
                call.update(path=path,input=[*flatten_path(p),m.get(path+0x1a0,'B'),
                    m.get(path+0x1a4),m.get(path+0x1b0,'B'),*m.get(actor+0x60,'fff'),
                    *m.get(actor+0x70,'fff'),*m.get(actor+0x3d0,'ff'),m.get(0x73ad34),
                    *[m.get(a,'f') for a in (0x77a4a8,0x6b7520,0x6b7524,0x6b7528,0x6b752c,0x6b7530)]])
            elif kind=='speed-publication':
                controller=m.get(sp+4)
                if m.get(controller+4)!=self.record['actor']:
                    raise ValueError('Speed publication actor identity')
                strategy=m.get(controller+0x3fc)
                call.update(controller=controller,driver=m.get(controller+8),cap_entry=m.get(m.get(strategy)+0x10))
            elif kind=='publish':
                p=reg('EAX')
                call.update(path=p,input=[*flatten_path(path_state(m,p)),m.get(p+0x1a0,'B'),m.get(p+0x1a4),m.get(p+0x1a8)])
            self.pending.append(call)
        except (ValueError, struct.error, ZeroDivisionError, OverflowError) as exc:
            self.unsupported.append(dict(kind=kind, reason=str(exc)))


PORT_SOURCE = r'''#include "nfsu/ai_action.h"
#include <iostream>
#include <iomanip>
#include <stdexcept>
#include <vector>
using namespace nfsu;
void read_history(AiDriverHistory& h){AiDriverHistorySnapshot s;unsigned n;std::cin>>n>>s.current;s.samples=n;
for(auto* r:{&s.time,&s.integral,&s.derivative}){unsigned count,next;std::cin>>count>>next>>r->sum>>r->average;r->count=count;r->next=next;for(auto& x:r->values)std::cin>>x;}
if(!h.restore(s))throw std::runtime_error("History restore refused");}
void emit_history(const AiDriverHistory& h){auto s=h.snapshot();std::cout<<' '<<unsigned(s.samples)<<' '<<s.current;
for(auto* r:{&s.time,&s.integral,&s.derivative}){std::cout<<' '<<unsigned(r->count)<<' '<<unsigned(r->next)<<' '<<r->sum<<' '<<r->average;for(float x:r->values)std::cout<<' '<<x;}}
void read_path(AiPlannedPath& p){unsigned n;std::cin>>n;if(n>20)throw std::runtime_error("Path ceiling");p.path.count=n;
for(unsigned i=0;i<n;++i){for(auto& x:p.path.points[i])std::cin>>x;unsigned r,k,c;std::cin>>r>>k>>c;p.route[i]=r;p.point[i]=k;p.category[i]=c;}std::cin>>p.length;}
void emit_path(const AiPlannedPath& p){std::cout<<unsigned(p.path.count);for(unsigned i=0;i<p.path.count;++i){for(float v:p.path.points[i])std::cout<<' '<<v;
std::cout<<' '<<unsigned(p.route[i])<<' '<<p.point[i]<<' '<<unsigned(p.category[i]);}std::cout<<' '<<p.length;}
int main(){try{std::cout<<std::setprecision(17);unsigned cases=0;std::string kind;
while(std::cin>>kind){bool ok=false;
if(kind=="publish"){AiPlannedPath p;read_path(p);std::cin>>p.cursor>>p.cursor_tick>>p.prediction_tick;
ok=ai_path_publish(p);std::cout<<int(ok)<<' ';emit_path(p);std::cout<<' '<<p.cursor<<' '<<p.cursor_tick<<' '<<p.prediction_tick;
}else if(kind=="cadence"){std::uint32_t frame,tick,previous;unsigned slot,count,have;bool full=false;
std::cin>>frame>>tick>>previous>>slot>>count>>have;ok=ai_rival_full_update(frame,tick,previous,slot,count,have!=0,full);
std::cout<<int(ok)<<' '<<int(full);
}else if(kind=="speed-publication"){AiSpeedPolicyInput in;AiSpeedPolicyState state;AiSpeedPublication out;Route route;RoutePoint marker;unsigned n,has_marker,queued,route_speed,enabled,dc,dr;std::uint32_t random;
std::cin>>n;if(n>4096)throw std::runtime_error("Speed route ceiling");route.points.resize(n);
for(auto& p:route.points)for(auto& s:p.speed){unsigned x;std::cin>>x;s=x;}std::cin>>has_marker;
if(has_marker)for(auto& s:marker.speed){unsigned x;std::cin>>x;s=x;}
std::cin>>in.point>>in.category>>in.direction>>in.context>>in.tick>>in.offset_interval>>in.context_scale>>in.catchup_scale>>in.strategy_cap>>in.offset_amplitude;
std::cin>>state.offset_tick>>state.point_offset>>random>>route_speed>>enabled>>dc>>dr>>queued;
std::cin>>in.action_speed>>in.duration>>in.auxiliary>>in.debug_auxiliary;
in.route=n?&route:nullptr;in.marker_point=has_marker?&marker:nullptr;in.route_speed=route_speed!=0;in.enabled=enabled!=0;
in.debug_constant=dc!=0;in.debug_route=dr!=0;in.queued_override=queued!=0;
ok=ai_action_publish_speed(in,state,random,out);std::cout<<int(ok)<<' '<<out.speed<<' '<<out.duration<<' '<<out.auxiliary<<' '<<int(out.enabled)<<' '<<state.offset_tick<<' '<<state.point_offset<<' '<<random;
}else if(kind=="driver-step"){AiDriverForwardInput in;AiDriverForwardState out;unsigned family,n,h,enabled,word,ready;float dt;
std::cin>>family>>dt>>n;if(n>25)throw std::runtime_error("Driver path count");in.path.count=n;
for(unsigned i=0;i<n;++i)for(auto& x:in.path.points[i])std::cin>>x;for(auto& x:in.position)std::cin>>x;for(auto& x:in.velocity)std::cin>>x;
std::cin>>h>>in.forward_speed>>in.absolute_speed>>in.desired_speed>>in.throttle_cap>>in.rear_drive_fraction;in.heading=h;
for(auto& b:in.contact){unsigned x;std::cin>>x;b=x!=0;}for(auto& x:in.wheel_slip)std::cin>>x;
std::cin>>in.route_distance>>in.category>>in.race_seconds>>enabled>>word>>out.pedals.throttle>>out.pedals.brake>>ready;
in.enabled=enabled!=0;out.steering.word=word;out.direction_ready=ready!=0;
for(auto* v:{&out.steering.heading,&out.steering.velocity_heading,&out.speed,&out.slip})read_history(*v);
ok=family==1?ai_driver_drag_step(in,dt,out):ai_driver_ordinary_step(in,dt,out);
std::cout<<int(ok)<<' '<<out.steering.word<<' '<<out.gear_request<<' '<<out.pedals.throttle<<' '<<out.pedals.brake<<' '<<int(out.direction_ready)<<' '<<out.target.point[0]<<' '<<out.target.point[1];
for(auto* v:{&out.steering.heading,&out.steering.velocity_heading,&out.speed,&out.slip})emit_history(*v);
}else if(kind=="fitness"){AiPlannedPath p;read_path(p);AiPathFitnessInput in;unsigned transition;
std::cin>>p.cursor>>p.cursor_tick>>transition;in.transition_end=transition;
for(auto& x:in.position)std::cin>>x;for(auto& x:in.velocity)std::cin>>x;
std::cin>>in.signed_speed>>in.absolute_speed>>in.tick;for(auto& x:in.retain_parameters)std::cin>>x;
std::cin>>in.minimum_time>>in.initial_time>>in.lateral_acceleration_limit;float score=0;
ok=ai_path_fitness(p,in,score);std::cout<<int(ok)<<' '<<score<<' '<<p.cursor<<' '<<p.cursor_tick;
}else if(kind=="target"||kind=="errors"){AiDriverPath path;unsigned n,h;int cat;AiDriverPoint p,v;float speed,absolute;
std::cin>>n;if(n>25)throw std::runtime_error("Target count");path.count=n;
for(unsigned i=0;i<n;++i)for(auto& x:path.points[i])std::cin>>x;for(auto& x:p)std::cin>>x;for(auto& x:v)std::cin>>x;
std::cin>>h>>speed>>absolute>>cat;AiDriverTarget out;ok=ai_driver_target(path,p,v,h,speed,absolute,cat,out);
std::cout<<int(ok)<<' ';if(kind=="target")std::cout<<out.point[0]<<' '<<out.point[1];else std::cout<<out.heading_error<<' '<<out.velocity_error;
}else if(kind=="pedals"){AiDriverPedalInput in;AiDriverPedalState out;unsigned special,limit;
std::cin>>in.desired_speed>>in.absolute_speed>>in.throttle_cap>>in.speed_error.current>>in.speed_error.integral>>in.speed_error.derivative;
std::cin>>in.slip_error.current>>in.slip_error.integral>>in.slip_error.derivative>>out.throttle>>out.brake>>in.mode>>special>>limit;
in.special=special!=0;in.slip_limit=limit!=0;ok=ai_driver_pedals(in,out);std::cout<<int(ok)<<' '<<out.throttle<<' '<<out.brake;
}else if(kind=="continue"){AiPathExtension in;unsigned selected,n;float offset;std::cin>>in.category>>in.mode>>selected>>offset;
std::cin>>in.offset_step>>in.target_offset>>in.clearance>>in.margin;for(auto& x:in.actor_position)std::cin>>x;
for(auto& x:in.actor_velocity)std::cin>>x;for(auto& x:in.actor_forward)std::cin>>x;std::cin>>in.absolute_speed>>n;
if(n>64||in.category<0||in.category>4)throw std::runtime_error("Road ceiling");std::vector<AiPathRoadPoint> roads(n);
for(auto& r:roads){unsigned route,index;float offset,low,high;auto& p=r.point;std::cin>>route>>index>>p.position[0]>>p.position[1]>>p.height>>p.forward[0]>>p.forward[1]>>p.unknown_2c>>p.unknown_30;
std::cin>>p.driving_offset[in.category]>>offset>>low>>high>>p.unknown_24>>p.unknown_28>>r.distance;r.offset[0]=offset;r.lower[0]=low;r.upper[0]=high;p.route_index=route;r.point_index=index;}
AiPlannedPath p;read_path(p);ok=ai_path_continue(roads.data(),roads.size(),selected,offset,in,p);std::cout<<int(ok)<<' ';emit_path(p);
}else throw std::runtime_error("Unknown component");
if(!std::cin)throw std::runtime_error("Truncated input");std::cout<<'\n';++cases;}
if(!cases)throw std::runtime_error("No cases");return 0;
}catch(const std::exception& e){std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}}
'''


def flatten_path(p):
    return [p['count'], *sum(([ *p['points'][i], p['route'][i], p['index'][i], p['category'][i]]
                            for i in range(p['count'])), []), p['length']]


def flatten(case):
    if case['kind']=='speed-publication':
        p=case['input']
        return ([len(p['rows']),*sum(p['rows'],[]),int(bool(p['marker'])),*p['marker'],p['point'],p['category'],
                 p['direction'],p['context'],p['tick'],p['interval'],p['scale'],p['catchup'],p['cap'],p['amplitude'],
                 p['offset_tick'],p['offset'],p['rng'],p['route_speed'],p['enabled'],p['debug_constant'],p['debug_route'],
                 p['queued_override'],p['action_speed'],p['duration'],p['auxiliary'],p['debug_auxiliary']],case['expected'])
    if case['kind'] != 'continue':
        return case['input'], case['expected']
    p = case['input']
    return ([p['category'], p['mode'], p['selected'], p['initial_offset'], *p['params'],
             *p['actor'], len(p['roads']), *sum(p['roads'], []), *flatten_path(p['before'])],
            flatten_path(case['expected']))


def load_session(session):
    manifest = json.loads((session/'manifest.json').read_text('ascii'))
    names = {'metadata.json','steps.jsonl','events.jsonl','summary.json'}
    if set(manifest) != names:
        raise ValueError('Unexpected capture manifest')
    for name, pin in manifest.items():
        p = session/name
        if p.stat().st_size != pin['bytes'] or digest(p) != pin['sha256']:
            raise ValueError('Capture integrity failure: '+name)
    metadata = json.loads((session/'metadata.json').read_text('ascii'))
    if metadata.get('schema') != 'nfsu-ai-decisions-1' or metadata.get('source',{}).get('sha256') != EXE_SHA256:
        raise ValueError('Unsupported capture identity')
    records = []
    for line in (session/'steps.jsonl').read_text('ascii').splitlines():
        r = json.loads(line); validate_record(r,len(records)); records.append(r)
    if not records:
        raise ValueError('Empty capture')
    return metadata, records


def captured_pages(record, code):
    pages={}
    for address,value in record['input_pages']:
        blob=bytes.fromhex(value)
        if address in pages or address in code.pages or address%4096 or len(blob)!=4096:
            raise ValueError('Invalid/duplicate/executable capture page')
        pages[address]=blob
    return pages


def build_probe(build, out, compiler):
    repo = Path(__file__).resolve().parents[1]
    cache = build/'CMakeCache.txt'
    homes = [s.split('=',1)[1] for s in cache.read_text('utf-8').splitlines() if s.startswith('CMAKE_HOME_DIRECTORY:INTERNAL=')]
    if len(homes) != 1 or Path(homes[0]).resolve() != repo:
        raise ValueError('Build belongs to a different checkout')
    result = subprocess.run(['cmake','--build',str(build),'--target','nfsu_ai_driver','-j','5'],capture_output=True,text=True)
    (out/'port-library-build.log').write_text(result.stdout+result.stderr,encoding='ascii',errors='backslashreplace')
    if result.returncode:
        raise RuntimeError('Port library build failed')
    source, exe = out/'port-probe.cpp',out/'port-probe.exe'
    source.write_text(PORT_SOURCE,encoding='ascii')
    libraries = [build/('libnfsu_'+n+'.a') for n in ('ai_driver','race_route','track_zones','track_visibility','track','core','image','scene','racing_line','vehicle','sha256','drift')]
    cmd = [compiler,'-std=c++20','-ffp-contract=off','-I',str(repo/'include'),str(source),
           '-Wl,--start-group',*map(str,libraries),'-Wl,--end-group','-o',str(exe)]
    result = subprocess.run(cmd,capture_output=True,text=True)
    (out/'port-probe-build.log').write_text(result.stdout+result.stderr,encoding='ascii',errors='backslashreplace')
    if result.returncode or not exe.is_file():
        raise RuntimeError('Port probe build failed')
    return exe, dict(libraries={p.name:digest(p) for p in libraries},cache_sha256=digest(cache),
                     probe_sha256=digest(exe),source_sha256=digest(source),
                     source_files={str(p.relative_to(repo)).replace('\\','/'):digest(p)
                                   for folder in ('src','include/nfsu') for p in (repo/folder).glob('ai_*') if p.is_file()})


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('session',type=Path)
    parser.add_argument('--retail',required=True,type=Path)
    parser.add_argument('--out',required=True,type=Path)
    parser.add_argument('--build',required=True,type=Path)
    parser.add_argument('--compiler',default='c++')
    parser.add_argument('--speed-publication',action='store_true',help='Attempt full-route speed mapping; missing uncaptured route pages remain unsupported')
    args = parser.parse_args()
    if args.out.exists() or args.out.resolve().is_relative_to(args.retail.resolve()):
        raise ValueError('Fresh non-retail output directory required')
    metadata, records = load_session(args.session)
    code = RetailCode(args.retail/'speed.exe')
    args.out.mkdir(parents=True)
    native, cases, unsupported = [], [], []
    for record in records:
        pages = captured_pages(record,code)
        def read(a):
            if a not in pages:
                raise RuntimeError('Uncaptured dependency at %08X'%a)
            return pages[a]
        observer = Observer(record,pages,code,args.speed_publication)
        result = shadow(code,record,read,seconds=15,observer=observer)
        verified = compare(result,record)
        native.append(dict(seq=record['seq'],kind=record['kind'],**verified))
        # Raw native mismatches remain failures. Even apparently harmless
        # padding differences do not authorize comparing intermediate states.
        if not verified['passed']:
            continue
        if observer.pending:
            unsupported.append(dict(seq=record['seq'],reason='Unreturned observed component'))
            continue
        unsupported.extend(dict(seq=record['seq'],**v) for v in observer.unsupported)
        for c in observer.cases:
            if c['kind'] == 'slip-gate':
                continue
            c.update(seq=record['seq'],actor_id=record['actor_id'])
            fx=bytes.fromhex(record['fxsave'])
            if (struct.unpack_from('<H',fx)[0] != 0x7f or
                struct.unpack_from('<I',fx,24)[0]&0xffc0 != 0x1f80):
                unsupported.append(dict(seq=record['seq'],kind=c['kind'],reason='Unsupported x87/SSE mode'))
            else:
                cases.append(c)
    write_json(args.out/'observations.json',dict(cases=cases,unsupported=unsupported,native=native))
    if not cases:
        raise ValueError('No admitted component cases')
    exe, identity = build_probe(args.build.resolve(),args.out.resolve(),args.compiler)
    rows, expected = [], []
    for c in cases:
        values, want = flatten(c)
        if not all(math.isfinite(v) for v in values + want):
            raise ValueError('Nonfinite component state')
        rows.append(c['kind']+' '+' '.join(format(v,'.17g') for v in values)); expected.append(want)
    (args.out/'port-input.txt').write_text('\n'.join(rows)+'\n',encoding='ascii')
    result = subprocess.run([str(exe)],input='\n'.join(rows)+'\n',capture_output=True,text=True)
    (args.out/'port-output.txt').write_text(result.stdout+result.stderr,encoding='ascii',errors='backslashreplace')
    if result.returncode or result.stderr or len(result.stdout.splitlines()) != len(cases):
        raise RuntimeError('Port did not produce complete fresh output')
    failures, counts = [], collections.Counter()
    worst = collections.defaultdict(float)
    for c,want,line in zip(cases,expected,result.stdout.splitlines()):
        got = [float(v) for v in line.split()]
        if not got or got[0] not in (0,1) or not all(math.isfinite(v) for v in got):
            raise ValueError('Malformed port output')
        differences = []
        if got[0] != 1 or len(got) != len(want)+1:
            differences.append(dict(refused=got[0]!=1,expected_fields=len(want),actual_fields=len(got)-1))
        else:
            for i,(a,b) in enumerate(zip(got[1:],want)):
                delta = abs(a-b); worst[c['kind']] = max(worst[c['kind']],delta)
                if delta > 2e-5 + 2e-6*abs(b):
                    differences.append(dict(field=i,port=a,original=b))
        counts[c['kind']]+=1
        if differences:
            failures.append(dict(seq=c['seq'],actor_id=c['actor_id'],kind=c['kind'],differences=differences))
    report = dict(capture_manifest_sha256=digest(args.session/'manifest.json'),
        synthetic_fixture=bool(metadata.get('synthetic_fixture')),records=len(records),
        native_passed=sum(v['passed'] for v in native),native_failures=[v for v in native if not v['passed']],
        components=dict(counts),component_failures=failures,unsupported=unsupported,
        cadence_branches=dict(collections.Counter('full' if c['expected'][0] else 'retained'
                                                  for c in cases if c['kind']=='cadence')),
        optional_speed_publication=args.speed_publication,
        max_abs_difference=dict(worst),port=identity,tolerance='2e-5 + 2e-6*abs(original)',
        scope='Isolated port components on observed intermediates from native-verified original replay. Not whole controller or chained race parity.',
        passed=not failures and not unsupported and all(v['passed'] for v in native))
    write_json(args.out/'report.json',report)
    print(json.dumps({k:v for k,v in report.items() if k not in ('port','component_failures','unsupported')},ensure_ascii=True))
    print(json.dumps(dict(component_failure_count=len(failures),unsupported_count=len(unsupported))))
    return 0 if report['passed'] else 1


if __name__ == '__main__':
    raise SystemExit(main())
