// Relevant world call boundaries and per-step state; no shadow emulation/waits.
'use strict';
const entries = [
    ['rival-controller',0x462430,'53568bf18b46048b'],
    ['ai-driver',0x45e970,'83ec3cd944244089'],
    ['human-driver',0x4615e0,'51894c24008b816c'],
    ['physics1',0x4774a0,'558bec83e4f083ec'],
    ['physics3',0x474a90,'558bec83e4f883ec'],
    ['rigid-integrate',0x46aa40,'81ec340200005355'],
    ['path-update',0x45ae10,'538b5c240c55568b'],
    ['driving-controls',0x45b150,'558bec83e4f081ec'],
    ['route-planner',0x44b1b0,'558bec83e4f06aff'],
    ['road-query-a',0x44bd10,'83ec3056578bf88b'],
    ['road-query-b',0x44c010,'8b44241083ec1053'],
    ['world-random-int',0x4216b0,'8b8fd002000033d2'],
    ['world-random-float',0x4216f0,'518b8ed002000057'],
    ['human-controls',0x461a60,'83ec0c8b4424108b'],
    ['transmission-state',0x46dac0,'d9442404ba030000'],
    ['drag-input-event',0x4605e0,'a18c60730085c00f'],
    ['action-selection',0x44dd30,'6aff68d865680064'],
    ['gear-request',0x46e5b0,'83ec0c558b6c2414'],
    ['launch-window',0x46e940,'a044a3780084c075'],
    ['human-steering-event',0x461840,'8b4424082d870000'],
    ['human-throttle-event',0x4618b0,'8b4424082d890000'],
    ['human-brake-event',0x461da0,'8b4c240881e98c00'],
    ['human-input-8f-event',0x4619c0,'8b4424082d8f0000'],
    ['human-input-91-event',0x461e10,'8b4424082d910000'],
    ['human-shift-up-event',0x461e70,'8a44240c84c00f84'],
    ['human-shift-down-event',0x461f40,'8a44240c84c00f84'],
    ['human-direct-input-event',0x461ff0,'8b4c241083b98402']
];
const EVENT_WORDS=40, SNAP_WORDS=1344, EVENT_CAP=16384, SNAP_CAP=1024;
const control=Memory.alloc(128), calls=Memory.alloc(EVENT_CAP*EVENT_WORDS*4);
const snapshots=Memory.alloc(SNAP_CAP*SNAP_WORDS*4), helpers=Memory.alloc(Process.pageSize);
Memory.protect(helpers,Process.pageSize,'rwx');
// Integer-only native helpers; no JavaScript callback runs on the game thread.
helpers.writeByteArray([0x64,0xa1,0x24,0,0,0,0xc3]); // TEB thread id
helpers.add(16).writeByteArray([0x0f,0xae,0xe8,0x0f,0x31,0xc3]); // lfence; rdtsc
helpers.add(32).writeByteArray([0x83,0xec,8,0xd9,0x3c,0x24,0x0f,0xae,0x5c,0x24,4,
    0x0f,0xb7,0x04,0x24,0x8b,0x54,0x24,4,0x83,0xc4,8,0xc3]);
