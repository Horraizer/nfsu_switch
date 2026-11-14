#include "nfsu/lan_link.h"
#include "nfsu/lan_race.h"
#include "nfsu/arg_path.h"
#include "nfsu/sha256.h"
#include "nfsu/lan_scene.h"
#include "nfsu/texture.h"
#include "nfsu/career.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <thread>
#include <stdexcept>

using namespace nfsu;
namespace {
std::uint64_t now_us(){return std::uint64_t(std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());}
void require(bool okay,const std::string& error){if(!okay)throw std::runtime_error(error);}
void check_split(const std::filesystem::path& root, std::string& error) {
    CareerCatalog catalog;
    require(catalog.load_free_drive(root,error),error);
    LanRaceOptions options;
    options.laps=1;
    options.rides[0].car=bin_hash("CIVIC");
    options.rides[1].car=bin_hash("MIATA");
    for(const auto mode:{QuickRaceMode::circuit,QuickRaceMode::sprint}){
        const auto choices=catalog.quick_courses(mode);
        require(!choices.empty(),"split-screen race type has no tracks");
        const auto& route=catalog.free_courses().at(choices.front());
        LanRaceWorld world;
        require(world.load_local(root,route.event,route.round,options,error),error);
        require(world.models()[0]=="CIVIC"&&world.models()[1]=="MIATA","split-screen model mapping");
        require(world.session().course().route.point_to_point==(mode==QuickRaceMode::sprint)&&
            world.session().course().route.laps==1,"split-screen route does not match selected mode");
        require(world.session().rival_count()==1&&!world.session().traffic()&&
            world.session().timing().racers.size()==2,"split-screen must start with only two humans");
        std::array<LanRaceView,2> views;
        for(unsigned i=0;i<2;++i)
            require(views[i].open(world.session(),world.cameras(i),i,error,&world.host_hull()),error);
        std::array<GamepadFrame,2> input{};
        for(auto& player:input)player.pads[0].connected=true;
        const auto gas=gamepad_button_bit(GamepadButton::trigger_right);
        for(unsigned active:{0U,1U}){
            input[active].pads[0].buttons=gas;
            input[1-active].pads[0].buttons=0;
            for(unsigned step=0;step<(active?60U:300U);++step)
                require(world.advance(input,error),error);
            const auto state=world.snapshot();
            require(state.go&&state.cars[active].throttle>.9F&&
                state.cars[1-active].throttle==0.F,"split-screen throttle reached the wrong racer");
            require(world.session().work().rival_driver_updates==0,"split-screen invoked a computer driver");
        }
        const auto state=world.snapshot();
        for(auto& view:views)require(view.apply(state,1./60,error),error);
        require(views[1].apply(state,1./60,error,false,true),error);
        require(views[0].session().camera().pov==0&&views[1].session().camera().pov!=0,
            "split-screen camera selection crossed player slots");
        LanScene scene;
        require(scene.load_local(root,route.event,route.round,options,world.models(),error),error);
        std::array<RaceGpuFrame,2> frames;
        RaceHudFrame hud;
        for(unsigned i=0;i<2;++i) {
            auto& frame=frames[i];
            require(scene.split_frame(world.session(),views[i],i,frame,hud,error),error);
            require(frame.track_draws&&frame.car_draws&&validate_recorded_commands(frame.commands,error),error);
            if(i) {
                std::vector<const RecordedDraw*> definitions;
                for(auto draw=frame.commands.static_track_draw_count();draw<frame.commands.draw_count();++draw)
                    definitions.push_back(&frame.commands.draw_view(draw).definition);
                RaceGpuFrame next;
                require(scene.split_frame(world.session(),views[0],0,next,hud,error)&&
                    validate_prepared_dynamic_prefix(definitions,next.commands,error),error);
                require(scene.split_frame(world.session(),views[i],i,frame,hud,error),error);
            }
            require(frame.commands.width==1280&&frame.commands.height==720&&
                std::abs(frame.commands.frame.projection[0]*1280-frame.commands.frame.projection[1]*360)<.001F,
                "split-screen projection must preserve square pixels");
            require(hud.quads&&std::abs(hud.readout.engine_rpm-state.cars[i].rpm)<.1,
                "split-screen HUD reads the wrong racer");
            for(unsigned v=0;v<hud.quads*4;++v){
                const auto& vertex=hud.vertices[v];
                require(std::isfinite(vertex.px)&&std::isfinite(vertex.py)&&
                    vertex.px>=-128&&vertex.px<=1408&&vertex.py>=-128&&vertex.py<=488,
                    "split-screen HUD geometry is outside the clipped view");
            }
            if(i)require(!frames[0].commands.generation_valid()&&
                frames[1].commands.package_identity()==frames[0].commands.package_identity(),
                "split-screen must reuse the immutable track and car package");
        }
        const auto& first=frames[0].commands.frame;
        const auto& second=frames[1].commands.frame;
        require(!std::equal(std::begin(first.eye),std::end(first.eye),std::begin(second.eye)),
            "split-screen cameras record identical views");
    }
    std::cout<<"PASS local split-screen circuit and sprint\n";
}
std::string frame_hash(const LanRaceFrame& frame){LanMessage m;m.kind=LanMessageKind::snapshot;m.frame=frame;std::vector<std::uint8_t> bytes;std::string error;require(lan_encode(m,bytes,error),error);return sha256_hex(sha256(bytes.data(),bytes.size()));}
}
int main(int argc,char** argv){
    try {
        std::filesystem::path root,trace;std::string ip,error;bool host=false,fast=false,inspect=false,split=false;unsigned port=kLanPort,steps=1800;
        LanRaceOptions options;bool guest_view=false,list_cars=false,demo_body=false;
        for(int i=1;i<argc;++i){const std::string a=argv[i];
            if(a=="--inspect")inspect=true;
            else if(a=="--split-screen")split=true;
            else if(a=="--list-cars")list_cars=true;
            else if(a=="--demo-body")demo_body=true;
            else if(a=="--host")host=true;
            else if(a=="--join"&&i+1<argc)ip=argv[++i];
            else if(a=="--root"&&i+1<argc)root=std::filesystem::u8path(argv[++i]);
            else if(a=="--trace"&&i+1<argc)trace=std::filesystem::u8path(argv[++i]);
            else if(a=="--steps"&&i+1<argc)steps=unsigned(std::stoul(argv[++i]));
            else if(a=="--port"&&i+1<argc)port=unsigned(std::stoul(argv[++i]));
            else if(a=="--guest-view")guest_view=true;
            else if((a=="--car"||a=="--guest-car")&&i+1<argc)
                options.rides[a=="--car"?0:1].car=bin_hash(argv[++i]);
            else if((a=="--host-part"||a=="--guest-part")&&i+2<argc){
                const auto slot=std::stoul(argv[++i]);const auto hash=std::stoull(argv[++i],nullptr,0);
                auto& installed=options.rides[a=="--host-part"?0:1].installed;
                require(slot<installed.size()&&hash<=UINT32_MAX,"LAN cosmetic slot or hash out of range");
                installed[slot]=static_cast<std::uint32_t>(hash);
            }
            else if((a=="--course"||a=="--laps"||a=="--tier")&&i+1<argc){
                const auto value=std::stoul(argv[++i]);require(value<=255,"LAN option out of range");
                auto& field=a=="--course"?options.course:a=="--laps"?options.laps:options.tier;
                field=static_cast<std::uint8_t>(value);
            }
            else if(a=="--fast")fast=true;
            else throw std::runtime_error("unknown/incomplete LAN probe argument");
        }
        require(!root.empty()&&(split||inspect||list_cars||(host!=!ip.empty()))&&port<=65535&&steps>0&&steps<=120000,
            "use --root PATH and --inspect, --list-cars, --host or --join IP");
        if(split){check_split(root,error);return 0;}
        if(list_cars){
            std::vector<std::string> models;require(load_lan_car_names(root,models,error),error);
            for(const auto& model:models)std::cout<<model<<'\n';
            return 0;
        }
        std::ofstream output;
        if(!trace.empty()){require(!std::filesystem::exists(trace),"LAN trace already exists");output.open(trace,std::ios::binary);require(bool(output),"LAN trace cannot open");}
        if(demo_body){
            require(inspect,"cosmetic fixture requires --inspect");
            CareerCatalog catalog;require(catalog.load_free_drive(root,error),error);
            std::vector<std::string> models;require(load_lan_car_names(root,models,error),error);
            const auto& parts=catalog.tuning().parts().records;
            for(auto& ride:options.rides){
                const auto* car=find_lan_car_name(models,ride.car);require(car!=nullptr,"cosmetic fixture car unavailable");
                for(const unsigned slot:{1U,17U,30U,116U}){
                    const auto part=std::find_if(parts.begin(),parts.end(),[&](const auto& item){
                        return item.slot==static_cast<int>(slot)&&item.upgrade_level==1&&
                            catalog.tuning().find_part(*car,slot,item.part_name_hash)==&item;
                    });
                    require(part!=parts.end(),"cosmetic fixture part unavailable");
                    ride.installed[slot]=part->part_name_hash;
                }
            }
        }
        LanRaceWorld world;require(world.load(root,error,options),error);LanRaceView view;
        const unsigned local=inspect?unsigned(guest_view):unsigned(!host);
        require(view.open(world.session(),world.cameras(local),local,error,&world.host_hull()),error);
        require(view.apply(world.snapshot(),1./60,error),error);
        if(inspect){
            LanScene scene;require(scene.load(root,options,world.models(),local,error),error);RaceGpuFrame gpu;RaceHudFrame hud;
            require(scene.frame(view,gpu,hud,error)&&validate_recorded_commands(gpu.commands,error),error);
            require(gpu.track_draws>0&&gpu.car_draws>0&&gpu.triangles>0,"LAN scene has no actual track/car draws");
            require(world.session().course().route.laps==options.laps,"LAN route lap count differs from lobby");
            require(view.session().spec().nitrous.installed==(options.tier!=0)&&
                view.session().rival(0).spec().nitrous.installed==(options.tier!=0),"LAN boost tier is missing from a selected car");
            if(demo_body){
                unsigned neon=0;
                for(std::size_t i=0;i<gpu.commands.draw_count();++i)
                    if(gpu.commands.draw_view(i).definition.name.find("car underbody neon")!=std::string::npos)++neon;
                require(neon==2,"both configured cars must draw their own neon");
            }
            std::cout<<"LAN_SCENE_PASS track_draws="<<gpu.track_draws<<" car_draws="<<gpu.car_draws<<" triangles="<<gpu.triangles<<
                " course="<<kLanCourses[options.course].name<<" reverse="<<world.session().course().route.reverse<<
                " laps="<<world.session().course().route.laps<<" car="<<world.models()[local]<<
                " opponent="<<world.models()[1-local]<<" tier="<<unsigned(options.tier)<<"\n";
            return 0;
        }
        LanLink link;auto now=now_us();auto nonce=std::uint32_t(now)^std::uint32_t(now>>32);if(!nonce)nonce=1;
        require(host?link.host(std::uint16_t(port),nonce,now,error):link.join(ip,std::uint16_t(port),nonce,now,error),error);
        if(host)require(link.configure_race(options,false,error),error);
        std::cout<<"LAN_LISTEN role="<<(host?"host":"guest")<<" port="<<link.port()<<"\n"<<std::flush;
        RaceRouteDriver pilot(RaceDrivingLine::event1_forward);std::uint64_t next=now,last_send=0,finish_at=0;
        std::uint64_t snapshots=0,simulations=0,last_print=~std::uint64_t(0);double simulation_ms=0,maximum_ms=0;
        LanRaceFrame latest=world.snapshot();bool done=false,ready=false;std::uint32_t consumed_input=0;
        const auto log=[&](const LanRaceFrame& f){
            if(output)output<<f.step<<' '<<frame_hash(f)<<' '<<f.cars[0].position[0]<<' '<<f.cars[0].position[1]<<' '<<f.cars[1].position[0]<<' '<<f.cars[1].position[1]<<' '<<unsigned(f.cars[0].status)<<' '<<unsigned(f.cars[1].status)<<'\n';
            if(f.step/300!=last_print){last_print=f.step/300;std::cout<<"LAN_FRAME step="<<f.step<<" laps="<<f.cars[0].highest_lap<<","<<f.cars[1].highest_lap<<" status="<<unsigned(f.cars[0].status)<<","<<unsigned(f.cars[1].status)<<"\n"<<std::flush;}
        };
        while(now_us()-now<600000000ULL){
            const auto tick=now_us();
            if(!link.poll(tick,error)){if(done&&error=="LAN peer left")break;throw std::runtime_error(error);}
            if(link.paired()&&!link.race_launched()){
                if(host&&link.guest_ride_ready())require(link.configure_race(options,true,error),error);
                else if(!host&&!link.guest_ride_ready())require(link.confirm_guest_ride(options.rides[1],error),error);
            }
            if(!ready&&link.race_launched()){
                require(link.race_options()==options,"frozen room settings differ from prepared world");
                link.set_ready();ready=true;
            }
            if(link.connected()){
                if(host&&(fast||tick>=next)&&(done||!fast||link.input_ack()!=consumed_input)){
                    if(!done){
                        std::array<GamepadFrame,2> pads{{pilot.sample(world.session()),{}}};pads[1].pads[0]=link.remote_input(tick);
                        consumed_input=link.input_ack();
                        const auto begin=now_us();require(world.advance(pads,error),error);const double elapsed=double(now_us()-begin)*.001;
                        simulation_ms+=elapsed;maximum_ms=std::max(maximum_ms,elapsed);++simulations;latest=world.snapshot();
                        done=latest.step>=steps||latest.outcome==unsigned(RaceOutcome::finished);if(done)finish_at=tick;
                    }
                    if(fast||latest.step%2==0||done){require(link.publish(latest,error),error);log(latest);++snapshots;}
                    next=tick+16667;
                    if(done&&tick-finish_at>500000)break;
                }else if(!host){
                    LanRaceFrame received;bool fresh=false;
                    if(link.take_snapshot(received)){fresh=true;
                        latest=received;require(view.apply(latest,1./60,error),error);++snapshots;log(latest);
                        done=latest.step>=steps||latest.outcome==unsigned(RaceOutcome::finished);if(done)break;
                    }
                    if(!last_send||(fast?(fresh||tick-last_send>=100000):tick-last_send>=16667)){
                        auto input=pilot.sample(view.session());require(link.send_input(input.pads[0],error),error);last_send=tick;
                    }
                }
            }
            if(fast)std::this_thread::yield();
            else std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        require(done&&snapshots>0,"LAN probe did not reach its requested boundary");
        if(output){output.flush();require(bool(output),"LAN trace write failed");}
        std::cout<<"LAN_DONE role="<<(host?"host":"guest")<<" step="<<latest.step<<" snapshots="<<snapshots
            <<" bytes_sent="<<link.sent_bytes()<<" bytes_received="<<link.received_bytes()<<" sim_mean_ms="<<(simulations?simulation_ms/double(simulations):0)
            <<" sim_max_ms="<<maximum_ms<<" finished="<<unsigned(latest.cars[0].finished)<<","<<unsigned(latest.cars[1].finished)<<"\n"<<std::flush;
        link.leave();return 0;
    }catch(const std::exception& e){std::cerr<<"FAIL LAN probe: "<<e.what()<<'\n';return 1;}
}
