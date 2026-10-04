#include "nfsu/frontend.h"
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
            check(art.load(root,error),error.c_str());
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