helpers.add(64).writeByteArray([0x64,0xa1,0x18,0,0,0,0xc3]);
let module=null, hooks=[], timer=null, batch=0, acknowledged=0, fixture=false;
const keep=[];
const source=`
#include <gum/guminterceptor.h>
#include <stdint.h>
extern volatile uint32_t state[];
extern uint32_t call_ring[];
extern uint32_t snapshot_ring[];
extern uint32_t native_tid(void);
extern uint64_t native_cycles(void);
extern uint64_t native_fp(void);
extern uint32_t native_teb(void);
extern uint32_t __attribute__((stdcall)) native_query(const void *,void *,uint32_t);
#define EWORDS 40
#define SWORDS 1344
#define ECAP 16384
#define SCAP 1024
#define U(a) (*(uint32_t *)(uintptr_t)(a))
#define actor_now state[12]
#define parent_now state[13]
#define depth state[14]
#define role_now state[16]
typedef struct { uint32_t ok,actor,role,kind,call,parent,previous_actor,previous_parent;
    uint32_t object,caller,args[4],previous_role; } Invocation;
static int pointer(uint32_t p) {return p>=0x10000 && p<0x7fff0000;}
static int readable(uint32_t p,uint32_t n) {
    uint32_t teb=native_teb(),error=U(teb+0x34),current=p,end=p+n,info[7],next;int result=1;
    if(!pointer(p)||!n||p+n<p)return 0;
    while(current<end) {
        if(native_query((const void *)(uintptr_t)current,info,sizeof(info))!=sizeof(info)||
            info[4]!=0x1000||(info[5]&0x100)||!(info[5]&0xee)) {result=0;break;}
        next=info[0]+info[3];if(next<=current) {result=0;break;}current=next;
    }
    U(teb+0x34)=error;
    if(!result)state[15]++;return result;
}
static uint32_t stage(void) {uint32_t p=U(0x73619c);return pointer(p)?U(p):0xffffffff;}
static void overflow(void) {state[5]++;state[0]=0;}
static uint32_t role_of(uint32_t actor) {
    uint32_t config;if(!readable(actor,0x18))return 0;config=U(actor+0x14);
    if(!readable(config,0x738))return 0;return U(config+4);
}
static uint32_t owner(uint32_t kind,GumInvocationContext *ic) {
    uint32_t object=ic->cpu_context->ecx,actor=0,selector;
    if(kind==0) {if(!readable(object,8))return 0;actor=U(object+4);
        if(!readable(actor,0x38)||U(actor+0x2c)!=object)return 0;}
    else if(kind==1||kind==2) {if(!readable(object,kind==2?0x270:0x1f4))return 0;actor=U(object+(kind==2?0x26c:0x1f0));
        if(!readable(actor,0x38)||U(actor+0x30)!=object)return 0;}
    else if(kind==3||kind==4) {if(!readable(object,kind==3?0x60:0x38))return 0;selector=kind==3?1:3;
        actor=U(object+(selector==1?0x5c:0x34));
        if(!readable(actor,0x38)||U(actor+0x34)!=object||U(actor+0x28)!=selector)return 0;}
    else if(kind==15||kind>=19) {
        object=(uint32_t)(uintptr_t)gum_invocation_context_get_nth_argument(ic,3);
        if(!readable(object,kind>=19?0x270:0x1f4))return 0;actor=U(object+(kind>=19?0x26c:0x1f0));
        if(!readable(actor,0x38)||U(actor+0x30)!=object)return 0;
    } else if(kind==5&&!actor_now) {
        object=(uint32_t)(uintptr_t)gum_invocation_context_get_nth_argument(ic,0);
        if(!pointer(object)||object<0x10008)return 0;object-=8;actor=U(object+0x5c);
        if(!pointer(actor)||U(actor+0x34)!=object||U(actor+0x28)!=1)return 0;
    } else actor=actor_now;
    return actor;
}
static uint32_t record(Invocation *d,GumInvocationContext *ic,uint32_t phase) {
    uint32_t i,*p,seq,head=state[1];uint64_t cycles,fp;GumCpuContext *c=ic->cpu_context;
    if(head-state[2]>=ECAP) {overflow();return 0;}
    p=call_ring+(head%ECAP)*EWORDS;for(i=0;i<EWORDS;i++)p[i]=0;
    seq=++state[8];cycles=native_cycles();fp=native_fp();
    p[0]=seq;p[1]=d->call;p[2]=d->kind;p[3]=phase;p[4]=d->actor;
    p[5]=(uint32_t)(int32_t)*(int16_t *)(uintptr_t)(d->actor+0xa);p[6]=d->role;
    p[7]=d->object;p[8]=U(0x7364dc);p[9]=U(0x73ad34);p[10]=U(0x6f0738);
    p[11]=stage();p[12]=state[7];p[13]=depth;p[14]=d->parent;p[15]=d->caller;
    p[16]=c->eax;p[17]=c->ecx;p[18]=c->edx;p[19]=c->ebx;p[20]=c->esi;
    p[21]=c->edi;p[22]=c->ebp;p[23]=c->esp;
    p[24]=d->args[0];p[25]=d->args[1];p[26]=d->args[2];
    p[27]=U((uint32_t)(uintptr_t)gum_invocation_context_get_listener_function_data(ic)+4);
    p[28]=(uint32_t)cycles;p[29]=(uint32_t)(cycles>>32);p[30]=U(0x6f227c);
    p[31]=U(d->actor+0x28);p[32]=(uint32_t)fp;p[33]=(uint32_t)(fp>>32);
    p[34]=U(0x78a2f0);p[35]=U(0x78a30c);p[36]=d->args[3];
    state[1]=head+1;return seq;
}
static int copy(uint32_t *to,uint32_t from,uint32_t bytes) {
    uint32_t i;if(!readable(from,bytes))return 0;
    for(i=0;i<bytes/4;i++)to[i]=U(from+i*4);return 1;
}
static void snapshot(Invocation *d,uint32_t phase,uint32_t seq) {
    uint32_t i,*p,head=state[3],a=d->actor,driver,physics;
    uint32_t rigid=0,controller,ride,config,sel,valid=1;
    uint32_t driver_bytes=0,controller_bytes=0,action=0,route=0,strategy=0,properties=0;
    if(!seq||!readable(a,0x400))return;
    driver=U(a+0x30);physics=U(a+0x34);controller=U(a+0x2c);ride=U(a+0x10);
    config=U(a+0x14);sel=U(a+0x28);if(!readable(config,0x738))return;
    if(head-state[4]>=SCAP) {overflow();return;}
    p=snapshot_ring+(head%SCAP)*SWORDS;for(i=0;i<SWORDS;i++)p[i]=0;
    p[0]=seq;p[1]=d->call;p[2]=d->kind;p[3]=phase;p[4]=a;
    p[5]=(uint32_t)(int32_t)*(int16_t *)(uintptr_t)(a+0xa);p[6]=d->role;
    p[7]=U(0x7364dc);p[8]=U(0x73ad34);p[9]=driver;p[10]=physics;
    p[12]=controller;p[13]=ride;p[14]=config;
    copy(p+16,a,0x400);
    if(readable(driver,4)) {
        driver_bytes=U(driver)==0x6c8ad0?0x2c0:(U(driver)==0x6c8c18?0x210:0x1e8);
        if(copy(p+272,driver,driver_bytes))valid|=2;else driver_bytes=0;
    }
    if(pointer(physics)&&(sel==1||sel==3)) {
        if(copy(p+448,physics,sel==1?0x80:0x38)) {valid|=4;rigid=U(physics+0x20);}
        if(pointer(rigid)&&copy(p+480,rigid,0x200))valid|=8;
    }
    p[11]=rigid;
    if(readable(controller,4)) {
        controller_bytes=U(controller)==0x6c89f0?0x4bc:0x20;
        if(copy(p+608,controller,controller_bytes))valid|=16;else controller_bytes=0;
        if(controller_bytes==0x4bc) {
            valid|=128;action=U(controller+0x3f8);route=U(controller+0x3d8);strategy=U(controller+0x3fc);
            if(pointer(action)&&copy(p+933,action,0x1f4))valid|=256;
            if(pointer(route)&&copy(p+1058,route,0x1c0))valid|=512;
            if(pointer(strategy)&&copy(p+1170,strategy,0x10))valid|=1024;
        }
    }
    if(readable(ride,0x428)&&copy(p+911,ride+8,88)) {valid|=32;p[1176]=*(uint8_t *)(uintptr_t)(ride+0x424);}
    p[1174]=U(config+0x10);p[1175]=(uint32_t)(int32_t)*(int8_t *)(uintptr_t)(config+0x735);
    p[1177]=sel;p[1178]=U(a+0x24);p[1179]=stage();p[1180]=U(0x78a2f0);p[1181]=U(0x78a30c);
    if(d->role==1&&p[1181]==3&&readable(controller,0x504)) {p[1182]=U(controller+0x500);valid|=64;}
    p[1183]=driver_bytes;p[1184]=controller_bytes;p[1185]=action;p[1186]=route;p[1187]=strategy;
    if(pointer(U(0x7361f8)))p[1188]=U(U(0x7361f8)+0x2d0);
    p[1190]=(uint32_t)(int32_t)*(int8_t *)(uintptr_t)(config+0x736);
    if(valid&8) {
        properties=U(rigid+0x14);p[1191]=properties;
        if(pointer(properties)&&copy(p+1192,properties,0x54))valid|=2048;
    }
    p[1213]=U(0x6f227c);
    if((valid&4)&&sel==1) {
        p[1214]=U(physics+0x48);p[1215]=U(physics+0x4c);
        if(pointer(p[1214])&&copy(p+1216,p[1214],0xa0))valid|=4096;
        if(pointer(p[1215])&&copy(p+1256,p[1215],0xe4))valid|=8192;
    }
    p[15]=valid;state[3]=head+1;
}
void on_enter(GumInvocationContext *ic) {
    Invocation *d=(Invocation *)gum_invocation_context_get_listener_invocation_data(ic,sizeof(Invocation));
    uint32_t i,actor,role,tid,kind=U((uint32_t)(uintptr_t)gum_invocation_context_get_listener_function_data(ic));
    d->ok=0;if(!state[0]||(!state[11]&&(stage()<2||stage()>5)))return;
    tid=native_tid();if(state[7]&&state[7]!=tid) {state[9]++;return;}
    if(!state[7]&&kind>4)return;
    actor=owner(kind,ic);role=actor&&actor==actor_now?role_now:role_of(actor);if(!actor||role<1||role>4)return;
    if(!state[7])state[7]=tid;
    d->ok=1;d->actor=actor;d->role=role;d->kind=kind;
    d->object=kind==17||kind==18?ic->cpu_context->esi:ic->cpu_context->ecx;
    d->caller=(uint32_t)(uintptr_t)gum_invocation_context_get_return_address(ic);
    for(i=0;i<4;i++)d->args[i]=(uint32_t)(uintptr_t)gum_invocation_context_get_nth_argument(ic,i);
    if(kind==15||kind>=19)d->object=d->args[3];
    d->parent=parent_now;d->previous_actor=actor_now;d->previous_parent=parent_now;d->previous_role=role_now;
    d->call=state[8]+1;actor_now=actor;role_now=role;parent_now=d->call;depth++;state[6]++;
    i=record(d,ic,1);if(kind<=5||kind==17||kind==18)snapshot(d,1,i);
}
void on_leave(GumInvocationContext *ic) {
    Invocation *d=(Invocation *)gum_invocation_context_get_listener_invocation_data(ic,sizeof(Invocation));
    uint32_t seq;if(!d->ok)return;
    seq=record(d,ic,2);if(d->kind<=5||d->kind==17||d->kind==18)snapshot(d,2,seq);
    if(!depth||parent_now!=d->call) {state[10]++;state[0]=0;}
    actor_now=d->previous_actor;role_now=d->previous_role;parent_now=d->previous_parent;if(depth)depth--;state[6]--;
}
`;
function status() {
    const a=Array.from(new Uint32Array(control.readByteArray(68)));
    return {enabled:!!a[0],events:a[1],event_tail:a[2],snapshots:a[3],snapshot_tail:a[4],
        overflows:a[5],active:a[6],thread:a[7],sequence:a[8],foreign_calls:a[9],errors:a[10],
        unreadable_ranges:a[15],sent:batch,acknowledged,fixture};
}
function drainRing(which,ring,words,cap,head,tailOffset) {
    let tail=control.add(tailOffset).readU32();
    while(tail!==head) {
        if(batch-acknowledged>=16) {control.writeU32(0);throw new Error('Host backpressure; capture incomplete');}
        const count=Math.min((head-tail)>>>0,cap-(tail%cap));
        send({kind:'binary',stream:which,batch:batch++,first:tail,count,words},
            ring.add((tail%cap)*words*4).readByteArray(count*words*4));
        tail=(tail+count)>>>0;control.add(tailOffset).writeU32(tail);
    }
}
function drain() {
    // The producer publishes a call before its snapshot. Freeze the snapshot
    // head first, then the call head, so no exported state outruns its call.
    const snapshotHead=control.add(12).readU32(),callHead=control.add(4).readU32();
    drainRing('calls',calls,EVENT_WORDS,EVENT_CAP,callHead,8);
    drainRing('snapshots',snapshots,SNAP_WORDS,SNAP_CAP,snapshotHead,16);
}
rpc.exports={
    start(testFixture) {
        if(module||Process.arch!=='ia32')throw new Error('Fresh 32-bit process required');
        fixture=testFixture===true;
        entries.forEach(([name,address,signature])=>{
            const actual=Array.from(new Uint8Array(ptr(address).readByteArray(8)),x=>x.toString(16).padStart(2,'0')).join('');
            if(actual!==signature)throw new Error('Original signature mismatch: '+name);
        });
        control.writeU32(0);control.add(44).writeU32(fixture?1:0);
        module=new CModule(source,{state:control,call_ring:calls,snapshot_ring:snapshots,
            native_tid:helpers,native_cycles:helpers.add(16),native_fp:helpers.add(32),native_teb:helpers.add(64),
            native_query:Process.getModuleByName('kernel32.dll').getExportByName('VirtualQuery')});
        try {
            entries.forEach((entry,i)=>{
                // The inner integrator consumes uninitialized scratch lanes.
                // Observe whole physics calls; do not perturb that inner stack.
                if(i===5)return;
                const descriptor=Memory.alloc(8);descriptor.writeU32(i);descriptor.add(4).writeU32(entry[1]);keep.push(descriptor);
                hooks.push(Interceptor.attach(ptr(entry[1]),{onEnter:module.on_enter,onLeave:module.on_leave},descriptor));
            });
            Interceptor.flush();control.writeU32(1);
            timer=setInterval(()=>{try{drain();}catch(e){control.writeU32(0);send({kind:'error',message:String(e)});}},50);
        } catch(e) {hooks.forEach(h=>h.detach());hooks=[];throw e;}
        return {entries:entries.map((entry,i)=>({name:entry[0],address:entry[1],signature:entry[2],enabled:i!==5})),event_words:EVENT_WORDS,snapshot_words:SNAP_WORDS,state:status()};
    },
    acknowledge(value) {if(value!==acknowledged)throw new Error('Out-of-order host acknowledgement');acknowledged++;},
    status,
    stop() {control.writeU32(0);if(!status().active)drain();return status();},
    detach() {
        control.writeU32(0);if(status().active)throw new Error('Native calls still active');
        if(timer!==null)clearInterval(timer);timer=null;drain();
        hooks.forEach(h=>h.detach());hooks=[];Interceptor.flush();return status();
    }
};
