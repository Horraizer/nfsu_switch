#include "nfsu/frontend.h"
#include "nfsu/frontend_scene.h"
#include "nfsu/stock_frame.h"
#include "nfsu/frontend_motion.h"
#include "nfsu/frontend_preset.h"
#include "nfsu/race_events.h"
#include "nfsu/sha256.h"
#include <cmath>
#include "nfsu/frontend_music.h"
#include <algorithm>
#include "nfsu/race_session.h"
#include "nfsu/race_run.h"
#include "nfsu/race_gpu_scene.h"
#include "nfsu/posix_file_system.h"
#include <iostream>
#include <stdexcept>
void check(bool value,const char* message) { if(!value) throw std::runtime_error(message); }
int main(int argc,char** argv) {
    using namespace nfsu;
    try {
        audio::Bank bank;std::vector<audio::Segment> segments;
        for(unsigned i=0;i<27;++i) {
            bank.entries.push_back({1,i*100,0});
            audio::Segment seg;seg.offset=i*100;seg.frames=i+1;segments.push_back(seg);
        }
        // AST physical order is independent of ABK raw song IDs.
        std::reverse(segments.begin(),segments.end());
        const auto fe=audio::original_music_playlist(bank,segments,true);
        const auto ig=audio::original_music_playlist(bank,segments,false);
        check(fe.size()==7 && fe.front().offset==2000 && fe.back().offset==2600,
            "native FE mask excludes preamble and IG songs");
        check(ig.size()==19 && ig.front().offset==100 && ig.back().offset==1900,
            "native IG mask excludes FE songs");
        check(std::string(audio::original_menu_song(0).title)=="Get Low",
            "fresh-profile song metadata");
        segments.pop_back();bool rejected=false;
        try {audio::original_music_playlist(bank,segments,true);} catch(const std::exception&) {rejected=true;}
        check(rejected,"missing AST bank boundary rejected");
        // Values come from bounded execution of retail 0x4AE1F0 and
        // 0x4AE530, with ready state, 50 km/h and a 1/60-second frame.
        const float native_velocity[]={13.6864938736F,13.6265792847F,14.0879993439F,12.9644823074F};
        const unsigned native_seconds[]={0,1,10,30};
        for(unsigned i=0;i<4;++i) {
            FrontendRoad road;road.offset=199.9F;
            road.update(1.0F/60,native_seconds[i]*4000);
            check(std::abs(road.velocity-native_velocity[i])<0.000002F,"native road speed modulation");
            check(road.offset>=0 && road.offset<1,"native road wraps rather than discontinuously resetting");
        }
        FrontendRoad road;road.offset=200;
        road.update(0,0,true);
        check(road.offset==200,"native road preserves exact wrap boundary");
        std::array<FrontendRoadPlacement,5> placements;
        check(road.placements(1,placements)==5 && placements[0].group==0 &&
            placements[0].longitudinal==200 && placements[1].group==1 &&
            placements[4].longitudinal==-200,"native repeated road placement and parity");
        check(road.placements(4,placements)==3 && placements[0].family==1 &&
            road.placements(11,placements)==3 && placements[0].family==2 &&
            road.placements(0,placements)==0,"native view selects road families and extent");
        road.speed_kmh=10;road.target_kmh=20;
        road.update(0.25F,0,true);
        check(road.speed_kmh==12.5F && std::abs(road.velocity-13.88888931F)<0.000002F,
            "forced road speed preserves native acceleration state");
        FrontendMenu menu;
        check(menu.update({false,false,true,false})==FrontendAction::underground,"initial Underground selection");
        check(menu.update({false,true,false,false})==FrontendAction::none,"navigation has no launch side effect");
        check(menu.update({false,false,true,false})==FrontendAction::free_drive,"free driving selection");
        check(menu.update({true,true,false,false})==FrontendAction::none && menu.selected()==1,"conflicting navigation stable");
        check(menu.update({true,false,false,false})==FrontendAction::none && menu.selected()==0,"selection wraps");
        check(menu.update({false,false,true,true})==FrontendAction::exit,"back wins over accept");
        RaceRun run; std::string error;
        check(run.set_free_drive(true,error),"free-drive configuration before startup");
        if(argc==2) {
            const std::filesystem::path root=argv[1]; FrontendArt art;
            std::vector<unsigned char> front,globals;std::vector<RaceTableIssue> part_issues;
            check(load_race_image(root/"FrontEnd/FrontB.lzc",front,part_issues),"original preset image");
            check(load_race_image(root/"Global/GLOBALB.BUN",globals,part_issues),"original part database image");
            const auto database=parse_car_part_database(globals.data(),globals.size());
            InstalledPartSelection installed;
            check(resolve_frontend_skyline_parts(front,database,installed,error),error.c_str());
            StockParts selected;
            check(select_installed_parts(database,"SKYLINE",installed,selected,error),error.c_str());
            std::vector<unsigned char> native_slots;
            for(const auto& part:installed.records) {
                const auto index=!part || *part==kNoCarPart?0xFFFFFFFFU:static_cast<std::uint32_t>(*part);
                for(unsigned byte=0;byte<4;++byte)native_slots.push_back(static_cast<unsigned char>(index>>(byte*8)));
            }
            for(const auto& slot:selected.slots)native_slots.push_back(slot.enabled?1:0);
            check(sha256_hex(sha256(native_slots.data(),native_slots.size()))==
                "27DE93661A7153D9B74C74198B14C2DD8016E863A5CE641FA93B86B54AF1074F",
                "native FE_SKYLINE 120 assigned slots and installation enable bytes");
            BodySkinSelection skin;
            check(resolve_frontend_skyline_skin(globals,database,installed,skin,error),error.c_str());
            check(skin.base_color==skin_pack_paint(145,63,16,128) &&
                skin.vinyl_layers[3] && skin.vinyl_layers[3]->texture=="EDDIE" &&
                !skin.vinyl_layers[3]->remap,"native FE_SKYLINE paint and Eddie vinyl");
            FrontendSponsorHashes sponsors;
            check(resolve_frontend_sponsors(database,selected,sponsors,error),error.c_str());
            const std::uint32_t native_doors[]={0x8A571203,0xB015F6F2,0xB891CDB4,0x0F4108B2};
            for(unsigned panel=0;panel<7;++panel)for(unsigned decal=0;decal<8;++decal) {
                const auto expected=panel==2 && decal==0?0xB8949CAFU:
                    (panel==3 || panel==4) && decal<4?native_doors[decal]:0x66994F4AU;
                check(sponsors[panel][decal]==expected,"native original 56C3D0 sponsor substitutions");
            }
            CarAssets skyline_assets;
            check(load_car_assets(root/"Global/GLOBALB.BUN","SKYLINE",skyline_assets,error),error.c_str());
            StockFrameInputs skyline_inputs;StockFrame skyline;
            check(read_car_frame_inputs(root,skyline_assets,skyline_inputs,true,error),error.c_str());
            auto skyline_request=stock_car_request("SKYLINE");
            skyline_request.installed_parts=installed;skyline_request.pass=CarDrawPass::front_end;
            auto skyline_skin=stock_car_skin("SKYLINE");skyline_skin.selection=skin;
            check(prepare_stock_frame(skyline_inputs,skyline_request,skyline_skin,skyline,error,true),error.c_str());
            check(bind_frontend_sponsors(skyline_inputs,installed,skyline,error),error.c_str());
            for(unsigned slot:{26U,27U}) {
                unsigned bound=0;
                for(const auto& instance:skyline.scene.instances)if(instance.part_slot==slot)
                    for(const auto& draw:skyline.scene.draws)if(draw.mesh==instance.mesh && draw.drawable)
                        for(auto hash:native_doors)if(draw.texture_hash==hash) {
                            check(draw.binding==BindingKind::resolved &&
                                skyline.scene.textures.at(draw.texture).claims_hash(hash),
                                "widebody door uses native sponsor texture pixels");++bound;
                        }
                check(bound==4,"original widebody door renders four selected sponsors");
            }
            audio::FrontendSfx sfx;
            check(sfx.load(root,error),error.c_str());
            std::vector<std::int16_t> unchanged(1024*2,1234);
            sfx.mix(unchanged.data(),1024);
            check(std::all_of(unchanged.begin(),unchanged.end(),[](auto v){return v==1234;}),
                "idle effects preserve in-place music samples");
            std::vector<std::int16_t> up(24000*2,0),down(up.size(),0),restart(up.size(),0);
            sfx.play(audio::FrontendSound::up);sfx.mix(up.data(),24000);
            sfx.reset();sfx.play(audio::FrontendSound::down);sfx.mix(down.data(),24000);
            check(std::any_of(up.begin(),up.end(),[](auto v){return v!=0;}) &&
                  std::any_of(down.begin(),down.end(),[](auto v){return v!=0;}) && up!=down,
                "original up and down cues are audible and distinct");
            sfx.reset();sfx.play(audio::FrontendSound::up);sfx.mix(restart.data(),24000);
            check(restart==up,"effect reset starts original cue from its first sample");
            sfx.play(audio::FrontendSound::select);sfx.reset();
            std::fill(restart.begin(),restart.end(),0);sfx.mix(restart.data(),24000);
            check(std::all_of(restart.begin(),restart.end(),[](auto v){return v==0;}),
                "effect reset retires queued voices before menu reentry");

            // Exercise retail bank indexing and decoder-thread failure cleanup
            // repeatedly without depending on an attached audio device.
            audio::FrontendMusic music;
            for(unsigned attempt=0;attempt<3;++attempt) {
                check(!music.start(root,error,[]{return false;}),"decoder setup failure must reach caller");
                check(!error.empty(),"decoder setup failure has user-visible reason");
                music.stop();
            }
            if(!art.load(root,error)) throw std::runtime_error(error);
            std::vector<unsigned char> pixels(1280*720*4,0);
            art.draw({pixels.data(),1280,720,1280*4},menu);
            check(pixels[3]==255 && pixels[(719*1280+1279)*4+3]==255,"menu fills full image");
            RaceGpuScene scene;
            check(run.prepare_metadata(root,error),error.c_str());
            check(!run.set_free_drive(false,error),"mode cannot change after metadata preparation");
            check(scene.load(posix_file_system(),root,error),error.c_str());
            check(run.start(root,scene.track(),error),error.c_str());
            const auto& session=run.session();
            check(session.course().route.laps==0,"free-drive configuration reaches actual runtime route");
            RaceRouteDriver driver(RaceDrivingLine::event1_forward);
            unsigned ticks=0;
            for(;ticks<90000 && race_laps_completed(session.progress())<3;++ticks)
                check(run.simulate(driver.sample(session),error) && run.commit(error),error.c_str());
            std::cout << "free-drive observed laps " << race_laps_completed(session.progress()) << " max " << session.progress().max_lap << " steps " << ticks << " resets " << session.telemetry().resets << std::endl;
            check(race_laps_completed(session.progress())>=3,"free driving passes former two-lap finish");
            check(!session.progress().finished && session.timing().outcome==RaceOutcome::running,"free driving still running");
            std::cout << "PASS: authenticated original menu texture load/render; free driving past three laps " << ticks << " ticks\n";
        }
        std::cout << "PASS: menu input launch, wrap, conflict and exit ordering\n";
        return 0;
    } catch(const std::exception& e) { std::cerr << "FAIL: " << e.what() << '\n'; return 1; }
}
