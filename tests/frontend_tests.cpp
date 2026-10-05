#include "nfsu/frontend.h"
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
