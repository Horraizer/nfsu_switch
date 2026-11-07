#include "nfsu/ai_action.h"
#include <cstring>
#include <iostream>
#include <limits>
#include <fstream>
#include <filesystem>
using namespace nfsu;
namespace {
struct OffsetCase {AiActionOffsetInput input;std::uint32_t value;};
struct PublicationCase {
    unsigned count;std::uint8_t rows[256][23],marker[23];bool has_marker;
    unsigned point;int category,direction,context;std::uint32_t tick;
    float interval,context_scale,catchup,cap;unsigned amplitude;
    std::uint32_t before_tick;int before_offset;std::uint32_t before_rng;
    bool route_speed,enabled,debug_constant,debug_route;
    float action_speed,duration,auxiliary,debug_auxiliary;bool has_route;
    std::uint32_t result[4],after_tick;int after_offset;std::uint32_t rng;
};
#include "ai_action_native.inc"
#include "drag_mechanics_native.inc"
struct OwnerCase {
    unsigned predicates;bool timed;int request,priority;bool present,ready;
    int result[6];unsigned count;int calls[16][3];
};
#include "ai_action_owner_native.inc"
struct FallbackCase {
    int role,index,category;
    float gap,lateral,lane;AiDriverPoint velocity;
    float cap,limit,desired_gap;
    std::uint32_t result[3];bool flags[4];
};
#include "ai_fallback_native.inc"
struct ContactSelectionCase {
    float gaps[3];int roles[3];bool records[3];AiStrategyContact contacts[3];
    unsigned count;float minimum;bool only;std::uint32_t target;bool complete;std::uint32_t after;
};
struct ContactUpdateCase {
    bool human;float gap,lateral;int category,role,road;float relative;bool present,opposite;
    AiDriverPoint velocity;float config[5],controller_speed;std::array<float,6> slowdown;
    std::uint32_t result[3];bool flags[4];int descriptor;
};
#include "ai_contact_native.inc"
struct ContactWitness {
    int road=0;unsigned calls=0;int kind=0;bool refuse=false;
    static bool forward(void* p,const AiContactManeuver& d,AiPlannedPath& path) {
        auto& w=*static_cast<ContactWitness*>(p);w.calls=10*w.calls+1;w.kind=d.kind;
        path.path.count=1;path.path.points[0]={1,2,3};return !w.refuse;
    }
    static bool missing(void* p,AiContactManeuver& d,AiPlannedPath& path) {
        auto& w=*static_cast<ContactWitness*>(p);w.calls=10*w.calls+2;
        path.path.count=1;path.path.points[0]={3,2,1};d.kind=1;return !w.refuse;
    }
    static bool classify(void* p,int& result) {
        auto& w=*static_cast<ContactWitness*>(p);w.calls=10*w.calls+3;result=w.road;return !w.refuse;
    }
};
struct OwnerWitness {
    const OwnerCase& input;
    int calls[16][3]{};unsigned count=0;
    bool overflow=false;
    void record(int op,const AiOwnedAction& action,int argument=0) {
        if(count==16){overflow=true;return;}
        calls[count][0]=op;calls[count][1]=int(action.handle);calls[count][2]=argument;++count;
    }
    static bool predicate(void* p,const AiOwnedAction& a,AiActionPredicate q,bool& yes) {
        auto& w=*static_cast<OwnerWitness*>(p);w.record(int(q),a);
        yes=(w.input.predicates&(1U<<unsigned(q)))!=0;return true;
    }
    static bool create(void* p,int kind,const AiOwnedAction& current,AiOwnedAction& out) {
        auto& w=*static_cast<OwnerWitness*>(p);w.record(3,current,kind);
        if(kind==4)out={3,4,0,false,0};
        else if(w.input.present)out={2,1,w.input.priority,false,0};
        else out={};
        return true;
    }
    static bool cleanup(void* p,const AiOwnedAction& a) {static_cast<OwnerWitness*>(p)->record(4,a);return true;}
    static void release(void* p,const AiOwnedAction& a) {static_cast<OwnerWitness*>(p)->record(5,a);}
    static bool update(void* p,const AiOwnedAction& a,bool& ready) {
        auto& w=*static_cast<OwnerWitness*>(p);w.record(6,a);ready=a.handle==3||w.input.ready;return true;
    }
};
unsigned failures=0;
void check(bool ok,const char* label){if(!ok){++failures;std::cerr<<"FAIL: "<<label<<'\n';}}
std::uint32_t bits(float v){std::uint32_t result;std::memcpy(&result,&v,4);return result;}
}
int main(int argc,char** argv){
    if(argc==3 && std::string(argv[1])=="--retail-drag") {
        std::ifstream file(std::filesystem::path(argv[2])/"Global/GLOBALB.BUN",std::ios::binary);
        std::array<unsigned char,0x82c> data{};unsigned char h[8];bool found=false;
        const auto word=[](const unsigned char* p){return unsigned(p[0])|unsigned(p[1])<<8|unsigned(p[2])<<16|unsigned(p[3])<<24;};
        while(file.read(reinterpret_cast<char*>(h),8)) {
            if(word(h)==0x34b00 && word(h+4)==data.size()) {
                found=bool(file.read(reinterpret_cast<char*>(data.data()),data.size()));break;
            }
            file.seekg(word(h+4),std::ios::cur);
        }
        check(found,"retail drag configuration present");
        if(found)for(const auto& v:drag_constructor_native) {
            AiSpeedConfiguration decoded;
            check(ai_speed_configuration(data.data(),data.size(),int(v[0]),decoded),"native drag constructor row decoded");
            for(unsigned j=0;j<4;++j)
                check(bits(decoded.drag_shift_offsets[j])==v[j+1],"original476E0D UP/DOWN constructor field order");
        }
        std::cout<<"Original drag constructor fields: 4 rows; failures="<<failures<<'\n';return failures?1:0;
    }
    if(argc==2) {
        std::array<unsigned char,0x82c> payload{};std::ifstream file(argv[1],std::ios::binary);
        if(!file.read(reinterpret_cast<char*>(payload.data()),payload.size())||file.peek()!=std::char_traits<char>::eof())return 2;
        for(int context=-1;context<10;++context) {
            AiSpeedConfiguration c;
            if(!ai_speed_configuration(payload.data(),payload.size(),context,c))return 1;
            std::cout<<"CONFIG "<<context;
            for(float p:c.road_probabilities)std::cout<<' '<<bits(p);
            std::cout<<' '<<bits(c.context_scale)<<' '<<bits(c.offset_interval)<<' '<<c.offset_amplitude<<'\n';
        }
        return 0;
    }
    if(argc!=1)return 2;
    for(const auto& c:drag_offset_native) {
        AiActionOffsetInput input;input.special_context=true;input.physics_selector=int(c[1]);
        std::memcpy(&input.absolute_speed,&c[0],4);float offset=-1;
        check(ai_action_offset_step(input,offset)&&bits(offset)==c[2],
              "original drag controller24 lateral progress curve");
    }
    {
        std::array<unsigned char,0x82c> data{};AiSpeedConfiguration c;c.context_scale=17;
        check(!ai_speed_configuration(nullptr,data.size(),0,c)&&c.context_scale==17,"null configuration retains output");
        check(!ai_speed_configuration(data.data(),data.size()-1,0,c)&&c.context_scale==17,"truncated configuration retains output");
        check(!ai_speed_configuration(data.data(),data.size(),10,c)&&c.context_scale==17,"invalid context retains output");
        data[0x396]=0x80;data[0x397]=0x7f;
        check(!ai_speed_configuration(data.data(),data.size(),0,c)&&c.context_scale==17,"nonfinite selected scale refused transactionally");
    }
    unsigned contact_index=0;
    for(const auto& c:contact_selection_cases) {
        AiNeighborhood n;n.count=4;n.self_index=0;n.entries[0].id=1;n.entries[0].distance=100;
        std::array<AiStrategyActor,16> actors{};
        for(unsigned i=0;i<3;++i) {
            n.entries[i+1].id=i+2;n.entries[i+1].distance=100+c.gaps[i];
            actors[i+1]={c.roles[i]>=0&&c.records[i],c.roles[i]==1,0};
        }
        std::uint32_t target=77;AiContactActionState state;state.target=4;bool done=false;
        const bool ok=ai_strategy_contact(n,actors,c.contacts,c.count,c.minimum,c.only,target);
        const bool finish=ai_action_contact_complete(n,actors,c.contacts,c.count,state,done);
        if(!ok||target!=c.target||!finish||done!=c.complete||state.target!=c.after) {
            ++failures;std::cerr<<"FAIL: contact selection "<<contact_index<<" target "<<target<<" expected "<<c.target
                <<" complete "<<done<<" retained "<<state.target<<'\n';
        }
        ++contact_index;
    }
    contact_index=0;
    const AiContactActionOps contact_ops{ContactWitness::forward,ContactWitness::missing,ContactWitness::classify};
    for(const auto& c:contact_update_cases) {
        AiNeighborhood n;n.count=c.present?2:1;n.self_index=0;
        n.entries[0].id=1;n.entries[0].distance=100;n.entries[0].half_extent[1]=2;
        auto& e=n.entries[1];e.id=2;e.distance=100+c.gap;e.lateral=c.lateral;e.half_extent[1]=2;
        e.relative_velocity[1]=c.relative;e.lane_lateral.fill(-c.lateral);
        AiContactActionInput input;input.neighbors=&n;input.category=c.category;input.self_role=c.role;
        input.target_human=c.human;input.target_velocity=c.velocity;input.self_direction={1,0,0};
        input.target_direction={c.opposite?-1.F:1.F,0,0};input.signed_speed=30;
        input.controller_speed=c.controller_speed;input.slowdown=c.slowdown;
        input.duration=c.config[0];input.human_speed_offset=c.config[1];input.other_speed_offset=c.config[2];
        input.gap_margin=c.config[3];input.minimum_speed=c.config[4];
        AiContactActionState state;state.target=2;state.speed={37,9,7,false,false,false,false};
        ContactWitness witness;witness.road=c.road;
        const bool ok=ai_action_contact_update(input,contact_ops,&witness,state);
        const auto& s=state.speed;
        const bool same=ok&&bits(s.speed)==c.result[0]&&bits(s.duration)==c.result[1]&&bits(s.auxiliary)==c.result[2]&&
            s.route_speed==c.flags[0]&&s.enabled==c.flags[1]&&s.catchup==c.flags[2]&&s.ready==c.flags[3]&&
            state.maneuver.kind==c.descriptor;
        if(!same) {
            ++failures;std::cerr<<"FAIL: contact update "<<contact_index<<" speed/duration "<<bits(s.speed)<<'/'<<bits(s.duration)
                <<" expected "<<c.result[0]<<'/'<<c.result[1]<<" mode "<<state.maneuver.kind<<'\n';
        }
        check(witness.calls==(c.present?13U:2U),"contact path before road query; missing target uses alternate path");
        check(state.path.path.count==1,"contact path producer output retained");
        const auto saved=state;witness={};witness.refuse=true;
        check(!ai_action_contact_update(input,contact_ops,&witness,state)&&state.target==saved.target&&
              bits(state.speed.speed)==bits(saved.speed.speed)&&witness.calls==(c.present?1U:2U),
              "failed contact path prevents classification and state publication");
        ++contact_index;
    }
    unsigned fallback_index=0;
    for(const auto& c:fallback_cases) {
        AiNeighborhood n;n.count=2;n.self_index=c.index;
        n.entries[0].id=11;n.entries[1].id=22;
        n.entries[0].half_extent[0]=n.entries[1].half_extent[0]=1;
        n.entries[0].distance=c.gap;n.entries[0].lateral=c.lateral;
        n.entries[0].lane_lateral.fill(c.lane);
        AiActionNeighborState actor{11,true,c.role,c.velocity};
        AiActionSpeedState state{c.cap,9,7,false,false,false,false};
        const auto before=failures;
        check(ai_action_fallback_speed(n,c.category,&actor,1,c.limit,c.desired_gap,state),"original fallback speed executes");
        check(bits(state.speed)==c.result[0]&&bits(state.duration)==c.result[1]&&bits(state.auxiliary)==c.result[2]&&
              state.route_speed==c.flags[0]&&state.enabled==c.flags[1]&&state.catchup==c.flags[2]&&state.ready==c.flags[3],
              "original fallback retained speed and flags");
        if(before!=failures)std::cerr<<"fallback case "<<fallback_index<<'\n';
        ++fallback_index;
    }
    {
        AiNeighborhood n;n.count=3;n.self_index=2;
        n.entries[0].id=1;n.entries[0].distance=1;n.entries[0].half_extent[0]=1;
        n.entries[1].id=2;n.entries[1].distance=2;n.entries[1].half_extent[0]=1;
        n.entries[2].id=3;n.entries[2].half_extent[0]=1;
        AiActionNeighborState actors[]={{1,true,2,{0,0,0}},{2,true,2,{10,0,0}}};
        AiActionSpeedState state{30,9,7,false,false,false,false};
        check(ai_action_fallback_speed(n,0,actors,2,20,5,state)&&state.speed==7&&!state.route_speed&&state.duration==0,
              "only immediate predecessor controls speed");
        n.entries[1].lateral=2;
        check(ai_action_fallback_speed(n,0,actors,2,20,5,state)&&state.route_speed&&state.speed==7,
              "leaving overlap restores route policy without erasing retained cap");
        n.entries[1].lateral=0;
        actors[1].race_actor=false;
        check(ai_action_fallback_speed(n,0,actors,2,20,5,state)&&state.route_speed&&state.speed==7,
              "non-race reference does not cap speed");
        const auto saved=state;
        check(!ai_action_fallback_speed(n,0,actors,1,20,5,state)&&state.speed==saved.speed&&state.route_speed==saved.route_speed,
              "unresolved predecessor metadata is not empty road");
        actors[1].race_actor=true;actors[1].velocity[0]=std::numeric_limits<float>::quiet_NaN();
        check(!ai_action_fallback_speed(n,0,actors,2,20,5,state)&&state.speed==saved.speed&&state.route_speed==saved.route_speed,
              "invalid fresh body state refuses transactionally");
    }
    const AiActionOwnerOps ops{OwnerWitness::predicate,OwnerWitness::create,OwnerWitness::cleanup,
        OwnerWitness::release,OwnerWitness::update};
    unsigned owner_index=0;
    for(const auto& c:owner_cases) {
        OwnerWitness witness{c};AiActionOwner owner{{1,10,100,c.timed,0},c.request};
        const auto before=failures;
        check(ai_action_owner_step(ops,&witness,owner),"original action ownership executes");
        const auto& a=owner.current;
        check(int(a.handle)==c.result[0]&&a.kind==c.result[1]&&a.priority==c.result[2]&&
            a.timed==bool(c.result[3])&&int(a.status)==c.result[4]&&owner.requested_kind==c.result[5],
            "original active action/status/request");
        check(!witness.overflow&&witness.count==c.count,"original virtual call count");
        for(unsigned j=0;j<std::min(witness.count,c.count);++j)
            check(std::equal(witness.calls[j],witness.calls[j]+3,c.calls[j]),"original ordered action effects");
        if(before!=failures)std::cerr<<"owner case "<<owner_index<<'\n';
        ++owner_index;
    }
    {
        OwnerWitness witness{owner_cases[0]};AiActionOwner owner;
        check(!ai_action_owner_step(ops,&witness,owner)&&witness.count==0,"missing initial action refuses before callbacks");
        owner.current={1,10,100,false,0};auto missing=ops;missing.create=nullptr;
        check(!ai_action_owner_step(missing,&witness,owner)&&witness.count==0,"missing factory is not a no-candidate result");
    }
    for(const auto& c:offsets){
        float value=-1;const bool ok=ai_action_offset_step(c.input,value);
        if(!ok||bits(value)!=c.value)std::cerr<<"offset mode "<<c.input.mode<<" speed "<<c.input.absolute_speed<<" actual "<<value<<'\n';
        check(ok&&bits(value)==c.value,"original action offset policy");
    }
    unsigned index=0;
    for(const auto& c:publications){
        Route route;route.points.resize(c.count);RoutePoint marker;
        for(unsigned j=0;j<c.count;++j)for(unsigned k=0;k<23;++k)route.points[j].speed[k]=c.rows[j][k];
        for(unsigned k=0;k<23;++k)marker.speed[k]=c.marker[k];
        AiSpeedPolicyInput in;in.route=c.has_route?&route:nullptr;in.point=c.point;in.category=c.category;
        in.direction=c.direction;in.context=c.context;in.tick=c.tick;in.offset_interval=c.interval;
        in.context_scale=c.context_scale;in.catchup_scale=c.catchup;in.strategy_cap=c.cap;
        in.offset_amplitude=c.amplitude;in.marker_point=c.has_marker?&marker:nullptr;
        in.route_speed=c.route_speed;in.enabled=c.enabled;in.debug_constant=c.debug_constant;in.debug_route=c.debug_route;
        in.action_speed=c.action_speed;in.duration=c.duration;in.auxiliary=c.auxiliary;in.debug_auxiliary=c.debug_auxiliary;
        AiSpeedPolicyState state{c.before_tick,c.before_offset};auto rng=c.before_rng;AiSpeedPublication out;
        const bool ok=ai_action_publish_speed(in,state,rng,out);
        const bool same=ok&&bits(out.speed)==c.result[0]&&bits(out.duration)==c.result[1]&&bits(out.auxiliary)==c.result[2]&&out.enabled==bool(c.result[3])&&
            state.offset_tick==c.after_tick&&state.point_offset==c.after_offset&&rng==c.rng;
        if(!same)std::cerr<<"publication "<<index<<" speed "<<std::hex<<bits(out.speed)<<" expected "<<c.result[0]<<std::dec<<" offset "<<state.point_offset<<" expected "<<c.after_offset<<'\n';
        check(same,"original speed publication, tick, offset and shared RNG");
        const auto saved=state;const auto saved_rng=rng;const auto speed=out.speed;
        in.strategy_cap=std::numeric_limits<float>::quiet_NaN();
        check(!ai_action_publish_speed(in,state,rng,out)&&rng==saved_rng&&state.point_offset==saved.point_offset&&
              state.offset_tick==saved.offset_tick&&out.speed==speed,"invalid speed policy is transactional");
        ++index;
    }
    std::cout<<"AI action original offsets "<<std::size(offsets)<<", publications "<<std::size(publications)
             <<", owner "<<std::size(owner_cases)<<", fallback "<<std::size(fallback_cases)
             <<", contact selection "<<std::size(contact_selection_cases)<<", contact update "<<std::size(contact_update_cases)
             <<", failures "<<failures<<'\n';
    return failures?1:0;
}
