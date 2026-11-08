#include "nfsu/custom_vinyl.h"
#include "nfsu/career.h"
#include "nfsu/arg_path.h"
#include "nfsu/career_ride.h"
#include "nfsu/car_neon.h"
#include "nfsu/image.h"
#include "nfsu/career_garage.h"
#include "nfsu/race_result.h"
#include "nfsu/intro_grid.h"
#include "nfsu/race_gpu_scene.h"
#include "nfsu/memory_budget.h"
#include "nfsu/race_hud.h"
#include "nfsu/posix_file_system.h"
#include <cmath>
#include "nfsu/frontend.h"
#include "nfsu/sha256.h"
#include "nfsu/texture.h"
#include "nfsu/frontend_scene.h"
#include "nfsu/draw_execution.h"
#include <chrono>
#include <future>
#include <thread>
#include <algorithm>
#include <cstring>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
using namespace nfsu;
#include "career_tuning_native.inc"
#include "career_tournament_native.inc"
#include "career_time_trial_native.inc"
#include "post_race_outcome_native.inc"
#include "career_unique_native.inc"
#include "career_wheel_fit_native.inc"
#include "career_tuning_owned_native.inc"
#include "career_tuning_reputation_native.inc"
#include "career_neon_native.inc"
#include "career_special_vinyl_native.inc"
#include "frontend_garage_motion_native.inc"
#include "career_garage_live.inc"
#include "garage_paint_isolation.inc"
#include "career_trade_live.inc"
#include "car_shadow_roster_retail.inc"
#include "career_garage_original_tests.inc"
#include "career_info_tests.inc"
#include "free_drive_retail.inc"
#include "custom_vinyl_retail.inc"
#include "bonus_cars_retail.inc"
int main(int argc,char** argv) {
    if(argc<2||argc>4){std::cerr<<"retail root required\n";return 2;}
    const auto base=std::filesystem::temp_directory_path()/("nfsu-career-"+
        std::to_string(std::chrono::high_resolution_clock::now().time_since_epoch().count()));
    const auto check=[&](bool ok,const std::string& name){if(!ok)throw std::runtime_error(name);};
    try {
        if(argc>=3&&std::string(argv[2])=="--bonus-cars"){verify_bonus_cars(argv[1],argc==4?std::filesystem::u8path(argv[3]):std::filesystem::path{});return 0;}
        if(argc>=3&&std::string(argv[2])=="--kr-vinyl"){verify_kr_vinyl(argv[1],argc==4?std::filesystem::u8path(argv[3]):std::filesystem::path{});return 0;}
        if(argc==3&&std::string(argv[2])=="--quick-race"){verify_quick_race(argv[1]);return 0;}
        if(argc>=3&&std::string(argv[2])=="--free-drive"){verify_free_drive(argv[1],argc==4?argv[3]:"");return 0;}
        CareerCatalog catalog;std::string error;if(!catalog.load(argv[1],error))throw std::runtime_error(error);
        if(argc==4&&std::string(argv[2])=="--info") {
            verify_career_info(argv[1],catalog,std::string(argv[3])=="verify"?std::filesystem::path{}:std::filesystem::u8path(argv[3]));return 0;
        }
        if(argc==4&&(std::string(argv[2])=="--magazine"||std::string(argv[2])=="--magazine-tuned")){
            const auto output=std::filesystem::u8path(argv[3]);check(!std::filesystem::exists(output),"fresh magazine capture required");
            CareerProfile photo;photo.car="CIVIC";CareerMagazineRecipe recipe;std::uint32_t seed=0;
            check(catalog.magazines().make(CareerMagazineCatalog::key(14),seed,recipe,error),error.c_str());
            if(std::string(argv[2])=="--magazine-tuned"){
                for(unsigned slot:{1U,2U,8U,10U,17U,30U}){
                    const auto& parts=catalog.tuning().parts().records;
                    const auto at=std::find_if(parts.begin(),parts.end(),[&](const auto& part){return part.slot==slot&&part.upgrade_level==1&&catalog.tuning().find_part(photo.car,slot,part.part_name_hash)==&part;});
                    check(at!=parts.end(),"actual compatible tuned photograph part");photo.ride.installed[slot]=at->part_name_hash;
                }
            }
            std::vector<unsigned char> globals;std::vector<RaceTableIssue> issues;
            check(load_race_image(std::filesystem::path(argv[1])/"Global/GLOBALB.BUN",globals,issues),"magazine car input");
            CareerRideSetup setup;check(resolve_career_ride(globals,catalog,photo,setup,error),error.c_str());
            FrontendGpuScene scene;if(!scene.load_garage(argv[1],1280,720,{setup},error))throw std::runtime_error("magazine scene: "+error);
            std::cout<<"MAGAZINE camera";for(auto value:recipe.camera)std::cout<<' '<<value;std::cout<<" textures "<<recipe.background<<' '<<recipe.front<<' '<<recipe.chick<<'\n';
            scene.magazine_camera(recipe.camera);if(!scene.frame(0,0,error))throw std::runtime_error("magazine frame: "+error);
            std::vector<unsigned char> car;if(!scene.render_reference(car,error))throw std::runtime_error("magazine car rendering: "+error);
            FrontendArt art;check(art.load(argv[1],error,false)&&art.load_magazine(argv[1],recipe,error),error.c_str());
            std::vector<unsigned char> rgba(1280*720*4);ScreenImage image{rgba.data(),1280,720,1280*4};art.draw_magazine(image,recipe,photo.car,true);
            unsigned visible=0;for(std::size_t i=0;i<car.size();i+=4){const auto alpha=car[i+3];visible+=alpha!=0;for(unsigned k=0;k<3;++k)rgba[i+k]=static_cast<unsigned char>((unsigned(car[i+k])*alpha+unsigned(rgba[i+k])*(255-alpha)+127)/255);rgba[i+3]=255;}
            check(visible>1000,"real car visible in magazine camera");art.draw_magazine(image,recipe,photo.car,false);
            //The original black paper-border quad begins at canvas x484.5.
            //This pixel previously leaked a visible grey car silhouette.
            for(unsigned c=0;c<3;++c)check(rgba[(470*1280+1000)*4+c]<=2,"opaque magazine paper mask clips car silhouette");
            std::ofstream file(output,std::ios::binary);check(write_tga(file,1280,720,rgba,error),error.c_str());file.close();check(bool(file),"magazine capture saved");
            std::cout<<"PASS real car magazine capture\n";return 0;
        }
        {
            const auto& tuning=catalog.tuning();check(tuning.ready()&&tuning.parts().ok(),"original tuning catalog resident");
            for(const auto& row:kNativeWheelFits)for(unsigned i=0;i<std::size(kNativeWheelIndices);++i) {
                const auto& part=tuning.parts().records.at(kNativeWheelIndices[i]);
                check(tuning.wheel_fits(row.car,part)==(row.matches[i]=='1'),"original50D779 wheel dimensions on all real racer/part pairs");
            }
            for(unsigned slot=0;slot<120;++slot)
                check(CareerTuningCatalog::slot_category(int(slot))==kNativeTuningSlotCategory[slot],"native5A03D0 slot category");
            for(const auto& witness:kNativeCareerNeon)
                check(car_neon_color(witness.settings,float(witness.ticks)*.00025F,witness.strength)==witness.color,"native56F061 neon color");
            for(const auto& witness:kNativeTuningReputation) {
                InstalledPartSelection parts;
                for(auto index:witness.indices)if(index!=0xffffffffU) {
                    const auto& part=tuning.parts().records.at(index);
                    parts.records[part.slot==58?59:part.slot]=index;
                }
                check(std::abs(tuning.reputation(parts)-witness.value)<.000001F,"original5A05B0 visual reputation");
            }
            check(CareerTuningCatalog::slot_category(-1)==-1&&CareerTuningCatalog::slot_category(120)==-1,"unsupported tuning slot refused");
            for(unsigned field=0;field<11;++field)
                check(CareerTuningCatalog::performance_category(field)==int(kNativeTuningPerformanceCategory[field]),"native5A0350 performance category");
            for(const auto& row:kNativeTuningPrice) {
                CarPartRecord part;part.slot=int(row[0]);part.upgrade_level=std::uint8_t(row[1]);
                check(tuning.part_price(part)==row[2],"native5A08F0 part price");
            }
            for(const auto& row:kNativeTuningUnlock) {
                std::array<std::uint8_t,256> status{};status[0]=kEventStatusWon;
                if(row[2]==1)for(unsigned event=1;event<=5;++event)status[event]=kEventStatusWon;
                if(row[2]==2){const int event=tuning.required_event(row[0],row[1]);if(event>=0)status[unsigned(event)]=kEventStatusWon;}
                check(tuning.category_available(row[0],row[1],status)==bool(row[3]),"native5A1550 category event gate");
            }
            for(const auto& row:kNativeTuningStyle)
                check(tuning.style_available(row[0],row[1],row[2])==bool(row[3]),"native4AA9D0 earned style gate");
            for(const auto& row:kNativeTuningOwned) {
                check(row[0]<tuning.parts().records.size(),"native ownership record present");
                const auto& part=tuning.parts().records[row[0]];
                check(CareerTuningCatalog::slot_category(part.slot)==int(row[1])&&tuning.ownership_key(part)==row[2],"original5A34D0 ownership key");
            }
            for(const auto& row:kNativeSpecialVinyl) {
                const auto* part=tuning.find_part("CIVIC",31,row[2]);
                check(part&&part->byte_12==row[0]&&part->upgrade_level==row[1],"original special vinyl record identity");
                std::array<std::uint8_t,256> status{};
                check(tuning.part_available(*part,status,row[3])==bool(row[4]),"original special vinyl style threshold including level4");
            }
            check(!tuning.style_available(0,0,0xffffffffU)&&!tuning.style_available(2,0,0xffffffffU),"absent vinyl/sponsor is not unlocked");
            check(tuning.style_available(3,0,0),"native unrelated style-kind default");
            std::cout<<"PASS native tuning prices, slot maps, campaign gates and style thresholds\n";
        }
        {
            for(const auto value:std::array<float,8>{{0,.8F,1,1.99F,2,3.8F,4.99F,5}}) {
                const int native=value<1?1:int(value);
                check(post_race_style_multiplier(value)==native,"native49F920 integer visual multiplier");
                PostRaceValues values{{"StyleValue1","123"},{"StyleLabel1","TEST"},{"CareerStyleBase","3402"},{"StyleMultiplier",std::to_string(native)}};
                PostRaceStyleAnimator animator(values);animator.finish();animator.bind(values);
                check(values.at("TotalPointsAnimate")==std::to_string(123*native)&&values.at("CareerStyleDisplayed")==std::to_string(3402+123*native),"final style multiplier and bank agree");
            }
            const auto unlocked=catalog.style_messages(4999,5000);
            check(unlocked.size()==1&&unlocked[0].find("Tear Level 1")!=std::string::npos,"native earned vinyl reward popup");
            check(catalog.style_messages(5000,5000).empty(),"no duplicate style unlock popup");
        }
        VehicleTrackCourse trial_course;
        check(build_vehicle_proving_ground(trial_course,error),"timed reward fixture course");
        for(const auto& native:kNativePostOutcome) {
            RaceSession terminal(trial_course);check(terminal.start(error),"native terminal outcome fixture starts");
            auto& record=const_cast<RaceTimingState&>(terminal.timing()).racers[0];
            record.status=static_cast<RacerStatus>(native.status);record.position=int(native.place);
            record.finish_valid=true;record.finish_ticks=16000;
            check(terminal.player_won()==bool(native.won),"original5A2F10 rejects non-first or failed terminal records, including stale finish time");
            const auto report=post_race_report(terminal,{},{{"Civic","","",""}},native.won?500:0);
            if(native.status==8)check(report.at("Time1")=="Timeup","native terminal Timeup text wins over stale elapsed clock");
        }
        trial_course.route.point_to_point=true;
        for(const auto& witness:kNativeTimeTrial) {
            RaceSession trial(trial_course);check(trial.set_time_trial(witness.limit,error)&&trial.start(error),"career time trial starts");
            auto& timing=const_cast<RaceTimingState&>(trial.timing());
            auto& record=timing.racers[0];record.status=RacerStatus::normal_completion;
            record.position=1;record.finish_valid=true;record.finish_ticks=witness.elapsed;
            record.crossing_ticks[0]=4000;record.crossing_valid[0]=true;
            record.lap_shown_valid=true;record.lap_shown_ticks=1234;record.lap_shown_since=4000;
            timing.step_ticks=4000+witness.elapsed;
            check(trial.player_won()==witness.success&&trial.player_position()==1,"original5A3098 reward predicate never overwrites the solo race place");
            RaceResultStatistics sprint_stats;
            const auto sprint=sprint_stats.values(trial,"CIVIC");
            check(sprint.at("DataName_3")=="TOP SPEED:"&&sprint.at("DataName_7").empty(),"native49CEE0 point-to-point omits lap statistics");
            const auto post=post_race_report(trial,{},{{"Civic","","",""}},0);
            check(post.at("Pos1")=="1"&&post.at("Pos2").empty()&&post.at("Pos3").empty()&&post.at("Pos4").empty(),"solo results do not invent three empty opponents");
            RaceLapHudPanel panel;
            build_race_lap_hud_panel(race_lap_hud_snapshot(trial.course().route,timing,0,true,trial.time_limit_ticks()),RaceLapHudStyle::native5,panel);
            check(panel.current_valid&&!panel.current_held&&panel.fields[0].text.data()==std::string(witness.text),"native4A1932 countdown overrides completed-lap hold");
        }
        for(const auto& witness:kNativeTournament) {
            CareerTournament tournament;tournament.event=6;tournament.round=witness.count;
            for(unsigned r=0;r<kRaceEventRaceSlots;++r)for(unsigned actor=0;actor<4;++actor)tournament.places[r][actor]=witness.places[r][actor];
            const auto totals=tournament.totals(),order=tournament.order();
            for(unsigned actor=0;actor<4;++actor)check(totals[actor]==witness.totals[actor]&&order[actor]==witness.order[actor],"original5A29C0/5A2AB0 tournament scoring and stable ranking");
        }
        for(const auto& tournament:kSupportedTournamentRows) {
            for(unsigned round=0;round<tournament.count;++round) {
                SupportedRace race;const auto& expected=tournament.rounds[round];
                if(!load_supported_race(std::filesystem::path(argv[1])/"FrontEnd/FrontB.lzc",std::filesystem::path(argv[1])/"Global/GLOBALB.BUN",tournament.event,race,error,round))throw std::runtime_error("tournament "+std::to_string(tournament.event)+": "+error);
                check(race.event==tournament.event&&race.race_index==round&&race.race_count==tournament.count&&race.type==unsigned(tournament.type),"tournament round preserves real parent event and mode");
                check(race.slot.track==expected.track&&race.slot.direction==expected.direction&&race.slot.laps==expected.laps,"tournament round is its own authored track/direction/lap selection");
            }
            SupportedRace refused;
            check(!load_supported_race(std::filesystem::path(argv[1])/"FrontEnd/FrontB.lzc",std::filesystem::path(argv[1])/"Global/GLOBALB.BUN",tournament.event,refused,error,tournament.count),"past-last tournament round refused");
        }
        check(catalog.media(1)->unlock_messages.empty() && catalog.media(2)->unlock_messages.empty() &&
            catalog.media(3)->unlock_messages.size()==1 && catalog.media(4)->unlock_messages.size()==2 &&
            catalog.media(5)->unlock_messages.size()==1,"original first-five reward popup counts");
        check(catalog.media(4)->unlock_messages[0].find("2003 Mitsubishi Lancer ES")!=std::string::npos &&
            catalog.media(4)->unlock_messages[1].find("14th & Vine")!=std::string::npos,"original drag car and reverse-track reward language");
        // Original executable581D30 initialized bounds,581B30 descriptor,
        //5802C0/5801B0/580400 at007F; tools/career_performance_vectors.py.
        const std::array<std::array<std::uint32_t,3>,5> ratings{{
            {{1022739087,1026234908,1047423395}},{{1032751407,1040909514,1053848460}},
            {{1032230392,1043011964,1049061465}},{{1032404653,1037426691,1049447319}},
            {{1028168434,1040909514,1051002774}}}};
        unsigned car_index=0;
        for(const auto& c:catalog.starters()) {
            std::cout<<"RATING "<<c.name;
            unsigned metric=0;
            for(float f:c.performance){std::uint32_t bits;std::memcpy(&bits,&f,4);std::cout<<' '<<bits;check(bits==ratings.at(car_index).at(metric++),"original stock UI rating");}
            ++car_index;
            std::cout<<'\n';
        }
        {
            CareerProfile installed;installed.car="CIVIC";
            std::array<float,3> stock{},upgraded{};
            check(catalog.performance_ratings(installed,stock),"installed stock rating resolution");
            installed.ride.performance.fill(3);
            check(catalog.performance_ratings(installed,upgraded),"installed tuned rating resolution");
            for(unsigned k=0;k<3;++k)check(upgraded[k]>stock[k],"installed upgrades increase garage rating");
            std::vector<unsigned char> globals;std::vector<RaceTableIssue> issues;
            check(load_race_image(std::filesystem::path(argv[1])/"Global/GLOBALB.BUN",globals,issues),"rating globals");
            CareerRideSetup ride;check(resolve_career_ride(globals,catalog,installed,ride,error),error.c_str());
            const auto levels=career_performance_levels(catalog,installed);
            check(levels.engine_a==ride.performance.engine_a && levels.nitrous==ride.performance.nitrous,
                "garage and race share installed performance resolution");
        }
        for(const auto& row:kNativeUniqueEligibility) {
            std::array<std::uint8_t,256> status{};
            for(unsigned event=0;event<=unsigned(row[0]);++event)status[event]=kEventStatusWon;
            check(catalog.tuning().category_available(unsigned(row[1]),4,status)==bool(row[3]),
                  "original5A1550 unique category availability");
        }
        for(const auto& row:kNativeUniqueMapping) {
            check(CareerTuningCatalog::performance_category(unsigned(row[1]))==row[0],
                  "original4C7360 unique category-to-installed-flag mapping");
        }
        for(const auto& row:kNativeUniqueSpeech) {
            const auto* media=catalog.media(unsigned(row[0]));
            if(media)check(media->reward_speech==((!media->unique_categories.empty()&&media->unique_categories.front()<16)?row[2]:(row[1]>=0?row[1]:row[2])),
                           "original reward voice differs from prerace briefing");
        }
        if(argc==4&&std::string(argv[3])=="unique-pages") {
            const auto out=argument_path(argv[2]);check(!out.empty()&&!std::filesystem::exists(out),"fresh unique page output");
            std::filesystem::create_directories(out);
            CareerProfile p;p.car="CIVIC";p.bank=100000;p.customization_seen=true;p.style_high_water=5000;
            for(const auto& event:catalog.events().records)if(event.number<=kCareerLastEvent)p.status[event.number]=kEventStatusWon;
            FrontendArt art;check(art.load(argv[1],error,false),error.c_str());
            if(!art.load_career_pages(argv[1],catalog,error))throw std::runtime_error("career pages: "+error);
            const auto capture=[&](const CareerGarage& chooser,const std::string& name) {
                FrontendUiFrame ui;art.record_career(1280,720,catalog,p,0,"",ui,&chooser);
                check(!ui.vertices.empty()&&ui.vertices.size()<=6*kFrontendUiMaxQuads,"unique UI bounded GPU recording");
                std::vector<unsigned char> pixels(1280*720*4,0);
                art.draw_career({pixels.data(),1280,720,1280*4},catalog,p,0,"",&chooser);
                std::ofstream image(out/(name+".ppm"),std::ios::binary);image<<"P6\n1280 720\n255\n";
                for(std::size_t i=0;i<pixels.size();i+=4)image.write(reinterpret_cast<const char*>(pixels.data()+i),3);
                check(bool(image),"unique UI diagnostic write");
                std::cout<<"UNIQUE_PAGE "<<name<<" quads="<<ui.vertices.size()/6<<'\n';
            };
            for(unsigned event:{7U,15U,26U,47U,68U,77U,97U,111U}) {
                p.page=CareerPage::unique_reward;p.pending_unique=event;p.selected_event=event;
                check(catalog.valid(p,error),error.c_str());
                CareerGarage chooser;chooser.reset_unique(catalog,p);
                check(!chooser.entries().empty(),"original unique options nonempty");
                check(chooser.fng()==(catalog.media(event)->unique_categories.front()>=16?0xbcf70cU:0xbd0d0cU),"dedicated original unique FNG selected");
                capture(chooser,"reward-"+std::to_string(event));
                bool bought=false;const auto bank=p.bank;
                check(chooser.unique_intro(),"original unique explanation starts visible");
                chooser.move(1);check(chooser.selected()==0,"original unique scroll waits for explanation dismissal");
                check(chooser.accept(catalog,p,bought,error)&&!bought&&!chooser.confirming()&&!chooser.unique_intro(),
                      "original first accept dismisses explanation without granting or confirming");
                capture(chooser,"choose-"+std::to_string(event));
                check(chooser.accept(catalog,p,bought,error)&&!bought&&chooser.confirming(),"unique confirmation leaves award unclaimed");
                capture(chooser,"confirm-"+std::to_string(event));
                check(!chooser.back(catalog,p)&&!chooser.confirming()&&p.pending_unique==event,"cancel preserves pending unique award");
                const auto chosen=chooser.entries().front().part;
                check(chooser.accept(catalog,p,bought,error)&&chooser.accept(catalog,p,bought,error)&&bought,
                      "confirmed unique applies once");
                check(p.bank==bank&&p.pending_unique==0&&catalog.valid(p,error),"native unique transaction preserves cash and consumes award");
                if(event==7)check(p.ride.unique[2],"native chosen engine flag");
                else if(event==26)check(p.ride.unique[5],"native chosen ECU flag");
                else if(event==15) {
                    const auto native=std::find_if(std::begin(kNativeUniqueHood),std::end(kNativeUniqueHood),
                        [&](const auto& row){return unsigned(row[0])==chosen;});
                    check(native!=std::end(kNativeUniqueHood),"actual Civic hood exercised by native4C8720");
                    check(p.ride.installed[8]==chosen&&catalog.owns(p,12,unsigned((*native)[1])),"native chosen hood installed and owned key granted");
                }
            }
            // Original constructor omits an already installed unique category.
            p.page=CareerPage::unique_reward;p.pending_unique=7;p.selected_event=7;
            CareerGarage remaining;remaining.reset_unique(catalog,p);
            check(remaining.entries().size()==2&&remaining.entries().front().value==19,"already installed unique is not offered twice");
            check(!catalog.award_unique(p,18,0,error)&&p.pending_unique==7,"duplicate unique cannot consume pending award");
            std::cout<<"PASS dedicated unique reward pages, confirmation, ownership and native reward voices\n";
            return 0;
        }
        if(argc==4&&std::string(argv[3])=="trade-live") {
            verify_trade_garage(argv[1],argv[2],catalog);return 0;
        }
        if(argc==4&&std::string(argv[3])=="shadow-roster") {
            verify_shadow_roster(argv[1]);return 0;
        }
        if(argc==4&&std::string(argv[3])=="paint-isolation") {verify_paint_isolation(argv[1],catalog,base);return 0;}
        if(argc==4&&std::string(argv[3])=="garage-live") {
            verify_live_garage(argv[1],argv[2],catalog);return 0;
        }
        if(argc==4&&std::string(argv[3])=="garage-pages") {
            const auto out=std::filesystem::path(argv[2]);check(!std::filesystem::exists(out),"fresh garage page output");std::filesystem::create_directories(out);
            CareerProfile p;p.page=CareerPage::garage;p.car="CIVIC";p.bank=100000;p.customization_seen=true;p.style_high_water=5000;
            FrontendArt art;check(art.load(argv[1],error,false),error.c_str());if(!art.load_career_pages(argv[1],catalog,error))throw std::runtime_error("career pages: "+error);
            const auto capture=[&](CareerGarage& menu,const std::string& name) {
                check(!menu.entries().empty(),("nonempty garage leaf "+name).c_str());
                FrontendUiFrame ui;art.record_career(1280,720,catalog,p,0,"",ui,&menu);
                check(!ui.vertices.empty()&&ui.additive.size()<=kFrontendUiMaxQuads,"garage leaf GPU bounds");
                std::vector<unsigned char> pixels(1280*720*4,0);art.draw_career({pixels.data(),1280,720,1280*4},catalog,p,0,"",&menu);
                std::ofstream image(out/(name+".ppm"),std::ios::binary);image<<"P6\n1280 720\n255\n";
                for(std::size_t i=0;i<pixels.size();i+=4)image.write(reinterpret_cast<const char*>(pixels.data()+i),3);
                check(bool(image),"garage leaf diagnostic write");
                std::cout<<"PAGE "<<name<<" entries="<<menu.entries().size()<<" selected="<<menu.entries()[menu.selected()].name<<" icon="<<menu.entries()[menu.selected()].icon<<" focus="<<menu.focus_slot()<<"\n";
            };
            const auto enter=[&](CareerGarage& menu) {
                bool bought=false;if(!menu.accept(catalog,p,bought,error))throw std::runtime_error("garage page: "+error);
                check(!bought,"page audit does not purchase");
            };
            check(career_decal_brand_icon(bin_hash("5ZIGEN"))==0xF75128B1U&&
                  career_decal_brand_icon(bin_hash("5Zigen"))==0xF75128B1U&&
                  career_decal_brand_icon(0)==0,"original sponsor aliases and unknown refusal");
            CareerGarage root;root.reset(catalog,p);capture(root,"root");
            auto visual=root;enter(visual);capture(visual,"visual");
            for(unsigned category=0;category<6;++category) {
                auto menu=visual;menu.move(int(category));enter(menu);capture(menu,"category-"+std::to_string(category));
                if(category==0||category==2)for(unsigned part=0;part<menu.entries().size();++part) {
                    auto leaf=menu;leaf.move(int(part));enter(leaf);capture(leaf,"part-"+std::to_string(leaf.slot()));
                    if(leaf.slot()==1)check(leaf.entries()[0].icon==0x739c4a1bU&&leaf.focus_slot()==1,"native front bumper icon and focus");
                }
                if(category==1) {
                    enter(menu);capture(menu,"rim-styles");check(menu.previewing()&&menu.slot()==17,"rims preview at style selection");
                    check(menu.preview(p).ride.installed[17]!=0,"rim style resolves an actual wheel part");enter(menu);capture(menu,"rim-sizes");
                }
                if(category==3) {enter(menu);capture(menu,"base-palette");}
                if(category==4) {
                    auto hood=menu;hood.move(6);enter(hood);capture(hood,"decal-sizes");
                    auto door=menu;door.move(2);enter(door);capture(door,"decal-slots");
                    enter(door);door.move(1);capture(door,"decal-brands");
                    door.toggle_decal_color(catalog,p);capture(door,"decal-white");
                    enter(menu);capture(menu,"window-brands");
                }
                if(category==5) {enter(menu);capture(menu,"vinyl-groups");enter(menu);capture(menu,"vinyl-types");enter(menu);capture(menu,"vinyl-designs");}
            }
            auto performance=root;performance.move(1);enter(performance);capture(performance,"performance");enter(performance);capture(performance,"packages");
            std::cout<<"GARAGE_ATLAS width="<<art.atlas().width<<" height="<<art.atlas().height<<" bytes="<<art.atlas().rgba.size()<<"\n";
            std::cout<<"PASS garage leaf navigation and authored UI recording\n";return 0;
        }
        if(argc==4 && std::string(argv[3])=="garage") {
            const auto out=std::filesystem::path(argv[2]);check(!std::filesystem::exists(out),"fresh garage output required");std::filesystem::create_directories(out);
            CareerProfile profile;check(catalog.intro_won(profile,error),"garage intro");check(catalog.movie_finished(profile,error),"garage choose car");
            const auto civic=std::find_if(catalog.starters().begin(),catalog.starters().end(),[](const auto& c){return c.name=="CIVIC";});
            check(civic!=catalog.starters().end(),"garage Civic starter");check(catalog.buy(profile,std::size_t(civic-catalog.starters().begin()),error),"garage car purchase");
            check(catalog.movie_finished(profile,error),"garage intro movie");profile.page=CareerPage::garage;profile.customization_seen=true;profile.bank=100000;
            std::vector<unsigned char> global;std::vector<RaceTableIssue> issues;
            check(load_race_image(std::filesystem::path(argv[1])/"Global/GLOBALB.BUN",global,issues),"garage read-only GLOBALB");
            CareerRideSetup stock;check(resolve_career_ride(global,catalog,profile,stock,error),error.c_str());check(stock.reputation==0,"stock original visual reputation");
            CareerGarage menu;menu.reset(catalog,profile);bool purchased=false;
            check(menu.entries().size()==3&&menu.entries()[0].name=="Visual","original career root categories");
            check(menu.accept(catalog,profile,purchased,error)&&!purchased,"enter visual");
            check(menu.accept(catalog,profile,purchased,error),"enter body");
            check(menu.entries().size()==7&&menu.entries()[0].value==1&&menu.entries()[2].value==10,"native body order and slots");
            check(menu.accept(catalog,profile,purchased,error),"enter front bumper");
            const auto stock_profile=profile;menu.move(1);const auto preview=menu.preview(profile);
            check(profile.ride.installed==stock_profile.ride.installed&&preview.ride.installed[1]!=0,"preview does not mutate profile");
            CareerRideSetup bumper;check(resolve_career_ride(global,catalog,preview,bumper,error),error.c_str());
            const auto before=profile.bank;check(menu.accept(catalog,profile,purchased,error)&&!purchased&&menu.confirming(),"purchase confirmation");
            std::cout<<"GARAGE transaction part="<<menu.entries()[menu.selected()].part<<" price="<<menu.entries()[menu.selected()].price<<" available="<<menu.entries()[menu.selected()].available<<" owned="<<menu.entries()[menu.selected()].owned<<"\n";
            if(!menu.accept(catalog,profile,purchased,error))throw std::runtime_error("garage transaction: "+error);check(purchased&&profile.bank<before,"confirmed original-price purchase");
            check(!menu.back(catalog,profile)&&menu.page()==GaragePage::body,"back restores body category");
            check(menu.preview(profile).ride.installed==profile.ride.installed,"leaving browser preserves purchased selection");
            CareerRideSetup installed;check(resolve_career_ride(global,catalog,profile,installed,error),error.c_str());
            auto lit=profile;
            const auto neon_part=std::find_if(catalog.tuning().parts().records.begin(),catalog.tuning().parts().records.end(),[](const auto& part){return part.slot==116&&part.upgrade_level==1;});
            check(neon_part!=catalog.tuning().parts().records.end(),"native initial neon part");
            if(!catalog.buy_part(lit,116,neon_part->part_name_hash,error))throw std::runtime_error("neon purchase: "+error);
            CareerRideSetup neon;if(!resolve_career_ride(global,catalog,lit,neon,error))throw std::runtime_error(error);
            const auto part_for=[&](unsigned slot,const auto& predicate)->const CarPartRecord& {
                for(const auto& part:catalog.tuning().parts().records)
                    if(catalog.tuning().find_part(profile.car,slot,part.part_name_hash)==&part&&predicate(part))return part;
                throw std::runtime_error("garage category witness absent slot="+std::to_string(slot));
            };
            const auto purchase=[&](CareerProfile& ride,unsigned slot,const CarPartRecord& part) {
                if(!catalog.buy_part(ride,slot,part.part_name_hash,error))throw std::runtime_error("garage category purchase "+std::to_string(slot)+": "+error);
            };
            auto painted=profile;purchase(painted,30,part_for(30,[](const auto& p){return p.upgrade_level==1&&p.hash_0c==0x02daab07U;}));
            auto wheeled=painted;purchase(wheeled,17,part_for(17,[](const auto& p){return p.upgrade_level==1;}));
            purchase(wheeled,41,part_for(41,[](const auto& p){return p.upgrade_level==1;}));
            auto invalid_paint=profile;const auto no_paint_bank=invalid_paint.bank;
            check(!catalog.buy_part(invalid_paint,41,wheeled.ride.installed[41],error)&&invalid_paint.bank==no_paint_bank,"stock wheel paint refused without charge");
            check(!catalog.tuning().find_part(profile.car,41,painted.ride.installed[30]),"base palette cannot enter wheel color slot");
            auto sticker=painted;sticker.style_high_water=5000;
            purchase(sticker,31,part_for(31,[](const auto& p){return p.byte_12==0&&p.upgrade_level==1;}));
            auto decaled=painted;
            const auto rectangle=bin_hash(profile.car+"_DECAL_FRONT_WINDOW_WIDE_MEDIUM");
            if(!catalog.buy_part(decaled,20,rectangle,error))throw std::runtime_error("garage decal rectangle: "+error);
            const auto name_key=bin_hash("NAME"),alpine=bin_hash("ALPINE");
            purchase(decaled,67,part_for(67,[&](const auto& p){
                for(int i=0;i<p.attribute_count;++i){const auto& a=catalog.tuning().parts().attributes[p.attribute_index+unsigned(i)];if(a.key==name_key&&a.value==alpine)return true;}return false;
            }));
            auto combined=decaled;combined.style_high_water=5000;
            purchase(combined,31,part_for(31,[](const auto& p){return p.byte_12==0&&p.upgrade_level==1;}));
            purchase(combined,17,part_for(17,[](const auto& p){return p.upgrade_level==1;}));
            purchase(combined,41,part_for(41,[](const auto& p){return p.upgrade_level==1;}));
            purchase(combined,116,*neon_part);
            CareerRideSetup raced;
            if(!resolve_career_ride(global,catalog,combined,raced,error))throw std::runtime_error("owned race selection: "+error);
            {
                RaceCarDress dress;dress.request=&raced.request;dress.skin=&raced.skin;
                dress.bind=[](StockFrame& frame,const StockFrameInputs& inputs,const StockSceneRequest& request,std::string& e,void*) {
                    return bind_frontend_runtime_skin(frame,request,e)&&bind_frontend_sponsors(inputs,request.installed_parts,frame,e);
                };
                IntroGrid grid;if(!load_race_grid(argv[1],1,profile.car,0,grid,error,&raced.performance,&dress))throw std::runtime_error("owned race grid: "+error);
                const auto human=std::find_if(grid.actors.begin(),grid.actors.end(),[](const auto& a){return a.config.role==Event0Role::human;});
                check(human!=grid.actors.end(),"owned human grid actor present");
                for(unsigned slot:{1U,17U,20U,30U,31U,41U,67U,116U})
                    check(human->frame->report.slots[slot].record==*raced.request.installed_parts.records[slot],"race installs the garage selection");
                RaceSession session(grid.course,human->placement_spec,RaceSteering::baseline,human->hull);
                if(!session.start(error,&human->pose))throw std::runtime_error("owned race start: "+error);
                RaceGpuScene gpu;gpu.set_race_visuals(true);
                if(!gpu.load(posix_file_system(),argv[1],error,nullptr,nullptr,false,true,true,&human->assets,&dress,1,true))throw std::runtime_error("owned race GPU load: "+error);
                RaceGpuFrame cached,reference;
                if(!gpu.frame(session,cached,error))throw std::runtime_error("owned race GPU cached frame: "+error);
                check(validate_recorded_commands(cached.commands,error),"owned cached commands validate");
                unsigned skid_batches=0;for(std::size_t i=0;i<cached.commands.draw_count();++i) {
                    const auto& d=cached.commands.draw_view(i).definition;
                    if(d.name.find("skid/material/")!=std::string::npos) {
                        ++skid_batches;check(d.material.fallback[3]==9&&
                            cached.commands.resource_table().resources.at(d.vertex_resource).streamed_vertices,
                            "real race binds fenced original skid batches");
                    }
                }
                check(skid_batches==3,"real race cached frame retains every skid material");

                if(!gpu.frame(session,reference,error,nullptr,true))throw std::runtime_error("owned race GPU reference frame: "+error);
                check(validate_recorded_commands(reference.commands,error),"owned reference commands validate");
                check(cached.commands.draw_count()==reference.commands.draw_count()&&cached.triangles==reference.triangles,"owned cached/reference draw and triangle counts agree");
                unsigned ground=0;
                for(std::size_t i=0;i<cached.commands.draw_count();++i)if(cached.commands.draw_view(i).definition.name.find("car underbody neon")!=std::string::npos)++ground;
                check(ground==1,"owned race submits one neon draw");
                std::cout<<"PASS owned race grid, combined body/rim/paint/vinyl/decal/neon loading and cached/reference frame; draws="<<cached.commands.draw_count()<<" car_bytes="<<gpu.car_bytes()<<"\n";
            }
            std::vector<CareerRideSetup> variants{stock,bumper,installed,neon};
            for(const auto* ride:{&painted,&wheeled,&sticker,&decaled}) {
                CareerRideSetup resolved;if(!resolve_career_ride(global,catalog,*ride,resolved,error))throw std::runtime_error("garage category resolve: "+error);
                variants.push_back(std::move(resolved));
            }
            variants.push_back(raced);
            FrontendGpuScene scene;const auto began=std::chrono::steady_clock::now();
            if(!scene.load_garage(argv[1],1280,720,variants,error))throw std::runtime_error("garage bank: "+error);
            const auto original=scene.commands().resource_table();
            for(unsigned selection=0;selection<variants.size();++selection) {
                check(scene.select_part(selection,error)&&!scene.car_transitioning(),"resident part preview has no car departure");
                check(scene.frame(.016F,selection*19,error),error.c_str());
                check(validate_draw_update(original,scene.commands(),scene.draw_capacity(),error),error.c_str());
            }
            check(scene.select_part(3,error)&&scene.frame(.016F,1234,error),"select installed neon for visual check");
            check(std::any_of(scene.commands().owning_draws().begin(),scene.commands().owning_draws().end(),[](const auto& d){return d.name.find("car underbody neon")!=std::string::npos&&d.material.alpha[0]>0;}),"installed neon submits genuine effect");
            std::vector<unsigned char> scene_pixels;
            if(!scene.render_reference(scene_pixels,error))throw std::runtime_error("garage CPU scene: "+error);
            std::ofstream preview_image(out/"garage-neon.ppm",std::ios::binary);preview_image<<"P6\n1280 720\n255\n";
            for(std::size_t i=0;i<scene_pixels.size();i+=4)preview_image.write(reinterpret_cast<const char*>(scene_pixels.data()+i),3);
            FrontendArt art;if(!art.load(argv[1],error,false))throw std::runtime_error("garage frontend: "+error);if(!art.load_career_pages(argv[1],catalog,error))throw std::runtime_error("garage artwork: "+error);
            FrontendUiFrame ui;art.record_career(1280,720,catalog,profile,0,"",ui,&menu);check(!ui.vertices.empty(),"original garage UI submits");
            std::vector<unsigned char> pixels(1280*720*4,0);art.draw_career({pixels.data(),1280,720,1280*4},catalog,profile,0,"",&menu);
            std::ofstream image(out/"garage-body.ppm",std::ios::binary);image<<"P6\n1280 720\n255\n";
            for(std::size_t i=0;i<pixels.size();i+=4)image.write(reinterpret_cast<const char*>(pixels.data()+i),3);
            check(bool(image),"garage UI diagnostic written");
            check(scene.select_part(variants.size()-1,error)&&scene.frame(.016F,1250,error),"combined garage selection");
            if(!scene.render_reference(scene_pixels,error))throw std::runtime_error("combined garage visual: "+error);
            art.draw_career({scene_pixels.data(),1280,720,1280*4},catalog,combined,0,"",&menu);
            std::ofstream combined_image(out/"garage-combined.ppm",std::ios::binary);combined_image<<"P6\n1280 720\n255\n";
            for(std::size_t i=0;i<scene_pixels.size();i+=4)combined_image.write(reinterpret_cast<const char*>(scene_pixels.data()+i),3);

            for(unsigned slot:{1U,17U}) {
                scene.focus_part(slot);
                const std::array<float,3> before_focus{{scene.commands().frame.eye[0],scene.commands().frame.eye[1],scene.commands().frame.eye[2]}};
                for(unsigned i=0;i<180;++i) {
                    scene.orbit(0.F,0.F); // The actual neutral-stick call must not cancel native focus.
                    check(scene.frame(1.F/60,1300+i*67,error),error.c_str());
                }
                const auto* after_focus=scene.commands().frame.eye;
                check(std::abs(after_focus[0]-before_focus[0])+std::abs(after_focus[1]-before_focus[1])+
                    std::abs(after_focus[2]-before_focus[2])>.1F,"neutral stick preserves smooth category camera movement");
                if(slot==1)check(std::any_of(scene.commands().owning_draws().begin(),scene.commands().owning_draws().end(),
                    [](const auto& d){return (d.frontend_sprite_kind==1||d.frontend_sprite_kind==2)&&
                        d.material.alpha[0]>0&&d.material.alpha[3]==8;}),"front focus retains native RGBA2x headlight sprite");
                check(scene.render_reference(scene_pixels,error),error.c_str());
                std::ofstream image(out/(slot==1?"garage-front.ppm":"garage-wheel.ppm"),std::ios::binary);image<<"P6\n1280 720\n255\n";
                for(std::size_t i=0;i<scene_pixels.size();i+=4)image.write(reinterpret_cast<const char*>(scene_pixels.data()+i),3);
                check(bool(image),"focused actual car diagnostic written");
            }
            {
                // Production now publishes a worker-prepared bank; synchronous
                // load/select checks alone do not exercise that ownership path.
                FrontendGpuScene prepared;
                std::string background_error;
                const std::vector<CareerRideSetup> next_variants{stock,bumper,variants[5]};
                const auto room=scene.environment();
                auto ready=std::async(std::launch::async,[&] {
                    return prepared.load_garage(argv[1],1280,720,next_variants,background_error,nullptr,room);
                });
                unsigned live_frames=0;
                while(ready.wait_for(std::chrono::milliseconds(0))!=std::future_status::ready) {
                    check(scene.frame(1.F/60,25000+live_frames*67,error),error.c_str());
                    ++live_frames;std::this_thread::sleep_for(std::chrono::milliseconds(1));
                }
                check(ready.get(),background_error.c_str());
                check(prepared.environment()==room,"background preview reuses the immutable room instead of rereading it");
                const auto appearance=[](const RecordedCommands& commands) {
                    std::ostringstream shape;
                    for(std::size_t i=0;i<commands.draw_count();++i) {
                        const auto& draw=commands.draw_view(i).definition;
                        if(std::strcmp(draw.program->name,"frontend_car_textured"))continue;
                        shape<<draw.name<<':'<<draw.index_count<<';';
                        for(auto resource:{draw.vertex_resource,draw.index_resource,draw.texture_resource}) {
                            if(resource==kNoHandle)continue;
                            const auto& payload=commands.resource_table().resources.at(resource);
                            shape<<sha256_hex(sha256(payload.data,std::size_t(payload.bytes)))<<';';
                        }
                    }
                    return shape.str();
                };
                std::array<std::string,3> expected;
                for(unsigned i=0;i<expected.size();++i) {
                    check(prepared.select_part(i,error)&&prepared.frame(0,0,error),error.c_str());
                    expected[i]=appearance(prepared.commands());
                }
                check(!expected[0].empty()&&expected[0]!=expected[1]&&expected[0]!=expected[2],
                    "actual stock, bumper and rim payloads differ before publication");
                const auto* borrowed=&scene.commands();
                check(!scene.adopt_garage(prepared,error,3)&&&scene.commands()==borrowed,
                    "invalid selection refuses publication without retiring visible scene");
                check(scene.adopt_garage(prepared,error,2),error.c_str());
                check(&prepared.commands()==borrowed,"background publication retains previous GPU command owner");
                check(appearance(scene.commands())==expected[2],"highlighted rims are present at the upload boundary, not stock");
                const auto resident=scene.commands().resource_table();
                for(unsigned i:{0U,1U,2U,1U,0U,2U}) {
                    check(scene.select_part(i,error)&&scene.frame(0,0,error),error.c_str());
                    check(validate_draw_update(resident,scene.commands(),scene.draw_capacity(),error),error.c_str());
                    check(appearance(scene.commands())==expected[i],"resident geometry and skin follow each selected part");
                    if(i==0||i==2) {
                        check(scene.render_reference(scene_pixels,error),error.c_str());
                        const auto name=i==0?"garage-preview-stock.ppm":"garage-preview-rims.ppm";
                        // Repeated selections intentionally reuse this run's output only.
                        std::ofstream image(out/name,std::ios::binary);image<<"P6\n1280 720\n255\n";
                        for(std::size_t at=0;at<scene_pixels.size();at+=4)image.write(reinterpret_cast<const char*>(scene_pixels.data()+at),3);
                        check(bool(image),"background-selected preview frame written");
                    }
                }
                std::cout<<"PASS asynchronous garage publication and changed stock/bumper/rims payloads; live_frames="<<live_frames<<"\n";
            }
            {
                const auto* borrowed=&scene.commands();
                FrontendGpuScene previous;
                check(scene.load_garage(argv[1],1280,720,{variants.front()},error,&previous),error.c_str());
                check(&previous.commands()==borrowed,"retired scene preserves borrowed command identity until GPU replacement");
                check(validate_recorded_commands(previous.commands(),error)&&validate_recorded_commands(scene.commands(),error),
                      "old and new scene resources remain valid across retained replacement boundary");
            }
            menu.reset(catalog,profile);check(menu.accept(catalog,profile,purchased,error),"palette enter visual");menu.move(3);
            check(menu.accept(catalog,profile,purchased,error)&&menu.page()==GaragePage::paint,"palette category");
            check(menu.accept(catalog,profile,purchased,error)&&menu.page()==GaragePage::colors&&!menu.entries().empty(),"native body palette");
            for(const auto& entry:menu.entries())check(catalog.tuning().find_part(profile.car,30,entry.part)->hash_0c==0x02daab07U,"gloss picker only contains gloss colors");
            pixels.assign(1280*720*4,0);art.draw_career({pixels.data(),1280,720,1280*4},catalog,profile,0,"",&menu);
            art.record_career(1280,720,catalog,profile,0,"",ui,&menu);check(!ui.vertices.empty()&&ui.additive.size()<=kFrontendUiMaxQuads,"color picker GPU bounds");
            std::ofstream color_image(out/"garage-colors.ppm",std::ios::binary);color_image<<"P6\n1280 720\n255\n";
            for(std::size_t i=0;i<pixels.size();i+=4)color_image.write(reinterpret_cast<const char*>(pixels.data()+i),3);
            menu.change_finish(1,catalog,profile);check(menu.color_family()==0x02daab07U&&!menu.entries().empty(),"career hides locked metallic finish");
            scene.orbit(90,-10);check(scene.frame(.016F,1300,error)&&validate_recorded_commands(scene.commands(),error),"orbit keeps resident resources");

            std::cout<<"PASS garage original navigation, transaction, shared ride, resident preview/resources and authored UI; resources="<<original.resources.size()
                <<" bytes="<<original.totals.vertex_bytes+original.totals.index_bytes+original.totals.texture_bytes
                <<" seconds="<<std::chrono::duration<double>(std::chrono::steady_clock::now()-began).count()<<"\n";
            return 0;
        }
        if(argc==4 && std::string(argv[3])=="browser") {
            const std::filesystem::path out(argv[2]);std::filesystem::create_directories(out);
            std::vector<std::string> names;for(const auto& c:catalog.starters())names.push_back(c.name);
            FrontendGpuScene scene;
            if(!scene.load_browser(argv[1],1280,720,names,error))throw std::runtime_error(error);
            const auto uploaded=scene.commands().resource_table();
            DrawExecutionPlan plan;check(plan_draw_execution(scene.commands(),plan,error),error.c_str());
            std::cout<<"BROWSER resident_resources="<<uploaded.resources.size()<<" texture_bytes="<<uploaded.totals.texture_bytes
                <<" geometry_bytes="<<uploaded.totals.vertex_bytes+uploaded.totals.index_bytes
                <<" capacity="<<scene.draw_capacity()<<std::endl;
            unsigned ticks=0;
            for(unsigned selected=0;selected<names.size();++selected) {
                check(scene.select_car(selected,error),error.c_str());
                for(unsigned i=0;i<250;++i) {
                    check(scene.frame(1.F/60,ticks+=67,error),error.c_str());
                    check(validate_draw_update(uploaded,scene.commands(),scene.draw_capacity(),error),error.c_str());
                    if(selected==1 && (i==0||i==45||i==91||i==135||i==249)) {
                        std::vector<unsigned char> pixels;check(scene.render_reference(pixels,error),error.c_str());
                        std::ofstream image(out/("switch-"+std::to_string(i)+".ppm"),std::ios::binary);
                        image<<"P6\n1280 720\n255\n";
                        for(std::size_t n=0;n<pixels.size();n+=4)image.write(reinterpret_cast<const char*>(pixels.data()+n),3);
                        check(bool(image),"browser frame written");
                    }
                }
                check(!scene.car_transitioning(),"browser transition completes");
                std::cout<<"BROWSER selected="<<names[selected]<<" resident_update=PASS"<<std::endl;
            }
            check(!scene.select_car(names.size(),error),"out of range selection refuses");
            check(scene.select_car(0,error),error.c_str());
            for(unsigned i=0;i<300;++i) {
                if(i<30)check(scene.select_car(i%names.size(),error),error.c_str());
                check(scene.frame(1.F/60,ticks+=67,error),error.c_str());
                check(validate_draw_update(uploaded,scene.commands(),scene.draw_capacity(),error),error.c_str());
            }
            check(!scene.car_transitioning(),"rapid selection coalesces and settles");
            std::cout<<"PASS resident browser switching, rapid input, unchanged uploads and draw bounds\n";
            return 0;
        }
        if(argc==4 && std::string(argv[3])=="feedback") {
            const char* senders[]={"","Jose","","Dirt","Klutch","","Samantha"};
            const int voices[]={-1,14,-1,6,33,-1,46};
            for(unsigned event=1;event<=6;++event) {
                const auto* message=catalog.media(event);
                check(message&&message->sender==senders[event]&&message->speech==voices[event],"original event sender and ordered speech table");
                check(bool(message->portrait)==!message->sender.empty(),"no invented sender portrait");
            }
            CareerProfile p;check(catalog.intro_won(p,error),"feedback intro");check(catalog.movie_finished(p,error),"feedback starter");
            check(catalog.buy(p,0,error),"feedback buy");check(catalog.movie_finished(p,error),"feedback map");
            FrontendArt art;if(!art.load(argv[1],error,false))throw std::runtime_error("garage frontend: "+error);if(!art.load_career_pages(argv[1],catalog,error))throw std::runtime_error("garage artwork: "+error);
            const std::filesystem::path out(argv[2]);std::filesystem::create_directories(out);
            for(unsigned event:{1U,3U,4U})for(bool briefing:{false,true}) {
                p.selected_event=event;p.page=briefing?CareerPage::briefing:CareerPage::map;
                std::vector<unsigned char> pixels(1280*720*4,0);FrontendUiFrame gpu;
                art.draw_career({pixels.data(),1280,720,1280*4},catalog,p,0);art.record_career(1280,720,catalog,p,0,"",gpu);
                check(!gpu.vertices.empty()&&gpu.additive.size()<=kFrontendUiMaxQuads,"map and sender command bounds");
                std::ofstream image(out/(std::string(briefing?"briefing-":"map-")+std::to_string(event)+".ppm"),std::ios::binary);
                image<<"P6\n1280 720\n255\n";for(std::size_t i=0;i<pixels.size();i+=4)image.write(reinterpret_cast<const char*>(pixels.data()+i),3);
                check(bool(image),"feedback reference frame written");
            }
            p.selected_event=1;p.page=CareerPage::briefing;
            check(catalog.event_finished(p,1,1,error),"completed circuit before live map submission");
            p.page=CareerPage::map;
            std::vector<unsigned char> pixels(1280*720*4,0);FrontendUiFrame gpu;
            art.draw_career({pixels.data(),1280,720,1280*4},catalog,p,0);
            art.record_career(1280,720,catalog,p,0,"",gpu);
            check(p.selected_event==2 && !gpu.vertices.empty(),"won circuit immediately renders next available event");
            std::cout<<"PASS original first-event senders/speech, map neighbors, post-win map and real frontend submissions\n";return 0;
        }
        if(argc==4 && std::string(argv[3])=="drift") {
            FrontendArt art;
            if(!art.load(argv[1],error,false)||!art.load_career_pages(argv[1],catalog,error))throw std::runtime_error(error);
            CareerProfile profile;check(catalog.intro_won(profile,error),"briefing intro");
            check(catalog.movie_finished(profile,error),"briefing shop");check(catalog.buy(profile,0,error),"briefing car");
            check(catalog.movie_finished(profile,error),"briefing map");
            profile.selected_event=3;profile.page=CareerPage::briefing;
            const std::filesystem::path out(argv[2]);std::filesystem::create_directories(out);
            for(unsigned d=0;d<3;++d) {
                profile.difficulty=d;
                std::vector<unsigned char> pixels(1280*720*4,0);FrontendUiFrame gpu;
                art.draw_career({pixels.data(),1280,720,1280*4},catalog,profile,0);
                art.record_career(1280,720,catalog,profile,0,"",gpu);
                check(!gpu.vertices.empty()&&gpu.additive.size()<=kFrontendUiMaxQuads,"drift briefing bounded");
                std::ofstream image(out/("briefing-"+std::to_string(d)+".ppm"),std::ios::binary);
                image<<"P6\n1280 720\n255\n";for(std::size_t i=0;i<pixels.size();i+=4)image.write(reinterpret_cast<const char*>(pixels.data()+i),3);
                auto winner=profile;const auto old=winner.bank;
                check(catalog.event_finished(winner,3,1,error),"drift win commits");
                check(winner.bank==old+catalog.media(3)->reward[d]&&winner.status[3]==kEventStatusWon,"difficulty reward");
                check(!catalog.event_finished(winner,3,1,error),"duplicate drift award refused");
                auto lost=profile;check(catalog.event_finished(lost,3,4,error),"drift defeat returns to map");
                check(lost.bank==old&&lost.status[3]==0,"defeat has no reward or unlock");
            }
            auto save=base;save+="-drift";
            check(save_career(save,catalog,profile,error),"briefing saved");
            CareerProfile loaded;check(load_career(save,catalog,loaded,error)&&loaded.page==CareerPage::briefing,"briefing resumed");
            for(unsigned slot=0;slot<2;++slot){auto path=save;path+="."+std::to_string(slot);std::filesystem::remove(path);}
        }
        if(argc==3) {
            FrontendArt art;
            if(!art.load(argv[1],error,false) || !art.load_career_pages(argv[1],catalog,error))throw std::runtime_error(error);
            const std::filesystem::path out(argv[2]);std::filesystem::create_directories(out);
            // Sequential reload matches the runtime ownership contract: all
            // commands from the previous car are released before load().
            FrontendGpuScene selection_scene;
            for(const auto& starter:catalog.starters()) {
                if(!selection_scene.load(argv[1],1280,720,error,starter.name) || !selection_scene.frame(0,0,error))throw std::runtime_error(error);
                check(!selection_scene.commands().owning_draws().empty(),"stock starter emits draws");
                std::cout<<"STARTER "<<starter.name<<" draws="<<selection_scene.commands().owning_draws().size()<<'\n';
            }
            if(!selection_scene.load(argv[1],1280,720,error) || !selection_scene.frame(0,0,error))throw std::runtime_error(error);
            check(!selection_scene.commands().owning_draws().empty(),"main Skyline reload after stock browser");
            std::cout<<"CAREER atlas="<<art.atlas().width<<'x'<<art.atlas().height<<" bytes="<<art.atlas().rgba.size()<<'\n';
            check(art.load_underground_welcome(argv[1],error),"original welcome FNG and resources load");
            {
                std::vector<unsigned char> background;
                check(selection_scene.render_reference(background,error),error.c_str());
                const auto save=[&](const char* name,const void* bytes,std::size_t count) {
                    std::ofstream file(out/name,std::ios::binary);
                    file.write(static_cast<const char*>(bytes),count);check(bool(file),"welcome diagnostic written");
                };
                save("welcome-background.rgba",background.data(),background.size());
                save("welcome-atlas.rgba",art.atlas().rgba.data(),art.atlas().rgba.size());
                FrontendUiFrame first,last;
                art.record_underground_welcome(1280,720,false,first,0);
                art.record_underground_welcome(1280,720,false,last,600);
                check(!last.vertices.empty() && first.additive.size()<=kFrontendUiMaxQuads,
                    "welcome entry and final submissions fit the overlay");
                check(first.vertices.size()!=last.vertices.size() ||
                    std::memcmp(first.vertices.data(),last.vertices.data(),last.vertices.size()*sizeof(TrackVertex))!=0,
                    "welcome evaluates original entry keys instead of jumping to the final dimmer");
                save("welcome-vertices.bin",last.vertices.data(),last.vertices.size()*sizeof(TrackVertex));
                save("welcome-blends.bin",last.additive.data(),last.additive.size());
                std::ofstream metadata(out/"welcome-meta.txt");
                metadata<<art.atlas().width<<' '<<art.atlas().height<<' '<<last.vertices.size()<<' '<<sizeof(TrackVertex)<<'\n';
            }
            for(unsigned choice=0;choice<2;++choice) {
                std::vector<unsigned char> pixels;
                if(!selection_scene.render_reference(pixels,error))throw std::runtime_error(error);
                art.draw_underground_welcome({pixels.data(),1280,720,1280*4},choice!=0);
                FrontendUiFrame gpu;art.record_underground_welcome(1280,720,choice!=0,gpu);
                check(!gpu.vertices.empty() && gpu.additive.size()<=kFrontendUiMaxQuads,"welcome submits bounded GPU commands");
                std::ofstream image(out/("welcome-"+std::to_string(choice)+".ppm"),std::ios::binary);
                image<<"P6\n1280 720\n255\n";for(std::size_t i=0;i<pixels.size();i+=4)image.write(reinterpret_cast<const char*>(pixels.data()+i),3);
                check(bool(image),"welcome frame written");
            }
            CareerProfile frame;check(catalog.intro_won(frame,error),"render UG0 win");check(catalog.movie_finished(frame,error),"render car selection");
            for(unsigned stage=0;stage<4;++stage) {
                FrontendGpuScene scene;const auto name=stage?frame.car:catalog.starters()[0].name;
                if(!scene.load(argv[1],1280,720,error,name) || !scene.frame(0,0,error))throw std::runtime_error(error);
                std::vector<unsigned char> pixels;
                if(!scene.render_reference(pixels,error))throw std::runtime_error(error);
                art.draw_career({pixels.data(),1280,720,1280*4},catalog,frame,0);
                FrontendUiFrame gpu;art.record_career(1280,720,catalog,frame,0,"",gpu);
                check(!gpu.vertices.empty() && gpu.additive.size()<=kFrontendUiMaxQuads,"career GPU commands bounded and present");
                std::ofstream image(out/("career-"+std::to_string(stage)+".ppm"),std::ios::binary);
                image<<"P6\n1280 720\n255\n";for(std::size_t i=0;i<pixels.size();i+=4)image.write(reinterpret_cast<const char*>(pixels.data()+i),3);
                check(bool(image),"career frame written");
                if(stage==1)for(auto event:catalog.available(frame)) {
                    auto selected=frame;selected.selected_event=event;
                    check(art.load_career_event(argv[1],event,error),error.c_str());
                    std::vector<unsigned char> map_pixels(1280*720*4,0);
                    art.draw_career({map_pixels.data(),1280,720,1280*4},catalog,selected,0);
                    art.record_career(1280,720,catalog,selected,0,"",gpu);
                    check(!gpu.vertices.empty()&&gpu.additive.size()<=kFrontendUiMaxQuads,"every available map submits bounded commands");
                    std::ofstream map(out/("event-"+std::to_string(event)+".ppm"),std::ios::binary);
                    map<<"P6\n1280 720\n255\n";for(std::size_t i=0;i<map_pixels.size();i+=4)map.write(reinterpret_cast<const char*>(map_pixels.data()+i),3);
                    check(bool(map),"selected map frame written");
                }
                if(stage==0){check(catalog.buy(frame,0,error),"render purchase");check(catalog.movie_finished(frame,error),"render map");}
                else if(stage==1)frame.page=CareerPage::menu;
                else if(stage==2){check(catalog.enter_customization(frame,error),"render tutorial");check(catalog.movie_finished(frame,error),"render garage");}
            }
        }
        check(catalog.starters().size()==5,"five original unlocked starters");
        check(catalog.media(0)->before=="01_DAY" && catalog.media(0)->after=="02_FAN","UG0 authored movies");
        check(catalog.media(78)->before=="11_PNK" && catalog.media(78)->after=="13_KIT","late event uses authored movies");
        CareerProfile p;check(catalog.available(p).empty(),"fresh career cannot skip UG0");
        check(!catalog.buy(p,0,error),"purchase before winning refused");
        check(catalog.intro_won(p,error),"intro completion");
        check(p.bank==10000 && catalog.pending_movie(p)=="02_FAN_s.mad","original UG0 reward/short movie");
        check(!catalog.intro_won(p,error) && p.bank==10000,"no duplicate reward");
        check(catalog.movie_finished(p,error),"short movie continuation");
        for(std::size_t i=0;i<catalog.starters().size();++i) {
            auto copy=p;const auto& car=catalog.starters()[i];check(catalog.buy(copy,i,error),"every unlocked starter purchasable");
            check(copy.car==car.name && copy.bank==10000-car.cost,"retail purchase charge");
            check(!catalog.buy(copy,i,error),"duplicate purchase refused");
        }
        check(save_career(base,catalog,p,error),"save choose-car state");
        check(catalog.buy(p,0,error),"purchase");check(save_career(base,catalog,p,error),"save purchased car before movie");
        CareerProfile resumed;check(load_career(base,catalog,resumed,error),"reload purchased car");
        check(resumed.car==p.car && resumed.bank==p.bank && catalog.pending_movie(resumed)=="02_FAN_e.mad","resume without double charge");
        check(catalog.movie_finished(resumed,error),"long movie continuation");
        const auto available=catalog.available(resumed);
        check(available==std::vector<unsigned>({1,2,3,4,5}),"original post-UG0 available events");
        {
            auto campaign=resumed;
            for(unsigned event:{5U,2U,4U,1U,3U}) {
                const auto open=catalog.available(campaign);
                check(std::find(open.begin(),open.end(),6U)==open.end(),"tournament stays locked until all five wins");
                campaign.selected_event=event;campaign.page=CareerPage::briefing;
                const auto bank=campaign.bank;
                auto loss=campaign;
                check(catalog.event_finished(loss,event,4,error),"first-five loss accepted");
                check(loss.bank==bank&&loss.status[event]==0&&loss.page==CareerPage::menu,"loss cannot unlock or award");
                check(catalog.event_finished(campaign,event,1,error),"first-five win accepted");
                check(campaign.bank==bank+catalog.media(event)->reward[campaign.difficulty],"authored reward per event");
                const auto remaining=catalog.available(campaign);
                check(std::find(remaining.begin(),remaining.end(),event)==remaining.end(),"won event removed from map");
                check(std::find(remaining.begin(),remaining.end(),campaign.selected_event)!=remaining.end(),"post-win map selects an available event");
                campaign.selected_event=event;campaign.page=CareerPage::briefing;
                check(!catalog.event_finished(campaign,event,1,error),"won event cannot award again");
                campaign.page=CareerPage::map;
            }
            const auto open=catalog.available(campaign);
            check(std::find(open.begin(),open.end(),6U)!=open.end(),"five wins unlock first tournament");
            campaign.selected_event=6;campaign.page=CareerPage::briefing;
            check(!catalog.event_finished(campaign,6,1,error),"tournament completion outside this stage");
            check(catalog.begin_event(campaign,error),"begin first tournament");
            check(catalog.tournament_round_finished(campaign,{{1,2,3,4}},error),"first round commits without completing tournament");
            const auto bank_before=campaign.bank;
            check(campaign.status[6]==0&&campaign.tournament.round==1&&campaign.page==CareerPage::briefing,"round1 retains parent and next round");
            {
                auto abandoned=campaign;const auto status=abandoned.status;const auto bank=abandoned.bank;
                check(catalog.abandon_event(abandoned,error)&&!abandoned.tournament.event&&abandoned.tournament.round==0&&
                    abandoned.page==CareerPage::map&&abandoned.races_lost==campaign.races_lost+1,"confirmed tournament exit discards round progress and records loss");
                check(abandoned.status==status&&abandoned.bank==bank,"quit cannot award a tournament prize or erase previous event wins");
                check(catalog.begin_event(abandoned,error)&&abandoned.tournament.event==6&&abandoned.tournament.round==0,"quitted tournament restarts at its first round");
                abandoned.page=CareerPage::menu;check(!catalog.abandon_event(abandoned,error),"inactive race cannot be abandoned");
            }
            auto mid=base;mid+="-tournament";check(save_career(mid,catalog,campaign,error),"save tournament round");
            CareerProfile restored;check(load_career(mid,catalog,restored,error),"restore tournament round");
            check(restored.tournament.round==1&&restored.tournament.places==campaign.tournament.places,"restore exact standings without replaying a reward");
            check(!catalog.tournament_round_finished(restored,{{1,1,3,4}},error)&&restored.tournament.round==1,"invalid standings cannot advance");
            check(catalog.tournament_round_finished(restored,{{1,3,2,4}},error)&&restored.bank==bank_before,"second round has no career event cash");
            check(catalog.tournament_round_finished(restored,{{2,1,3,4}},error),"last round resolves aggregate victory");
            check(restored.status[6]==kEventStatusWon&&!restored.tournament.event&&restored.selected_event==7,"aggregate win opens TJ challenge");
            check(restored.bank==bank_before+catalog.media(6)->reward[restored.difficulty],"one tournament prize");
            for(unsigned slot=0;slot<2;++slot){auto path=mid;path+="."+std::to_string(slot);std::filesystem::remove(path);}
            check(catalog.begin_event(restored,error),"begin TJ challenge");
            check(catalog.pending_movie(restored)=="05_ENG.mad","TJ authored movie precedes challenge briefing");
            check(save_career(mid,catalog,restored,error),"pending event movie saves");
            CareerProfile movie_resume;check(load_career(mid,catalog,movie_resume,error)&&catalog.pending_movie(movie_resume)=="05_ENG.mad","pending movie survives reload");
            check(catalog.movie_finished(restored,error)&&catalog.pending_movie(restored).empty()&&restored.page==CareerPage::briefing,"movie returns to selected briefing once");
            auto failed=restored;check(catalog.event_finished(failed,7,2,error),"challenge timeout resolves without prize");
            const auto after_failure=catalog.available(failed);
            check(failed.status[7]==5&&failed.pending_unique==0&&std::find(after_failure.begin(),after_failure.end(),8U)!=after_failure.end(),"native optional challenge failure unlocks successor without unique");
            check(catalog.event_finished(restored,7,1,error),"challenge victory waits for choice");
            check(restored.page==CareerPage::unique_reward&&restored.pending_unique==7,"unique selector durable state");
            check(save_career(mid,catalog,restored,error),"save pending unique selector");
            CareerProfile reward;check(load_career(mid,catalog,reward,error)&&reward.pending_unique==7,"resume unclaimed unique without losing award");
            const auto award_bank=reward.bank;
            CareerGarage chooser;chooser.reset_unique(catalog,reward);
            check(chooser.entries().size()==3,"TJ offers original three category choices");
            bool purchased=false;
            check(chooser.accept(catalog,reward,purchased,error)&&!purchased&&!chooser.unique_intro(),"native reward explanation dismissed first");
            check(chooser.accept(catalog,reward,purchased,error)&&!purchased,"first award confirmation only");
            check(chooser.accept(catalog,reward,purchased,error)&&purchased,"single confirmed unique applies");
            check(reward.ride.unique[2]&&!reward.ride.unique[6]&&!reward.ride.unique[7]&&reward.bank==award_bank&&!reward.pending_unique,"one unique engine flag without package/cash mutation");
            check(!catalog.award_unique(reward,19,0,error),"no second unique from same challenge");
            for(unsigned event:{8U,9U,10U,11U,12U,13U,14U,15U}) {
                reward.selected_event=event;check(catalog.begin_event(reward,error),"next authored campaign event opens");
                if(event==15)check(catalog.pending_movie(reward)=="06_SAM.mad"&&catalog.movie_finished(reward,error),"Samantha movie plays before unique challenge");
                if(supported_tournament_row(event)) {
                    for(unsigned round=0;round<3;++round)check(catalog.tournament_round_finished(reward,{{1,2,3,4}},error),"later tournament round");
                }else check(catalog.event_finished(reward,event,1,error),"next standalone event win");
            }
            check(reward.pending_unique==15&&reward.page==CareerPage::unique_reward,"Samantha unique hood selection");
            chooser.reset_unique(catalog,reward);check(!chooser.entries().empty(),"actual car has authored unique hood options");
            const auto hood=chooser.entries().front().part;
            check(chooser.accept(catalog,reward,purchased,error)&&!purchased&&!chooser.unique_intro(),"native Samantha explanation dismissed first");
            check(chooser.accept(catalog,reward,purchased,error)&&chooser.accept(catalog,reward,purchased,error),"unique hood confirm/install");
            check(reward.ride.installed[8]==hood&&!reward.pending_unique&&catalog.valid(reward,error),"one unique hood owned and installed");
            #include "career_block30.inc"
            auto magazine_reward=reward;magazine_reward.page=CareerPage::unique_reward;magazine_reward.pending_unique=15;
            CareerMagazine earned;earned.index=14;earned.car=magazine_reward.car;earned.installed=magazine_reward.ride.installed;
            magazine_reward.magazine_covers.push_back(earned);
            check(catalog.award_unique(magazine_reward,12,hood,error)&&magazine_reward.page==CareerPage::magazines,"unique award continues to earned magazine before menu");
            #include "career_magazine_lifecycle_native.inc"
            for(const auto& witness:kNativeMagazineDismiss) {
                auto profile=resumed;profile.page=CareerPage::magazines;profile.magazine_covers.clear();
                for(unsigned index=0;index<27;++index)if((witness.before|witness.pending)&(1U<<index)) {
                    CareerMagazine cover;cover.index=index;cover.car=profile.car;cover.installed=profile.ride.installed;
                    cover.seen=(witness.before&(1U<<index))!=0;profile.magazine_covers.push_back(cover);
                }
                check(catalog.dismiss_magazines(profile,error),error.c_str());unsigned mask=0;
                for(const auto& cover:profile.magazine_covers)if(cover.seen)mask|=1U<<cover.index;
                check(mask==witness.after,"actual original terminal Giveth-batch acknowledgement");
            }
            auto second_cover=earned;second_cover.index=15;magazine_reward.magazine_covers.push_back(second_cover);
            check(catalog.dismiss_magazines(magazine_reward,error)&&magazine_reward.page==CareerPage::menu,
                "confirmed native Giveth dismissal returns to menu");
            check(std::all_of(magazine_reward.magazine_covers.begin(),magazine_reward.magazine_covers.end(),
                [](const auto& cover){return cover.seen;}),"native dismissal acknowledges whole magazine batch");
            check(catalog.refresh_magazines(magazine_reward,true,error)&&
                std::none_of(magazine_reward.magazine_covers.begin(),magazine_reward.magazine_covers.end(),
                    [](const auto& cover){return !cover.seen;}),"later victory does not reopen dismissed magazine rewards");
            check(save_career(mid,catalog,reward,error)&&load_career(mid,catalog,restored,error),"expanded campaign saves/reloads");
            check(restored.ride.unique==reward.ride.unique&&restored.ride.installed==reward.ride.installed&&restored.status==reward.status,"all unique and campaign progress survives");
            for(unsigned slot=0;slot<2;++slot){auto path=mid;path+="."+std::to_string(slot);std::filesystem::remove(path);}
            campaign.style_points=3402;campaign.style_high_water=3402;
            auto save=base;save+="-first-five";
            check(save_career(save,catalog,campaign,error),"first-five checkpoint saves");
            CareerProfile loaded;check(load_career(save,catalog,loaded,error),"first-five checkpoint reloads");
            check(loaded.status==campaign.status&&loaded.bank==campaign.bank&&loaded.car==campaign.car&&loaded.style_points==3402,"reload preserves progress reward owned car and style points");
            for(unsigned slot=0;slot<2;++slot){auto path=save;path+="."+std::to_string(slot);std::filesystem::remove(path);}
        }
        {
            auto stale=resumed;stale.selected_event=1;stale.status[1]=kEventStatusWon;
            auto save=base;save+="-stale-map";
            for(auto page:{CareerPage::menu,CareerPage::map,CareerPage::briefing}) {
                stale.page=page;check(save_career(save,catalog,stale,error),"save older completed selection");
                CareerProfile loaded;check(load_career(save,catalog,loaded,error),"load older completed selection");
                const auto open=catalog.available(loaded);
                check(loaded.status[1]==kEventStatusWon && loaded.bank==stale.bank,"selection recovery preserves win and bank");
                check(std::find(open.begin(),open.end(),loaded.selected_event)!=open.end(),"recovered map selection is available");
                check(loaded.page==(page==CareerPage::briefing?CareerPage::map:page),"completed briefing returns to map");
            }
            for(unsigned slot=0;slot<2;++slot){auto path=save;path+="."+std::to_string(slot);std::filesystem::remove(path);}
        }
        {
            #include "career_magazine_native.inc"
            for(const auto& row:kNativeMagazineChoices){
                auto random=std::uint32_t(row[1]);CareerMagazineRecipe recipe;
                check(catalog.magazines().make(std::uint32_t(row[0]),random,recipe,error),error.c_str());
                check(random==std::uint32_t(row[2]),"native magazine random consumption");
                for(unsigned i=0;i<6;++i)check(recipe.choices[i]==row[3+i],"native magazine authored choice order");
                check(recipe.background==row[9]&&recipe.front==row[10]&&recipe.chick==row[11]&&recipe.flags==row[12],"native magazine output texture bindings and background mode");
            }
        }
        {
            #include "career_magazine_tasks_native.inc"
            for(const auto& row:kNativeMagazinePerformance){
                auto profile=resumed;profile.car="CIVIC";profile.ride={};profile.magazine_pending=0;
                for(unsigned field=0;field<9;++field)profile.ride.performance[field]=row[field];
                check(catalog.refresh_magazines(profile,false,error)&&((profile.magazine_pending&7U)==row[9]),"native complete performance magazine conditions");
            }
            for(const auto& witness:kNativeMagazineTasks){
                auto profile=resumed;profile.car="CIVIC";profile.ride={};profile.magazine_pending=0;
                for(auto index:witness.parts)if(index!=0xffffffffU){const auto& part=catalog.tuning().parts().records.at(index);profile.ride.installed[part.slot==58?59:part.slot]=part.part_name_hash;}
                check(catalog.refresh_magazines(profile,false,error),error.c_str());
                if((profile.magazine_pending&0xf8U)!=witness.mask)throw std::runtime_error("original magazine task predicate mismatch: expected="+std::to_string(witness.mask)+" actual="+std::to_string(profile.magazine_pending&0xf8U));
            }
        }
        {
            auto photo=resumed;photo.bank=10000000;photo.page=CareerPage::garage;photo.customization_seen=true;
            for(unsigned id=1;id<=112;++id)if(find_race_event(catalog.events(),id))photo.status[id]=kEventStatusWon;
            for(unsigned field=0;field<9;++field)check(catalog.buy_performance(photo,field,1,error),error.c_str());
            check((photo.magazine_pending&1)&&photo.magazine_covers.empty(),"performance cover pending until career victory");
            check(catalog.refresh_magazines(photo,true,error)&&!photo.magazine_covers.empty(),"earned magazine captures installed ride");
            const auto old=photo.magazine_covers.front();photo.ride.performance[2]=0;
            check(catalog.refresh_magazines(photo,true,error)&&photo.magazine_covers.front().performance==old.performance,"later tuning does not alter earned car photo");
            check(catalog.buy_performance(photo,2,1,error),error.c_str());
            auto save=base;save+="-magazines";
            check(save_career(save,catalog,photo,error),error.c_str());CareerProfile loaded;
            check(load_career(save,catalog,loaded,error)&&loaded.magazine_covers.size()==photo.magazine_covers.size()&&loaded.magazine_covers.front().installed==old.installed,"earned magazine snapshot persists");
            auto seed=old.seed;CareerMagazineRecipe recipe;check(catalog.magazines().make(CareerMagazineCatalog::key(old.index),seed,recipe,error),error.c_str());
            FrontendArt art;if(!art.load(argv[1],error,false)||!art.load_career_pages(argv[1],catalog,error)||!art.load_magazine(argv[1],recipe,error))throw std::runtime_error("magazine artwork: "+error);
            FrontendUiFrame frame;art.record_magazine(1280,720,recipe,old.car,frame);
            check(frame.background_quads>0&&frame.background_quads<frame.additive.size()&&frame.vertices.size()==frame.additive.size()*4,"native magazine layers straddle real car scene");
            check(art.load_career_event(argv[1],7,error),"restore selected challenge map after magazine textures retire");
            auto map=photo;map.status[7]=0;map.selected_event=7;map.page=CareerPage::map;art.record_career(1280,720,catalog,map,0,"",frame);
            check(!frame.vertices.empty()&&!frame.background_quads,"ordinary map restores overlay ordering after album");
            for(const auto& v:frame.vertices)check(std::isfinite(v.px)&&std::isfinite(v.py),"finite magazine native layout");
            for(unsigned slot=0;slot<2;++slot){auto file=save;file+="."+std::to_string(slot);std::filesystem::remove(file);}
        }
        resumed.page=CareerPage::menu;check(catalog.enter_customization(resumed,error),"first customization");
        check(catalog.pending_movie(resumed)=="04_GUM_s.mad","authored customization movie");
        check(catalog.movie_finished(resumed,error),"customization movie continuation");
        resumed.page=CareerPage::menu;check(catalog.enter_customization(resumed,error),"second customization");
        check(resumed.page==CareerPage::garage && catalog.pending_movie(resumed).empty(),"tutorial plays only once");
        {
            auto queued=resumed;queued.movie_event=16;queued.movie_phase=2;queued.page=CareerPage::unique_reward;
            check(catalog.pending_movie(queued).empty(),"unique selector has priority over post-race movie");
            queued.page=CareerPage::menu;check(!catalog.pending_movie(queued).empty(),"post-race movie resumes after unique selector");
        }
        {
            auto folder=base;folder+="-profiles";std::filesystem::create_directories(folder);
            auto retained=resumed;retained.races_won=7;retained.races_lost=3;retained.restarts=2;
            check(save_career(folder/"career",catalog,retained,error),"legacy profile fixture");
            CareerProfiles profiles(folder);std::vector<CareerProfileEntry> entries;unsigned selected=0;
            check(profiles.list(entries,selected,error)&&entries.size()==1&&entries[0].id==0,"legacy career stays selectable");
            CareerProfileEntry first,second;
            check(!profiles.create("",catalog,first,error)&&!profiles.create("bad ",catalog,first,error),"invalid names cannot create saves");
            check(profiles.create("NEW CAREER",catalog,first,error),"create independent career");
            check(!profiles.create("new career",catalog,second,error),"case insensitive duplicate refused");
            check(profiles.create("A/B",catalog,second,error),"display names never become paths");
            check(second.base.parent_path().filename().string().rfind("p",0)==0,"profile uses ASCII numeric identifier");
            check(profiles.list(entries,selected,error)&&entries.size()==3&&entries[selected].id==second.id,"selection persists");
            CareerProfile fresh,legacy;
            check(load_career(first.base,catalog,fresh,error)&&fresh.page==CareerPage::intro&&fresh.car.empty(),"new career has fresh original introduction");
            check(load_career(folder/"career",catalog,legacy,error)&&legacy.bank==resumed.bank&&legacy.car==resumed.car&&legacy.races_won==7&&legacy.races_lost==3&&legacy.restarts==2,"profile creation preserves legacy progress");
            {
                auto file=first.base;file+=".0";std::ifstream in(file,std::ios::binary);
                std::string bytes((std::istreambuf_iterator<char>(in)),{});in.close();
                auto body=bytes.substr(0,bytes.rfind("SHA256 "));
                body.erase(body.find("STATS "));
                body.replace(0,std::string("NFSU_CAREER 11").size(),"NFSU_CAREER 8");
                const auto final=body.rfind('\n',body.size()-2);body.erase(final+1);
                {std::ofstream out(file,std::ios::binary|std::ios::trunc);out<<body<<"SHA256 "<<sha256_hex(sha256(body.data(),body.size()))<<'\n';}
                check(load_career(first.base,catalog,fresh,error)&&fresh.page==CareerPage::intro&&fresh.races_won==0&&fresh.races_lost==0,"existing schema8 profiles remain readable");
                check(save_career(first.base,catalog,fresh,error),"upgrade a schema8 profile");
                auto backup=first.base;backup+=".before-v9.0";
                std::ifstream retained(backup,std::ios::binary);std::string original((std::istreambuf_iterator<char>(retained)),{});
                check(original==body+"SHA256 "+sha256_hex(sha256(body.data(),body.size()))+'\n',"schema8 rollback backup is byte exact");
            }
            check(profiles.select(0,error)&&profiles.list(entries,selected,error)&&entries[selected].id==0,"legacy can be selected again");
            check(!profiles.select(9999,error),"unknown profile cannot become active");
            // Both files belong solely to this process-unique test directory.
            std::filesystem::remove_all(folder);
            std::cout<<"PASS named profiles, save isolation, safe legacy access and durable selection\n";
        }
        {
            // Existing owner v1 saves migrate with a zero style-point bank.
            auto legacy=base;legacy+="-legacy";auto path=legacy;path+=".0";
            std::string body="NFSU_CAREER 1\n1 0 1 0 1 0 -\n"+std::string(256,'0')+"\n";
            auto sum=sha256_hex(sha256(body.data(),body.size()));
            {std::ofstream file(path,std::ios::binary);file<<body<<"SHA256 "<<sum<<'\n';}
            CareerProfile old;check(load_career(legacy,catalog,old,error),"v1 career save remains readable");
            check(old.style_points==0&&old.page==CareerPage::intro,"v1 style bank migration");
            std::filesystem::remove(path);
        }
        {
            auto repaint=resumed;repaint.bank=100000;
            const auto& tuning=catalog.tuning();
            const CarPartRecord *first=nullptr,*other=nullptr,*closed=nullptr;
            for(const auto& part:tuning.parts().records)if(part.slot==30&&tuning.find_part(repaint.car,30,part.part_name_hash)==&part) {
                if(!tuning.part_available(part,repaint.status,repaint.style_high_water)){if(!closed)closed=&part;continue;}
                if(!first)first=&part;else if(!other)other=&part;
            }
            check(first&&other&&closed,"paint job fixtures include two available colors and a locked palette");
            const auto price=tuning.part_price(*first);
            check(price&&catalog.part_price(repaint,*first)==price,"first paint job uses authored price");
            check(catalog.buy_part(repaint,30,first->part_name_hash,error)&&repaint.bank==100000-price,"first paint job pays once");
            const auto inventory=repaint.ride.owned[0];const auto bank=repaint.bank;
            check(catalog.part_price(repaint,*other)==0&&catalog.buy_part(repaint,30,other->part_name_hash,error),"later available color consumes existing paint job");
            check(repaint.bank==bank&&repaint.ride.owned[0]==inventory&&catalog.valid(repaint,error),"recolor is free without filling purchased-parts inventory");
            check(!catalog.buy_part(repaint,30,closed->part_name_hash,error)&&repaint.bank==bank&&repaint.ride.installed[30]==other->part_name_hash,"free paint service does not bypass locked palettes");
            auto save=base;save+="-repaint";
            check(save_career(save,catalog,repaint,error),"save recolored ride with paint job entitlement");
            CareerProfile loaded;check(load_career(save,catalog,loaded,error)&&loaded.bank==bank&&loaded.ride.installed[30]==other->part_name_hash,"reload preserves free recolor and current color");
            check(catalog.buy_part(loaded,30,first->part_name_hash,error)&&loaded.bank==bank,"free color change after save reload");
            for(unsigned i=0;i<2;++i){auto path=save;path+="."+std::to_string(i);std::filesystem::remove(path);}
            std::cout<<"PASS one paid paint job, free available recolors, retained locks and save round trip\n";
        }
        {
            auto ride=resumed;ride.bank=100000;
            const auto& tuning=catalog.tuning();const CarPartRecord *bumper=nullptr,*locked=nullptr,*vinyl=nullptr,*foreign=nullptr;
            for(const auto& part:tuning.parts().records) {
                if(part.slot==1&&part.car_type_hash==bin_hash(ride.car)&&part.upgrade_level==1&&!bumper)bumper=&part;
                if(part.slot==1&&part.car_type_hash==bin_hash(ride.car)&&part.upgrade_level==2&&!locked)locked=&part;
                if(part.slot==1&&part.car_type_hash!=bin_hash(ride.car)&&part.upgrade_level==1&&!foreign)foreign=&part;
                if(part.slot==31&&part.byte_12==0&&part.upgrade_level==1&&!vinyl)vinyl=&part;
            }
            check(bumper&&locked&&vinyl&&foreign,"authored shop transaction witnesses present");
            auto refused=ride;refused.bank=0;
            check(!catalog.buy_part(refused,1,bumper->part_name_hash,error)&&refused.bank==0&&refused.ride.installed[1]==0,"unaffordable part cannot mutate bank or selection");
            check(!catalog.buy_part(ride,1,locked->part_name_hash,error)&&ride.bank==100000,"event-locked cosmetic cannot be purchased");
            check(!catalog.buy_part(ride,1,foreign->part_name_hash,error)&&ride.bank==100000,"other car bumper cannot be purchased");
            check(catalog.buy_part(ride,1,bumper->part_name_hash,error),"purchase available original bumper");
            const auto bank=100000-tuning.part_price(*bumper);
            check(ride.bank==bank&&ride.ride.installed[1]==bumper->part_name_hash,"purchase charges authored price and installs selection");
            check(catalog.buy_part(ride,1,0,error)&&ride.bank==bank&&ride.ride.installed[1]==0,"stock restore keeps money and purchase inventory");
            check(catalog.buy_part(ride,1,bumper->part_name_hash,error)&&ride.bank==bank,"owned part reinstall does not charge again");
            check(!catalog.buy_part(ride,31,vinyl->part_name_hash,error)&&ride.bank==bank,"vinyl below earned style threshold refused");
            ride.style_high_water=5000;
            check(catalog.buy_part(ride,31,vinyl->part_name_hash,error),"vinyl remains unlocked after earned point bank has fallen");
            check(ride.style_points==0&&ride.style_high_water==5000,"current and earned style points remain separate");
            const auto before=ride.bank;
            check(!catalog.buy_performance(ride,2,1,error)&&ride.bank==before,"first engine package locked before tournament win");
            ride.status[6]=kEventStatusWon;
            check(catalog.buy_performance(ride,2,1,error)&&ride.bank==before-tuning.price(18,1),"authored tournament unlock purchases engine package");
            const auto engine_bank=ride.bank;
            check(catalog.buy_performance(ride,2,0,error)&&ride.ride.performance[2]==0,"performance can return to stock");
            check(catalog.buy_performance(ride,2,1,error)&&ride.bank==engine_bank,"owned performance package reinstall is free");
            auto save=base;save+="-tuning";
            check(save_career(save,catalog,ride,error),"customized career saves");CareerProfile loaded;
            check(load_career(save,catalog,loaded,error),"customized career reloads");
            check(loaded.ride.installed==ride.ride.installed&&loaded.ride.owned==ride.ride.owned&&loaded.ride.performance==ride.ride.performance&&
                loaded.style_high_water==5000&&loaded.style_points==0&&loaded.bank==ride.bank,"customized save preserves selections purchases earned unlocks and cash");
            check(catalog.buy_part(loaded,1,bumper->part_name_hash,error)&&loaded.bank==ride.bank,"owned reinstall stays free after reload");
            for(unsigned slot=0;slot<2;++slot){auto path=save;path+="."+std::to_string(slot);std::filesystem::remove(path);}
            std::cout<<"PASS career tuning purchases, locked/insufficient/incompatible refusals, free reinstall and persistence\n";
        }
        {
            auto legacy=base;legacy+="-v2";auto path=legacy;path+=".0";
            std::string body;
            // Use a valid non-new profile for a nonzero existing style bank.
            auto old=resumed;old.style_points=3402;old.style_high_water=3402;
            std::ostringstream record;record<<"NFSU_CAREER 2\n1 "<<unsigned(old.page)<<" "<<old.difficulty<<" "<<old.bank<<" "<<old.selected_event<<" "<<unsigned(old.customization_seen)<<" "<<old.car<<"\n";
            for(auto state:old.status)record<<unsigned(state);record<<"\n3402\n";body=record.str();
            {std::ofstream file(path,std::ios::binary);file<<body<<"SHA256 "<<sha256_hex(sha256(body.data(),body.size()))<<'\n';}
            CareerProfile loaded;check(load_career(legacy,catalog,loaded,error),"v2 career remains readable");
            check(loaded.style_points==3402&&loaded.style_high_water==3402&&loaded.ride.installed==CareerRide{}.installed,"v2 initializes earned style and stock tuning");
            std::filesystem::remove(path);
        }
        auto invalid=resumed;invalid.difficulty=3;check(!save_career(base,catalog,invalid,error),"invalid difficulty refused");
        {auto damaged=base;damaged+=".1";std::ofstream f(damaged,std::ios::trunc);f<<"interrupted write";}
        check(load_career(base,catalog,resumed,error),"recover previous complete slot");
        check(resumed.page==CareerPage::choose_car && resumed.car.empty(),"previous snapshot intact");
        {auto damaged=base;damaged+=".0";std::ofstream f(damaged,std::ios::trunc);f<<"interrupted write";}
        check(!load_career(base,catalog,resumed,error),"damaged saves not replaced by new career");
        check(!save_career(base,catalog,p,error),"damaged saves not overwritten");
        for(unsigned i=0;i<2;++i){auto path=base;path+=i?".1":".0";std::filesystem::remove(path);}
        verify_original_garage(argv[1],catalog);
        std::cout<<"PASS career retail transitions, original rewards/starters/movies, save resume and interrupted-write recovery\n";
        return 0;
    }catch(const std::exception& e){std::cerr<<"FAIL career: "<<e.what()<<'\n';return 1;}
}
