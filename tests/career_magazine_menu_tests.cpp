#include "nfsu/career_magazine_menu.h"
#include "nfsu/career_ride.h"
#include "nfsu/frontend.h"
#include "nfsu/frontend_scene.h"
#include "nfsu/sha256.h"
#include <algorithm>
#include <chrono>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <sstream>
#include "career_magazine_menu_native.inc"
#include "career_magazine_album_native.inc"

int main(int argc,char** argv) {
    if(argc<2||argc>3)return 2;
    using namespace nfsu;
    const auto check=[](bool ok,const std::string& why){if(!ok)throw std::runtime_error(why);};
    const bool camera_only=argc==3&&std::string(argv[2])=="--camera-return";
    std::filesystem::path temporary;
    try {
        const auto temp=std::filesystem::temp_directory_path();
        const auto tick=std::chrono::high_resolution_clock::now().time_since_epoch().count();
        for(unsigned attempt=0;!camera_only&&attempt<128;++attempt) {
            auto candidate=temp/("nfsu-magazine-flow-"+std::to_string(tick)+"-"+std::to_string(attempt));
            if(std::filesystem::create_directory(candidate)){temporary=std::move(candidate);break;}
        }
        check(camera_only||!temporary.empty(),"exclusive magazine fixture directory");
        std::filesystem::path output;
        if(argc==3&&!camera_only){output=std::filesystem::u8path(argv[2]);check(!std::filesystem::exists(output),"fresh magazine captures required");std::filesystem::create_directories(output);}
        const auto read=[](const std::filesystem::path& p){std::ifstream f(p,std::ios::binary);return std::string(std::istreambuf_iterator<char>(f),{});};
        const auto write=[](const std::filesystem::path& p,const std::string& s){std::ofstream f(p,std::ios::binary);f<<s;if(!f)throw std::runtime_error("fixture write failed");};
        CareerCatalog catalog;std::string error;check(catalog.load(argv[1],error),error);
        CareerProfile starter;check(catalog.intro_won(starter,error)&&catalog.movie_finished(starter,error),error);
        const auto car=std::find_if(catalog.starters().begin(),catalog.starters().end(),[](const auto& row){return row.name=="CIVIC";});
        check(car!=catalog.starters().end(),"starter present");
        check(catalog.buy(starter,std::size_t(car-catalog.starters().begin()),error)&&catalog.movie_finished(starter,error),error);
        for(unsigned i=1;i<19;++i)starter.status[i]=kEventStatusWon;
        starter.selected_event=19;starter.page=CareerPage::briefing;
        check(catalog.event_finished(starter,19,1,error),error);
        check(starter.page==CareerPage::magazines&&starter.magazine_covers.size()==1,"production event19 grants one snapshot");
        std::vector<unsigned char> globals;std::vector<RaceTableIssue> issues;
        check(load_race_image(std::filesystem::path(argv[1])/"Global/GLOBALB.BUN",globals,issues),"read-only photo globals");
        const auto verify_camera_return=[&]{
            const auto difference=[](const DrawFrameUniform& a,const DrawFrameUniform& b){
                double d=0;
                for(unsigned i=0;i<4;++i)for(const auto& pair:{std::make_pair(a.eye,b.eye),std::make_pair(a.right,b.right),std::make_pair(a.up,b.up),std::make_pair(a.forward,b.forward),std::make_pair(a.projection,b.projection)})d=std::max(d,std::abs(double(pair.first[i])-pair.second[i]));
                return d;
            };
            for(unsigned route=0;route<3;++route) {
                auto state=starter;FrontendGpuScene visible;CareerRideSetup owned;
                check(resolve_career_ride(globals,catalog,state,owned,error),error);
                check(visible.load_garage(argv[1],1280,720,{owned},error),error);
                visible.focus_part(0);check(visible.frame(0,0,error),error);
                const auto ordinary=visible.commands().frame;
                for(unsigned flow=0;flow<2;++flow) {
                    if(flow)check(catalog.enter_magazine_album(state,error),error);
                    CareerMagazineMenu current;current.reset(state);
                    if(current.notice())check(current.accept(catalog,state,error),error);
                    check(current.accept(catalog,state,error)&&current.viewing(),error);
                    CareerMagazineRecipe recipe;CareerRideSetup selected;
                    check(current.prepare_photo(globals,catalog,state,recipe,selected,error),error);
                    check(visible.load_garage(argv[1],1280,720,{selected},error,nullptr,visible.environment()),error);
                    visible.magazine_camera(recipe.camera);check(visible.frame(0,0,error),error);
                    check(visible.commands().frontend_car_only&&difference(ordinary,visible.commands().frame)>.01,"real photo mode differs from ordinary camera");
                    check(current.back(catalog,state,error)&&!current.viewing(),error);
                    if(route==0)visible.focus_part(0); // Actual magazine-list entry.
                    if(route==2) {
                        // Same-scene owned-ride reload (mode reentry).
                        check(visible.load_owned_ride(argv[1],1280,720,globals,catalog,state,error,visible.environment()),error);
                    }else {
                        // Actual publish_career_scene ownership transfer.
                        FrontendGpuScene prepared;
                        check(prepared.load_owned_ride(argv[1],1280,720,globals,catalog,state,error,visible.environment()),error);
                        check(visible.adopt_garage(prepared,error),error);
                    }
                    visible.road_target(20);
                    for(unsigned frame=0;frame<360;++frame)check(visible.frame(1.F/60,frame*4000/60,error),error);
                    const auto delta=difference(ordinary,visible.commands().frame);
                    std::cout<<"MAGAZINE_CAMERA route="<<route<<" album="<<flow<<" return_delta="<<delta<<'\n';
                    check(!visible.commands().frontend_car_only&&delta<1e-5,"photo return must restore ordinary camera through actual owned scene publication");
                    check(current.back(catalog,state,error)&&state.page==CareerPage::menu,error);
                    FrontendGpuScene main_menu;
                    check(main_menu.load_owned_ride(argv[1],1280,720,globals,catalog,state,error,visible.environment())&&visible.adopt_garage(main_menu,error),error);
                    check(visible.frame(1.F/60,25000,error)&&difference(ordinary,visible.commands().frame)<1e-5,"album exit keeps ordinary Underground camera");
                }
            }
            std::cout<<"PASS shared-scene magazine/photo/album/main-menu camera return\n";
        };
        verify_camera_return();if(camera_only)return 0;
        FrontendArt art;check(art.load(argv[1],error,false)&&art.load_career_pages(argv[1],catalog,error),error);
        FrontendGpuScene room;CareerRideSetup normal;
        check(resolve_career_ride(globals,catalog,starter,normal,error),error);
        check(room.load_garage(argv[1],1280,720,{normal},error),error);room.focus_part(0);room.road_target(20);
        check(room.frame(0,1000,error),error);
        const auto capture=[&](const char* name,const CareerProfile& p,const CareerMagazineMenu& menu){
            FrontendUiFrame ui;art.record_magazine_menu(1280,720,p,menu,ui);
            check(!ui.vertices.empty()&&ui.vertices.size()==ui.additive.size()*4,"original magazine FNG produces bounded submitted quads");
            for(const auto& v:ui.vertices)check(std::isfinite(v.px)&&std::isfinite(v.py),"finite original menu placements");
            if(menu.album()) {
                // Identify the real status texture from its single-item Giveth
                // submission, then inspect that same atlas region in the album.
                auto reference=starter;CareerMagazineMenu reference_menu;reference_menu.reset(reference);
                check(reference_menu.accept(catalog,reference,error),error);FrontendUiFrame reference_ui;
                art.record_magazine_menu(1280,720,reference,reference_menu,reference_ui);
                std::array<float,4> icon_uv{};bool found=false;
                const auto bounds=[](const TrackVertex* q){
                    std::array<float,4> b{{1e6F,1e6F,-1e6F,-1e6F}};
                    for(unsigned j=0;j<4;++j){b[0]=std::min(b[0],q[j].px);b[1]=std::min(b[1],q[j].py);b[2]=std::max(b[2],q[j].px);b[3]=std::max(b[3],q[j].py);}return b;
                };
                for(std::size_t i=0;i<reference_ui.vertices.size();i+=4) {
                    const auto* q=reference_ui.vertices.data()+i;const auto b=bounds(q);
                    if(q[0].pz!=-1.F||b[2]-b[0]<200||b[2]-b[0]>300||b[3]-b[1]<80||b[3]-b[1]>110||b[1]<0||b[3]>140)continue;
                    icon_uv={{q[0].u,q[0].v,q[2].u,q[2].v}};found=true;break;
                }
                check(found,"native status-icon reference quad found");unsigned icons=0,owned=0;
                for(unsigned row=menu.first_visible();row<std::min(menu.count(),menu.first_visible()+8);++row)if(menu.earned(row))++owned;
                for(std::size_t i=0;i<ui.vertices.size();i+=4) {
                    const auto* q=ui.vertices.data()+i;if(q[0].pz!=-1.F)continue;
                    bool status=true;
                    for(unsigned j=0;j<4;++j)status&=q[j].u>=icon_uv[0]-1e-7F&&q[j].u<=icon_uv[2]+1e-7F&&q[j].v>=icon_uv[1]-1e-7F&&q[j].v<=icon_uv[3]+1e-7F;
                    if(!status)continue;
                    const auto b=bounds(q);check(b[2]-b[0]<=320&&b[3]-b[1]<=128,"album status icon stays inside a grid slot instead of covering the album");++icons;
                }
                check(icons==owned,"album submits one status icon per earned visible slot");
            }
            if(output.empty())return;
            std::vector<unsigned char> pixels;check(room.render_reference(pixels,error),error);
            art.draw_magazine_menu({pixels.data(),1280,720,1280*4},p,menu);
            std::ofstream file(output/name,std::ios::binary);file<<"P6\n1280 720\n255\n";
            for(std::size_t i=0;i<pixels.size();i+=4)file.write(reinterpret_cast<const char*>(pixels.data()+i),3);
            check(bool(file),"menu capture saved");
        };
        const auto photo=[&](const char* name,const CareerProfile& p,const CareerMagazineMenu& menu){
            CareerMagazineRecipe recipe;CareerRideSetup setup;
            check(menu.prepare_photo(globals,catalog,p,recipe,setup,error),error);
            FrontendGpuScene scene;check(scene.load_garage(argv[1],1280,720,{setup},error),error);
            const auto& saved=p.magazine_covers.at(menu.cover_index());
            check(scene.selected_car_type()==saved.car,"actual selected photo car comes from its saved snapshot");
            scene.magazine_camera(recipe.camera);check(scene.frame(0,0,error),error);
            check(art.load_magazine(argv[1],recipe,error),error);FrontendUiFrame ui;
            art.record_magazine(1280,720,recipe,saved.car,ui);check(ui.background_quads>0,"original selected photo layers prepared");
            if(output.empty())return;
            std::vector<unsigned char> pixels(1280*720*4);
            art.draw_magazine({pixels.data(),1280,720,1280*4},recipe,saved.car,true);
            // Use the same background/car/foreground composition as the viewer.
            std::vector<unsigned char> vehicle;check(scene.render_reference(vehicle,error),error);
            for(std::size_t i=0;i<pixels.size();i+=4){const auto a=vehicle[i+3];for(unsigned c=0;c<3;++c)pixels[i+c]=static_cast<unsigned char>((unsigned(vehicle[i+c])*a+unsigned(pixels[i+c])*(255-a)+127)/255);pixels[i+3]=255;}
            art.draw_magazine({pixels.data(),1280,720,1280*4},recipe,saved.car,false);
            std::ofstream file(output/name,std::ios::binary);file<<"P6\n1280 720\n255\n";
            for(std::size_t i=0;i<pixels.size();i+=4)file.write(reinterpret_cast<const char*>(pixels.data()+i),3);
            check(bool(file),"photo capture saved");
        };
        for(unsigned i=0;i<27;++i){check(career_magazine_title_key(i)==kNativeMagazineText[i][1],"native magazine title key");check(career_magazine_description_key(i)==kNativeMagazineText[i][0],"native magazine description key");}
        auto p=starter;CareerMagazineMenu menu;menu.reset(p);
        std::cout<<"MAGAZINE_ENTRY event=19 covers="<<menu.count()<<" notice="<<menu.notice()<<" photo="<<menu.viewing()<<'\n';
        check(menu.notice()&&!menu.viewing(),"earned cover enters Hot off the press before list and photo");
        capture("single-notice.ppm",p,menu);const auto snapshot=p.magazine_covers.front();
        CareerMagazineRecipe blocked;CareerRideSetup blocked_ride;
        check(!menu.prepare_photo(globals,catalog,p,blocked,blocked_ride,error),"notice cannot load a photo");
        check(!menu.move(1)&&!menu.move(-1),"notice blocks list input");
        check(menu.accept(catalog,p,error)&&!menu.notice()&&!menu.viewing()&&!p.magazine_covers[0].seen,error);
        check(!menu.move(1)&&!menu.move(-1),"single list clamps at both ends");capture("single-list.ppm",p,menu);
        check(!menu.prepare_photo(globals,catalog,p,blocked,blocked_ride,error),"unconfirmed list cannot load a photo");
        check(menu.accept(catalog,p,error)&&menu.viewing()&&p.magazine_covers[0].seen,error);
        photo("single-photo.ppm",p,menu);
        check(menu.back(catalog,p,error)&&menu.active()&&!menu.viewing()&&p.page==CareerPage::magazines,"photo back restores award list");
        check(menu.accept(catalog,p,error)&&menu.viewing(),error);
        check(menu.accept(catalog,p,error)&&!menu.viewing(),"photo confirmation also restores list");
        check(menu.back(catalog,p,error)&&!menu.active()&&p.page==CareerPage::menu,"single delivery ends at menu");
        std::cout<<"PASS single notice/list/photo/back/acknowledge\n";
        auto batch=starter;CareerMagazine task=snapshot;task.index=14;task.seed=99;batch.magazine_covers.push_back(task);
        task.index=11;task.car="240SX";task.seed=1;task.performance.fill(1);batch.magazine_covers.push_back(task);
        const auto original_random=batch.magazine_random;check(catalog.valid(batch,error),error);menu.reset(batch);
        check(menu.count()==3&&menu.magazine_index(0)==11&&menu.magazine_index(1)==14&&menu.magazine_index(2)==snapshot.index,"native task-first then race-cover order independent of insertion order");
        check(menu.accept(catalog,batch,error),error);capture("batch-list.ppm",batch,menu);
        check(menu.accept(catalog,batch,error)&&menu.viewing(),error);
        check(batch.magazine_covers[2].seen&&!batch.magazine_covers[0].seen&&!batch.magazine_covers[1].seen,"view marks exactly selected item");
        photo("batch-240sx-photo.ppm",batch,menu);
        check(menu.back(catalog,batch,error)&&menu.selected()==0&&menu.count()==3,"photo return retains cursor and original batch");
        check(!menu.move(-1)&&menu.move(1)&&menu.magazine_index()==14,"native left edge clamps and right selects next cover");
        check(menu.accept(catalog,batch,error)&&menu.viewing(),error);photo("batch-civic-photo.ppm",batch,menu);
        check(menu.back(catalog,batch,error)&&menu.move(1)&&!menu.move(1),"native right edge clamps");
        check(menu.back(catalog,batch,error)&&batch.page==CareerPage::menu,"batch skip returns to Underground");
        check(std::all_of(batch.magazine_covers.begin(),batch.magazine_covers.end(),[](const auto& c){return c.seen;})&&batch.magazine_random==original_random,"batch acknowledged once without changing random or snapshots");
        check(catalog.refresh_magazines(batch,true,error)&&batch.page==CareerPage::menu&&
            std::all_of(batch.magazine_covers.begin(),batch.magazine_covers.end(),[](const auto& c){return c.seen;}),"acknowledged delivery cannot be issued again");
        auto skip=starter;menu.reset(skip);check(menu.back(catalog,skip,error)&&skip.magazine_covers[0].seen,"back on initial notice skips whole delivery");
        check(catalog.enter_magazine_album(batch,error)&&batch.page==CareerPage::magazine_album,error);menu.reset(batch);
        check(menu.album()&&!menu.notice()&&!menu.viewing()&&menu.count()==27,"separate album enters original owned/locked grid");
        capture("album-list.ppm",batch,menu);check(menu.accept(catalog,batch,error)&&menu.viewing(),error);
        check(menu.back(catalog,batch,error)&&!menu.viewing(),error);
        check(menu.move(1)&&!menu.earned(menu.selected()),"album retains locked entries");
        check(menu.accept(catalog,batch,error)&&!menu.viewing(),"locked entry cannot request a photo");
        check(menu.move_vertical(1)&&menu.selected()==3,"album two-column vertical input");
        check(menu.back(catalog,batch,error)&&batch.page==CareerPage::menu,"album closes without replaying award notice");
        check(catalog.enter_magazine_album(batch,error),error);menu.reset(batch);check(menu.album()&&!menu.notice(),"repeated album entry remains album");
        check(menu.back(catalog,batch,error),error);
        std::cout<<"PASS batch selection, saved-car photos, skip, album and reentry\n";
        for(unsigned cell=0;cell<27;++cell)for(unsigned action=0;action<4;++action) {
            auto grid=batch;grid.page=CareerPage::magazine_album;
            CareerMagazineMenu cursor;cursor.reset(grid);
            check(cursor.selected()==0&&cursor.first_visible()==0,"native album constructor starts at first cell, even if locked");
            for(unsigned row=0;row<cell/2;++row)check(cursor.move_vertical(1),"reach native album row from real input");
            if(cell%2)check(cursor.move(1),"reach native album second column from real input");
            check(cursor.selected()==cell&&cursor.first_visible()==kNativeMagazineAlbumFirst[cell][0],"native scrolling window before grid direction");
            const auto before_revision=cursor.revision();
            const unsigned expected=kNativeMagazineAlbumMoves[cell][action];
            const bool moved=action<2?cursor.move(action==0?-1:1):cursor.move_vertical(action==2?-1:1);
            const std::string location="album cell="+std::to_string(cell+1)+" action="+std::to_string(action);
            check(cursor.selected()==expected,"native row/column boundary "+location);
            const unsigned expected_first=kNativeMagazineAlbumFirst[cell][action+1];
            check(cursor.first_visible()==expected_first,"native scrolling window after grid direction "+location);
            check(moved==(expected!=cell)&&cursor.revision()==before_revision+unsigned(moved),"only actual movement changes cursor revision "+location);
        }
        auto bottom=batch;bottom.page=CareerPage::magazine_album;menu.reset(bottom);
        for(unsigned row=0;row<12;++row)check(menu.move_vertical(1),"reach last full album row");
        check(menu.move(1)&&menu.selected()==25&&menu.first_visible()==18,"cover26 at bottom of penultimate window");
        check(menu.move_vertical(1)&&menu.selected()==26&&menu.first_visible()==20,"native scroll plus UI update selects last valid cover");
        check(menu.move_vertical(-1)&&menu.move(1)&&menu.selected()==25&&menu.first_visible()==20,"same cover26 in final window");
        check(!menu.move_vertical(1)&&menu.selected()==25,"cover26 Down in final window stays on cover26");
        check(menu.move(-1)&&menu.move_vertical(1)&&menu.selected()==26&&menu.first_visible()==20,"last row preserves first column");
        check(!menu.move(1)&&!menu.move_vertical(1),"cover27 cannot enter an absent right or lower cell");
        capture("album-last-row.ppm",bottom,menu);
        auto task_only=batch;task_only.page=CareerPage::magazine_album;
        task_only.magazine_covers.resize(1);task_only.magazine_covers[0].index=14;
        menu.reset(task_only);check(menu.selected()==0&&!menu.earned(0),"native album starts on locked first cell instead of first earned cover");
        std::cout<<"PASS all108 native visible album directions, row scroll and final-row update\n";
        const auto legacy=[&](CareerProfile value,const std::string& name){
            const auto stage=temporary/(name+"-stage");check(save_career(stage,catalog,value,error),error);
            auto body=read(stage.string()+".0");body.resize(body.rfind("SHA256 "));
            check(body.rfind("NFSU_CAREER 14",0)==0,"current writer schema14");body.replace(0,14,"NFSU_CAREER 13");
            body+="SHA256 "+sha256_hex(sha256(body.data(),body.size()))+'\n';
            const auto base=temporary/name;write(base.string()+".0",body);return std::make_pair(base,body);
        };
        auto old=starter;old.page=CareerPage::magazines;old.magazine_covers[0].seen=true;
        auto old_album=legacy(old,"old-album");CareerProfile loaded;
        check(load_career(old_album.first,catalog,loaded,error)&&loaded.page==CareerPage::magazine_album,"v13 all-seen page10 resumes separate album");
        check(save_career(old_album.first,catalog,loaded,error),error);
        check(read(old_album.first.string()+".before-v14.0")==old_album.second,"v13 rollback backup remains byte exact");
        old=starter;old.page=CareerPage::menu;task=snapshot;task.index=14;task.seen=true;old.magazine_covers.push_back(task);
        const auto partial=legacy(old,"old-partial");check(load_career(partial.first,catalog,loaded,error)&&loaded.page==CareerPage::magazines,"v13 menu recovers dropped unread batch");
        menu.reset(loaded);check(menu.notice()&&menu.count()==1&&menu.magazine_index()==snapshot.index,"resume excludes previously viewed cover");
        const auto current=temporary/"current";check(save_career(current,catalog,loaded,error)&&load_career(current,catalog,loaded,error),error);
        menu.reset(loaded);check(menu.notice()&&menu.count()==1,"new unread save resumes pending notice");
        check(menu.accept(catalog,loaded,error)&&menu.accept(catalog,loaded,error),error);
        check(save_career(current,catalog,loaded,error)&&load_career(current,catalog,loaded,error)&&loaded.page==CareerPage::menu,"fully viewed new delivery does not reopen on load");
        const auto album_save=temporary/"album";check(catalog.enter_magazine_album(loaded,error)&&save_career(album_save,catalog,loaded,error)&&load_career(album_save,catalog,loaded,error)&&loaded.page==CareerPage::magazine_album,"new album page persists separately");
        std::cout<<"PASS legacy/new save recovery and rollback backup\n";
        auto priority=starter;priority.page=CareerPage::unique_reward;priority.pending_unique=15;priority.movie_event=16;priority.movie_phase=2;
        check(catalog.valid(priority,error)&&catalog.pending_movie(priority).empty(),"unique precedes movie");
        menu.reset(priority);check(!menu.active()&&!menu.accept(catalog,priority,error),"unique cannot enter magazine handler");
        check(!catalog.enter_magazine_album(priority,error),"unique cannot be bypassed by album entry");
        const auto& parts=catalog.tuning().parts().records;
        const auto hood=std::find_if(parts.begin(),parts.end(),[&](const auto& part){return part.slot==8&&part.upgrade_level==4&&catalog.tuning().find_part(priority.car,8,part.part_name_hash)==&part;});
        check(hood!=parts.end()&&catalog.award_unique(priority,12,hood->part_name_hash,error),error);
        check(priority.page==CareerPage::magazines&&!catalog.pending_movie(priority).empty(),"unique continuation retains pending movie before magazines");
        menu.reset(priority);check(!menu.accept(catalog,priority,error),"movie prevents premature magazine confirmation");
        check(catalog.movie_finished(priority,error)&&menu.accept(catalog,priority,error)&&!menu.notice(),"movie acknowledgement unlocks magazine list");
        std::cout<<"PASS unique/movie/magazine precedence\n";
        check(std::filesystem::equivalent(temporary.parent_path(),temp)&&temporary.filename().string().rfind("nfsu-magazine-flow-",0)==0,"owned cleanup boundary");
        std::filesystem::remove_all(temporary);
        std::cout<<"PASS career magazine menu and snapshot production path\n";
    }catch(const std::exception& e){std::cerr<<"FAIL magazine menu: "<<e.what()<<'\n';if(!temporary.empty())std::cerr<<"fixture retained: "<<temporary.filename().string()<<'\n';return 1;}
}
