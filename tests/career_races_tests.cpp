#include "nfsu/race_audio.h"
#include "nfsu/race_props.h"
#include "nfsu/intro_grid.h"
#include "nfsu/player_powertrain.h"
#include "nfsu/race_run.h"
#include "nfsu/race_traffic.h"
#include "nfsu/race_result.h"
#include "nfsu/campaign_sweep.h"
#include "nfsu/campaign_finish_save.h"
#include "nfsu/track_slice.h"
#include "nfsu/posix_file_system.h"
#include "nfsu/ai_road.h"
#include "nfsu/race_hud.h"
#include "nfsu/race_gpu_scene.h"
#include "nfsu/frontend_scene.h"
#include "nfsu/drive_session.h"
#include "nfsu/track_render.h"
#include "nfsu/ai_powertrain.h"
#include "nfsu/draw_execution.h"
#include "nfsu/deko_command_memory.h"
#include "nfsu/deko_texture_layout.h"
#include <iomanip>
#include <cmath>
#include <cstring>
#include <algorithm>
#include <iostream>
#include <fstream>
#include <cstdlib>
#include <stdexcept>
using namespace nfsu;
#include "race_stream_backing_retail.inc"
#include "career_road_native_cases.inc"
int main(int argc,char** argv) {
    try {
        if(argc<2||argc>4)throw std::runtime_error("retail-root [event] [steps]");
        const std::filesystem::path root=argv[1];
        if(argc==4&&std::string(argv[2])=="--finish-save-sweep") {
            CareerCatalog career;
            std::string error;
            if(!career.load(root,error))throw std::runtime_error(error);
            unsigned count=0;
            for(const auto& test:campaign_sweep_cases(career.events(),kCareerLastEvent)) {
                try {
                    const auto base=std::filesystem::path(argv[3])/("career-event-"+std::to_string(test.event));
                    const auto profile=load_campaign_finish_case(base,career,test,"CIVIC");
                    const auto* tournament=supported_tournament_row(test.event);
                    CareerRaceReport report;report.started=true;report.won=true;
                    save_campaign_finish_case(base,career,test,tournament&&test.round+1<tournament->count,
                        profile,{{1,2,3,4}},17,report);
                    ++count;
                } catch(const std::exception& e) {
                    throw std::runtime_error("finish save event "+std::to_string(test.event)+" round "+
                        std::to_string(test.round)+": "+e.what());
                }
            }
            std::cout<<"PASS career finish save cases="<<count<<'\n';return 0;
        }
        if(argc==3&&std::string(argv[2])=="--sweep-manifest") {
            std::vector<unsigned char> image;
            const auto table=load_race_event_table(root/"FrontEnd/FrontB.lzc",image);
            for(const auto& test:campaign_sweep_cases(table,kCareerLastEvent)) {
                SupportedRaceSelection selection;
                if(!supported_race_selection(test.event,test.round,selection)||selection.slot.track!=test.track||
                   selection.slot.direction!=test.direction)throw std::runtime_error("sweep event selection differs from retail manifest");
                std::cout<<test.event<<' '<<test.round<<'\n';
            }
            return 0;
        }
        if(argc==4&&std::string(argv[2])=="road-init") {
            std::vector<unsigned char> image;
            const auto track=load_race_route_track(root/"Tracks"/("TRACKB"+std::string(argv[3])+".lzc"),image);
            RacingLineTrack lines;std::vector<AiRoadEvent> events;std::string error;
            if(!track.ok()||!parse_racing_line_image(image.data(),image.size(),{},lines))throw std::runtime_error("road input invalid");
            const auto u32=[](const unsigned char* p){return unsigned(p[0])|(unsigned(p[1])<<8)|(unsigned(p[2])<<16)|(unsigned(p[3])<<24);};
            bool found=false;
            for(std::size_t at=0;at+8<=image.size();) {
                const auto tag=u32(image.data()+at),size=u32(image.data()+at+4);at+=8;
                if(size>image.size()-at)throw std::runtime_error("road chunk truncated");
                if(tag==0x34122){if(found||!ai_road_events(image.data()+at,size,events))throw std::runtime_error("road events invalid");found=true;}
                at+=size;
            }
            if(!found||!ai_road_initialize(track,lines,events,error))throw std::runtime_error(error);
            const unsigned track_id=unsigned(std::stoul(argv[3]));
            const auto bits=[](float value){std::uint32_t out;std::memcpy(&out,&value,4);return out;};
            unsigned checked=0;
            for(const auto& expected:native_roads)if(expected.track==track_id) {
                const auto* r=lines.find(static_cast<std::int16_t>(expected.id));
                if(!r||bits(r->progress_begin)!=expected.begin||bits(r->progress_end)!=expected.end)
                    throw std::runtime_error("native road projection differs: "+std::to_string(expected.id));
                checked+=2;
            }
            for(const auto& expected:native_events)if(expected.track==track_id) {
                const auto e=std::find_if(events.begin(),events.end(),[&](const auto& v){
                    return v.source_route==expected.source&&v.source_point==expected.point&&
                        v.target_route==expected.target&&v.target_point==expected.target_point;});
                if(e==events.end()||bits(e->progress)!=expected.progress)throw std::runtime_error("native event projection differs");
                ++checked;
            }
            if(checked!=lines.routes.size()*2+events.size())throw std::runtime_error("native road coverage incomplete");
            std::cout<<std::setprecision(9);
            for(const auto& r:lines.routes)std::cout<<"ROAD "<<r.index<<' '<<r.progress_begin<<' '<<r.progress_end<<'\n';
            for(const auto& e:events)std::cout<<"EVENT "<<unsigned(e.source_route)<<' '<<e.source_point<<' '<<unsigned(e.target_route)<<' '<<e.target_point<<' '<<e.progress<<'\n';
            std::cout<<"PASS road initialization\n";return 0;
        }
        if(argc>2&&(std::string(argv[2])=="difficulty"||std::string(argv[2])=="fixed-difficulty")) {
            const auto table=load_car_type_table(root/"Global/GLOBALB.BUN");
            const auto record=[&](const std::string& name) {
                const auto index=find_car_type(table,name);if(!table.ok()||index==kNoCarType)throw std::runtime_error("adaptive record absent");
                std::ifstream in(root/"Global/GLOBALB.BUN",std::ios::binary);in.seekg(table.records[index].offset);
                std::array<unsigned char,kVehicleRecordSize> raw{};in.read(reinterpret_cast<char*>(raw.data()),raw.size());
                if(!in)throw std::runtime_error("adaptive record read failed");return raw;
            };
            const int VehicleUpgradeLevels::*fields[11]={&VehicleUpgradeLevels::weight,&VehicleUpgradeLevels::suspension,
                &VehicleUpgradeLevels::engine_a,&VehicleUpgradeLevels::induction,&VehicleUpgradeLevels::nitrous,
                &VehicleUpgradeLevels::engine_b,&VehicleUpgradeLevels::gearbox,&VehicleUpgradeLevels::tyres,
                &VehicleUpgradeLevels::grip,&VehicleUpgradeLevels::aero_a,&VehicleUpgradeLevels::aero_b};
            std::string reference_name,target_name;unsigned level=0;
            while(std::cin>>reference_name>>target_name>>level) {
                int selector=-1;if(std::string(argv[2])=="fixed-difficulty"&&!(std::cin>>selector))throw std::runtime_error("missing fixed selector");
                VehicleUpgradeLevels reference_levels,target_levels;
                int VehicleUpgradeLevels::*mutable_fields[11]={&VehicleUpgradeLevels::weight,&VehicleUpgradeLevels::suspension,
                    &VehicleUpgradeLevels::engine_a,&VehicleUpgradeLevels::induction,&VehicleUpgradeLevels::nitrous,
                    &VehicleUpgradeLevels::engine_b,&VehicleUpgradeLevels::gearbox,&VehicleUpgradeLevels::tyres,
                    &VehicleUpgradeLevels::grip,&VehicleUpgradeLevels::aero_a,&VehicleUpgradeLevels::aero_b};
                for(auto field:mutable_fields)reference_levels.*field=int(level);
                auto a=record(reference_name),b=record(target_name);VehicleSpec reference,target;std::string error;
                if(!make_race_vehicle_spec(a.data(),a.size(),reference_levels,reference,error)||
                    !adaptive_rival_setup(b.data(),b.size(),reference,reference_levels,target_levels,target,error,selector))throw std::runtime_error(error);
                float rr=0,tr=0,unused=0;ai_setup_performance(reference.handling,rr,unused);ai_setup_performance(target.handling,tr,unused);
                std::uint32_t rb=0,tb=0;std::memcpy(&rb,&rr,4);std::memcpy(&tb,&tr,4);
                std::cout<<"ADAPTIVE "<<reference_name<<' '<<target_name<<' '<<level;
                for(auto field:fields)std::cout<<' '<<target_levels.*field;
                std::cout<<' '<<rb<<' '<<tb;
                for(float value:{target.handling.downforce_front,target.handling.downforce_rear,target.handling.aero_preload}) {
                    std::uint32_t word=0;std::memcpy(&word,&value,4);std::cout<<' '<<word;
                }
                std::cout<<'\n';
            }
            return 0;
        }
        if(argc==3&&std::string(argv[2])=="audit-setups") {
            std::vector<unsigned char> front_image;const auto events=load_race_event_table(root/"FrontEnd/FrontB.lzc",front_image);
            if(!events.ok())throw std::runtime_error("campaign setup event table invalid");
            unsigned count=0;
            for(const auto& event:events.records) {
                if(!event.number||event.number>113||event.kind==8)continue;
                for(unsigned difficulty=0;difficulty<3;++difficulty) {
                    if(event.type==unsigned(RaceType::drift)) {
                        for(unsigned grade=0;grade<4;++grade) {
                            DriftCompetition competition;std::string error;
                            if(!competition.load(root,event.number,difficulty,grade,300,0x60000000U,error))
                                throw std::runtime_error("drift setup event="+std::to_string(event.number)+": "+error);
                            ++count;
                        }
                        continue;
                    }
                    for(unsigned round=0;round<event.races.size();++round) {
                        const auto& race=event.races[round];
                        const auto setup=load_race_grid_setup(root/"FrontEnd/FrontB.lzc",root/"Global/GLOBALB.BUN",
                            root/"Tracks"/track_bundle_file_name(race.track),event.number,"CIVIC",difficulty,round);
                        if(!setup.ok())throw std::runtime_error("grid metadata event="+std::to_string(event.number)+
                            ": "+(setup.issues.empty()?"no descriptor":setup.issues.front().message));
                        const auto& grid=*setup.descriptor;unsigned humans=0;
                        for(unsigned i=0;i<grid.racer_count;++i) {
                            const auto& actor=grid.actors[i];humans+=actor.role==Event0Role::human;
                            if(actor.ai_mode>2||(actor.role==Event0Role::opponent&&(actor.selected_context<0||actor.selected_context>=10)))
                                throw std::runtime_error("unsupported rival context in campaign");
                        }
                        if(humans!=1||grid.race.track!=race.track||grid.race.direction!=race.direction||
                           grid.traffic.requested_configs>kRaceTrafficCapacity)
                            throw std::runtime_error("campaign grid/traffic metadata mismatch");
                        std::cout<<"GRID_META event="<<event.number<<" round="<<round<<" difficulty="<<difficulty
                            <<" racers="<<grid.racer_count<<" traffic="<<unsigned(grid.traffic.requested_configs)<<'\n';
                        ++count;
                    }
                }
            }
            std::cout<<"PASS campaign difficulty/roster/traffic setup cases="<<count<<'\n';return 0;
        }
        const unsigned event=argc>2?unsigned(std::stoul(argv[2])):1;
        const unsigned steps=argc>3?unsigned(std::stoul(argv[3])):600;
        const bool driving_check=std::getenv("NFSU_CAREER_DRIVING_CHECK")!=nullptr;
        const bool finish_sweep=std::getenv("NFSU_CAREER_FINISH_SWEEP")!=nullptr;
        unsigned driving_frames=0;double driving_speed=0;
        const unsigned frame_limit=steps+(driving_check?2400U:0U);

        std::string error;
        const auto verify=[&](bool ok,unsigned line){if(!ok)throw std::runtime_error("line "+std::to_string(line)+": "+error);};
#define check(...) verify((__VA_ARGS__),__LINE__)
        const std::string player_name="WWW_[Racer]~";
        const auto verify_finish_sweep=[&](RaceRun& run,RaceGpuScene& scene,unsigned round) {
            RaceGpuFrame frame;
            check(scene.frame(run.session(),frame,error));
            std::vector<const RecordedDraw*> definitions;
            for(auto i=frame.commands.static_track_draw_count();i<frame.commands.draw_count();++i)
                definitions.push_back(&frame.commands.draw_view(i).definition);
            const auto record=[&] {
                check(scene.frame(run.session(),frame,error)&&frame.car_draws&&frame.track_draws&&
                    validate_recorded_commands(frame.commands,error)&&validate_prepared_dynamic_prefix(definitions,frame.commands,error));
            };
            const auto tick=[&](bool gas) {
                const auto buttons=gas?gamepad_button_bit(GamepadButton::trigger_right):0;
                check(run.simulate(race_input_frame({1,buttons,0},run.session().timing().step),error)&&run.commit(error));
            };
            for(unsigned i=0;i<2400;++i) {
                const auto& t=run.session().timing();
                if(t.go_valid&&t.step_ticks-t.go_ticks>=2*kRaceTicksPerSecond)break;
                tick(t.go_valid);
            }
            record();
            check(run.arm_finish_probe(error));tick(false);
            check(run.session().timing().outcome==RaceOutcome::finished&&run.session().player_won());
            record();
            if(run.session().rival_count()||run.session().is_drift()) {
                for(unsigned i=0;i<360;++i) {
                    tick(false);
                    if(i>=179&&run.session().car().owner_speeds.magnitude<=1.F)break;
                }
            }
            record();
            const std::array<std::string,4> labels{{"P","A","B","C"}};
            auto values=post_race_report(run.session(),RaceResultStatistics{}.values(run.session(),labels[0]),labels,0);
            check(values.at("PlayerRow")=="1"&&values.at("Time1")!="0:00.00");
            check(values.at("Name1")==player_name&&values.at("UserName")==player_name&&
                values.at("RacerName")==player_name+" - P");
            if(event) {
                FrontendArt art,results;
                check(art.load_startup(root,error)&&art.load_post_race(root,error)&&art.prepare_post_race(results,error));
                PostRaceMenu menu(true,run.session().style().total()!=0,0);
                const auto* tournament=supported_tournament_row(event);
                const bool next=tournament&&round+1<tournament->count;
                if(tournament) {
                    auto places=run.session().result_positions();
                    if(run.session().is_drift())for(unsigned i=0;i<4;++i)
                        places[run.session().drift_competition().standings()[i].actor]=i+1;
                    CareerTournament result;result.event=event;result.round=round+1;
                    for(unsigned i=0;i<round;++i)result.places[i]={{1,2,3,4}};
                    result.places[round]=places;
                    bind_post_race_tournament(values,run.session(),result,places,{{0,1,2,3}},labels);
                    check(values.at("Name1")==player_name&&values.at("PlayerRow")=="1"&&
                        !values.at("Name2").empty()&&!values.at("Name3").empty()&&!values.at("Name4").empty());
                    menu.tournament(next);
                }
                menu.animate();
                FrontendUiFrame ui;
                results.record_post_race(1280,720,menu,values,ui);
                check(menu.page()==PostRacePage::results&&!ui.vertices.empty());
                PostRaceAction action=PostRaceAction::none;
                for(unsigned i=0;i<90&&action==PostRaceAction::none;++i) {
                    action=menu.advance(1.0/60.0);
                    if(action==PostRaceAction::none)action=menu.update(campaign_finish_post_input(menu));
                }
                check(action==(next?PostRaceAction::next_race:PostRaceAction::leave));
            }
            if(std::getenv("NFSU_CAREER_FINISH_SCENE")) {
                SupportedRaceSelection selection;
                check(supported_race_selection(event,round,selection));
                auto& timing=const_cast<RaceTimingState&>(run.session().timing());
                const auto before=timing.racers[0];
                for(unsigned place:{1U,4U}) {
                    timing.racers[0].position=int(place);
                    const auto live_camera=frame.commands.frame.eye[0];
                    check(scene.load_finish_scene(posix_file_system(),root,run.session(),selection.region,0,error));
                    record();
                    const auto ending_camera=frame.commands.frame;
                    scene.set_finish_scene_seconds(2.);
                    record();
                    check(std::abs(ending_camera.eye[0]-frame.commands.frame.eye[0])+
                          std::abs(ending_camera.eye[2]-frame.commands.frame.eye[2])>.01F);
                    check(timing.racers[0].position==int(place)&&timing.racers[0].finish_ticks==before.finish_ticks);
                    scene.clear_finish_scene();
                    record();
                    check(live_camera==frame.commands.frame.eye[0]);
                    std::cout<<"PASS ending NIS command stream event="<<event<<" place="<<place<<'\n';
                }
                timing.racers[0]=before;
            }
            std::cout<<"PASS career finish probe event="<<event<<'\n';
        };
        const auto verify_payload=[&](RaceGpuScene& scene,const RaceRun& run,const RaceHudAtlas& hud,
                                      const std::string& car,const VehicleUpgradeLevels& levels,const IntroGrid* grid) {
            std::vector<audio::RaceRemoteEngineBinding> bindings;
            if(grid)for(const auto& a:grid->actors)if(a.config.role==Event0Role::opponent)
                bindings.push_back({a.config.visual.car,a.config.performance?a.config.performance->engine_a:0,a.config.performance?a.config.performance->induction:0,std::any_of(a.placement_spec.induction.begin(),a.placement_spec.induction.end(),[](float value){return value>0.F;})});
            audio::RaceAudioService sound;sound.set_suspended(true);std::string diagnostic;
            const bool started=sound.start(root,diagnostic,{}, {},true,car,levels.engine_a,levels.nitrous,
                levels.tyres,bindings,levels.induction,grid?unsigned(grid->traffic.size()):0,
                run.session().props()?run.session().props()->sound_materials():0);
            if(!started && diagnostic.find("audio device unavailable:")!=0)
                throw std::runtime_error(diagnostic);
            // An unavailable output device still retains the decoded banks.
            // Missing assets/failed bank loads must not make the check cheaper.
            for(const auto* failed:{"engine:","effects:","remote0:","remote1:","remote2:","music:",
                                   "turbo:","remote Turbo","traffic:","small props:","collision:","cues:","cue sample "})
                if(diagnostic.find(failed)!=std::string::npos)throw std::runtime_error(diagnostic);
            const RacePersistentPayload payload{scene.car_bytes(),run.persistent_bytes(),
                scene.track().contacts.memory.resident_bytes,scene.cache_bytes(),sound.resident_bytes(),hud.rgba.size()};
            std::cout<<"RACE_PAYLOAD "<<payload.description()<<std::endl;
            check(check_race_persistent_payload(payload,error));
            // Include the actual two-frame command reservations. A scene-only
            // upload sum missed these and passed while the Switch refused HUD.
            RaceGpuFrame frame;check(scene.frame(run.session(),frame,error));
            const auto& commands=frame.commands;
            DrawExecutionPlan plan;check(plan_draw_execution(commands,plan,error));
            SceneMemoryRequirement need;
            check(upload_table_requirement(commands.resource_table(),deko_texture_image_bound,need,error));
            const auto page=[](std::uint64_t n){return (n+4095)&~UINT64_C(4095);};
            const auto capacity=scene.draw_capacity();
            const auto dynamic=commands.package_identity()?
                commands.draw_count()-commands.static_track_draw_count():capacity;
            const auto root_draws=commands.package_identity()?dynamic+deko_track_list_calls(capacity):capacity;
            const auto reflections=(commands.frame.options[2]&10U)?UINT64_C(4096):0;
            const auto cube=commands.race_environment?kRaceCubeDrawLimit:6*commands.frontend_cube_draws.size();
            const auto root_commands=2*page(deko_command_memory(root_draws+cube+reflections));
            const auto upload_commands=page(deko_command_memory(root_draws+cube+reflections,
                commands.resource_table().resources.size()))+page(deko_command_memory(root_draws+cube+reflections));
            const auto static_commands=commands.package_identity()?2*page(deko_command_memory(capacity)):0;
            const auto command_bytes=root_commands+static_commands;
            const auto static_uniforms=commands.package_identity()?page((commands.static_track_instance_count()+
                commands.static_track_definition_count())*UINT64_C(256)):0;
            std::uint64_t moving=0;
            for(std::size_t i=0;i<commands.static_track_instance_count();++i)
                if(commands.movable_track_instance(i))++moving;
            const auto uniform_bytes=page(2*(1+2*dynamic)*UINT64_C(256))+static_uniforms+
                page(2*moving*256)+page(2*(1+2*reflections)*256)+page(2*(6+2*cube)*256);
            // Current <=720p presentation, scene/depth, FXAA, blur and light
            // history targets, cube/reflection images, streamed vertices,
            // shader code fit this conservative allowance. Texture staging is
            // fence-retired before static commands and render/HUD targets are
            // allocated. Charge the two peaks separately, as the renderer does.
            const auto base=page(need.buffer_extent)+page(need.image_extent)+root_commands+uniform_bytes+
                page(plan.descriptor_bytes+96)+4*kMiB;
            const auto upload_bound=base-root_commands+upload_commands+page(plan.staging_bytes);
            const auto resident_bound=base+static_commands+page(hud.rgba.size())+48*kMiB;
            const auto gpu_bound=std::max(upload_bound,resident_bound);
            std::cout<<"GPU_CAPACITY bound="<<gpu_bound<<" limit="<<kGpuBudgetPlaceholder
                <<" commands="<<command_bytes<<" uniforms="<<uniform_bytes
                <<" upload="<<upload_bound<<" resident="<<resident_bound<<std::endl;
            if(gpu_bound>kGpuBudgetPlaceholder)
                throw std::runtime_error("campaign GPU capacity bound exceeds configured budget");
        };
        const std::string selected_car=std::getenv("NFSU_CAREER_TEST_CAR")?std::getenv("NFSU_CAREER_TEST_CAR"):"CIVIC";
        const unsigned difficulty=std::getenv("NFSU_CAREER_TEST_DIFFICULTY")?unsigned(std::stoul(std::getenv("NFSU_CAREER_TEST_DIFFICULTY"))):1U;
        const unsigned round=std::getenv("NFSU_CAREER_TEST_ROUND")?unsigned(std::stoul(std::getenv("NFSU_CAREER_TEST_ROUND"))):0U;
        SupportedRaceSelection selection;check(supported_race_selection(event,round,selection));
        const bool silhouettes = std::getenv("NFSU_CAREER_SHADOW_SILHOUETTE") != nullptr;
        if(selection.type==RaceType::drift) {
            RaceRun run;RaceGpuScene scene;
            check(run.set_event(event,error,round)&&run.set_car(selected_car,error)&&run.set_difficulty(difficulty,error));
            check(run.prepare_metadata(root,error));
            CarAssets drift_car;check(load_car_assets(root/"Global/GLOBALB.BUN",selected_car,drift_car,error));
            scene.set_race_visuals(true);scene.set_wet_road_reflections(true);check(scene.set_original_draw_distance(true));
            scene.set_silhouette_shadows(silhouettes);
            check(scene.load(posix_file_system(),root,error,nullptr,nullptr,false,true,true,&drift_car,nullptr,event,true,round));
            check(run.start(root,scene.track(),error,nullptr,player_name));
            const auto& drift_rows=run.session().drift_competition().standings();
            check(std::any_of(drift_rows.begin(),drift_rows.end(),[&](const auto& row) {
                return row.actor==0&&row.name==player_name;
            }));
            if(finish_sweep){verify_finish_sweep(run,scene,round);return 0;}
            RaceHudAtlas hud;check(load_race_hud_atlas(root,hud,error,false,false,true,unsigned(selection.slot.track)));
            if(std::getenv("NFSU_CAREER_SCENE_CHECK"))verify_payload(scene,run,hud,selected_car,{},nullptr);
            RaceRouteDriver driver(RaceDrivingLine::progress_spline);
            if(const char* expected=std::getenv("NFSU_CAREER_EXPECT_PROPS")) {
                if(!run.session().props()||run.session().props()->objects().size()!=std::stoul(expected))
                    throw std::runtime_error("drift world omitted authored physical objects");
            }
            for(unsigned step=0;step<frame_limit;++step) {
                const bool driving=!race_car_held(run.session().timing());
                check(run.simulate(driver.sample(run.session()),error)&&run.commit(error));
                if(driving) {++driving_frames;driving_speed=std::max(driving_speed,double(run.session().car().owner_speeds.magnitude));}
                if(driving_check&&driving_frames>=steps)break;
            }
            if(driving_check&&(driving_frames<steps||driving_speed<1))
                throw std::runtime_error("drift audit never reached moving gameplay");
            RaceGpuFrame frame;check(scene.frame(run.session(),frame,error));check(validate_recorded_commands(frame.commands,error));
            RaceHudFrame overlay;build_race_hud_frame(run.session(),hud,1280,720,overlay);
            RaceMinimapState drift_map;check(append_race_minimap(run.session(),hud,1280,720,drift_map,overlay));
            if(driving_check&&!overlay.quads)throw std::runtime_error("drift gameplay HUD remained hidden");
            std::cout<<"COVERAGE driving="<<driving_frames<<" max_speed="<<driving_speed<<" HUD="<<overlay.quads<<'\n';
            check(frame.track_draws&&frame.car_draws&&frame.triangles);
            std::cout<<"PASS career drift event="<<event<<" track="<<selection.slot.track<<" steps="<<steps<<"\n";
            return 0;
        }
        VehicleUpgradeLevels owned_levels;
        if(const char* levels=std::getenv("NFSU_CAREER_TEST_LEVELS"))check(parse_performance_levels(levels,owned_levels,error));
        const bool defer_player_visuals=silhouettes && !std::getenv("NFSU_CAREER_SHADOW_PROBE") &&
            !std::getenv("NFSU_CAREER_SHADOW_FRAME");
        IntroGrid grid;check(load_race_grid(root,event,selected_car,difficulty,grid,error,&owned_levels,nullptr,round,
            defer_player_visuals?GridPlayerVisuals::load_with_scene:GridPlayerVisuals::prepare));
        if(std::getenv("NFSU_CAREER_SHADOW_PROBE")) {
            std::cout<<std::setprecision(9);
            for(const auto& actor:grid.actors) {
                const auto& f=*actor.frame;const auto& a=actor.shadow;
                const auto texture=std::find_if(f.car_pack.textures.begin(),f.car_pack.textures.end(),
                    [&](const auto& t){return t.hash==a.texture.hash;});
                if(texture==f.car_pack.textures.end()||a.texture.mip_count!=texture->mip_count)
                    throw std::runtime_error("authored shadow mip chain discarded");
                RecordedCommands commands;RenderOptions options;
                check(record_car_shadow_commands(a,{},options,commands,error));
                if(commands.owning_uploads().resources.at(2).texture.mip_count!=a.texture.mip_count)
                    throw std::runtime_error("shadow upload lost mip chain");
                std::cout<<"SHADOW "<<actor.assets.car_type_name<<" bounds";
                for(float v:a.low)std::cout<<' '<<v;for(float v:a.high)std::cout<<' '<<v;
                std::cout<<" texture "<<a.texture.hash<<'\n';
                for(unsigned slot=0;slot<12;++slot) {
                    const auto record=f.report.slots[slot].record;
                    if(record!=kNoCarPart) {
                        const auto& part=f.parts.records.at(record);
                        std::cout<<"PART "<<actor.assets.car_type_name<<' '<<slot;
                        for(auto h:part.lod_mesh_hashes)std::cout<<' '<<h;
                        std::cout<<'\n';
                    }
                }
            }
        }
        const auto human=std::find_if(grid.actors.begin(),grid.actors.end(),[](const auto& a){return a.config.role==Event0Role::human;});
        if(human==grid.actors.end()||human->config.visual.car!=selected_car||!human->config.performance ||
           (event!=0&&!performance_levels_equal(*human->config.performance,owned_levels)))throw std::runtime_error("owned stock human not preserved");
        if(defer_player_visuals && (human->frame || human->hull.points.empty()))
            throw std::runtime_error("deferred player visuals must retain only the physics setup");
        std::vector<RaceRivalSetup> rivals;std::array<int,3> contexts{};
        for(auto& a:grid.actors) {
            std::cout<<"GRID "<<event<<' '<<unsigned(a.config.final_grid)<<' '<<a.config.visual.car
                <<' '<<a.config.event_label<<" context="<<a.config.selected_context
                <<" ground="<<a.pose.ground.x<<','<<a.pose.ground.y<<','<<a.pose.ground.z<<" marker="<<a.config.marker.raw_position[2]<<'\n';
            if(event==4) {
                std::cout<<"DRAG_GEAR actor="<<unsigned(a.config.final_grid)
                    <<" profile="<<difficulty<<" context="<<a.config.selected_context
                    <<" clutch_scale="<<a.placement_spec.drag_clutch_scale<<" offsets=";
                for(float v:a.placement_spec.drag_shift_offsets)std::cout<<v<<',';
                std::cout<<'\n';
                float from[3]={float(a.pose.ground.x),float(a.pose.ground.y),a.config.marker.raw_position[2]+2};
                const float to[3]={from[0],from[1],-128};
                for(unsigned j=0;j<6;++j) {
                    TrackContact hit;
                    if(query_track_contact_segment(grid.course.world,from,to,hit)!=TrackContactStatus::hit)break;
                    const auto plane=vehicle_track_ground_plane(grid.course.world,hit.polygon);
                    const float z=plane[2]?-(plane[0]*from[0]+plane[1]*from[1]-plane[3])/plane[2]:9999;
                    std::cout<<"GROUND "<<hit.polygon<<" hit="<<hit.point[2]<<" plane="<<z<<" up="<<hit.up<<" kind="<<int(hit.kind)<<'\n';
                    from[2]=hit.point[2]-.05F;
                }
            }
            if(a.config.role==Event0Role::opponent) {
                contexts[rivals.size()]=a.config.selected_context;
                rivals.push_back({a.placement_spec,a.pose,a.hull,a.config.event_label,a.config.visual.car,a.config.ai_mode});
            }
        }
        if(rivals.size()+1!=grid.setup->racer_count)throw std::runtime_error("authored rival count differs");
        RaceRun run;check(run.set_event(event,error,round));check(run.set_difficulty(difficulty,error));
        check(run.set_car(selected_car,error));check(run.set_performance_levels(*human->config.performance,error));
        check(run.set_steering(RaceSteering::recovered_predicate1,error));
        check(run.set_intro_grid(human->pose,human->hull,rivals,contexts,error));
        std::vector<RaceTrafficSetup> traffic;
        for(auto& a:grid.traffic)traffic.push_back({a.placement_spec,a.hull,a.assets.car_type_name});
        check(run.set_intro_traffic(traffic,error));
        if(std::getenv("NFSU_CAREER_TRAFFIC_SWEEP")) {
            if(traffic.empty())throw std::runtime_error("traffic sweep requires an authored pool");
            RaceTraffic probe;std::uint32_t random=1;
            check(probe.load(root,grid.course,traffic,error,std::max(1U,unsigned(grid.setup->traffic.selector)),&random));
            VehicleState observer;VehicleModel observer_model(human->placement_spec);
            check(observer_model.reset(observer,human->pose));
            const std::array<const VehicleState*,4> racers{{&observer}};
            const auto observer_shape=race_contact_box(human->hull);
            const std::array<const RaceContactBox*,4> shapes{{&observer_shape}};
            unsigned active_samples=0;double peak=0;
            const auto& spline=grid.course.route.spline;
            for(unsigned sample=0;sample<=100;++sample) {
                float xy[3];evaluate_track_path(spline,float(spline.count-1)*sample/100.F,0,xy);
                observer.position.x=xy[0];observer.position.y=xy[1];
                for(unsigned frame=0;frame<60;++frame) {
                    const auto tick=sample*60+frame;
                    check(probe.advance(racers,tick,error,int(tick*4000/60),1.F/60.F,{},shapes));
                    for(const auto& actor:probe.actors())if(actor.active) {
                        ++active_samples;const auto& v=actor.car.velocity;
                        peak=std::max(peak,std::sqrt(v.x*v.x+v.y*v.y+v.z*v.z));
                    }
                }
            }
            std::cout<<"TRAFFIC_SWEEP spawns="<<probe.spawns<<" active_samples="<<active_samples<<" peak_mps="<<peak<<'\n';
            if(!probe.spawns||!active_samples||!std::isfinite(peak)||peak<1)
                throw std::runtime_error("authored traffic never activates along the course");
            std::cout<<"PASS campaign traffic course coverage\n";return 0;
        }

        const bool backing_test=std::getenv("NFSU_CAREER_ZONE_BACKING")!=nullptr;
        const bool complete_backing=backing_test||(finish_sweep&&(selection.type==RaceType::sprint||
            selection.type==RaceType::drag||selection.type==RaceType::time_trial));
        TrackSlice track;check(load_track_slice(posix_file_system(),root,track,error,nullptr,nullptr,event,false,round,
            complete_backing?TrackStreamSet::zones:TrackStreamSet::permanent));
        const bool start_scene_check=selection.type!=RaceType::drag && std::getenv("NFSU_CAREER_START_SCENE_CHECK")!=nullptr;
        StartCrowd start_camera;RaceStartClock start_clock;unsigned start_scene=0;
        if(start_scene_check) {
            check(select_road_start_scene(posix_file_system(),root,unsigned(grid.actors.size()),unsigned(grid.setup->desired_player_grid),round,start_scene,error));
            if(const char* value=std::getenv("NFSU_CAREER_TEST_START_SCENE"))start_scene=unsigned(std::stoul(value));
            check(start_camera.load_camera(posix_file_system(),root,start_scene,selection.region,start_clock,error));
            check(run.set_start_clock(start_clock.release,start_clock.go,error));
        }
        check(run.start(root,track,error,nullptr,player_name));
        check(run.session().leaderboard_label(0)==player_name);
        if(start_scene_check) {
            check(run.session().start_cinematic());
            if(const char* tick=std::getenv("NFSU_CAREER_START_CAPTURE_TICK"))for(unsigned t=0;t<unsigned(std::stoul(tick));++t) {
                check(run.simulate(race_input_frame({0,0,0},run.session().timing().step),error));check(run.commit(error));
            }
        }
        if(const char* output=std::getenv("NFSU_CAREER_SHADOW_FRAME")) {
            for(auto& texture:track.scene.region.textures) {
                if(texture.storage==RenderTextureStorage::rgba8)continue;
                const auto format=texture.storage==RenderTextureStorage::dxt1?PixelFormat::dxt1:PixelFormat::dxt3;
                std::uint64_t bytes=0;check(level_size(format,texture.width,texture.height,bytes));
                check(decode_level(format,texture.width,texture.height,texture.blocks.data(),std::size_t(bytes),texture.rgba,error));
                texture.storage=RenderTextureStorage::rgba8;texture.blocks.clear();texture.mip_count=1;
            }
            track.scene.region.cost.texture_bytes=0;
            for(const auto& texture:track.scene.region.textures)track.scene.region.cost.texture_bytes+=texture.payload().size();
            RenderOptions options;options.width=960;options.height=540;
            options.material_state=MaterialStateMode::retail;
            options.texture_filter=TextureFilterMode::linear;
            check(drive_render_camera(run.session().camera(),ChaseFovMode::vertical_4_3,
                options.width,options.height,options.camera,error));
            if(start_scene_check) {
                start_camera.frame(run.session());
                check(start_camera.camera(run.session(),options.camera,options.width,options.height));
                std::cout<<"START_CAMERA scene="<<start_scene<<" target="<<options.camera.target.x<<','<<options.camera.target.y<<','<<options.camera.target.z
                    <<" yaw="<<options.camera.yaw_deg<<" pitch="<<options.camera.pitch_deg<<" fov="<<options.camera.fov_y_deg<<'\n';
            }
            std::vector<TrackVisibleInstance> visible;
            for(const auto& section:track.scene.sections)for(std::size_t k=0;k<section.source_instances;++k) {
                TrackVisibleInstance item;item.section_index=section.section_index;item.instance_index=k;
                visible.push_back(item);
            }
            TrackDrawList list;TrackDrawListOptions order;order.order=TrackDrawOrder::retail_bucket;
            check(build_track_draw_list(track.scene,visible,order,list,error));
            Frame background;RenderStats stats;TrackRenderStats track_stats;
            options.sky=true;
            check(render_track_draw_list(track.scene,list,options,{},background,stats,track_stats,error));
            std::size_t shadow_rival=0;
            for(auto& actor:grid.actors) {
                const RaceSession* session=&run.session();
                if(actor.config.role==Event0Role::opponent)
                    session=&run.session().rival(shadow_rival++);
                const auto placement=place_car_shadow(actor.shadow,*session);
                check(render_car_shadow(actor.shadow,placement,options,background,stats,error));
                check(drive_place_instances(actor.base_instances,session->car(),session->spec(),actor.frame->scene.instances));
                std::cout<<"SHADOW_FRAME "<<actor.assets.car_type_name<<" alpha="<<placement.alpha
                    <<" strips="<<placement.strip_mask<<" origin="<<session->car().position.z
                    <<" center="<<placement.transform.rows[12]<<','<<placement.transform.rows[13]<<','<<placement.transform.rows[14]
                    <<" heights="<<placement.road_heights[0]<<','<<placement.road_heights[15]<<'\n';
            }
            for(auto& actor:grid.actors) {
                check(render_stock_frame(*actor.frame,options,error));Frame composed;
                check(compose_race_frame(background,actor.frame->frame,composed));background=std::move(composed);
            }
            const auto pixels=background.rgba();
            std::ofstream image(output,std::ios::binary);
            image<<"P6\n"<<options.width<<' '<<options.height<<"\n255\n";
            for(std::size_t i=0;i<pixels.size();i+=4)image.write(reinterpret_cast<const char*>(pixels.data()+i),3);
            if(!image)throw std::runtime_error("shadow frame output failed");
        }
        RaceHudAtlas hud;
        const bool knockout=selection.type==RaceType::lap_knockout;
        check(load_race_hud_atlas(root,hud,error,selection.type!=RaceType::drag,true,false,
            unsigned(selection.slot.track),knockout));
        for(unsigned char c:player_name)check(hud.lap_fonts[0][c].width>0);
        if(knockout)for(unsigned char c:std::string_view("Youwereknockedout!"))
            check(hud.lap_fonts[1][c].width>0);
        if(selection.type==RaceType::drag)check(load_race_drag_hud(root,selected_car,run.session().spec().handling,hud,error));
        RaceHudFrame hud_frame;RaceMinimapState map_state;
        build_race_hud_frame(run.session(),hud,1280,720,hud_frame);
        check(append_race_minimap(run.session(),hud,1280,720,map_state,hud_frame));
        // Exercise the production Switch scene/resource path with the same
        // authored grid, without opening a window or touching a profile.
        std::unique_ptr<RaceGpuScene> driving_scene;
        if(std::getenv("NFSU_CAREER_SCENE_CHECK")) {
            auto owned_scene=std::make_unique<RaceGpuScene>();auto& scene=*owned_scene;
            scene.set_particles_enabled(true);
            scene.set_race_visuals(true);scene.set_wet_road_reflections(!std::getenv("NFSU_CAREER_DRY_SCENE"));
            scene.set_silhouette_shadows(silhouettes);
            check(scene.set_complete_zone_backing(complete_backing));
            check(scene.set_original_draw_distance(true));
            if(start_scene_check)check(scene.set_start_scene(start_scene,selection.region));
            // Switch career races supply a dress even when the selected parts
            // are stock. That preparation consumes the raw geometry buffer.
            RaceCarDress dress;
            dress.request=&human->request;
            dress.skin=&human->skin;
            dress.bind=[](StockFrame& frame,const StockFrameInputs& inputs,
                          const StockSceneRequest& request,std::string& error,void*) {
                return bind_frontend_runtime_skin(frame,request,error) &&
                    bind_frontend_sponsors(inputs,request.installed_parts,frame,error);
            };
            check(scene.load(posix_file_system(),root,error,nullptr,nullptr,
                false,true,true,&human->assets,silhouettes?&dress:nullptr,event,true,round));
            if(start_scene_check) {
                RaceStartClock gpu_clock;
                check(scene.load_start_camera(posix_file_system(),root,start_scene,selection.region,gpu_clock,error));
                check(gpu_clock.go==start_clock.go && gpu_clock.release==start_clock.release);
            }
            std::vector<CarShadowPlacement> rival_shadow_poses;
            for(const auto& actor:grid.actors)if(actor.config.role==Event0Role::opponent)
                rival_shadow_poses.push_back(place_car_shadow(actor.shadow,run.session().rival(rival_shadow_poses.size())));
            for(auto& actor:grid.actors) {
                if(actor.config.role==Event0Role::opponent)
                    check(scene.add_rival(std::move(actor.frame->scene),std::move(actor.base_instances),
                        actor.frame->skin_binding.source,std::move(actor.shadow),error,false,actor.glow_anchors,&actor.hull));
                actor.frame.reset();
            }
            std::size_t shared_shadow_traffic = kNoHandle;
            for(auto& actor:grid.traffic) {
                if(shared_shadow_traffic==kNoHandle && actor.assets.car_type_name!="AMBULANCE" &&
                   actor.shadow.texture.usable())
                    shared_shadow_traffic=static_cast<std::size_t>(&actor-grid.traffic.data());
                check(scene.add_rival(std::move(actor.frame->scene),std::move(actor.base_instances),
                    kNoHandle,std::move(actor.shadow),error,true,actor.glow_anchors,&actor.hull));
                actor.frame.reset();
            }
            if(finish_sweep){verify_finish_sweep(run,scene,round);return 0;}
            verify_payload(scene,run,hud,selected_car,*human->config.performance,&grid);
            if(backing_test){verify_zone_backing_finish(root,run,scene);return 0;}
            if (std::getenv("NFSU_CAREER_SHADOW_WITNESS")) {
                if (shared_shadow_traffic == kNoHandle || !run.session().traffic())
                    throw std::runtime_error("shadow fixture needs traffic with a shared SHADOWIG texture");
                auto& traffic = const_cast<RaceTrafficActor&>(run.session().traffic()->actors().at(shared_shadow_traffic));
                const auto shadow_hash=bin_hash(grid.traffic[shared_shadow_traffic].assets.car_type_name+"_SHADOWIG");
                traffic.car = run.session().car(); // Put the traffic hull on the starting road.
                const std::string prefix = "rival" + std::to_string(run.session().rival_count()+shared_shadow_traffic+1) + "/";
                const std::string name = prefix + "shadow silhouette";
                std::vector<const RecordedDraw*> body_definitions;
                for (bool active : {false,true,false}) {
                    traffic.active = active;
                    RaceGpuFrame witness;
                    check(scene.frame(run.session(),witness,error));
                    check(validate_recorded_commands(witness.commands,error));
                    const auto& table = witness.commands.resource_table();
                    unsigned ramp_count = 0;
                    for (const auto& resource : table.resources)
                        if (resource.texture_hash == 0xbadb4475) ++ramp_count;
                    bool found = false,found_ground=false;
                    std::vector<const RecordedDraw*> current_definitions;
                    for (std::size_t i = 0; i < witness.commands.draw_count(); ++i) {
                        const auto view = witness.commands.draw_view(i);
                        if(view.definition.name.compare(0,prefix.size(),prefix)==0 && view.instance!=kNoHandle) {
                            if(view.submit!=active)throw std::runtime_error("inactive traffic body submission differs");
                            current_definitions.push_back(&view.definition);
                        }
                        if(view.definition.name==prefix+"shadow ground") {
                            found_ground=true;
                            const auto& texture=table.resources.at(view.definition.texture_resource);
                            check(texture.texture_hash==shadow_hash&&texture.texture.mip_count>1&&
                                (view.material.alpha[0]>0)==active&&view.submit==active);
                        }
                        if (view.definition.name != name) continue;
                        found = true;
                        const auto& stream = table.resources.at(view.definition.vertex_resource);
                        const auto* vertices = reinterpret_cast<const MeshVertex*>(stream.data);
                        const auto& ramp = table.resources.at(view.definition.texture_resource);
                        if (!stream.streamed_vertices || ramp.texture_hash != 0xbadb4475 || ramp_count != 1 ||
                            view.material.alpha[0] != (active ? 1.F : 0.F) || view.submit!=active ||
                            (active && (!vertices[0].color[3] || vertices[1].px == vertices[2].px)))
                            throw std::runtime_error("traffic silhouette missing, static, or visible in an inactive slot");
                    }
                    if (!found || !found_ground) throw std::runtime_error("traffic shadow layers have no prepared draw");
                    if(current_definitions.empty() || (!body_definitions.empty() && current_definitions!=body_definitions))
                        throw std::runtime_error("traffic body slots changed across activation");
                    body_definitions=std::move(current_definitions);
                }
                std::cout << "PASS SHADOWRAMP and shared SHADOWIG for active and inactive traffic\n";
                return 0;
            }
            RaceGpuFrame frame;check(scene.frame(run.session(),frame,error));
            for(std::size_t rival=0;rival<rival_shadow_poses.size();++rival) {
                const auto name="rival"+std::to_string(rival+1)+"/shadow ground";
                bool found=false;
                for(std::size_t d=0;d<frame.commands.draw_count();++d) {
                    const auto view=frame.commands.draw_view(d);if(view.definition.name!=name)continue;
                    found=true;const auto& expected=rival_shadow_poses[rival];
                    if(std::strcmp(view.definition.program->name,"car_shadow_textured") ||
                       view.material.alpha[0]!=expected.alpha || view.material.alpha[1]!=float(expected.strip_mask))
                        throw std::runtime_error("rival starting shadow visibility differs from native projection");
                    const float* rows[]={view.material.fallback,view.material.environment_min,
                        view.material.environment_range,view.material.environment_color};
                    for(unsigned i=0;i<16;++i)if(rows[i/4][i%4]!=expected.road_heights[i])
                        throw std::runtime_error("rival starting shadow lost a terrain tap before GPU upload");
                }
                if(!found)throw std::runtime_error("rival starting shadow draw missing");
            }
            if(start_scene_check) {
                const bool skip_check=std::getenv("NFSU_CAREER_START_SKIP")!=nullptr;
                const auto distance2=[](const VehicleVec3& a,const VehicleVec3& b) {
                    const double x=a.x-b.x,y=a.y-b.y,z=a.z-b.z;return x*x+y*y+z*z;
                };
                const auto origin=run.session().car().position;
                std::vector<VehicleVec3> rival_origins(run.session().rival_count());
                for(std::size_t i=0;i<rival_origins.size();++i)rival_origins[i]=run.session().rival(i).car().position;
                const auto end=kRaceWorldStartTicks+std::int32_t(std::ceil(double(start_clock.go)*kRaceTicksPerSecond/30))+500;
                unsigned camera_frames=0;
                while(run.session().timing().now_ticks<end) {
                    if(skip_check && camera_frames==12 && run.session().start_cinematic()) {
                        const auto now=run.session().timing().now_ticks;
                        check(run.skip_start_cinematic() && !run.skip_start_cinematic());
                        check(run.session().timing().now_ticks==now &&
                            run.session().timing().stage==RaceStage::countdown &&
                            run.session().timing().countdown_start_ticks==now &&
                            race_countdown_value(run.session().timing_config(),run.session().timing())==3);
                        check(run.session().start_scene_ticks()==std::int64_t(
                            std::ceil(double(start_clock.release)*kRaceTicksPerSecond/30)));
                    }
                    check(scene.frame(run.session(),frame,error));check(validate_recorded_commands(frame.commands,error));
                    for(std::size_t i=0;i<rival_origins.size();++i) {
                        const auto& rival=run.session().rival(i);
                        check(rival.timing().now_ticks==run.session().timing().now_ticks);
                        check(rival.timing().stage==run.session().timing().stage);
                        if(race_car_held(run.session().timing())) {
                            check(distance2(rival.car().position,rival_origins[i])<1e-10);
                        }
                    }
                    if(race_car_held(run.session().timing())) {
                        check(distance2(run.session().car().position,origin)<1e-10);
                    }
                    camera_frames+=run.session().start_cinematic();
                    check(run.simulate(race_input_frame({0,0,0},run.session().timing().step),error));check(run.commit(error));
                }
                check(run.session().timing().go_valid && !run.session().start_cinematic());
                if(skip_check)check(camera_frames==12);
                std::cout<<"PASS career event "<<event<<" selected grid and bounded runtime; authored start round="<<round<<" camera_frames="<<camera_frames<<" synchronized GO\n";
            }
            if(std::getenv("NFSU_CAREER_CAPACITY_SWEEP")) {
                const auto saved=run.session().camera();
                auto& camera=const_cast<ChaseCameraView&>(run.session().camera());
                std::size_t maximum=0;
                for(double x=660;x<=800;x+=20)for(double y=80;y<=300;y+=20)for(double z=45;z<=85;z+=10)
                    for(unsigned pitch:std::array<unsigned,3>{{0,3040,6000}})for(unsigned angle=0;angle<36;++angle) {
                        const VehicleVec3 point{x,y,z};
                        const double a=angle*(6.2831853071795864769/36);
                        camera=saved;camera.eye=point;camera.heading=std::uint16_t(angle*65536U/36);camera.pitch=std::uint16_t(pitch);camera.fov=16384;camera.forward={std::cos(a),std::sin(a),-.1};
                        if(!scene.frame(run.session(),frame,error))
                            throw std::runtime_error("capacity sweep pitch="+std::to_string(pitch)+" angle="+std::to_string(angle)+" eye="+std::to_string(point.x)+","+std::to_string(point.y)+","+std::to_string(point.z)+": "+error);
                        // Every selection is capacity-checked by frame(); validate the
                        // complete immutable package when a new maximum is reached.
                        // plan_draw_execution also validates every command/resource.
                        if(frame.commands.draw_count()>maximum) {
                            DrawExecutionPlan plan;check(plan_draw_execution(frame.commands,plan,error));
                            maximum=frame.commands.draw_count();
                        }
                    }
                camera=saved;
                std::cout<<"PASS tournament capacity sweep max_draws="<<maximum<<" capacity="<<scene.draw_capacity()<<"\n";
                return 0;
            }
            // Same complete command validation as the Switch renderer open,
            // including reflected geometry and all combined resource bindings.
            check(validate_recorded_commands(frame.commands,error));
            const auto aliases=frame.commands.texture_alias_count();
            if(event==1&&!aliases)throw std::runtime_error("first circuit lost authored world animation");
            for(std::size_t i=0;i<aliases;++i){const auto a=frame.commands.texture_alias(i);const auto& r=frame.commands.resource_table().resources;
                if(a.resource>=r.size()||a.frame>=r.size()||r[a.resource].kind!=UploadResourceKind::texture||r[a.frame].kind!=UploadResourceKind::texture)
                    throw std::runtime_error("authored animation descriptor outside texture resources");}
            std::vector<DrawTextureAlias> before;for(std::size_t i=0;i<aliases;++i)before.push_back(frame.commands.texture_alias(i));
            for(unsigned i=0;i<32;++i){auto pad=race_input_frame({0,0,0},run.session().timing().step);
                if(!run.simulate(pad,error)||!run.commit(error))throw std::runtime_error(error);}
            check(scene.frame(run.session(),frame,error));check(validate_recorded_commands(frame.commands,error));
            bool changed=false;for(std::size_t i=0;i<aliases;++i)changed=changed||before[i].frame!=frame.commands.texture_alias(i).frame;
            if(aliases&&!changed)throw std::runtime_error("authored animation did not advance in live frame");
            if(event==1 && start_scene_check==false) {
                // The separate animation witness pins simulation step32.
                // A complete authored start advances the live clock first.
                // Authenticated INGAMEB/STREAML1RA definitions: neon arrow and fountain
                // families must reach the live GPU descriptor source, not just
                // parse successfully into an unused metadata table.
                for(const auto& family:std::array<std::array<unsigned,3>,3>{{
                    {{0xb852154aU,0x152c962bU,3U}},{{0xb852158cU,0x152daeeeU,4U}},
                    {{0x636c197dU,0xc9e6d6f2U,10U}}}}) {
                    bool found=false;
                    for(std::size_t i=0;i<aliases;++i) {
                        const auto a=frame.commands.texture_alias(i);const auto& resources=frame.commands.resource_table().resources;
                        if(resources[a.resource].texture_hash==family[0]) {
                            if(run.session().timing().step!=32)throw std::runtime_error("named animation probe clock must be32");
                            if(resources[a.frame].texture_hash!=family[1])throw std::runtime_error("named world animation did not reach expected live descriptor");
                            found=true;std::cout<<"ANIMATED_FAMILY target="<<family[0]<<" frame="<<resources[a.frame].texture_hash<<" fps="<<family[2]<<"\n";
                        }
                    }
                    if(!found)throw std::runtime_error("named original arrow/fountain animation is not bound");
                }
            }
            if(frame.commands.frame.light[3]!=texture_world_seconds(run.session().timing().step_ticks))
                throw std::runtime_error("world UV animation lost the native simulation clock");
            if(event==1) {
                bool found=false;
                for(std::size_t i=0;i<frame.commands.static_track_definition_count();++i) {
                    const auto& d=frame.commands.static_track_definition(i);
                    if(d.texture_resource==kNoHandle)continue;
                    const auto& resource=frame.commands.resource_table().resources.at(d.texture_resource);
                    if(resource.texture_hash!=0x6b098df4U)continue;
                    found=true;
                    if(d.material.uv_animation[3]!=1.F || d.material.uv_animation[0]!=0.F ||
                       d.material.uv_animation[1]!=30720.F/32768)
                        throw std::runtime_error("fountain jet UV scroll metadata did not reach GPU material");
                }
                if(!found)throw std::runtime_error("actual fountain jet texture is absent from static world definitions");
            }
            std::cout<<"WORLD_TEXTURE_ANIMATION aliases="<<aliases<<"\n";
            if(!frame.commands.draw_count()||!frame.car_draws||!frame.track_draws)
                throw std::runtime_error("career scene has empty car or track commands");
            if(std::getenv("NFSU_CAREER_RETIREMENT_CHECK")) {
                if(event!=5||run.session().rival_count()!=3)throw std::runtime_error("retirement requires the actual first knockout grid");
                unsigned countdown=0;
                while(race_car_held(run.session().timing())&&countdown++<1000) {
                    const auto pad=race_input_frame({0,0,0},run.session().timing().step);
                    check(run.simulate(pad,error)&&run.commit(error));
                }
                if(race_car_held(run.session().timing()))throw std::runtime_error("retirement countdown did not finish");
                auto& state=const_cast<RaceTimingState&>(run.session().timing());
                auto transition=state;transition.racers[2].new_lap=true;transition.racers[2].highest_lap=1;
                const unsigned order[]={0,1,2,3};
                check(apply_lap_knockout(run.session().timing_config(),transition,order,4,error));
                state.racers[3]=transition.racers[3];
                const auto original_step=state.step;const auto original_clock=state.now_ticks;
                const auto draws=frame.commands.draw_count();
                for(unsigned phase=0;phase<3;++phase) {
                    state.step=(original_step&~std::size_t(1))+(phase==1?1:0);
                    state.now_ticks=original_clock+(phase==2?8000:0);
                    check(scene.frame(run.session(),frame,error));check(validate_recorded_commands(frame.commands,error));
                    if(frame.commands.draw_count()!=draws)throw std::runtime_error("retirement changed resident command identity/count");
                    unsigned bodies=0,shadows=0;
                    for(std::size_t i=0;i<frame.commands.draw_count();++i) {
                        const auto d=frame.commands.draw_view(i);
                        if(d.definition.name.rfind("rival3/",0)!=0)continue;
                        if(d.definition.name.find("shadow")!=std::string::npos) {
                            ++shadows;
                            if(phase&&d.material.alpha[0]!=0)throw std::runtime_error("retired shadow remains visible");
                        }else {
                            ++bodies;
                            const bool zero=std::all_of(std::begin(d.placement.rows),std::end(d.placement.rows),[](float v){return v==0;});
                            if(zero!=(phase!=0))throw std::runtime_error("retired body blink/expiry differs from native visibility");
                        }
                    }
                    if(!bodies||!shadows)throw std::runtime_error("retirement witness missed original body/shadow draws");
                }
                state.step=original_step;state.now_ticks=original_clock;
                for(unsigned i=0;i<10;++i) {
                    const auto pad=race_input_frame({0,0,0},run.session().timing().step);
                    check(run.simulate(pad,error)&&run.commit(error));
                }
                if(!run.session().actor_ghost(3)||run.session().rival_count()!=3)
                    throw std::runtime_error("planned stop actor lost retirement or was deleted");
                if(traffic.empty())throw std::runtime_error("retirement traffic setup missing");
                RaceTraffic pool;
                check(pool.load(root,run.session().course(),{traffic.front()},error));
                const auto human_box=race_contact_box(human->hull);
                VehicleState human=run.session().car(),blocker;
                std::array<const VehicleState*,4> observers{{&human,nullptr,nullptr,nullptr}};
                std::array<const RaceContactBox*,4> shapes{{&human_box}};
                check(pool.advance(observers,0,error,-1,1.F/60.F,{},shapes));
                if(!pool.actors()[0].active)throw std::runtime_error("retirement traffic spawn absent");
                const auto baseline=pool.actors()[0];
                blocker=baseline.car;
                blocker.position.x+=2*std::cos(baseline.motion.heading);
                blocker.position.y+=2*std::sin(baseline.motion.heading);
                observers[1]=&blocker;shapes[1]=&baseline.box;
                check(pool.advance(observers,1,error,-1,1.F/60.F,{},shapes));
                const float visible_brake=pool.actors()[0].driver.pedals.brake;
                pool.contact_actors()[0]=baseline;
                check(pool.advance(observers,1,error,-1,1.F/60.F,{{false,true,false,false}},shapes));
                if(!(visible_brake>pool.actors()[0].driver.pedals.brake))
                    throw std::runtime_error("traffic still brakes for retired ghost as for visible car");
                std::cout<<"KNOCKOUT_RETIREMENT live grid/planner/body/shadow commands PASS\n";
            }
            std::cout<<"SCENE event="<<event<<" draws="<<frame.commands.draw_count()
                <<" car_bytes="<<scene.car_bytes()<<" cache_bytes="<<scene.cache_bytes()
                <<" hud_bytes="<<hud.rgba.size()<<'\n';
                    if(driving_check)driving_scene=std::move(owned_scene);
}
        if(std::getenv("NFSU_CAREER_SURFACE_PROBE")) {
            const auto& course=run.session().course();const auto& spline=course.route.spline;
            for(unsigned step=0;step<=1000;++step) {
                float xy[3];evaluate_track_path(spline,float(spline.count-1)*step/1000.F,0,xy);
                float from[3]={xy[0],xy[1],100},to[3]={xy[0],xy[1],-100};TrackContact hit;
                if(query_track_contact_segment(course.world,from,to,hit)!=TrackContactStatus::hit)continue;
                const auto code=course.world.collision.polygons[hit.polygon].unknown0[2];
                const float fraction=(xy[2]*spline.distance_scale+spline.distance_offset)/spline.path_length;
                std::cout<<"SURFACE percent="<<fraction*100<<" x="<<xy[0]<<" y="<<xy[1]
                    <<" z="<<hit.point[2]<<" polygon="<<hit.polygon<<" material="<<unsigned(code)<<'\n';
            }
        }
        if(std::getenv("NFSU_CAREER_SURFACE_DRIVE")) {
            const auto& world=run.session().course().world;
            float point[3]={2319.5F,-100.F,55.F};TrackContact hit;
            if(query_track_contact_point(world,point,10,10,hit)!=TrackContactStatus::hit ||
               world.collision.polygons[hit.polygon].unknown0[2]!=8)throw std::runtime_error("sprint dirt fixture absent");
            std::array<VehicleState,2> states;std::array<VehicleTelemetry,2> telemetry;
            VehicleModel model(human->placement_spec);VehicleInput throttle;throttle.throttle=1;
            for(unsigned material=0;material<2;++material) {
                VehiclePose pose;pose.ground={point[0],point[1],hit.point[2]};pose.heading=-1.5707963267948966;pose.speed=20;
                check(model.reset(states[material],pose));TrackVehicleGround ground(world,material!=0);
                unsigned dirt_samples=0;
                for(unsigned n=0;n<120;++n) {
                    check(model.step(states[material],throttle,ground,telemetry[material]));
                    for(const auto& wheel:telemetry[material].wheels)if(wheel.surface==8)++dirt_samples;
                }
                if(material && !dirt_samples)throw std::runtime_error("ordinary car never consumed dirt material");
                std::cout<<"DIRT_DRIVE actual_material="<<material<<" dirt_samples="<<dirt_samples
                    <<" position="<<states[material].position.x<<','<<states[material].position.y<<','<<states[material].position.z<<'\n';
            }
            if(states[0].position.y==states[1].position.y && states[0].position.z==states[1].position.z)
                throw std::runtime_error("ordinary dirt physical response unchanged");
            std::cout<<"PASS actual sprint dirt -> ordinary wheel force/height integration\n";
        }
        for(const auto& boundary:run.session().course().route.boundaries)
            std::cout<<"BOUNDARY "<<boundary.name<<' '<<boundary.percent<<'\n';
        RaceResultStatistics statistics;
        RaceRouteDriver driver(race_event_driving_line(event));
        const char* drag_presentation=event==4?std::getenv("NFSU_DRAG_PRESENTATION_PROBE"):nullptr;
        unsigned drag_message_events=0;
        unsigned last_shift=0;
        const bool cutoff_probe=event==4&&std::getenv("NFSU_DRAG_CUTOFF_PROBE");
        unsigned cutoff_observations=0;
        const bool launch_probe=event==4&&std::getenv("NFSU_DRAG_LAUNCH_PROBE");
        bool launch_seen=false,launch_boost=false;float launch_quality=0,prior_launch_rpm=0;

        const bool idle_launch=event==4&&std::getenv("NFSU_DRAG_IDLE_LAUNCH_PROBE");
        bool idle_accelerated=false;float idle_go_rpm=0;
        const bool lane_probe=event==4&&std::getenv("NFSU_DRAG_LANE_PROBE");
        std::array<double,3> lane_y{};
        std::array<RacerStatus,4> last_status{};
        std::array<double,3> rival_speed_peaks{};
        std::array<bool,kRaceTrafficCapacity> traffic_seen{};
        unsigned traffic_activations=0;double traffic_speed_peak=0;
        if(const char* expected=std::getenv("NFSU_CAREER_EXPECT_PROPS")) {
            if(!run.session().props()||run.session().props()->objects().size()!=std::stoul(expected))
                throw std::runtime_error("race world omitted authored physical objects");
        }
        for(unsigned n=0;n<frame_limit&&run.session().timing().outcome==RaceOutcome::running;++n) {
            const bool driving_before=!race_car_held(run.session().timing());
            auto pad=driver.sample(run.session());
            if(event==4) {
                // Bounded test driver only: full throttle with manual upshift
                // pulses. Production drag always consumes the owner's buttons.
                pad={};pad.time_us=std::uint64_t(n)*1000000/60;pad.pads[0].connected=true;
                pad.pads[0].buttons=n?gamepad_button_bit(GamepadButton::trigger_right):0;
                if(launch_probe&&race_car_held(run.session().timing())) {
                    const auto& c=run.session().car();
                    const float rpm=c.engine.angular_speed*kPlayerRadSToRpm;
                    const float target=run.session().drag_upshift_rpm()-150.F;
                    const float predicted=rpm+std::max(0.F,rpm-prior_launch_rpm)*20.F;
                    pad.pads[0].buttons=predicted<target?gamepad_button_bit(GamepadButton::trigger_right):0;
                    prior_launch_rpm=rpm;
                }
                if(lane_probe) {
                    if(n>=600&&n<620)pad.pads[0].buttons|=gamepad_button_bit(GamepadButton::dpad_left);
                    if(n>=1200&&n<1220)pad.pads[0].buttons|=gamepad_button_bit(GamepadButton::dpad_right);
                }
                const auto& car=run.session().car();
                if(!race_car_held(run.session().timing())&&n-last_shift>=30&&
                   car.transmission.requested_gear==car.gear&&
                   car.engine_rpm>run.session().spec().handling.redline_rpm*.95&&
                   car.gear<int(run.session().spec().handling.forward_gears)+1) {
                    pad.pads[0].buttons|=gamepad_button_bit(GamepadButton::shoulder_right);
                    last_shift=n;
                }
            }
            if(idle_launch&&race_car_held(run.session().timing()))pad.pads[0].buttons=0;
            const float cutoff_clock=float(run.session().timing().now_ticks)*.00025F;
            const bool was_held=race_car_held(run.session().timing());
            if(!run.simulate(pad,error)||!run.commit(error))
                throw std::runtime_error("step "+std::to_string(n)+": "+error);
            statistics.observe(run.session());
            if(idle_launch) {
                const auto& c=run.session().car();
                if(was_held&&!race_car_held(run.session().timing()))idle_go_rpm=c.engine.angular_speed*kPlayerRadSToRpm;
                if(!race_car_held(run.session().timing())&&c.owner_speeds.magnitude>10&&
                   c.engine.angular_speed*kPlayerRadSToRpm>run.session().spec().handling.idle_rpm*2)
                    idle_accelerated=true;
            }
            if(drag_presentation) {
                const int old=hud_frame.drag_gear;
                build_race_hud_frame(run.session(),hud,1280,720,hud_frame);
                if(old>=1&&run.session().car().gear>old) {
                    if(hud_frame.drag_shift_at!=run.session().timing().now_ticks||
                       hud_frame.drag_quality!=run.session().car().transmission.shift_quality)
                        throw std::runtime_error("physical upshift lost HUD quality event");
                    ++drag_message_events;
                    std::cout<<"SHIFT_EVENT gear="<<run.session().car().gear<<" quality="<<hud_frame.drag_quality<<'\n';
                }
            }
            if(cutoff_probe) {
                const float deadline=run.session().car().engine.cutoff_until;
                if(deadline>0) {
                    ++cutoff_observations;
                    if(deadline<cutoff_clock||deadline>cutoff_clock+.075F)
                        throw std::runtime_error("drag cutoff escaped original world-clock lifetime");
                }
            }
            if(launch_probe) {
                const auto& session=run.session();const auto& c=session.car();
                if(was_held&&!race_car_held(session.timing())) {
                    launch_seen=true;launch_quality=c.transmission.shift_quality;
                    const float physical=c.engine.angular_speed*kPlayerRadSToRpm;
                    const auto expected=player_drag_shift_quality(physical,session.drag_upshift_rpm(),
                        session.drag_category(),1,session.drag_shift_scale());
                    std::cout<<"LAUNCH rpm="<<physical<<" target="<<session.drag_upshift_rpm()<<" quality="<<launch_quality<<" expected="<<expected<<'\n';
                    if(launch_quality!=expected||launch_quality<=0)
                        throw std::runtime_error("held-grid release did not preserve original positive launch quality");
                }
                if(launch_seen&&c.gear==2&&c.engine.throttle_multiplier>1)launch_boost=true;
            }
            if(lane_probe&&(n==599||n==959||n==1559)) {
                const unsigned sample=n==599?0:n==959?1:2;
                lane_y[sample]=run.session().car().position.y;
                std::cout<<"LANE_PROBE step="<<n+1<<" y="<<lane_y[sample]<<'\n';
                if(sample==2 && !(lane_y[1]<lane_y[0]-.1&&lane_y[2]>lane_y[1]+.1))
                    throw std::runtime_error("drag left/release/right did not follow requested road side");
            }
            for(std::size_t i=0;i<run.session().timing().racers.size();++i) {
                const auto status=run.session().timing().racers.at(i).status;
                if(status!=last_status[i])std::cout<<"STATUS "<<n+1<<" actor="<<i<<" status="<<int(status)<<'\n';
                last_status[i]=status;
            }
            if(n%3000==2999||(event==4&&n%300==299)) {
                for(std::size_t i=0;i<=run.session().rival_count();++i) {
                    const auto& s=i?run.session().rival(i-1):run.session();
                    const auto& p=s.car().position;
                    std::cout<<"AT "<<n+1<<" actor="<<i<<" x="<<p.x<<" y="<<p.y<<" z="<<p.z
                        <<" speed="<<s.car().velocity.x<<","<<s.car().velocity.y<<","<<s.car().velocity.z
                        <<" throttle="<<s.telemetry().model.throttle<<" brake="<<s.telemetry().model.brake<<" support="<<s.car().published_support
                        <<" gear="<<s.car().gear<<" rpm="<<s.car().engine_rpm<<" stage="<<int(s.timing().stage)<<" go="<<s.timing().go_valid
                        <<" status="<<int(run.session().timing().racers[i].status)<<" progress="<<s.progress().coordinate.fraction<<" lap="<<run.session().timing().racers[i].highest_lap<<'\n';
                }
                std::cout.flush();
            }
            if(driving_before) {
                ++driving_frames;driving_speed=std::max(driving_speed,double(run.session().car().owner_speeds.magnitude));
                for(std::size_t i=0;i<run.session().rival_count();++i) {
                    const auto& c=run.session().rival(i).car();const auto& v=c.velocity;
                    const double speed=std::sqrt(v.x*v.x+v.y*v.y+v.z*v.z);
                    if(!std::isfinite(speed)||!std::isfinite(c.position.x)||!std::isfinite(c.position.y)||!std::isfinite(c.position.z))
                        throw std::runtime_error("campaign rival produced non-finite motion");
                    rival_speed_peaks[i]=std::max(rival_speed_peaks[i],speed);
                }
                if(const auto* traffic=run.session().traffic())for(std::size_t i=0;i<traffic->actors().size();++i) {
                    const auto& actor=traffic->actors()[i];if(!actor.active)continue;
                    if(!traffic_seen[i]){traffic_seen[i]=true;++traffic_activations;}
                    const auto& v=actor.car.velocity;const double speed=std::sqrt(v.x*v.x+v.y*v.y+v.z*v.z);
                    if(!std::isfinite(speed))throw std::runtime_error("campaign traffic produced non-finite motion");
                    traffic_speed_peak=std::max(traffic_speed_peak,speed);
                }
            }
            if(driving_check&&driving_frames>=steps)break;
        }
        if(driving_check) {
            if(driving_frames<steps||driving_speed<1)throw std::runtime_error("race audit never reached moving gameplay");
            for(std::size_t i=0;i<run.session().rival_count();++i) {
                std::cout<<"RIVAL_MOTION actor="<<i<<" peak_mps="<<rival_speed_peaks[i]<<'\n';
                if(rival_speed_peaks[i]<1) {
                    const auto& r=run.session().rival(i);const auto& c=r.car();
                    std::cout<<"RIVAL_STATIONARY rest="<<c.body_rest_state<<" gravity="<<c.body_gravity
                        <<" support="<<c.published_support<<" throttle="<<r.telemetry().model.throttle
                        <<" contacts=";for(const auto& w:c.wheels)std::cout<<w.contact;
                    std::cout<<'\n';
                    throw std::runtime_error("campaign rival remained stationary after GO");
                }
            }
            std::cout<<"TRAFFIC_COVERAGE seen="<<traffic_activations<<" peak_mps="<<traffic_speed_peak<<'\n';
            build_race_hud_frame(run.session(),hud,1280,720,hud_frame);
            if(!hud_frame.quads)throw std::runtime_error("race gameplay HUD remained hidden");
            if(driving_scene) {
                RaceGpuFrame moving;check(driving_scene->frame(run.session(),moving,error));
                check(validate_recorded_commands(moving.commands,error));
                check(moving.track_draws&&moving.car_draws);
                check(append_race_minimap(run.session(),hud,1280,720,map_state,hud_frame));
            }
            std::cout<<"COVERAGE driving="<<driving_frames<<" max_speed="<<driving_speed<<" HUD="<<hud_frame.quads<<'\n';
        }
        if(std::getenv("NFSU_CAREER_FINISH_PROBE")) {
            if(run.session().timing().outcome!=RaceOutcome::finished)throw std::runtime_error("finish probe did not finish");
            const auto place=run.session().timing().racers[0].position;
            const auto stamp=run.session().timing().racers[0].finish_ticks;
            const auto initial=run.session().car().position;
            const auto initial_speed=run.session().car().owner_speeds.magnitude;
            for(unsigned i=0;i<360;++i){
                auto pad=race_input_frame({1,1,0},run.session().timing().step);
                pad.pads[0].buttons|=gamepad_button_bit(GamepadButton::shoulder_left);
                if(!run.simulate(pad,error)||!run.commit(error))throw std::runtime_error("finish stop: "+error);
                if(run.session().nitrous().active)throw std::runtime_error("finish reactivated nitrous");
            }
            const auto& c=run.session().car();
            std::cout<<"FINISH_STOP before="<<initial_speed<<" after="<<c.owner_speeds.magnitude
                     <<" distance="<<std::hypot(c.position.x-initial.x,c.position.y-initial.y)<<'\n';
            if(c.owner_speeds.magnitude>1||std::hypot(c.position.x-initial.x,c.position.y-initial.y)<1||
               run.session().timing().racers[0].position!=place||run.session().timing().racers[0].finish_ticks!=stamp)
                throw std::runtime_error("finish did not brake physical car while preserving its result");
        }
        if(cutoff_probe) {
            if(!cutoff_observations)throw std::runtime_error("drag cutoff regression never reached limiter");
            std::cout<<"PASS actual drag cutoff world-clock observations="<<cutoff_observations<<'\n';
        }
        if(idle_launch) {
            if(!idle_accelerated||idle_go_rpm>run.session().spec().handling.idle_rpm+1)
                throw std::runtime_error("full throttle could not launch from held-grid idle");
            std::cout<<"PASS actual idle launch rpm="<<idle_go_rpm<<" accelerated above10m/s without nitrous\n";
        }
        if(launch_probe) {
            if(!launch_seen||!launch_boost)throw std::runtime_error("positive launch never reached first-gear torque boost");
            std::cout<<"PASS held neutral -> original launch quality -> first gear torque boost\n";
        }
        if(std::getenv("NFSU_CAREER_RESULTS_PROBE")) {
            const auto values=post_race_report(run.session(),statistics.values(run.session(),selected_car),
                {{"Civic","MX-5","Neon","Golf"}},500);
            if(event==4) {
                if(values.at("DataName_3")!="TOP SPEED:" || values.at("DataName_4")!="PERFECT SHIFTS:" ||
                   values.at("DataName_5")!="PERFECT SHIFT %:" || !values.at("AllLapTimesTitle").empty() ||
                   values.count("LapNumber_1") || !run.session().drag_shifts() ||
                   run.session().drag_perfect_shifts()>run.session().drag_shifts())
                    throw std::runtime_error("drag statistics still use circuit fields or invalid shift counters");
                for(const auto& field:values)if(field.first.rfind("Data",0)==0)
                    std::cout<<"DRAG_RESULT "<<field.first<<'='<<field.second<<'\n';
            }
            std::cout<<"PASS actual post-race bindings mode="<<values.at("RaceMode")<<'\n';
        }
        for(const auto& line:run.result_lines())std::cout<<line<<'\n';
        build_race_hud_frame(run.session(),hud,1280,720,hud_frame);
        check(append_race_minimap(run.session(),hud,1280,720,map_state,hud_frame));
        if(const char* output=std::getenv("NFSU_CAREER_HUD_FRAME")) {
            std::vector<unsigned char> pixels(1280*720*4,40);
            composite_race_hud(hud,hud_frame,pixels.data(),1280,720,1280*4);
            std::ofstream image(output,std::ios::binary);image<<"P6\n1280 720\n255\n";
            for(std::size_t at=0;at<pixels.size();at+=4)image.write(reinterpret_cast<const char*>(pixels.data()+at),3);
            check(bool(image));
        }
        if(event==4&&std::getenv("NFSU_DRAG_LAMP_PROBE")) {
            const char* states[]={"blue","green","red"};
            const float window=player_drag_shift_window(run.session().drag_category(),unsigned(run.session().car().gear),run.session().drag_shift_scale());
            const float offsets[]={-.7F,-.1F,.1F};
            const bool light=std::any_of(hud.drag_objects.begin(),hud.drag_objects.end(),[](const RaceDragHudObject& o){return o.name=="Shift_light";});
            const bool back=std::any_of(hud.drag_objects.begin(),hud.drag_objects.end(),[](const RaceDragHudObject& o){return o.name=="Shift_light_back";});
            if(!light||!back)throw std::runtime_error("authored shift light resources omitted");
            for(unsigned i=0;i<3;++i) {
                RaceHudFrame sample;sample.readout=hud_frame.readout;sample.atlas_height=hud.height;
                sample.readout.engine_rpm=run.session().drag_upshift_rpm()+window*offsets[i];
                append_race_drag_hud(run.session(),hud,1280,720,sample);
                if(sample.drag_lamp!=int(i))throw std::runtime_error("authored shift light phase differs");
                const auto a=sample_hud_animation(hud.drag_animations,"Shift_light",i==0?"PulseBlue":i==1?"PulseGreen":"PulseRed",0);
                if((i==0&&a.color[2]<=a.color[0])||(i==1&&a.color[1]<=a.color[0])||(i==2&&a.color[0]<=a.color[2]))
                    throw std::runtime_error("authored shift light script color differs");
                std::vector<unsigned char> pixels(1280*720*4,40);
                composite_race_hud(hud,sample,pixels.data(),1280,720,1280*4);
                const auto output=std::filesystem::path(std::getenv("NFSU_DRAG_LAMP_PROBE"))/(std::string("lamp-")+states[i]+".ppm");
                std::ofstream image(output,std::ios::binary);image<<"P6\n1280 720\n255\n";
                for(std::size_t at=0;at<pixels.size();at+=4)image.write(reinterpret_cast<const char*>(pixels.data()+at),3);
                check(bool(image));
                std::cout<<"LAMP phase="<<states[i]<<" rpm="<<sample.readout.engine_rpm<<" quads="<<sample.quads<<'\n';
            }
            std::cout<<"PASS actual retail Shift_light/back and three authored color scripts\n";
        }
        if(drag_presentation) {
            const auto& session=run.session();const auto now=session.timing().now_ticks;
            const float window=player_drag_shift_window(session.drag_category(),unsigned(session.car().gear),session.drag_shift_scale());
            const auto save=[&](const RaceHudFrame& frame,const char* name) {
                std::vector<unsigned char> pixels(1280*720*4,40);
                composite_race_hud(hud,frame,pixels.data(),1280,720,1280*4);
                std::ofstream image(std::filesystem::path(drag_presentation)/(std::string(name)+".ppm"),std::ios::binary);
                image<<"P6\n1280 720\n255\n";
                for(std::size_t at=0;at<pixels.size();at+=4)image.write(reinterpret_cast<const char*>(pixels.data()+at),3);
                check(bool(image));
            };
            const auto base=[&]() {
                RaceHudFrame f;f.readout=hud_frame.readout;f.atlas_height=hud.height;
                f.drag_gear=session.car().gear;return f;
            };
            for(int gear:{1,2})for(unsigned phase=0;phase<3;++phase) {
                auto f=base();f.readout.gear=gear;
                f.readout.engine_rpm=session.drag_upshift_rpm()+window*(phase==0?-.7F:phase==1?-.1F:.1F);
                f.drag_lamp=int(phase);f.drag_lamp_selected=true;
                f.drag_lamp_at=now-4000; // A completed pulse, as after a long hidden approach.
                append_race_drag_hud(session,hud,1280,720,f);
                const auto& g=hud.lap_fonts[2][gear==1?'N':'1'];
                bool found=false;
                for(unsigned q=0;q<f.quads;++q) {
                    const auto& v=f.vertices[q*4];
                    if(v.px<640&&v.u==float(g.x)/kRaceHudAtlasWidth&&v.v==float(g.y)/hud.height) {
                        const auto color=session.car().transmission.mode==3?
                            std::array<unsigned char,3>{{150,190,255}}:std::array<unsigned char,3>{{255,128,0}};
                        if(v.color[0]!=color[0]||v.color[1]!=color[1]||v.color[2]!=color[2])
                            throw std::runtime_error("gear glyph received needle hint tint");
                        found=true;
                    }
                }
                if(!found)throw std::runtime_error("neutral/first retail LCD glyph omitted");
                const auto light=std::find_if(hud.drag_objects.begin(),hud.drag_objects.end(),[](const RaceDragHudObject& o){return o.name=="Shift_light";});
                if(light==hud.drag_objects.end())throw std::runtime_error("shift light object absent");
                bool visible=false;
                for(unsigned q=0;q<f.quads;++q) {
                    const auto& v=f.vertices[q*4];
                    if(v.u==(light->texture[0]+.5F)/kRaceHudAtlasWidth&&v.v==(light->texture[1]+.5F)/hud.height&&v.color[3])visible=true;
                }
                if(!visible||f.drag_lamp_at!=now)throw std::runtime_error("completed shift light did not restart visibly");
                save(f,(std::string(gear==1?"neutral-":"first-")+(phase==0?"blue":phase==1?"green":"red")).c_str());
            }
            const char* expected[]={"Good Shift!","Perfect Shift!","Short Shift!","Over Rev!"};
            const float quality[]={.5F,.85F,0.F,-1.F};
            for(unsigned i=0;i<4;++i) {
                if(hud.drag_shift_labels[i]!=expected[i])throw std::runtime_error("retail shift label binding differs");
                auto f=base();f.drag_quality=quality[i];f.drag_shift_at=now-1000;
                append_race_drag_hud(session,hud,1280,720,f);
                std::string rendered;
                for(unsigned q=0;q<f.quads;++q) {
                    const auto& v=f.vertices[q*4];
                    if(v.px<360||v.px>920||v.py<110||v.py>260||!v.color[3])continue;
                    for(unsigned c=33;c<128;++c) {
                        const auto& g=hud.lap_fonts[1][c];
                        if(g.width&&v.u==float(g.x)/kRaceHudAtlasWidth&&v.v==float(g.y)/hud.height) {
                            rendered+=char(c);break;
                        }
                    }
                }
                std::string wanted;for(const char c:std::string(expected[i]))if(c!=' ')wanted+=c;
                if(rendered!=wanted)throw std::runtime_error("shift message lost font glyphs: "+rendered+" != "+wanted);
                save(f,(std::string("message-")+std::to_string(i)).c_str());
                std::cout<<"SHIFT_TEXT quality="<<quality[i]<<" rendered="<<rendered<<'\n';
            }
            if(!drag_message_events)throw std::runtime_error("no production upshift HUD event exercised");
            std::cout<<"PASS neutral/first glyphs, needle-only hint tint, expired blue/green/red restart, all4 full shift messages, actual upshift events="<<drag_message_events<<'\n';
        }
        if(!rivals.empty()) {
        IntroAiPlanner probe;std::array<const VehicleHull*,4> hulls{};
        for(unsigned i=0;i<rivals.size();++i)hulls[i]=&rivals[i].hull;
        hulls[3]=&human->hull;RaceRivalPlans plans;
        check(probe.load(root,contexts,hulls,true,run.track_number(),
            run.session().course().route.reverse?1:0,run.session().is_drag(),0,selection.slot.laps,
            run.session().course().route.point_to_point,grid.setup->traffic.selector));
        check(probe.plan(run.session(),plans));
        for(unsigned i=0;i<rivals.size();++i) {
            const auto& p=plans.cars[i];
            std::cout<<"PLAN "<<i<<" speed="<<p.desired_speed<<" count="<<unsigned(p.path.count)<<'\n';
            for(unsigned j=0;j<p.path.count;++j)std::cout<<"POINT "<<p.path.points[j][0]<<' '<<p.path.points[j][1]<<' '<<p.path.points[j][2]<<'\n';
        }
        }
        const auto difficulty_line=run.difficulty_line();
        if(!rivals.empty()&&difficulty_line.find(" traffic_selector="+std::to_string(grid.setup->traffic.selector)+" ")==std::string::npos)
            throw std::runtime_error("production catchup lost authored traffic density");
        if(event==4) {
            // Native career constructor leaves D8/DC at1 on every difficulty;
            //44BD10 must not update catchup or amplify a drag rival's engine.
            if(run.session().drag_shift_scale()!=1.F || difficulty_line.find(" updates=0 ")==std::string::npos)
                throw std::runtime_error("drag received quick-race windows or ordinary catchup");
            for(const auto& actor:grid.actors)if(actor.placement_spec.drag_clutch_scale!=1.F)
                throw std::runtime_error("career drag received quick-race clutch difficulty");
        }
        std::cout<<difficulty_line<<'\n';
        std::cout<<"PASS career event "<<event<<" selected grid and bounded runtime\n";
        return 0;
    } catch(const std::exception& e) {std::cerr<<"FAIL career race: "<<e.what()<<'\n';return 1;}
}
