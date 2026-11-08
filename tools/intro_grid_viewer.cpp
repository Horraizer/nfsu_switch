#include "speed_blur_preview.h"
#include <iomanip>
#include "nfsu/deko_draw_bindings.h"
#include "nfsu/deko_draw_order.h"
#include "nfsu/deko_gpfifo_budget.h"
#include "nfsu/race_effect_fast.h"
#include "nfsu/race_result.h"
#include "nfsu/intro_grid.h"
#include "nfsu/frontend.h"
#include "nfsu/race_run.h"
#include "nfsu/frontend_preset.h"
#include "nfsu/frontend_scene.h"
#include "nfsu/race_gpu_scene.h"
#include "nfsu/arg_path.h"
#include "nfsu/posix_file_system.h"
#include "nfsu/track_render.h"
#include "nfsu/track_slice.h"
#include "nfsu/race_hud.h"
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <algorithm>
#include <chrono>
#include <cstring>
#include <cstdlib>
using namespace nfsu;
#include "../tests/post_race_style_native.inc"
namespace {
class ProfileClock final:public TimingService {
    std::uint64_t now_us() override {
        return std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();
    }
};
void require(bool ok,const std::string& error) {if(!ok)throw std::runtime_error(error);}
// Host-only differential signature: no pointers, string addresses or padding.
std::string command_signature(const RecordedCommands& commands) {
    Sha256 hash;
    const auto number=[&](std::uint64_t value){unsigned char b[8];for(unsigned i=0;i<8;++i)b[i]=static_cast<unsigned char>(value>>(8*i));hash.update(b,8);};
    const auto text=[&](const std::string& s){number(s.size());hash.update(s.data(),s.size());};
    const auto frame=[&](const auto& f){static_assert(sizeof(f)==112 || sizeof(f)==592);hash.update(&f,sizeof(f));};
    const auto draw=[&](const RecordedDrawView& view) {
        const auto& d=view.definition;const auto& p=d.pipeline;
        text(d.name);number(d.group);number(d.mesh);number(view.instance);number(view.track_instance);
        number(d.vertex_resource);number(d.index_resource);number(d.texture_resource);
        number(d.first_vertex);number(d.vertex_count);number(d.first_index);number(d.index_count);
        number(d.frontend_sprite_kind);number(d.frontend_sprite_view);text(d.program?d.program->name:"");
        text(p.name?p.name:"");number(unsigned(p.alpha_test));number(p.alpha_ref);number(p.blend);
        number(unsigned(p.blend_src));number(unsigned(p.blend_dst));number(unsigned(p.blend_op));
        number(unsigned(p.depth_test));number(p.depth_write);number(unsigned(p.color_write));number(unsigned(p.cull));
        static_assert(sizeof(DrawInstanceUniform)==64 && sizeof(DrawMaterialUniform)==208);
        hash.update(&view.placement,sizeof(view.placement));hash.update(&view.material,sizeof(view.material));
    };
    frame(commands.frame);frame(commands.headlight_frame);number(commands.headlight_texture);
    number(commands.race_environment);number(commands.cube_face_mask);
    number(commands.width);number(commands.height);hash.update(commands.clear_color,4);
    number(commands.draw_count());
    for(std::size_t i=0;i<commands.draw_count();++i)draw(commands.draw_view(i));
    number(commands.reflection_draw_count());
    for(std::size_t i=0;i<commands.reflection_draw_count();++i) {
        const auto& d=commands.reflection_draw(i);draw({d,d.instance,d.placement,d.material,false});
    }
    for(const auto& f:commands.frontend_cube_frames)frame(f);
    number(commands.frontend_cube_draws.size());
    for(const auto& d:commands.frontend_cube_draws)draw({d,d.instance,d.placement,d.material,false});
    return sha256_hex(hash.finish());
}
// Host submission audit. Counts API calls, not GPU time or hardware FPS.
void binding_audit(const RecordedCommands& commands) {
    std::size_t old_calls=0,new_calls=0,draws=0;
    const RecordedDraw* previous=nullptr;
    const auto visit=[&](const RecordedDraw& d) {
        const bool fixed=!previous || d.program!=previous->program ||
            d.texture_resource!=previous->texture_resource ||
            std::memcmp(&d.pipeline,&previous->pipeline,sizeof d.pipeline)!=0;
        const auto b=deko_draw_bindings(d,previous);
        old_calls+=2+(fixed?9+(d.texture_resource!=kNoHandle):0);
        new_calls+=b.shaders+6*b.pipeline+2*b.layout+
            (b.texture&&d.texture_resource!=kNoHandle)+b.vertex+b.index;
        previous=&d;++draws;
    };
    std::size_t last_old=0,last_new=0,last_draws=0;
    const auto report=[&](const char* pass) {
        std::cout<<"BINDING_PASS "<<pass<<" draws="<<draws-last_draws<<" old_calls="<<old_calls-last_old
            <<" new_calls="<<new_calls-last_new<<"\n";
        last_old=old_calls;last_new=new_calls;last_draws=draws;
    };
    const auto count=commands.static_track_draw_count();
    std::size_t emitted=0;
    deko_visit_opaque_first(count,[&](std::size_t i) {
        const auto& d=commands.draw_view(i).definition;const auto& p=d.pipeline;
        return deko_draw_in_opaque_pass(std::strcmp(d.program->name,"sky_textured")==0,
            p.blend,p.alpha_test!=shader_manifest::CompareOp::always,p.depth_write,false);
    },[&](std::size_t i) {
        if(emitted++%kDekoTrackChunkDraws==0)previous=nullptr;
        visit(commands.draw_view(i).definition);
    });
    report("static-track-on-rerecord");
    previous=nullptr;
    for(std::size_t i=count;i<commands.draw_count();++i) {
        const auto v=commands.draw_view(i);
        if(!race_effect_no_output(v))visit(v.definition);
    }
    report("dynamic");
    previous=nullptr;
    for(std::size_t i=0;i<commands.reflection_draw_count();++i)visit(commands.reflection_draw(i));
    report("road-reflection");
    for(unsigned face=0;face<6;++face) {
        previous=nullptr;
        for(const auto& d:commands.frontend_cube_draws)
            if(!d.frontend_sprite_view||d.frontend_sprite_view==face+10)visit(d);
    }
    report("six-cube-faces");
    std::cout<<"BINDING_AUDIT draws="<<draws<<" old_calls="<<old_calls<<" new_calls="<<new_calls
        <<" saved="<<old_calls-new_calls<<" percent="<<100.*(old_calls-new_calls)/old_calls
        <<" (same draws; excludes uniform/HUD/global binds; opaque-first, no streak history)\n";
}
bool road_query(const VehicleVec3& at,TrackContact& result,const void* user) {
    const auto& world=*static_cast<const TrackContactWorld*>(user);
    const float point[]={float(at.x),float(at.y),float(at.z)};
    return query_track_contact_point(world,point,2,8,result)==TrackContactStatus::hit;
}
}
int main(int argc,char** argv) {try {
    if(argc<3 || argc>5)throw std::runtime_error("usage: nfsu_intro_grid_viewer retail-root output-prefix [ticks] [pair-probe|swept-pair-probe|terrain-probe|profile|spatial-profile|grid-revs|loading|post-race|complete|traffic-assets|props-check|grid-start|traffic-impact|speed-blur]");
    const int ticks=argc>=4?std::stoi(argv[3]):0;
    const bool speed_blur_preview_mode=argc==5 && std::string(argv[4])=="speed-blur";
    const bool grid_start=argc==5 && std::string(argv[4])=="grid-start";
    const bool terrain_probe=argc==5 && std::string(argv[4])=="terrain-probe";
    const bool swept_pair_probe=argc==5 && std::string(argv[4])=="swept-pair-probe";
    const bool pair_probe=swept_pair_probe || (argc==5 && std::string(argv[4])=="pair-probe");
    const bool traffic_impact=argc==5 && std::string(argv[4])=="traffic-impact";
    const bool props_check=argc==5 && std::string(argv[4])=="props-check";
    const bool complete=argc==5 && std::string(argv[4])=="complete";
    const bool spatial_profile=argc==5 && std::string(argv[4])=="spatial-profile";
    const bool profiling=spatial_profile || (argc==5 && std::string(argv[4])=="profile");
    const bool grid_revs=argc==5 && std::string(argv[4])=="grid-revs";
    const bool loading=argc==5 && std::string(argv[4])=="loading";
    const bool traffic_assets=argc==5 && std::string(argv[4])=="traffic-assets";
    const bool post_race=argc==5 && std::string(argv[4])=="post-race";
    require(argc<5 || terrain_probe || pair_probe || speed_blur_preview_mode || traffic_impact || grid_start || props_check || profiling || complete || grid_revs || loading || post_race || traffic_assets,"unknown viewer option");
    ProfileClock profile_clock;std::array<double,6> sim_sum{},sim_max{};
    std::array<double,3> contact_sum{},contact_max{};unsigned sim_samples=0;
    require(ticks>=0 && ticks<=120000,"invalid simulation tick count");
    std::filesystem::path root,prefix;std::string error;
    require(argument_to_path(argv[1],root,error),error);require(argument_to_path(argv[2],prefix,error),error);
    if(traffic_assets) {
        const auto table=load_car_type_table(root/"Global/GLOBALB.BUN");require(table.ok(),"car table");
        bool all_assets=true;
        for(const auto& record:table.records)if(record.class_id==2) {
            CarAssets assets;require(resolve_traffic_assets(table,record.name,assets,error),error);
            StockFrameInputs inputs;require(read_car_frame_inputs(root,assets,inputs,false,error),error);
            StockFrame frame;const auto request=stock_car_request(record.name);
            const bool ok=prepare_stock_frame(inputs,request,frame,error);
            const auto anchors=collect_car_glow_anchors(frame);
            std::cout<<"ANCHORS "<<record.name;for(const auto& a:anchors)std::cout<<" "<<!a.empty();
            std::cout<<" slots";for(const auto& instance:frame.scene.instances)std::cout<<" "<<instance.part_slot;
            std::cout<<" shadow="<<std::count_if(frame.car_pack.textures.begin(),frame.car_pack.textures.end(),[&](const auto& t){return t.hash==bin_hash(record.name+"_SHADOWIG");})<<"\n";
            all_assets=all_assets&&ok;
            std::cout<<"TRAFFIC_ASSET "<<record.name<<" ok="<<ok<<" instances="<<frame.scene.instances.size()<<" error="<<error<<"\n";
        }
        return all_assets?0:1;
    }
    if(post_race) {
        FrontendArt art;require(art.load(root,error,false),error);require(art.load_post_race(root,error),error);
        PostRaceValues values{{"RacerName","YOU - INTEGRA"},{"RaceFinishedPosition","04"},
            {"Data_1","03:12.34"},{"Data_2","00:12.01"},{"AllLapTimes_1","01:35.12"},{"AllLapTimes_2","01:37.22"},
            {"LapNumber_1","01"},{"LapNumber_2","02"},{"PositionAfterEachLap_1","04"},{"PositionAfterEachLap_2","04"}};
        const char* labels[]={"FINISH TIME:","TIME BEHIND:","BEST LAP TIME:","TOP SPEED:","AVERAGE SPEED:","TRIP ODOMETER:","LAPS LED:"};
        const char* data[]={"03:12.34","00:12.01","01:35.12","145.6 MPH","82.3 MPH","4.2 MI","0"};
        for(unsigned i=0;i<7;++i){values["DataName_"+std::to_string(i+1)]=labels[i];values["Data_"+std::to_string(i+1)]=data[i];}
        for(unsigned page=0;page<4;++page) {
            PostRaceMenu menu;for(unsigned i=0;i<(page==3?1:page);++i)menu.update({true,false,false,false});
            if(page==3)menu.update({false,false,true,false});
            std::vector<unsigned char> pixels(1280*720*4,60);for(std::size_t i=3;i<pixels.size();i+=4)pixels[i]=255;
            art.draw_post_race({pixels.data(),1280,720,1280*4},menu,values);
            FrontendUiFrame gpu;art.record_post_race(1280,720,menu,values,gpu);
            require(!gpu.vertices.empty() && gpu.vertices.size()==gpu.additive.size()*4,"post-race GPU quads");
            auto file=prefix;file+="-post"+std::to_string(page)+".tga";
            FrameWriteResult result;require(write_rgba_tga(1280,720,pixels,file,{},result),result.error);
            std::cout<<"POST page="<<page<<" quads="<<gpu.additive.size()<<"\n";
        }
        for(unsigned stage=0;stage<3;++stage) {
            auto report=values;
            for(unsigned n=1;n<=4;++n) {
                const auto key=std::to_string(n);
                report["Pos"+key]=key;report["Name"+key]=n==1?"YOU":n==2?"CARL":n==3?"KLUTCH":"DARYL";
                report["CarModel_"+key]=n==1?"Civic":n==2?"MX-5":"Neon";
                report["Time"+key]=n==1?"0:41.18":"0:43.27";report["Lead"+key]=report["Time"+key];report["BestLap_"+key]="";
            }
            report["PlayerRow"]="1";report["StyleCategory"]="NEAR MISS";report["StyleCategoryPoints"]="100";
            report["CountingStylePoints"]="1250";report["StyleMultiplier"]="1";report["TotalPointsAnimate"]="1250";
            report["UserName"]="YOU";report["RewardBlurbString"]="Congratulations!  You have 500 added to your Bank.";report["Victory_Title"]="Race Victory Reward";
            report["CareerStyleDisplayed"]="3402";
            PostRaceMenu flow(true,true,true);
            for(unsigned n=0;n<stage;++n)flow.update({false,false,true,false});
            std::vector<unsigned char> pixels(1280*720*4,60);for(std::size_t i=3;i<pixels.size();i+=4)pixels[i]=255;
            art.draw_post_race({pixels.data(),1280,720,1280*4},flow,report);
            FrontendUiFrame frame;art.record_post_race(1280,720,flow,report,frame);
            require(!frame.vertices.empty(),"post-race sequence has no UI");
            auto path=prefix;path+="-flow"+std::to_string(stage)+".tga";
            FrameWriteResult written;require(write_rgba_tga(1280,720,pixels,path,{},written),written.error);
        }
        CareerCatalog rewards;require(rewards.load(root,error),error);
        const auto* drag_reward=rewards.media(4);require(drag_reward&&drag_reward->unlock_messages.size()==2,"original drag reward records");
        for(unsigned i=0;i<drag_reward->unlock_messages.size();++i) {
            PostRaceValues notice{{"RewardBlurbString",drag_reward->unlock_messages[i]},{"Victory_Title","Race Victory Reward"}};
            PostRaceMenu page(false,false,1);
            std::vector<unsigned char> pixels(1280*720*4,60);for(std::size_t k=3;k<pixels.size();k+=4)pixels[k]=255;
            art.draw_post_race({pixels.data(),1280,720,1280*4},page,notice);
            FrontendUiFrame frame;art.record_post_race(1280,720,page,notice,frame);require(!frame.vertices.empty(),"reward page has no UI");
            auto path=prefix;path+="-unlock"+std::to_string(i)+".tga";
            FrameWriteResult written;require(write_rgba_tga(1280,720,pixels,path,{},written),written.error);
        }
        for(const auto& expected:post_style_native) {
            PostRaceValues values{{"StyleLabel22","NEAR MISS"},{"StyleValue22",std::to_string(expected.points)}};
            PostRaceStyleAnimator animator(values);double seconds=double(expected.tick)/4000;
            while(seconds>1){animator.advance(1);seconds-=1;}animator.advance(seconds);animator.bind(values);
            require(std::stoi(values["CountingStylePoints"])==expected.value,"original49F1B0 category numeric result");
        }
        PostRaceValues style_values{{"StyleLabel22","NEAR MISS"},{"StyleValue22","100"},{"CareerStyleBase","1250"}};
        PostRaceStyleAnimator animator(style_values);animator.bind(style_values);
        require(style_values["CountingStylePoints"]=="0","native style count starts blank");
        animator.advance(1);animator.bind(style_values);
        require(style_values["CountingStylePoints"]=="40","native half-second pause then1.25-second category count");
        animator.finish();animator.bind(style_values);
        require(animator.done()&&style_values["CareerStyleDisplayed"]=="1350","skip finishes subtotal and persistent style gauge");
        require(post_race_time(5560,true)=="+   1.39" && post_race_time(165000)=="   41.25" &&
            post_race_time(240040,true)=="+1:00.01","original586580 result/behind formatting");
        PostRaceMenu win(true,true,true);
        require(win.page()==PostRacePage::results,"results first");
        require(win.update({false,false,false,true})==PostRaceAction::none && win.page()==PostRacePage::results,"back cannot discard result flow");
        win.update({false,false,true,false});require(win.page()==PostRacePage::style,"style second");
        win.update({false,false,true,false});require(win.page()==PostRacePage::reward,"reward third");
        win.update({false,false,true,false});require(win.page()==PostRacePage::menu,"menu last");
        PostRaceMenu drag_rewards(true,false,3);drag_rewards.update({false,false,true,false});
        for(unsigned i=0;i<3;++i) {
            require(drag_rewards.page()==PostRacePage::reward && drag_rewards.reward_index()==i,"cash/car/track reward order");
            drag_rewards.update({false,false,true,false});
        }
        require(drag_rewards.page()==PostRacePage::menu,"all rewards precede menu");
        PostRaceMenu loss(true,false,false);loss.update({false,false,true,false});require(loss.page()==PostRacePage::menu,"loss has no cash reward");
        PostRaceMenu menu;require(menu.update({true,false,true,false})==PostRaceAction::none && menu.statistics(),"statistics entry");
        require(menu.update({false,false,false,true})==PostRaceAction::none && !menu.statistics(),"statistics back");
        require(menu.update({true,false,true,false})==PostRaceAction::none&&menu.confirming(),"restart selection requires original confirmation");
        FrontendInput ok;ok.left=true;menu.update(ok);ok.left=false;ok.accept=true;
        require(menu.update(ok)==PostRaceAction::restart,"confirmed restart selection");
        PostRaceMenu leave;leave.update({false,false,true,false});require(leave.confirming()&&!leave.confirmed(),"continue defaults to Cancel");
        ok.accept=false;ok.left=true;leave.update(ok);ok.accept=true;ok.left=false;
        require(leave.update(ok)==PostRaceAction::leave,"confirmed continue selection");
        std::cout<<"PASS post-race menu/statistics/selection\n";return 0;
    }
    if(loading) {
        RaceLoadingArt art;require(art.load(root,error),error);
        std::vector<unsigned char> pixels(1280*720*4);
        for(unsigned i=0;i<4;++i) {
            art.draw({pixels.data(),1280,720,1280*4},double(i)/3);
            auto file=prefix;file+="-loading"+std::to_string(i)+".tga";
            FrameWriteResult result;require(write_rgba_tga(1280,720,pixels,file,{},result),result.error);
        }
        for(const auto dimensions: {std::pair<unsigned,unsigned>{1280,720},{640,480},{1280,720}}) {
            const unsigned w=dimensions.first,h=dimensions.second,stride=w*4+32;
            std::vector<unsigned char> actual(std::size_t(stride)*h,123),reference=actual;
            for(double t: {0.,.32, .325, .333333333, .99, 1.33333333}) {
                art.draw({actual.data(),w,h,stride},t);
                art.draw({reference.data(),w,h,stride},t,false);
                require(actual==reference,"loading cache differs from original full redraw or corrupts stride padding");
            }
        }
        for(bool cached: {false,true}) {
            const auto begin=std::chrono::steady_clock::now();
            for(int i=0;i<ticks;++i)art.draw({pixels.data(),1280,720,1280*4},double(i)/60,cached);
            const auto elapsed=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-begin).count();
            std::cout<<"LOADING_DRAW cached="<<cached<<" frames="<<ticks<<" total_ms="<<elapsed<<" mean_ms="<<(ticks?elapsed/ticks:0)<<"\n";
        }
        std::cout<<"PASS original loading artwork, four activity positions\n";return 0;
    }
    IntroGrid grid;require(load_intro_grid(root,grid,error),error);
    require(grid.setup->authenticated_inputs,"unauthenticated setup");
    const int expected[]={100,0,102,101};
    for(std::size_t i=0;i<4;++i) {
        const auto& a=grid.actors[i];require(a.config.config_id==expected[i],"wrong final grid order");
        require(a.config.final_grid==i+1 && a.config.marker.index==i,"wrong grid marker");
        require(a.frame->report.car_type_name==a.config.visual.car && a.frame->skinned,"wrong authored car");
        unsigned unresolved=0;
        for(const auto& draw:a.frame->scene.draws)if(draw.binding==BindingKind::unresolved && draw.drawable) {
            std::cout<<"UNRESOLVED "<<a.config.visual.name<<" "<<std::hex<<draw.texture_hash<<std::dec
                     <<" mesh="<<a.frame->scene.meshes.at(draw.mesh).name<<"\n";
            ++unresolved;
        }
        require(unresolved==0,"drawable unresolved intro textures: "+a.config.visual.name);
        std::cout<<"ACTOR "<<a.config.visual.name<<" "<<a.config.visual.car<<" grid="<<i+1
                 <<" ground="<<a.pose.ground.x<<","<<a.pose.ground.y<<","<<a.pose.ground.z
                 <<" draws="<<a.frame->scene.draws.size()<<"\nPARTS "<<a.config.visual.name;
        for(const auto& record:a.request.installed_parts.records)
            std::cout<<" "<<(record && *record!=kNoCarPart?static_cast<long long>(*record):-1LL);
        std::cout<<"\n";
    }
    if(terrain_probe) {
        const auto& world=grid.course.world;
        const auto& actor=grid.actors[1];
        struct Site {const char* name;VehicleVec3 p;};
        const Site sites[]={{"start",actor.state.position},{"avenue",{1194.705,364.269,46.793}},
            {"parking",{988.813,346.362,41.001}},{"theater-approach",{735.990,109.627,55.246}}};
        Sha256 signature;
        for(const auto& site:sites)for(unsigned heading=0;heading<4;++heading) {
            const float cs=heading==0?1:heading==2?-1:0,sn=heading==1?1:heading==3?-1:0;
            std::array<float,3> origin{{float(site.p.x),float(site.p.y),float(site.p.z)}};
            std::vector<std::array<float,3>> ends;
            for(const auto& p:actor.hull.points) {
                const float x=p[0]-actor.placement_spec.handling.centre_of_mass[0];
                const float y=p[1]-actor.placement_spec.handling.centre_of_mass[1];
                ends.push_back({origin[0]+cs*x-sn*y,origin[1]+sn*x+cs*y,
                    origin[2]+p[2]-actor.placement_spec.handling.centre_of_mass[2]});
            }
            std::vector<TrackContact> contacts(ends.size());
            std::vector<TrackContactStatus> statuses(ends.size());
            const auto start=std::chrono::steady_clock::now();
            for(int n=0;n<ticks;++n)
                query_track_contact_fan(world,origin.data(),ends.data(),ends.size(),contacts.data(),statuses.data());
            const auto ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
            std::vector<std::size_t> cells;
            float minx=origin[0],maxx=minx,miny=origin[1],maxy=miny;
            for(const auto& e:ends){minx=std::min(minx,e[0]);maxx=std::max(maxx,e[0]);miny=std::min(miny,e[1]);maxy=std::max(maxy,e[1]);}
            require(query_track_collision_cells(world.collision,minx,miny,maxx,maxy,cells),"terrain cells unavailable");
            std::size_t named=0;for(const auto c:cells)named+=world.collision.cells[c].index_count;
            unsigned hit=0;
            for(unsigned i=0;i<ends.size();++i) {
                TrackContact control;
                const auto status=query_track_contact_segment(world,origin.data(),ends[i].data(),control);
                require(status==statuses[i],"terrain batch status differs");
                signature.update(&status,sizeof(status));
                if(status==TrackContactStatus::hit) {
                    require(control.polygon==contacts[i].polygon && control.triangle==contacts[i].triangle &&
                        control.distance==contacts[i].distance &&
                        std::equal(control.point,control.point+3,contacts[i].point) &&
                        std::equal(control.normal,control.normal+3,contacts[i].normal),"terrain batch contact differs");
                    signature.update(&control.distance,sizeof(control.distance));
                    signature.update(control.point,sizeof(control.point));++hit;
                }
            }
            std::cout<<"TERRAIN site="<<site.name<<" heading="<<heading<<" rays="<<ends.size()
                <<" hit="<<hit<<" cells="<<cells.size()<<" polygon_refs="<<named<<" batch_us="<<ms*1000/ticks<<"\n";
        }
        std::cout<<"PASS terrain batch/scalar signature="<<sha256_hex(signature.finish())<<"\n";return 0;
    }
    if(pair_probe) {
        // Isolates the production pair adapter, with optional pose search. No track,
        // controller, suspension integration or original-solver parity claim.
        const auto& player=grid.actors[1];const auto box_a=race_contact_box(player.hull);
        VehicleModel model_a(player.placement_spec);
        auto path=prefix;path += "-pairs.csv";
        require(!std::filesystem::exists(path),"pair probe output already exists");
        std::ofstream out(path);require(bool(out),"pair probe output unavailable");
        out<<std::setprecision(17)<<"kind,name,speed,overlap_x,lateral_fraction,offset_x,offset_y,overlapping,applied,depth,move_a_z,move_b_z,velocity_a_z,velocity_b_z,spin_a,spin_b\n";
        unsigned cases=0,vertical_moves=0;double max_move=0;
        auto probe=[&](const char* kind,const std::string& name,const VehicleSpec& spec,const VehicleHull& hull) {
            VehicleModel model_b(spec);const auto box_b=race_contact_box(hull);
            for(const double speed:{0.,10.,30.,50.})for(const double overlap:{.01,.25,.75,1.5})
            for(const double lateral:{0.,.5,.9}) {
                VehiclePose pose_a;pose_a.heading=0;pose_a.speed=speed;pose_a.ground={0,0,0};
                VehiclePose pose_b=pose_a;pose_b.speed=0;
                // Heading zero maps the authored local X/Y axes to world X/Y.
                pose_b.ground.x=box_a.high.x-box_b.low.x-overlap;
                pose_b.ground.y=(box_a.high.y-box_b.low.y)*lateral;
                VehicleState a,b;
                require(model_a.reset(a,pose_a)&&model_b.reset(b,pose_b),"pair probe reset failed");
                const auto initial_a=a.position,initial_b=b.position;
                RacePairContact c;
                if(swept_pair_probe) {
                    auto previous_a=race_contact_pose(a);const auto previous_b=race_contact_pose(b);
                    previous_a.position.x-=overlap+.01;
                    require(resolve_swept_race_pair(a,player.placement_spec,box_a,b,spec,box_b,c,previous_a,previous_b),"swept pair probe refused");
                    require(!c.overlapping||(c.searched&&c.depth<=(overlap+.01)/256+.001),
                        "retail hull first-contact bracket failed");
                } else require(resolve_race_pair(a,player.placement_spec,box_a,b,spec,box_b,c),"pair probe refused");
                const double dz_a=a.position.z-initial_a.z,dz_b=b.position.z-initial_b.z;
                const auto length=[](VehicleVec3 v){return std::sqrt(v.x*v.x+v.y*v.y+v.z*v.z);};
                out<<kind<<','<<name<<','<<speed<<','<<overlap<<','<<lateral<<','<<pose_b.ground.x<<','<<pose_b.ground.y
                    <<','<<c.overlapping<<','<<c.applied<<','<<c.depth<<','<<dz_a<<','<<dz_b
                    <<','<<a.velocity.z<<','<<b.velocity.z<<','<<length(a.angular_velocity)<<','<<length(b.angular_velocity)<<'\n';
                ++cases;if(dz_a!=0||dz_b!=0)++vertical_moves;
                max_move=std::max({max_move,std::abs(dz_a),std::abs(dz_b)});
            }
        };
        for(unsigned i=0;i<grid.actors.size();++i)if(i!=1) {
            const auto& a=grid.actors[i];probe("rival",a.config.visual.name,a.placement_spec,a.hull);
        }
        for(const auto& a:grid.traffic)probe("traffic",a.assets.car_type_name,a.placement_spec,a.hull);
        out.flush();require(bool(out),"pair probe output failed");
        std::cout<<"PAIR_PROBE cases="<<cases<<" vertical_moves="<<vertical_moves<<" max_vertical_move="<<max_move
            <<" original_parity=unmeasured full_dynamics=unmeasured\n";
        return 0;
    }
    if(traffic_impact) {
        VehicleTrackCourse ground;require(build_vehicle_proving_ground(ground,error),error);
        // The physical course stays the finite proving ground. Traffic's road
        // loader separately needs the authenticated retail route image name.
        ground.name=grid.course.name;
        std::vector<RaceTrafficSetup> setups;
        for(const auto& a:grid.traffic)setups.push_back({a.placement_spec,a.hull,a.assets.car_type_name});
        RaceTraffic traffic;require(traffic.load(root,ground,setups,error),error);
        std::vector<VehicleState> legacy;
        std::vector<std::unique_ptr<VehicleModel>> models;
        for(auto& a:traffic.contact_actors()) {
            auto model=std::make_unique<VehicleModel>(a.setup.spec);
            VehiclePose pose=ground.start;pose.ground={48,92,ground.start.ground.z};
            pose.heading=vehicle_track_heading(16384);pose.speed=20;
            require(model->reset(a.car,pose),"impact fixture reset");a.active=true;a.impacted=true;a.crash_stop=true;
            legacy.push_back(a.car);models.push_back(std::move(model));
        }
        const auto observer=traffic.actors()[0].car;
        const std::array<const VehicleState*,4> racers{{&observer,&observer,&observer,&observer}};
        std::array<const RaceContactBox*,4> racer_shapes;racer_shapes.fill(&traffic.actors()[0].box);
        VehicleInput stop;stop.brake=1;TrackVehicleGround old_ground(ground.world);
        double old_time=0,new_time=0;unsigned legacy_escaped=0;
        std::vector<double> farthest(traffic.actors().size()),highest(traffic.actors().size());
        for(unsigned tick=1;tick<=180;++tick) {
            auto begin=std::chrono::steady_clock::now();
            for(unsigned i=0;i<legacy.size();++i)for(unsigned slice=0;slice<4;++slice) {
                VehicleTelemetry telemetry;
                require(models[i]->step(legacy[i],stop,old_ground,telemetry,1.0/60,1.0/240,slice==0),"legacy impact baseline refused");
            }
            old_time+=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-begin).count();
            begin=std::chrono::steady_clock::now();require(traffic.advance(racers,tick,error,-1,1.F/60.F,{},racer_shapes),error);
            new_time+=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-begin).count();
            for(unsigned i=0;i<traffic.actors().size();++i) {
                const auto& a=traffic.actors()[i];
                farthest[i]=std::max(farthest[i],a.car.position.y);highest[i]=std::max(highest[i],a.car.position.z);
                require(a.active&&a.impacted&&std::isfinite(a.car.position.z),"impact body disappeared or invalid");
            }
        }
        for(unsigned i=0;i<traffic.actors().size();++i) {
            const auto& a=traffic.actors()[i];
            std::cout<<"IMPACT "<<a.setup.name<<" old_y="<<legacy[i].position.y<<" new_max_y="<<farthest[i]
                <<" new_max_z="<<highest[i]<<" final_z="<<a.car.position.z<<"\n";
            require(farthest[i]<97&&a.car.position.z>ground.start.ground.z-1&&highest[i]<ground.start.ground.z+3,"traffic crossed wall or launched");
            legacy_escaped+=legacy[i].position.y>=97?1U:0U;
        }
        require(legacy_escaped>0,"wall probe did not reproduce former escape");
        std::cout<<"PASS "<<traffic.actors().size()<<" retail traffic hulls: former escapes="<<legacy_escaped
            <<" fixed=0 frames=180 old_ms="<<old_time<<" new_ms="<<new_time<<"\n";return 0;
    }
    if(props_check) {
        RaceProps baseline;require(baseline.load(root,error),error);
        std::vector<std::string> done;unsigned slept=0,rewoken=0;
        for(std::size_t index=0;index<baseline.objects().size();++index) {
            const auto& original=baseline.objects()[index];
            if(std::find(done.begin(),done.end(),original.name)!=done.end())continue;
            done.push_back(original.name);RaceProps props;require(props.load(root,error),error);
            const auto& player=grid.actors[1];VehicleModel model(player.placement_spec);VehicleState car;
            VehiclePose pose;unsigned best=0;
            const auto& origin=original.state.position;
            for(unsigned direction=0;direction<16;++direction) {
                const double angle=direction*6.283185307179586/16,c=std::cos(angle),s=std::sin(angle);
                const float start[3]={float(origin.x-4*c),float(origin.y-4*s),float(origin.z)};TrackContact ground;
                if(query_track_contact_point(grid.course.world,start,3,5,ground)!=TrackContactStatus::hit)continue;
                unsigned count=0;
                for(unsigned distance=2;distance<=40;distance+=2) {
                    const float q[3]={float(origin.x+distance*c),float(origin.y+distance*s),float(origin.z)};TrackContact hit;
                    if(query_track_contact_point(grid.course.world,q,3,5,hit)==TrackContactStatus::hit)++count;
                }
                if(count>best){best=count;pose.heading=angle;pose.ground={start[0],start[1],ground.point[2]};}
            }
            require(best>0,"prop probe has no supported approach");pose.speed=15;
            require(model.reset(car,pose),"prop probe car reset");
            const auto box=race_contact_box(player.hull);
            auto distant=car;distant.position={0,0,100000};distant.rigid_body_ready=false;
            require(props.meet(distant,player.placement_spec,box,error)&&props.hits==0&&!distant.rigid_body_ready,"distant prop mutated car");
            for(unsigned tick=0;tick<300;++tick) {
                if(tick<30){car.position.x+=car.velocity.x/60;car.position.y+=car.velocity.y/60;car.rigid_body_ready=false;
                    require(props.meet(car,player.placement_spec,box,error),error);}
                require(props.advance(grid.course.world,error),error);
                if(index==0 && tick%30==0) {
                    const auto& p=props.objects()[index].state.position;const float q[3]={float(p.x),float(p.y),float(p.z)};TrackContact ground;
                    const auto status=query_track_contact_point(grid.course.world,q,3,150,ground);
                    std::cout<<"PROP_TRACE "<<tick<<" position="<<p.x<<","<<p.y<<","<<p.z<<" ground="<<int(status)<<":"<<ground.point[2]<<"\n";
                }
            }
            const auto& hit=props.objects()[index];
            const auto& a=original.state.position;const auto& b=hit.state.position;
            const double moved=std::hypot(b.x-a.x,b.y-a.y);
            require(props.hits>0&&hit.moved,"prop never reacted: "+original.name);
            std::cout<<"PROP_DELTA "<<original.name<<" "<<moved<<" "<<b.z-a.z<<" bounds="<<hit.box.low.x<<","<<hit.box.low.y<<","<<hit.box.low.z<<":"<<hit.box.high.x<<","<<hit.box.high.y<<","<<hit.box.high.z<<"\n";
            require(std::isfinite(b.z)&&(hit.expired||std::abs(b.z-a.z)<20)&&moved<100,"prop escaped finite scene: "+original.name);
            if(!hit.awake&&!hit.expired) {
                ++slept;const auto resting=hit.state.rigid_body;
                for(unsigned tick=0;tick<60;++tick)require(props.advance(grid.course.world,error),error);
                require(hit.state.rigid_body.position==resting.position&&hit.state.rigid_body.axes==resting.axes,
                    "resting prop drifted: "+original.name);
                for(unsigned direction=0;direction<16&&!hit.awake;++direction) {
                    const double angle=direction*6.283185307179586/16;
                    VehiclePose retry;retry.heading=angle;retry.speed=15;
                    retry.ground={hit.state.position.x-2*std::cos(angle),hit.state.position.y-2*std::sin(angle),0};
                    const float q[3]={float(retry.ground.x),float(retry.ground.y),float(hit.state.position.z)};
                    TrackContact ground;if(query_track_contact_point(grid.course.world,q,3,5,ground)!=TrackContactStatus::hit)continue;
                    retry.ground.z=ground.point[2];require(model.reset(car,retry),"repeat impact reset");
                    require(props.meet(car,player.placement_spec,box,error),error);
                }
                if(hit.awake){++rewoken;require(props.advance(grid.course.world,error),error);
                    require(hit.awake,"newly struck prop retired immediately");}
            }
            std::cout<<"PROP "<<original.name<<" hits="<<props.hits<<" moved="<<moved<<" z="<<b.z-a.z<<" expired="<<hit.expired<<" awake="<<hit.awake<<" steps="<<props.steps<<"\n";
        }
        require(slept>0&&rewoken>0,"no runtime sleep/reimpact witness");
        std::cout<<"PROP_REST slept="<<slept<<" rewoken="<<rewoken<<"\n";
        std::cout<<"PASS props unique="<<done.size()<<" placements="<<baseline.objects().size()<<"\n";return 0;
    }
    // A wrong car/preset identity must not silently substitute another actor.
    std::vector<unsigned char> front;std::vector<RaceTableIssue> issues;RaceTableLimits lim;
    lim.max_file_size=32*1024*1024;lim.max_image_size=64*1024*1024;
    require(load_race_image(root/"FrontEnd/FrontB.lzc",front,issues,lim),"front image");
    InstalledPartSelection wrong;
    const auto& player=grid.actors[1];
    require(!resolve_frontend_preset_parts(front,static_cast<std::size_t>(player.config.visual.offset),
            "SUPRA",player.config.visual.name,player.frame->parts,wrong,error),"wrong car accepted");
    TrackSlice track;require(load_track_slice(posix_file_system(),root,track,error,nullptr,nullptr,0),error);
    RaceRun run;
    run.set_recovery_trace(std::getenv("NFSU_TRACE_RECOVERY")!=nullptr);
    require(run.set_event(0,error),error);
    require(run.set_car(player.assets.car_type_name,error),error);
    require(run.set_steering(RaceSteering::recovered_predicate1,error),error);
    require(run.set_performance_levels(*player.config.performance,error),error);
    std::vector<RaceRivalSetup> setups;std::array<int,3> contexts{};
    for(const auto& actor:grid.actors)if(actor.config.role==Event0Role::opponent) {
        contexts[setups.size()]=actor.config.selected_context;
        setups.push_back({actor.placement_spec,actor.pose,actor.hull,actor.config.event_label,actor.config.visual.car,actor.config.ai_mode});
    }
    require(run.set_intro_grid(player.pose,player.hull,setups,contexts,error,player.config.final_grid),error);
    std::vector<RaceTrafficSetup> traffic_setup;
    for(const auto& car:grid.traffic)traffic_setup.push_back({car.placement_spec,car.hull,car.assets.car_type_name,unsigned(car.assets.car_type)});
    require(run.set_intro_traffic(traffic_setup,error),error);
    require(run.start(root,track,error),error);
    const auto& session=run.session();
    auto cpu_scene=std::move(track.scene);
    for(auto& texture:cpu_scene.region.textures)if(texture.storage!=RenderTextureStorage::rgba8) {
        require(decode_level(texture.storage==RenderTextureStorage::dxt1?PixelFormat::dxt1:PixelFormat::dxt3,
                texture.width,texture.height,texture.blocks.data(),texture.blocks.size(),texture.rgba,error),error);
        texture.storage=RenderTextureStorage::rgba8;texture.blocks.clear();
    }
    cpu_scene.region.cost.texture_bytes=0;
    for(const auto& texture:cpu_scene.region.textures)cpu_scene.region.cost.texture_bytes+=texture.payload().size();
    grid.course.start=player.pose;
    RaceResultStatistics result_stats;
    std::array<double,4> grid_z{};
    for(unsigned i=0;i<4;++i)grid_z[i]=(i?session.rival(i-1).car():session.car()).position.z;
    RaceRouteDriver driver(RaceDrivingLine::event1_forward);
    std::array<RaceRacerTiming,3> finished{};
    std::array<VehicleVec3,3> finish_position{};
    std::array<bool,3> moved_after_finish{};
    unsigned stuck_ticks=0,recovery_requests=0;
    Sha256 prop_sequence,rival_sequence;
    SpeedBlur blur;SpeedBlurFrame blur_frame;
    const auto advance=[&]() {
        RaceSimulationProfile profile;
        const bool measured=profiling && !race_car_held(session.timing());
        auto pad=driver.sample(session);
        if(speed_blur_preview_mode && session.timing().step+12>=std::size_t(ticks))
            pad.pads[0].buttons|=gamepad_button_bit(GamepadButton::shoulder_left);
        if(complete && !race_car_held(session.timing())) {
            const auto& v=session.car().velocity;
            stuck_ticks=v.x*v.x+v.y*v.y<1?stuck_ticks+1:0;
            if(stuck_ticks==300) {
                // Explicit completion/recovery scenario: press the ordinary
                // reset input after five seconds stuck. Never writes pose,
                // lap or finish state. The unassisted run remains separate.
                pad.pads[0].buttons|=gamepad_button_bit(GamepadButton::select);
                ++recovery_requests;stuck_ticks=0;
            }
        }
        if(grid_revs && race_car_held(session.timing()) && session.timing().step>0)
            pad=race_input_frame({1,gamepad_button_bit(GamepadButton::trigger_right),0},session.timing().step);
        require(run.simulate(pad,error,measured?&profile:nullptr,measured?&profile_clock:nullptr),error);
        if(measured) {
            const auto values=profile.values();++sim_samples;
            for(unsigned i=0;i<6;++i){sim_sum[i]+=values[i];sim_max[i]=std::max(sim_max[i],values[i]);}
            const double children[]={profile.traffic_motion,profile.prop_motion,profile.prop_contacts};
            for(unsigned i=0;i<3;++i){contact_sum[i]+=children[i];contact_max[i]=std::max(contact_max[i],children[i]);}
        }
        if(spatial_profile && measured && session.timing().step%60==0) {
            const auto& p=session.car().position;const auto& c=session.progress().coordinate;
            std::cout<<"SPATIAL tick="<<session.timing().step<<" x="<<p.x<<" y="<<p.y<<" z="<<p.z
                <<" node="<<c.node<<" fraction="<<c.fraction<<" player_ms="<<profile.player
                <<" rivals_ms="<<profile.rivals<<" traffic_ms="<<profile.traffic_motion
                <<" props_ms="<<profile.prop_motion<<" pair_ms="<<profile.contacts<<"\n";
            if(session.props())for(const auto& prop:session.props()->objects())if(prop.awake&&!prop.expired) {
                const auto& at=prop.state.position;const double dx=at.x-p.x,dy=at.y-p.y;
                std::cout<<"SPATIAL_PROP tick="<<session.timing().step<<" section="<<prop.section
                    <<" instance="<<prop.instance<<" name="<<prop.name<<" vertices="<<prop.points.size()
                    <<" x="<<at.x<<" y="<<at.y<<" z="<<at.z<<" distance="<<std::sqrt(dx*dx+dy*dy)
                    <<" age="<<prop.age<<" quiet="<<prop.quiet<<" lifetime="<<prop.lifetime;
                const auto& body=prop.state.rigid_body;
                float speed2=0,spin2=0;
                for(float v:body.velocity)speed2+=v*v;
                for(float v:body.angular_velocity)spin2+=v*v;
                std::cout<<" speed2="<<speed2<<" spin2="<<spin2<<"\n";
            }
        }
        if(grid_start && session.timing().step<360) {
            for(unsigned i=0;i<4;++i) {
                const auto& body=i?session.rival(i-1).car():session.car();
                if(session.timing().step<=163) {
                    require(body.position.z-grid_z[i]<.005,"initial grid release lifted body over 5 mm");
                    for(const auto& wheel:body.wheels)require(wheel.contact,"grid release lost a wheel contact");
                }
                std::cout<<"GRID_BODY "<<session.timing().step<<" actor="<<i<<" held="<<race_car_held(session.timing())
                    <<" z="<<std::setprecision(10)<<body.position.z<<" vz="<<body.velocity.z
                    <<" compression="<<body.wheels[0].compression<<" contact="<<body.wheels[0].contact<<"\n";
            }
        }
        if(speed_blur_preview_mode) {
            const auto& c=session.camera();const auto& v=session.camera_velocity();
            const VehicleVec3 right{c.forward.y*c.up.z-c.forward.z*c.up.y,
                c.forward.z*c.up.x-c.forward.x*c.up.z,c.forward.x*c.up.y-c.forward.y*c.up.x};
            const auto local=camera_motion_to_view(v,{{float(right.x),float(right.y),float(right.z)}},
                {{float(c.up.x),float(c.up.y),float(c.up.z)}},{{float(c.forward.x),float(c.forward.y),float(c.forward.z)}});
            if(session.timing().stage==RaceStage::racing && session.timing().outcome==RaceOutcome::running)
                blur_frame=blur.frame(session.nitrous_boosting(),local[0],local[1],local[2]);
            else {blur.reset();blur_frame={};}
        }
        if(profiling && session.props())for(const auto& prop:session.props()->objects()) {
            const auto& b=prop.state.rigid_body;
            for(const auto& v:{b.position,b.momentum,b.angular_momentum,b.velocity,b.angular_velocity,b.force,b.torque})
                for(float x:v)prop_sequence.update(&x,sizeof(x));
            for(const auto& matrix:{b.axes,b.snapshot_axes})for(const auto& row:matrix)
                for(float x:row)prop_sequence.update(&x,sizeof(x));
            for(float x:{b.adaptive_damping,b.yaw_torque_limit,prop.impact.activation,prop.age,prop.quiet})
                prop_sequence.update(&x,sizeof(x));
            const unsigned char flags[]={static_cast<unsigned char>(prop.moved),static_cast<unsigned char>(prop.awake),static_cast<unsigned char>(prop.expired)};
            prop_sequence.update(flags,sizeof(flags));
        }
        if(profiling)for(unsigned i=0;i<3;++i) {
            const auto& rival=session.rival(i);
            const auto& record=rival.record();
            rival_sequence.update(record.data(),record.size());
            const auto& a=rival.ai_physics();const auto& b=a.body;const auto& e=a.engine;
            const auto number=[&](const auto& v){rival_sequence.update(&v,sizeof(v));};
            for(const auto& v:{b.position,b.momentum,b.angular_momentum,b.velocity,b.angular_velocity,b.force,b.torque})
                for(float x:v)number(x);
            for(const auto& matrix:{b.axes,b.snapshot_axes})for(const auto& row:matrix)
                for(float x:row)number(x);
            for(float x:{b.adaptive_damping,b.yaw_torque_limit,e.rpm,e.throttle,e.brake,e.boost,e.grip,
                         e.low_slip,e.extra_torque_scale,a.component_interval})number(x);
            for(const auto& w:e.wheels) {
                for(float x:{w.spin,w.compression,w.compression_rate,w.load,w.feedback[0],w.feedback[1],
                             w.slip[0],w.slip[1],w.excess,w.preload})number(x);
                for(float x:w.normal)number(x);
                number(w.contact);number(w.sliding);number(w.locked);
            }
            number(e.steering);number(e.gear);number(e.gear_request);number(e.extra_torque);
            number(e.traction_until);number(e.world_time);number(a.prepared);
            number(a.slices);number(a.ground_queries);
        }
        result_stats.observe(session);
        require(run.commit(error),error);
        for(unsigned i=0;i<3;++i) {
            const auto& r=session.rival(i);const auto& result=r.timing().racers[0];
            if(!result.finish_valid)continue;
            if(!finished[i].finish_valid) {finished[i]=result;finish_position[i]=r.car().position;}
            require(result.finish_step==finished[i].finish_step&&result.finish_ticks==finished[i].finish_ticks,
                "post-finish physics changed the result");
            const auto p=r.car().position;const auto at=finish_position[i];
            if((p.x-at.x)*(p.x-at.x)+(p.y-at.y)*(p.y-at.y)>25)moved_after_finish[i]=true;
        }
    };
    for(int tick=0;tick<ticks && session.timing().outcome==RaceOutcome::running;++tick) {
        advance();
        if(tick==599) {
            std::size_t r=0;
            for(const auto& actor:grid.actors) {
                const auto& state=actor.config.role==Event0Role::human?session.car():session.rival(r++).car();
                const double dx=state.position.x-actor.pose.ground.x,dy=state.position.y-actor.pose.ground.y;
                require(dx*dx+dy*dy>25,"actor did not leave starting grid in ten seconds");
            }
        }
    }
    if(grid_start){std::cout<<"PASS grid body trace\n";return 0;}
    std::cout<<"RECOVERY input_requests="<<recovery_requests<<" assisted="<<complete<<"\n";
    if(session.traffic()) {
        const auto& traffic=*session.traffic();
        std::cout<<"TRAFFIC decisions="<<traffic.decisions<<" spawns="<<traffic.spawns<<" retired="<<traffic.retirements<<"\n";
        for(const auto& a:traffic.actors())std::cout<<"TRAFFIC_CAR "<<a.setup.name<<" active="<<a.active
            <<" route="<<a.cursor.route<<" point="<<a.cursor.point<<" lane="<<a.lane
            <<" x="<<a.car.position.x<<" y="<<a.car.position.y<<" speed="<<a.motion.speed
            <<" target="<<a.target_speed<<" ended="<<a.ended<<" contacts="<<a.contacts<<"\n";
    }
    std::size_t rival=0;
    for(auto& actor:grid.actors) {
        const auto& racer=actor.config.role==Event0Role::human?session:session.rival(rival++);
        actor.state=racer.car();
        require(drive_place_instances(actor.base_instances,actor.state,actor.placement_spec,
            actor.frame->scene.instances,&racer.animation()),"moving placement");
        const double dx=actor.state.position.x-actor.pose.ground.x,dy=actor.state.position.y-actor.pose.ground.y;
        std::cout<<"MOTION "<<actor.config.visual.name<<" tick="<<session.timing().step
            <<" metres="<<vehicle_sqrt(dx*dx+dy*dy)<<" speed="
            <<vehicle_sqrt(actor.state.velocity.x*actor.state.velocity.x+actor.state.velocity.y*actor.state.velocity.y)
            <<" lap="<<racer.progress().lap<<" parameter="<<racer.progress().coordinate.parameter
            <<" hull="<<actor.hull.points.size()<<"\n";
    }
    require(session.timing_config().racers==4,"missing shared four-racer timing");
    if(session.props())std::cout<<"PROPS loaded="<<session.props()->objects().size()<<" hits="<<session.props()->hits<<" candidates="<<session.props()->candidates<<" steps="<<session.props()->steps<<"\n";
    std::cout<<"CONTACTS impulses="<<session.pair_impulses()<<"\n";
    if(ticks>=40000)require(session.timing().outcome==RaceOutcome::finished,"intro autopilot did not finish two laps");
    if(ticks>=40000)for(unsigned i=0;i<3;++i)
        require(finished[i].finish_valid&&moved_after_finish[i],"finished rival froze on the finish line");
    std::cout<<"RACE outcome="<<race_outcome_name(session.timing().outcome)
        <<" racers="<<session.timing().racers.size()<<" player_laps="<<session.progress().lap<<"\n";
    std::cout<<"STYLE total="<<session.style().total()<<" ready="<<session.style().ready();
    for(unsigned i=0;i<kRaceStyleKinds;++i)if(session.style().item(i).total!=0)
        std::cout<<" ["<<session.style().rule(i).label<<":"<<session.style().item(i).total<<"]";
    std::cout<<"\n";
    // Admission negative and an actual hit: no teleport/reset of route state.
    auto ca=session.car(),cb=ca;
    ca.orientation=cb.orientation={};
    ca.position={0,0,1};cb.position={100,0,1};
    ca.velocity={10,0,0};cb.velocity={};
    ca.angular_velocity=cb.angular_velocity={};
    ca.rigid_body_ready=cb.rigid_body_ready=true;
    ca.rigid_body.momentum={10*player.placement_spec.handling.mass,0,0};cb.rigid_body.momentum={};
    ca.rigid_body.angular_momentum=cb.rigid_body.angular_momentum={};
    const auto box=race_contact_box(player.hull);RacePairContact contact;
    require(resolve_race_pair(ca,player.placement_spec,box,cb,player.placement_spec,box,contact) &&
        !contact.overlapping && ca.velocity.x==10 && cb.velocity.x==0,"distant contact mutated bodies");
    if(grid_revs)std::cout << "GRID_REVS tick=" << session.timing().step
        << " rpm=" << session.car().engine_rpm << " gear=" << session.car().gear
        << " throttle=" << session.telemetry().model.throttle
        << " held=" << race_car_held(session.timing()) << '\n';
    // A shallow longitudinal overlap makes X the unique SAT minimum;
    // fixed 3m separation can instead select Z for a longer selected hull.
    cb.position={box.high.x-box.low.x-.1,0,1};
    require(resolve_race_pair(ca,player.placement_spec,box,cb,player.placement_spec,box,contact) &&
        contact.applied && ca.velocity.x<10 && cb.velocity.x>0,"closing cars did not exchange impulse");
    require(!resolve_race_pair(ca,player.placement_spec,box,ca,player.placement_spec,box,contact),"self contact accepted");
    RaceHudAtlas hud_atlas;require(load_race_hud_atlas(root,hud_atlas,error,true),error);
    StartCrowd crowd;require(crowd.load(posix_file_system(),root,error),error);crowd.frame(session);
    const auto& crowd_timing=session.timing();
    const bool crowd_expected=!crowd_timing.go_valid ||
        (std::int64_t(crowd_timing.now_ticks)-crowd_timing.go_ticks)*30<140LL*kRaceTicksPerSecond;
    require((crowd.draw_count()!=0)==crowd_expected,"crowd start/scene-end visibility");
    std::cout<<"CROWD actors="<<crowd.actor_count()<<" draws="<<crowd.draw_count()<<" bytes="<<crowd.bytes()<<"\n";
    std::array<RenderTexture,3> glow_textures;
    require(load_original_car_glow_textures(root,glow_textures,error),error);
    if(session.props())for(const auto& prop:session.props()->objects())if(prop.moved) {
        for(auto& instance:cpu_scene.instances)if(instance.section_number==prop.section&&instance.instance_index==prop.instance) {
            const auto& b=prop.state.rigid_body;
            for(unsigned r=0;r<3;++r)for(unsigned c=0;c<3;++c)instance.transform.linear[3*r+c]=prop.expired?0:b.axes[r][c];
            for(unsigned c=0;c<3;++c) {
                float offset=0;for(unsigned r=0;r<3;++r)offset+=b.axes[r][c]*prop.spec.handling.centre_of_mass[r];
                instance.transform.translation[c]=b.position[c]-offset;
            }
        }
    }
    for(int view=0;view<(speed_blur_preview_mode?2:(session.traffic()?3:2));++view) {
        RenderOptions options;options.width=1280;options.height=720;
        options.material_state=MaterialStateMode::retail;options.car_shading=true;options.sky=true;
        require(drive_render_camera(session.camera(),ChaseFovMode::vertical_4_3,
            options.width,options.height,options.camera,error),error);
        if(view==1 && !speed_blur_preview_mode) {options.camera.distance=22;options.camera.pitch_deg=25;}
        if(view==2) {
            const RaceTrafficActor* selected=nullptr;
            for(const auto& a:session.traffic()->actors())if(a.active&&(!selected||a.motion.speed>selected->motion.speed))selected=&a;
            if(!selected)continue;
            const auto& p=selected->car.position;
            options.camera.target={p.x,p.z,-p.y};options.camera.distance=12;
            options.camera.yaw_deg=180-selected->motion.heading*180/3.14159265358979323846;
            options.camera.pitch_deg=18;
            std::cout<<"TRAFFIC_VIEW "<<selected->setup.name<<" position="<<p.x<<","<<p.y<<","<<p.z<<"\n";
        }
        options.camera.far_plane=400;
        CameraFrame basis;require(make_camera_frame(options.camera,options.width,options.height,basis,error),error);
        TrackCamera camera;camera.eye[0]=float(basis.eye.x);camera.eye[1]=float(basis.eye.y);camera.eye[2]=float(basis.eye.z);
        camera.target[0]=float(options.camera.target.x);camera.target[1]=float(options.camera.target.y);camera.target[2]=float(options.camera.target.z);
        camera.up[0]=0;camera.up[1]=1;camera.up[2]=0;camera.aspect=float(options.width)/options.height;
        camera.vertical_fov=float(options.camera.fov_y_deg*3.14159265358979323846/180);
        camera.near_plane=float(options.camera.near_plane);camera.far_plane=400;
        TrackFrustum frustum;require(track_frustum_look_at(camera,frustum),"frustum");
        const auto visible=cull_track_region(track.objects,frustum);
        TrackDrawList list;TrackDrawListOptions order;order.order=TrackDrawOrder::retail_bucket;
        require(build_track_draw_list(cpu_scene,visible.visible,order,list,error),error);
        Frame composed;RenderStats stats;TrackRenderStats track_stats;
        require(render_track_draw_list(cpu_scene,list,options,{},composed,stats,track_stats,error),error);
        if(speed_blur_preview_mode && view==1) {
            require(session.nitrous().active && blur_frame.active(),"preview must use live NOS resource");
            auto blurred=composed.rgba();
            speed_blur_preview(blurred,options.width,options.height,blur_frame);
            require(blurred!=composed.rgba(),"NOS blur must change the actual world");
            require(composed.replace_colors(std::move(blurred)),"postprocess dimensions");
            std::cout<<"SPEED_BLUR live_nos=1 alpha="<<unsigned(blur_frame.alpha)<<" envelope="<<blur.envelope()<<" offsets=";
            for(auto value:blur_frame.offsets)std::cout<<value<<",";
            std::cout<<" world only, cars/HUD after; CPU reference, not GPU timing\n";
        }
        for(auto& actor:grid.actors) {
            const auto shadow=project_car_shadow(actor.shadow,actor.state,actor.placement_spec,road_query,&grid.course.world);
            require(render_car_shadow(actor.shadow,shadow,options,composed,stats,error),error);
        }
        for(auto& actor:grid.actors) {
            require(render_stock_frame(*actor.frame,options,error),error);
            Frame next;require(compose_race_frame(composed,actor.frame->frame,next),"car composition");
            composed=std::move(next);
        }
        if(session.traffic())for(unsigned i=0;i<grid.traffic.size();++i) {
            const auto& state=session.traffic()->actors()[i];if(!state.active)continue;
            auto& actor=grid.traffic[i];
            require(drive_place_instances(actor.base_instances,state.car,actor.placement_spec,
                actor.frame->scene.instances,&state.animation),"traffic placement");
            if(actor.shadow.texture.usable()) {
                const auto shadow=project_car_shadow(actor.shadow,state.car,actor.placement_spec,road_query,&grid.course.world);
                require(render_car_shadow(actor.shadow,shadow,options,composed,stats,error),error);
            }
            require(render_stock_frame(*actor.frame,options,error),error);
            Frame next;require(compose_race_frame(composed,actor.frame->frame,next),"traffic composition");
            composed=std::move(next);
        }
        for(auto& actor:grid.actors)require(draw_original_car_glows(composed,options,glow_textures,
            place_original_car_glows(actor.glow_anchors,actor.frame->scene,basis,options.width,0),stats,error),error);
        if(session.traffic())for(unsigned i=0;i<grid.traffic.size();++i) {
            const auto& state=session.traffic()->actors()[i];if(!state.active)continue;
            const auto& actor=grid.traffic[i];
            const auto lights=place_original_car_glows(actor.glow_anchors,actor.frame->scene,basis,options.width,state.animation.braking?1:0,true);
            unsigned lit=0;for(const auto& light:lights)if(light.rgba[3])++lit;
            std::cout<<"TRAFFIC_LIGHTS "<<state.setup.name<<" visible="<<lit<<"\n";
            require(draw_original_car_glows(composed,options,glow_textures,lights,stats,error),error);
        }
        require(crowd.render(options,composed,stats,error),error);
        auto file=prefix;file+=speed_blur_preview_mode&&view==1?"-nos.tga":view==2?"-traffic.tga":view?"-grid.tga":"-chase.tga";
        auto pixels=composed.rgba();
        RaceHudFrame hud;
        build_race_hud_frame(session,hud_atlas,options.width,options.height,hud);
        RaceMinimapState minimap;
        require(append_race_minimap(session,hud_atlas,options.width,options.height,minimap,hud),"minimap frame");
        require(hud.quads<kRaceHudMaxQuads,"HUD quad capacity exhausted");
        composite_race_hud(hud_atlas,hud,pixels.data(),options.width,options.height,options.width*4);
        FrameWriteResult result;require(write_rgba_tga(options.width,options.height,pixels,file,{},result),result.error);
        std::cout<<"LEADERBOARD mode="<<int(session.leaderboard_mode());
        for(unsigned actor=0;actor<4;++actor)std::cout<<" ["<<session.leaderboard_label(actor)<<"]";
        std::cout<<"\n";
        std::cout<<"HUD quads="<<hud.quads<<" position="<<session.player_position()<<" order=";
        for(unsigned i=0;i<session.standings().count;++i)std::cout<<session.standings().order[i]<<",";
        std::cout<<"\n";
        std::cout<<"FRAME "<<view<<" "<<sha256_hex(sha256(pixels.data(),pixels.size()))<<"\n";
    }
    RaceGpuScene gpu;
    require(gpu.set_texture_filter(TextureFilterMode::linear),"filter policy");
    require(gpu.set_original_draw_distance(true),"visibility policy");
    gpu.set_race_visuals(true);gpu.set_wet_road_reflections(true);
    RaceCarDress dress;dress.request=&player.request;dress.skin=&player.skin;
    dress.bind=[](StockFrame& frame,const StockFrameInputs& inputs,const StockSceneRequest& request,
                  std::string& e,void*) {
        return bind_frontend_runtime_skin(frame,request,e) &&
               bind_frontend_sponsors(inputs,request.installed_parts,frame,e);
    };
    require(gpu.load(posix_file_system(),root,error,nullptr,nullptr,false,true,true,
        &player.assets,&dress,0,true),error);
    for(auto& actor:grid.actors)if(actor.config.role==Event0Role::opponent)
        require(gpu.add_rival(std::move(actor.frame->scene),actor.base_instances,
            actor.frame->skin_binding.source,std::move(actor.shadow),error,false,actor.glow_anchors),error);
    for(auto& actor:grid.traffic)
        require(gpu.add_rival(std::move(actor.frame->scene),actor.base_instances,
            kNoHandle,std::move(actor.shadow),error,true,actor.glow_anchors),error);
    RaceGpuFrame package;require(gpu.frame(session,package,error),error);
    require(validate_recorded_commands(package.commands,error),error);
    if(profiling)std::cout<<"COMMANDS initial "<<command_signature(package.commands)<<"\n";
    std::vector<const RecordedDraw*> dynamic_definitions;
    for(auto i=package.commands.static_track_draw_count();i<package.commands.draw_count();++i)
        dynamic_definitions.push_back(&package.commands.draw_view(i).definition);
    require(validate_prepared_dynamic_prefix(dynamic_definitions,package.commands,error),error);
    const auto& uploads=package.commands.resource_table();
    std::vector<const void*> pointers;for(const auto& r:uploads.resources)pointers.push_back(r.data);
    const auto dynamic=package.car_draws,resources=uploads.resources.size();
    const auto identity=package.commands.package_identity();
    if(session.timing().outcome==RaceOutcome::running)
        for(int i=0;i<30;++i)advance();
    require(gpu.frame(session,package,error),error);
    require(validate_recorded_commands(package.commands,error),error);
    require(package.car_draws==dynamic && package.commands.package_identity()==identity &&
        package.commands.resource_table().resources.size()==resources,"dynamic package identity moved");
    for(std::size_t i=0;i<resources;++i)
        require(package.commands.resource_table().resources[i].data==pointers[i],"rival payload address moved");
    // Exercise the actual console's prepared-prefix check across GO.
    while(!session.timing().go_valid) {
        advance();require(gpu.frame(session,package,error),error);
        require(validate_prepared_dynamic_prefix(dynamic_definitions,package.commands,error),error);
    }
    require(validate_recorded_commands(package.commands,error),error);
    if(ticks==90) {
        require(package.commands.draw_count()-package.commands.static_track_draw_count()==dynamic_definitions.size(),"crowd retired prematurely at GO");
        while((std::int64_t(session.timing().now_ticks)-session.timing().go_ticks)*30<140LL*kRaceTicksPerSecond) {
            advance();require(gpu.frame(session,package,error),error);
            require(validate_prepared_dynamic_prefix(dynamic_definitions,package.commands,error),error);
        }
        require(package.commands.draw_count()-package.commands.static_track_draw_count()+66==dynamic_definitions.size(),"crowd suffix did not retire at scene end");
    }
    auto bad_definitions=dynamic_definitions;bad_definitions[0]=nullptr;
    require(!validate_prepared_dynamic_prefix(bad_definitions,package.commands,error),"changed dynamic definition accepted");error.clear();
    binding_audit(package.commands);
    std::cout<<"GPU_PACKAGE resources="<<resources<<" cars_and_rival_shadows="<<dynamic
        <<" draws="<<package.commands.draw_count()<<" car_bytes="<<gpu.car_bytes()<<" cache_bytes="<<gpu.cache_bytes()
        <<" persistent="<<gpu.car_bytes()+gpu.cache_bytes()+grid.course.world.memory.resident_bytes<<"\n";
    if(session.timing().outcome==RaceOutcome::finished) {
        FrontendArt art;require(art.load(root,error,false),error);require(art.load_post_race(root,error),error);
        auto values=result_stats.values(session,run.car());
        require(values.at("LapNumber_1")=="1" && values.at("LapNumber_2")=="2","post-race actual two lap bindings");
        require(values.at("RaceFinishedPosition")=="04","post-race actual finish position");
        for(const auto& v:values)std::cout<<"RESULT "<<v.first<<"="<<v.second<<"\n";
        PostRaceMenu menu;menu.update({true,false,true,false});
        std::vector<unsigned char> pixels(1280*720*4,60);for(std::size_t i=3;i<pixels.size();i+=4)pixels[i]=255;
        art.draw_post_race({pixels.data(),1280,720,1280*4},menu,values);
        FrameWriteResult result;auto path=prefix;path+="-results.tga";
        require(write_rgba_tga(1280,720,pixels,path,{},result),result.error);
    }
    std::cout<<"PASS shared RaceRun planner/selector3 and four-car rendering; stock rivals/ordinary strategy/common contact queue\n";
    std::cout<<"SEQUENCE "<<run.sequence_hash()<<"\n";
    if(profiling)std::cout<<"PROP_SEQUENCE "<<sha256_hex(prop_sequence.finish())<<"\n";
    if(profiling)std::cout<<"RIVAL_SEQUENCE "<<sha256_hex(rival_sequence.finish())<<"\n";
    if(sim_samples) {
        const char* names[]={"planner","drivers","rival_physics","player_physics","pair_contacts","timing_camera"};
        for(unsigned i=0;i<6;++i)std::cout<<"SIM_PROFILE "<<names[i]<<" samples="<<sim_samples
            <<" mean_ms="<<sim_sum[i]/sim_samples<<" max_ms="<<sim_max[i]<<"\n";
        const char* children[]={"traffic_motion","prop_motion","prop_contacts"};
        for(unsigned i=0;i<3;++i)std::cout<<"CONTACT_PROFILE "<<children[i]<<" samples="<<sim_samples
            <<" mean_ms="<<contact_sum[i]/sim_samples<<" max_ms="<<contact_max[i]<<"\n";
        std::cout<<"COMMANDS moved "<<command_signature(package.commands)<<"\n";
        RaceFrameProfile sample;std::array<double,7> total{};
        for(unsigned i=0;i<121;++i) {
            require(gpu.frame(session,package,error,&sample,false,&profile_clock),error);
            if(!i)continue;
            const double values[]={sample.cull_ms,sample.list_ms,sample.car_ms,sample.track_ms,sample.combine_ms,
                sample.road_reflections_ms,sample.race_visuals_ms};
            for(unsigned j=0;j<7;++j)total[j]+=values[j];
        }
        std::cout<<"COMMANDS warmed "<<command_signature(package.commands)<<"\n";
        const char* stages[]={"cull","list","car","track","combine","road_reflections_child","visuals_child"};
        for(unsigned i=0;i<7;++i)std::cout<<"RENDER_PROFILE "<<stages[i]<<" stationary_samples=120 mean_ms="<<total[i]/120<<"\n";
    }
    return 0;
} catch(const std::exception& e) {std::cerr<<"FAIL: "<<e.what()<<"\n";return 1;} }
