#include "nfsu/race_particles.h"
#include "nfsu/texture_decode.h"
#include "nfsu/arg_path.h"
#include "vehicle_particle_bursts_native.inc"
#include "road_particle_birth_native.inc"
using nfsu::RaceParticleEffect;
#include "particle_draw_native.inc"
#include "bottom_particle_birth_native.inc"
#include "bottom_particle_motion_native.inc"
#include "nfsu/race_events.h"
#include <cstring>
#include "race_particle_owner_checks.h"
#include <memory>
#include <algorithm>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>
namespace nfsu {
struct RaceParticleBirthProbe {
    static RaceParticle birth(const RaceParticles& p,const NativeBottomBirth& row) {
        const VehicleQuat q=row.pose==2?VehicleQuat{std::sqrt(.5),std::sqrt(.5),0,0}:
            row.pose?VehicleQuat{std::sqrt(.5),0,0,std::sqrt(.5)}:VehicleQuat{};
        return p.spawn_bottom_particle(row.part,{10,20,30},q,row.part==2?VehicleVec3{-1.2,0,-.3}:VehicleVec3{},
            row.pose?VehicleVec3{2,3,-1}:VehicleVec3{},row.seed);
    }
    static RaceParticle motion(RaceParticles& p,const NativeBottomMotion& row) {
        const NativeBottomBirth initial{row.part,121,0,row.pose,0,0,{},{}};
        auto child=birth(p,initial);p.reset();p.step_=1;child.born_step=1;
        p.emit_vehicle(child);
        for(unsigned i=0;i<row.frames;++i){++p.step_;p.finish_vehicle_frame(row.dt);}
        return p.vehicle_[0];
    }
};
struct PriorContactObserverLayout {
    std::uint32_t polygon=0;VehicleVec3 impulse,forward,up,normal,point;double slide_mps=0;std::uint32_t audio_order=0;
};
static_assert(sizeof(VehicleContactObservation)==sizeof(PriorContactObserverLayout),"event stamp must fit existing observer padding");
struct RaceWorldContactProbe {
    static void fx(RaceSession& driver) {
        for(auto& t:driver.track_telemetry_) {
            t.reset=VehicleTrackReset::none;t.model.owner_speeds.magnitude=4.5F;
            for(unsigned i=0;i<4;++i) {
                auto& w=t.model.wheels[i];w.contact=true;w.surface=1;
                w.ground_normal={0,0,1};w.ground_point={t.position.x+.2*i,t.position.y,t.position.z};
                w.effect_slip_velocity={80,0};
            }
        }
    }
    static void wall_fx(RaceSession& driver,unsigned mask,bool aliases=false) {
        const auto origin=driver.track_telemetry_.back().position;
        for(unsigned j=0;j<driver.track_telemetry_.size();++j) {
            auto& t=driver.track_telemetry_[j];t.audio_contact_count=0;t.reset=VehicleTrackReset::none;
            t.model.owner_speeds.magnitude=0;for(auto& w:t.model.wheels)w.contact=false;
            for(unsigned hit=0;hit<2;++hit)if((mask&(1U<<hit)) && (aliases || j==2*hit)) {
                auto& c=t.audio_contacts[t.audio_contact_count++];c={};c.polygon=0;
                c.point={origin.x+1+hit,origin.y+hit,origin.z};c.normal={0,0,1};c.impulse={15,0,0};
                c.audio_order=101+hit;
            }
        }
    }
    static void bottom_fx(RaceSession& driver,bool enabled=true) {
        auto& state=const_cast<VehicleState&>(driver.car());
        state.bottom_out_impulse=enabled?std::array<float,4>{.6F,.6F,.6F,.6F}:std::array<float,4>{};
        for(auto& w:state.wheels)w.contact=true;
        for(auto& t:driver.track_telemetry_) {
            t.reset=VehicleTrackReset::none;t.audio_contact_count=0;t.model.owner_speeds={};
            //A late contact completion updates the actor after this solver sample.
            t.model.bottom_out_impulse={};
            for(auto& w:t.model.wheels){w.contact=true;w.effect_slip_velocity={};}
        }
    }
    static void reset_fx(RaceSession& driver,unsigned slice,bool count_only=false) {
        if(count_only)++driver.track_telemetry_.back().resets;
        else driver.track_telemetry_.at(slice).reset=VehicleTrackReset::manual;
    }
};
}
namespace {
using namespace nfsu;
void check(bool ok,const char* reason) { if(!ok)throw std::runtime_error(reason); }
VehicleTrackTelemetry sample() {
    VehicleTrackTelemetry t;
    t.orientation.w=1;
    t.model.solver_dt=1.0/240.0;
    for(unsigned i=0;i<4;++i) {
        auto& w=t.model.wheels[i];w.contact=true;w.surface=1;
        w.ground_normal={0,0,1};w.ground_point={double(i),double(i&1),0};
        w.effect_slip_velocity={80,0};
    }
    return t;
}
void frame(RaceParticles& p,VehicleTrackTelemetry& t,const VehicleSpec& spec,unsigned surface=0) {
    for(unsigned i=0;i<4;++i) {
        ++t.step;t.model.simulation_seconds=double(t.step)/240.0;
        p.advance(t,spec,0,surface);
    }
}
}
int main(int argc,char** argv) {
    try {
        check(argc==3,"expected retail root and case");std::string error;
        check(nfsu::check_arguments(argc,argv,1,error),error.c_str());
        std::ifstream input(nfsu::argument_path(argv[1])/"Global/GLOBALB.BUN",std::ios::binary);
        std::vector<unsigned char> data((std::istreambuf_iterator<char>(input)),{});
        check(!data.empty(),"missing GLOBALB");
        RaceParticles p;check(p.load_presets(data,error),error.c_str());
        VehicleSpec spec;auto t=sample();const std::string kind=argv[2];
        if(kind=="walls") {
            VehicleTrackCourse course;check(build_vehicle_proving_ground(course,error),error.c_str());
            course.world.collision.polygons[0].unknown0[2]=20;
            auto world=std::make_unique<RaceSession>(course);check(world->start(error),error.c_str());
            while(race_car_held(world->timing()))check(world->advance({},error),error.c_str());
            check(world->advance({},error),error.c_str());RaceWorldContactProbe::wall_fx(*world,0);p.advance(*world);
            auto a=p,b=p,both=p,aliases=p;
            check(world->advance({},error),error.c_str());
            RaceWorldContactProbe::wall_fx(*world,1);a.advance(*world);
            RaceWorldContactProbe::wall_fx(*world,2);b.advance(*world);
            RaceWorldContactProbe::wall_fx(*world,3);both.advance(*world);
            check(a.spark_count()==10 && b.spark_count()==10,"single native wall event must emit10");
            check(both.spark_count()==a.spark_count()+b.spark_count(),"separate solver wall events collapsed into one burst");
            unsigned at_a=0,at_b=0;
            for(std::size_t i=0;i<both.spark_count();++i) {
                at_a+=both.spark(i).origin.x==world->telemetry().position.x+1;
                at_b+=both.spark(i).origin.x==world->telemetry().position.x+2;
            }
            check(at_a==10 && at_b==10,"wall event lost its own contact point");
            RaceWorldContactProbe::wall_fx(*world,1,true);aliases.advance(*world);
            check(aliases.spark_count()==10,"held event aliases were counted more than once");
            aliases.advance(*world);check(aliases.spark_count()==10,"repeated visual callback counted held contact twice");
            course.world.collision.polygons[0].unknown0[2]=1;
            check(world->advance({},error),error.c_str());
            RaceWorldContactProbe::wall_fx(*world,1);RaceParticles ground_only;
            check(ground_only.load_presets(data,error),error.c_str());ground_only.advance(*world);
            check(ground_only.spark_count()==0,"ROAD hull contact became a wall spark without a bottom stop");
        } else if(kind=="road_spawn") {
            RaceChunkPayload definitions;std::vector<RaceTableIssue> issues;
            check(find_race_chunk(data.data(),data.size(),0x35021,definitions,issues),"road definitions");
            const auto word=[](const unsigned char* b){std::uint32_t v;std::memcpy(&v,b,4);return v;};
            std::size_t road=0;
            const auto first=((definitions.begin+15)&~std::uint64_t{15})+16;
            for(auto at=first;at+0x220<=definitions.end;at+=0x220)
                if(word(data.data()+at+8)==0xccf7bd61U)road=at;
            check(road!=0,"native road preset missing");
            spec.handling.centre_of_mass={0,0,0};
            for(auto& w:t.model.wheels)w.effect_slip_velocity={};
            t.model.owner_speeds.magnitude=50;
            auto point=data;std::memset(point.data()+road+0x70,0,12);
            check(p.load_presets(point,error),error.c_str());frame(p,t,spec,1);
            check(p.smoke_count()==3,"stock road strength-one burst changed");
            for(std::size_t i=0;i<p.smoke_count();++i) {
                const auto& at=p.smoke(i).origin;
                check(std::abs(at.x-kNativeRoadCentre[0])<1e-6 && std::abs(at.y-kNativeRoadCentre[1])<1e-6 &&
                      std::abs(at.z-kNativeRoadCentre[2])<1e-6,"road marker0 birth origin differs from original402D30");
            }
            check(p.load_presets(data,error),error.c_str());t.step=0;
            std::array<double,3> low{1e9,1e9,1e9},high{-1e9,-1e9,-1e9};
            for(unsigned n=0;n<100;++n) {
                frame(p,t,spec,1);
                for(std::size_t i=0;i<p.smoke_count();++i) {
                    const auto& puff=p.smoke(i);if(puff.born_step!=t.step)continue;
                    const auto& at=puff.origin;const double v[]{at.x,at.y,at.z};
                    for(unsigned k=0;k<3;++k) {
                        check(v[k]>=kNativeRoadBounds[k][0]-1e-6 && v[k]<=kNativeRoadBounds[k][1]+1e-6,
                              "road birth escaped its native placement volume");
                        low[k]=std::min(low[k],v[k]);high[k]=std::max(high[k],v[k]);
                    }
                }
            }
            for(unsigned k=0;k<3;++k)check(high[k]-low[k]>.8*kNativeRoadObservedSpan[k],"road births collapsed their authored region");
            // Marker0 ignores the editor translation and follows the body pose.
            RaceChunkPayload placements;check(find_race_chunk(point.data(),point.size(),0x35020,placements,issues),"road placements");
            for(auto at=((placements.begin+15)&~std::uint64_t{15})+16;at+0xc0<=placements.end;at+=0xc0)
                if(word(point.data()+at+12)==0xccf7bd61U)for(unsigned k=0;k<3;++k) {
                    const float editor=1000.F;std::memcpy(point.data()+at+0x60+4*k,&editor,4);
                }
            // Zero angular/speed spread isolates the original launch axis.
            for(const auto offset:{0x44U,0x48U,0x4cU,0x50U,0x54U,0x84U,0x88U})std::memset(point.data()+road+offset,0,4);
            check(p.load_presets(point,error),error.c_str());t.step=0;t.position={1,2,3};
            t.orientation={std::sqrt(.5),0,0,std::sqrt(.5)};
            frame(p,t,spec,1);
            for(std::size_t i=0;i<p.smoke_count();++i) {
                const auto& puff=p.smoke(i);const auto& at=puff.origin;
                check(std::abs(at.x-(1-kNativeRoadCentre[1]))<1e-6 && std::abs(at.y-(2+kNativeRoadCentre[0]))<1e-6 &&
                      std::abs(at.z-(3+kNativeRoadCentre[2]))<1e-6,"road marker0 used the editor position or lost car rotation");
                check(std::abs(puff.velocity.x)<1e-5 && puff.velocity.y< -5 && puff.velocity.z>3,
                      "road launch lost the placement's tilted axis");
            }
        } else if(kind=="owners") {
            // Real RaceSession advances with two computer drivers. Only the
            // published FX witnesses are replaced, never the vehicle state.
            for(unsigned owner=0;owner<3;++owner) {
                VehicleTrackCourse course;check(build_vehicle_proving_ground(course,error),error.c_str());
                for(auto& polygon:course.world.collision.polygons)polygon.unknown0[2]=13;
                auto world=std::make_unique<RaceSession>(course);
                auto a=course.start,b=course.start;a.ground.x+=16;b.ground.x+=32;
                check(world->set_rivals({{stock_240sx_vehicle_spec(),a,stock_240sx_hull(),"one"},
                                        {stock_240sx_vehicle_spec(),b,stock_240sx_hull(),"two"}},error),error.c_str());
                check(world->start(error),error.c_str());
                while(race_car_held(world->timing()))check(world->advance({},error),error.c_str());
                p.reset();
                const auto driver=[&](unsigned i)->RaceSession& {
                    return i?const_cast<RaceSession&>(world->rival(i-1)):*world;
                };
                const auto advance=[&] {
                    check(world->advance({},error),error.c_str());
                    for(unsigned i=0;i<3;++i)RaceWorldContactProbe::fx(driver(i));
                };
                for(unsigned i=0;i<20;++i){advance();p.advance(*world);}
                const auto before=nfsu::test::before_owner_reset(p,owner,world->world_frame_seconds());
                std::cout<<"OWNER_RESET owner="<<owner<<" vehicle="<<before.owned_vehicle
                         <<" water="<<before.owned_water<<" other_live_next="<<before.others.size()<<'\n';
                check(before.owned_vehicle && before.owned_water && !before.others.empty(),
                      "owner reset needs live vehicle and water witnesses for all three actors");
                advance();
                // P1 final slice, P2 intermediate slice, AI reset whose marker
                // was overwritten by later slices but whose counter survived.
                RaceWorldContactProbe::reset_fx(driver(owner),owner==0?3:1,owner==2);
                p.advance(*world);
                nfsu::test::check_owner_reset(p,owner,before);
            }
        } else if(kind=="bursts") {
            for(const auto& row:kNativeVehicleBursts)
                check(race_particle_burst_size(row.strength,row.interval)==row.children,
                      "native402D30 burst differs at a float subtraction boundary");
        } else if(kind=="wheels" || kind=="lifetime") {
            unsigned frames=0;
            while(!p.smoke_count() && frames++<30)frame(p,t,spec);
            check(p.smoke_count()>0,"smoke gate did not open");
            if(kind=="wheels") {
                check(p.smoke_count()==4,"one visual frame must emit all four eligible wheels");
                for(unsigned i=0;i<4;++i) {
                    check(p.smoke(i).origin.x==i,"wheel emission order or origin changed");
                    check(p.smoke(i).born_step==t.step,"tyre emission escaped the visual-frame boundary");
                }
            } else {
                const auto first=p.smoke(0);
                for(unsigned i=0;i<60;++i)frame(p,t,spec);
                bool retained=false;
                for(std::size_t i=0;i<p.smoke_count();++i)
                    retained|=p.smoke(i).born_step==first.born_step && p.smoke(i).origin.x==first.origin.x;
                check(retained,"live authored smoke was evicted before its minimum lifetime");
                check(p.smoke_count()>16,"dense tyres retained the old16 cap");
            }
        } else if(kind=="pool") {
            RaceParticle first;bool have=false;std::size_t peak=0;
            for(unsigned i=0;i<160;++i) {
                frame(p,t,spec);peak=std::max(peak,p.smoke_count());
                check(p.smoke_count()<=kRaceWaterPool,"vehicle pool exceeded its native ceiling");
                if(!have && p.smoke_count()){first=p.smoke(0);have=true;}
            }
            bool retained=false;
            for(std::size_t i=0;i<p.smoke_count();++i)
                retained|=p.smoke(i).born_step==first.born_step && p.smoke(i).origin.x==first.origin.x;
            check(peak==kRaceWaterPool && retained,"a full native pool must drop births without evicting live smoke");
        } else if(kind=="cpu_size") {
            for(unsigned i=1;i<4;++i)t.model.wheels[i].contact=false;
            while(!p.smoke_count())frame(p,t,spec);
            check(p.smoke_count()==1,"CPU size witness needs exactly one native puff");
            for(auto& w:t.model.wheels)w.contact=false;
            for(unsigned i=0;i<60;++i)frame(p,t,spec);
            VehicleVec3 at;double diameter=0,opacity=0;std::array<unsigned,3> rgb{};
            check(p.visual(p.smoke(0),at,diameter,opacity,rgb),"size witness expired");
            RenderOptions options;options.width=320;options.height=180;
            options.camera.distance=2;options.camera.target={at.x,at.z,-at.y};
            std::vector<unsigned char> rgba(options.width*options.height*4);
            check(draw_race_particles(rgba,options,p,error),error.c_str());
            bool outside_old_quad=false;
            for(unsigned y=0;y<options.height;++y)for(unsigned x=0;x<options.width;++x) {
                const auto i=(y*options.width+x)*4;
                if(std::abs(int(x)-160)>7 || std::abs(int(y)-90)>7)
                    outside_old_quad|=rgba[i]||rgba[i+1]||rgba[i+2];
            }
            check(outside_old_quad,"CPU overlay clipped a native puff to the old48px-at720p limit");
        } else if(kind=="bottom_motion") {
            for(const auto& row:kNativeBottomMotions) {
                const auto got=RaceParticleBirthProbe::motion(p,row);
                const double pos[]{got.origin.x+got.displacement.x,got.origin.y+got.displacement.y,got.origin.z+got.displacement.z};
                const double vel[]{got.velocity.x,got.velocity.y,got.velocity.z};
                const auto remaining=got.life_ticks-unsigned(got.elapsed_seconds*1024);
                const auto cell=race_particle_atlas_cell(got,p.step())-kRaceParticleAtlases[unsigned(got.atlas)].first_cell;
                bool same=remaining==row.life && cell==row.cell;
                double distance=0,speed=0;
                for(unsigned k=0;k<3;++k){distance=std::max(distance,std::abs(pos[k]-row.point[k]));speed=std::max(speed,std::abs(vel[k]-row.velocity[k]));}
                same=same && distance<1e-5 && speed<1e-6;
                if(!same)std::cout<<"MOTION part="<<row.part<<" pose="<<row.pose<<" frames="<<row.frames<<" dt="<<row.dt
                    <<" position_error="<<distance<<" velocity_error="<<speed<<" cell="<<cell<<'/'<<row.cell<<" remaining="<<remaining<<'/'<<row.life<<'\n';
                check(same,"original403A60 landing drag/gravity/compressed velocity differs");
            }
        } else if(kind=="bottom_birth") {
            RaceChunkPayload definitions;std::vector<RaceTableIssue> issues;
            check(find_race_chunk(data.data(),data.size(),0x35021,definitions,issues),"bottom birth table");
            std::size_t smoke_row=0;
            for(auto at=((definitions.begin+15)&~std::uint64_t{15})+16;at+0x220<=definitions.end;at+=0x220) {
                std::uint32_t key;std::memcpy(&key,data.data()+at+8,4);
                if(key==bin_hash("XEBO_ROAD_SmokeA"))smoke_row=at;
            }
            check(smoke_row!=0,"bottom smoke record");
            const auto controlled=[&](unsigned variant) {
                auto copy=data;
                if(variant) {
                    copy[smoke_row+0x18]=variant==1?1:0;
                    for(unsigned offset:{0x44U,0x48U,0x4cU,0x50U,0x54U,0x84U,0x88U})
                        std::memset(copy.data()+smoke_row+offset,0,4);
                }
                return copy;
            };
            for(const auto& row:kNativeBottomBirths) {
                check(p.load_presets(controlled(row.variant),error),error.c_str());
                const auto got=RaceParticleBirthProbe::birth(p,row);
                const double pos[]{got.origin.x,got.origin.y,got.origin.z};
                const double vel[]{got.velocity.x,got.velocity.y,got.velocity.z};
                bool same=got.life_ticks==row.life && got.initial_cell==row.cell;
                for(unsigned k=0;k<3;++k)same=same && std::abs(pos[k]-row.point[k])<1e-5 && std::abs(vel[k]-row.velocity[k])<1e-6;
                if(!same)std::cout<<"BIRTH part="<<row.part<<" seed="<<row.seed<<" pose="<<row.pose<<" variant="<<row.variant
                    <<" life="<<got.life_ticks<<'/'<<row.life<<" cell="<<got.initial_cell<<'/'<<row.cell
                    <<" pos="<<pos[0]<<','<<pos[1]<<','<<pos[2]<<" vel="<<vel[0]<<','<<vel[1]<<','<<vel[2]<<'\n';
                check(same,"original569A00/402D30 landing birth differs at the same pose and RNG");
            }
            check(p.load_presets(controlled(1),error),error.c_str());t=sample();
            t.model.bottom_out_impulse={.5F,.5F,.5F,.5F};
            for(auto& w:t.model.wheels)w.effect_slip_velocity={};
            frame(p,t,spec,1);unsigned smoke=0;
            for(std::size_t i=0;i<p.smoke_count();++i)if(p.smoke(i).effect==RaceParticleEffect::bottom_smoke) {
                ++smoke;check(std::abs(p.smoke(i).velocity.z)<1e-5,"real visual-frame path ignored native planar birth");
            }
            check(smoke==41,"planar fix changed the native smoke burst budget");
        } else if(kind=="landings") {
            std::vector<unsigned char> ingame,globals;
            const auto packs=load_texture_pack_set(nfsu::argument_path(argv[1])/"Global/INGAMEB.BUN",ingame);
            const auto global_packs=load_texture_pack_set(nfsu::argument_path(argv[1])/"Global/GLOBALB.BUN",globals);
            RaceParticleTextures textures;
            check(decode_race_particle_textures(ingame,packs,textures,error,false,&globals,&global_packs),error.c_str());
            const auto& flare=textures[unsigned(RaceParticleAtlas::bottom_flash)];
            check(flare.usable() && flare.width==128 && flare.height==128,"native bottom-out flare pixels missing");
            spec=stock_240sx_vehicle_spec();const VehicleModel model(spec);VehicleFlatGround floor;
            for(const double down:{0.0,-1.0,-5.0,5.0}) {
                check(p.load_presets(data,error),error.c_str());VehicleState state;VehiclePose pose;
                pose.ground.z=-.5;pose.speed=3;
                check(model.reset(state,pose),"landing model reset");
                state.velocity.z=down;state.forces_prepared=true;
                state.prepared_force={};state.prepared_torque={};state.published_support=4;
                t={};
                for(unsigned i=0;i<4;++i) {
                    check(model.step(state,{},floor,t.model,i==3?1.0/60.0:0,1.0/240.0,i==0),"landing slice");
                    t.step=state.step;t.position=state.position;t.orientation=state.orientation;t.velocity=state.velocity;
                    p.advance(t,spec,0,1);
                }
                std::cout<<"LANDING vz="<<down<<" sparks="<<p.spark_count()<<" smoke="<<p.smoke_count()<<'\n';
                unsigned flashes=0,smoke=0,sparks=0;
                for(std::size_t i=0;i<p.spark_count();++i) {
                    flashes+=p.spark(i).effect==RaceParticleEffect::bottom_flash;
                    sparks+=p.spark(i).effect==RaceParticleEffect::bottom_sparks;
                }
                for(std::size_t i=0;i<p.smoke_count();++i)smoke+=p.smoke(i).effect==RaceParticleEffect::bottom_smoke;
                //Real402D30 stock strength-one budgets:FlashA3,SmokeA41,SprksA101.
                check(down== -5 ? sparks==101 && flashes==3 && smoke==41 : sparks==0 && flashes==0 && smoke==0,
                    "native bottom-out signal must emit all3 road parts only above its1..2 impulse band");
                VehicleTelemetryHash original,changed;original.add(t.model);auto visual_only=t.model;
                visual_only.bottom_out_impulse={12,12,12,12};changed.add(visual_only);
                check(original.finish()==changed.finish(),"visual accumulator changed legacy telemetry bytes");
                auto clear=state;clear.bottom_out_impulse={12,12,12,12};VehicleTelemetry next;
                VehicleFlatGround empty(-100);
                check(model.step(clear,{},empty,next,0,1.0/240.0,true),"new outer landing sample");
                check(next.bottom_out_impulse==std::array<float,4>{},"outer frame retained an old landing impulse");
            }
            //The ordinary RaceSession FX path shares one pool across both views and AI.
            VehicleTrackCourse course;check(build_vehicle_proving_ground(course,error),error.c_str());
            for(auto& polygon:course.world.collision.polygons)polygon.unknown0[2]=1;
            auto world=std::make_unique<RaceSession>(course);auto a=course.start,b=course.start;
            a.ground.x+=16;b.ground.x+=32;
            check(world->set_rivals({{spec,a,stock_240sx_hull(),"p2"},{spec,b,stock_240sx_hull(),"ai"}},error),error.c_str());
            check(world->start(error),error.c_str());
            while(race_car_held(world->timing()))check(world->advance({},error),error.c_str());
            check(world->advance({},error),error.c_str());
            check(p.load_presets(data,error),error.c_str());
            auto& falling=const_cast<VehicleState&>(world->car());
            falling.position.z+=1;falling.velocity.z=-8;
            float peak_impulse=0;unsigned landing_sparks=0;
            for(unsigned frame=0;frame<45;++frame) {
                check(world->advance({},error),error.c_str());p.advance(*world,&world->rival(0));
                const auto& impulse=world->car().bottom_out_impulse;
                peak_impulse=std::max(peak_impulse,((impulse[0]+impulse[1])+impulse[2])+impulse[3]);
                check(world->telemetry().model.bottom_out_impulse==impulse,"world contact tail lost its final FX accumulator");
                for(std::size_t i=0;i<p.spark_count();++i)
                    landing_sparks+=p.spark(i).effect==RaceParticleEffect::bottom_sparks && p.spark(i).water_owner==0;
            }
            std::cout<<"GAME_LANDING peak_impulse="<<peak_impulse<<" observed_sparks="<<landing_sparks<<'\n';
            check(peak_impulse>1 && landing_sparks>0,"ordinary RaceSession landing failed to feed the native spark producer");
            check(p.load_presets(data,error),error.c_str());
            for(unsigned actor=0;actor<3;++actor)RaceWorldContactProbe::bottom_fx(actor?const_cast<RaceSession&>(world->rival(actor-1)):*world);
            p.advance(*world,&world->rival(0));
            std::array<unsigned,3> owners{};
            for(std::size_t i=0;i<p.spark_count();++i)if(p.spark(i).water_owner<3)++owners[p.spark(i).water_owner];
            std::cout<<"LANDING_OWNERS "<<owners[0]<<' '<<owners[1]<<' '<<owners[2]<<'\n';
            check(owners==std::array<unsigned,3>{104,104,104},"split/player/rival path lost a bottom-out group");
            const auto count=p.spark_count();p.advance(*world,&world->rival(0));
            check(p.spark_count()==count,"second view duplicated the landing burst");
        } else if(kind=="road_visibility") {
            for(auto& w:t.model.wheels)w.effect_slip_velocity={};
            t.velocity.x=40;t.model.owner_speeds.magnitude=40;
            for(unsigned i=0;i<46;++i)frame(p,t,spec,1);
            const auto alive=p.smoke_count();check(alive>16,"road birth/lifetime control vanished");
            std::size_t visible=0;VehicleVec3 at;double diameter=0,opacity=0;std::array<unsigned,3> tint{};
            for(std::size_t i=0;i<alive;++i)visible+=p.visual(p.smoke(i),at,diameter,opacity,tint);
            RenderOptions options;options.width=320;options.height=180;
            const auto& oldest=p.smoke(0);at={oldest.origin.x+oldest.displacement.x,
                oldest.origin.y+oldest.displacement.y,oldest.origin.z+oldest.displacement.z};
            options.camera.distance=2;options.camera.target={at.x,at.z,-at.y};
            std::vector<unsigned char> image(options.width*options.height*4);
            check(draw_race_particles(image,options,p,error),error.c_str());
            std::size_t pixels=0;for(std::size_t i=0;i<image.size();i+=4)pixels+=image[i]||image[i+1]||image[i+2];
            std::cout<<"ROAD_DRAW alive="<<alive<<" visible="<<visible<<" pixels="<<pixels<<'\n';
            // Original404240..40425E rejects this key before owner/depth/age.
            check(visible==0 && pixels==0,"stock road driver reached generic draw despite original40425E suppression");
            check(p.smoke_count()==alive,"draw suppression changed allocation/lifetime");
            for(const auto key:{0xccf7bd60U,0xccf7bd62U}) {
                auto adjacent=data;RaceChunkPayload defs,places;std::vector<RaceTableIssue> issues;
                check(find_race_chunk(adjacent.data(),adjacent.size(),0x35021,defs,issues) &&
                      find_race_chunk(adjacent.data(),adjacent.size(),0x35020,places,issues),"adjacent-key tables");
                const auto replace=[&](RaceChunkPayload table,unsigned stride,unsigned offset) {
                    for(auto at=((table.begin+15)&~std::uint64_t{15})+16;at+stride<=table.end;at+=stride) {
                        std::uint32_t old;std::memcpy(&old,adjacent.data()+at+offset,4);
                        if(old==0xccf7bd61U)std::memcpy(adjacent.data()+at+offset,&key,4);
                    }
                };
                replace(defs,0x220,8);replace(places,0xc0,12);
                check(p.load_presets(adjacent,error),error.c_str());t=sample();
                for(auto& w:t.model.wheels)w.effect_slip_velocity={};
                t.model.owner_speeds.magnitude=40;
                for(unsigned i=0;i<20;++i)frame(p,t,spec,1);
                bool drawn=false;for(std::size_t i=0;i<p.smoke_count();++i)
                    drawn|=p.visual(p.smoke(i),at,diameter,opacity,tint)&&opacity>0;
                check(drawn,"road group was hidden instead of the exact native preset key");
            }
            check(p.load_presets(data,error),error.c_str());t=sample();
            //Same original main-view draw path: enabled skid/water controls
            //keep their packed color, alpha and metre-sized quad at half life.
            for(const auto& native:kNativeParticleVisuals) {
                RaceParticle probe;probe.effect=native.effect;probe.life_ticks=native.life;
                probe.elapsed_seconds=double(native.life-native.remaining)/1024;
                const auto draw=p.visual(probe,at,diameter,opacity,tint);
                check(draw==native.draw,"native404110 preset draw policy changed");
                if(draw) {
                    const unsigned rgba=(unsigned(std::lround(opacity*255))<<24)|(tint[0]<<16)|(tint[1]<<8)|tint[2];
                    check(rgba==native.rgba,"native404110 enabled control packed alpha/color changed");
                    check(std::abs(diameter-native.diameter)<1e-5,"native404110 enabled quad dimensions changed");
                }
            }
            for(auto& w:t.model.wheels)w.effect_slip_velocity={};
            t.velocity.x=40;t.model.owner_speeds.magnitude=40;
            for(unsigned i=0;i<20;++i)frame(p,t,spec,14);
            bool water_visible=false;for(std::size_t i=0;i<p.water_count();++i)
                water_visible|=p.visual(p.water(i),at,diameter,opacity,tint) && opacity>0;
            check(water_visible,"enabled water control was hidden with road aerosol");
            check(p.load_presets(data,error),error.c_str());t=sample();
            for(unsigned i=0;i<30;++i)frame(p,t,spec);
            bool tyre_visible=false;for(std::size_t i=0;i<p.smoke_count();++i)
                tyre_visible|=p.visual(p.smoke(i),at,diameter,opacity,tint) && opacity>0;
            check(tyre_visible,"enabled wheel-slip control was hidden with road aerosol");
        } else if(kind=="road") {
            for(auto& w:t.model.wheels)w.effect_slip_velocity={};
            t.velocity.x=40;t.model.owner_speeds.magnitude=40;
            frame(p,t,spec,1);
            check(p.smoke_count()==2,"road intensity must issue its native burst on each visual frame");
            for(unsigned i=0;i<45;++i)frame(p,t,spec,1);
            check(p.smoke_count()>16,"road aerosol retained the old timer or eight-slot cap");
            for(std::size_t i=0;i<p.smoke_count();++i)
                check(p.smoke(i).effect==RaceParticleEffect::road_spray,"road aerosol mixed with tyre or deep-water groups");
        } else if(kind=="impact") {
            for(auto& w:t.model.wheels)w.contact=false;
            frame(p,t,spec);RacePairFrame pair;pair.count=1;
            auto& contact=pair.pairs[0].contact;contact.applied=true;
            contact.point={1,2,3};contact.impulse={15,0,0};
            p.emit_car_impact_sparks(pair,{});
            check(p.spark_count()==10,"strong car impact clipped its native ten-particle burst to eight");
            check(p.smoke_count()>0,"impact lost its separate smoke preset");
        } else throw std::runtime_error("unknown case");
        std::cout<<"PASS native vehicle particle "<<kind<<" smoke="<<p.smoke_count()<<" sparks="<<p.spark_count()<<'\n';
    }catch(const std::exception& e){std::cerr<<"FAIL native vehicle particle: "<<e.what()<<'\n';return 1;}
}
