#include "nfsu/frontend_glow_fast.h"
#include "nfsu/frontend.h"
#include "nfsu/frontend_scene.h"
#include "nfsu/track.h"
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
#include <cstring>
#include <stdexcept>
constexpr float native_floor_sprite[4][3]={{-6.0781331062316895F,-1.393865942955017F,-26.813892364501953F},
                {-8.132206916809082F,.6465035676956177F,-23.452316284179688F},
                {-7.844092845916748F,.4596937894821167F,5.491459846496582F},
                {-5.790019512176514F,-1.580675721168518F,2.1298818588256836F}};
constexpr float native_floor_sprite1280[4][3]={
    {-6.409250259399414F,-1.0463234186172485F,-20.75663948059082F},
    {-7.693046569824219F,.22890740633010864F,-18.65565299987793F},
    {-7.512975692749023F,.11215132474899292F,-.5657939910888672F},
    {-6.229179382324219F,-1.1630796194076538F,-2.666780471801758F}};
void check(bool value,const char* message) { if(!value) throw std::runtime_error(message); }
#include "frontend_browser_camera_samples.inc"
#include "frontend_garage_camera_samples.inc"
#include "frontend_manual_camera_samples.inc"
#include "post_race_selector_native.inc"
#include "post_race_speech_native.inc"
int main(int argc,char** argv) {
    using namespace nfsu;
    try {
        {
            FrontendMenu menu(true);FrontendInput down;down.down=true;FrontendInput accept;accept.accept=true;
            menu.update(down);check(menu.update(accept)==FrontendAction::free_drive,"LAN menu preserves Quick Race");
            menu.update(down);check(menu.update(accept)==FrontendAction::multiplayer,"Multiplayer row opens LAN");
            menu.update(down);check(menu.update(accept)==FrontendAction::underground,"LAN menu wraps back to Underground");
            FrontendMenu with_options(true,true);
            with_options.update(down);with_options.update(down);
            check(with_options.update(accept)==FrontendAction::options,"Options opens before LAN");
            with_options.update(down);
            check(with_options.update(accept)==FrontendAction::multiplayer,"Options leaves LAN reachable");
            FrontendMenu with_statistics(true,true,true);
            for(auto expected:{FrontendAction::underground,FrontendAction::free_drive,FrontendAction::statistics,
                               FrontendAction::options,FrontendAction::multiplayer,FrontendAction::underground}) {
                check(with_statistics.update(accept)==expected,"Statistics preserves the authored main-menu order");
                with_statistics.update(down);
            }
        }
        {
            RaceLoadingTips tips;
            check(tips.prologue()==2,"native prologue picks the input card");
            tips.shown(2);check(tips.prologue()==0,"native prologue card is capped at one showing");
            CareerProfile profile;RaceEventRecord event;
            const unsigned general[]={14,15,16,17,18,14};
            for(unsigned expected:general) {
                const auto selected=tips.career(event,profile);
                check(selected==expected,"native equal-priority loading hint rotation");
                tips.shown(selected);
            }
            event.type=unsigned(RaceType::drag);
            check(tips.career(event,profile)==20,"native drag tutorial wins a tie");
            tips.shown(20);
            check(tips.career(event,profile)==4,"native drag steering follows tutorial");
            event.kind=1;
            check(tips.career(event,profile)==9,"native tournament tip outranks the race mode");
            check(tips.quick(QuickRaceMode::lap_knockout,0)==7,"native Quick Race knockout selection");
            check(tips.quick(QuickRaceMode::lap_knockout,1)==8,"native new nitrous tier takes priority");
            for(unsigned i=0;i<8;++i) {
                check(tips.current()==8,"native retained nitrous hint survives reload");
                tips.shown(8);
            }
            check(tips.current()==7,"native limit drops the retained nitrous hint after eight views");
            check(tips.quick(QuickRaceMode::lap_knockout,1)==7,"native nitrous high-water preserves prior tier");
        }
        check(audio::original_music_next(3,{100,200,300},0)==3,"unplayed music does not advance");
        check(audio::original_music_next(0,{100,200,300},99)==1&&
              audio::original_music_next(0,{100,200,300},100)==2&&
              audio::original_music_next(0,{100,200,300},301)==0,
              "playlist continues after actual unequal song boundaries");
        {
            FrontendChyron banner;banner.show(100);
            check(banner.update(20899)==FrontendChyron::Action::none && banner.visible(),"native Chyron20799 hold boundary");
            check(banner.update(20900)==FrontendChyron::Action::hide && banner.visible(),"native Chyron20800 starts Hide");
            check(banner.update(24899)==FrontendChyron::Action::none && banner.visible(),"native Chyron24799 hide grace");
            check(banner.update(24900)==FrontendChyron::Action::close && !banner.visible(),"native Chyron24800 closes package");
            banner.show(30000);check(banner.update(30000)==FrontendChyron::Action::none && banner.visible(),"fresh Chyron announces after close");
            FrontendChyron late;late.show(0);
            check(late.update(40000)==FrontendChyron::Action::hide && late.visible(),"native late update starts Hide before close branch");
            check(late.update(40000)==FrontendChyron::Action::close && !late.visible(),"native late next update closes package");
            FrontendChyron wrap;wrap.show(0xfffffff0U);
            check(wrap.update(0x50)==FrontendChyron::Action::none && wrap.visible(),"original unsigned tick subtraction crosses wrap");
            FrontendMenu metadata;const auto revision=metadata.song_revision();
            metadata.song(metadata.artist().c_str(),metadata.title().c_str(),metadata.album().c_str());
            check(metadata.song_revision()==revision,"same per-frame metadata does not restart announcement");
            metadata.announce_song();check(metadata.song_revision()!=revision,"same song restarted after reentry announces again");
        }
        {
            for(const auto& c:kNativeStatsSelector) {
                PostRaceMenu native(false,false,0,c.count);native.update({true});native.update({false,false,true});
                FrontendInput input;input.right=true;for(unsigned i=1;i<c.before;++i)native.update(input);
                const auto before=native.racer();input.right=c.direction>0;input.left=c.direction<0;native.update(input);
                check(native.racer()+1==c.after&&unsigned(native.racer()!=before)==c.cues&&unsigned(native.cue())==c.notification,
                      "original49CBD0 selected racer, sound admission and LEFT/RIGHT notification identity");
            }
            {
                FrontendInput accept;accept.accept=true;FrontendInput back;back.back=true;FrontendInput left;left.left=true;
                PostRaceMenu loss(true,true,0,4,2);loss.animate();
                loss.update(back);check(loss.page()==PostRacePage::results&&loss.cue()==PostRaceCue::none,"first results page rejects Back silently");
                loss.update(accept);check(loss.fading()&&loss.page()==PostRacePage::results,"native results waits for authored FadeOut");
                loss.advance(.1);loss.update(accept);check(loss.fading()&&loss.cue()==PostRaceCue::none,"input during fade cannot skip another page or repeat sound");
                loss.advance(.07);check(loss.page()==PostRacePage::style,"loss still presents earned style after results");
                loss.update(back);check(loss.page()==PostRacePage::results&&loss.cue()==PostRaceCue::back,"native49FBC0 returns to previous package");
                loss.update(accept);loss.advance(.17);loss.update(accept);
                check(loss.page()==PostRacePage::menu,"loss without unlocks has no victory reward page");
                loss.update(accept);check(loss.confirming()&&!loss.confirmed()&&loss.dialog_sound(),"native continue dialog defaults to Cancel");
                check(loss.update(accept)==PostRaceAction::none&&loss.page()==PostRacePage::menu,"default cancel cannot leave results");
                loss.update(accept);loss.update(left);
                check(loss.update(accept)==PostRaceAction::leave,"continue requires explicit OK");
                PostRaceMenu rewards(true,true,2);rewards.update(accept);rewards.update(accept);rewards.update(accept);
                check(rewards.reward_index()==1,"second reward is a separate queue entry");rewards.update(back);
                check(rewards.page()==PostRacePage::reward&&rewards.reward_index()==0,"Back preserves reward queue order");rewards.update(back);
                check(rewards.page()==PostRacePage::style,"Back from first reward returns to style");
                PostRaceMenu ongoing(false,false,0);ongoing.tournament(true);
                check(ongoing.count()==5,"native ongoing tournament has five context actions");
                for(unsigned i=0;i<4;++i)ongoing.update({true});ongoing.update(accept);
                check(ongoing.confirmation()==PostRaceAction::next_race&&!ongoing.confirmed(),"Next Race has its own native confirmation");
                ongoing.update(left);check(ongoing.update(accept)==PostRaceAction::next_race,"next race is not quit or restart");
                PostRaceMenu final(false,false,0);final.tournament(false);
                check(final.count()==4,"completed tournament has no next race action");
                final.update({true});final.update({true});final.update(accept);final.update(left);
                check(final.update(accept)==PostRaceAction::restart_tournament,"Restart Tournament is distinct from Restart Race");
            }
            for(const auto& native:kNativePostSpeech) {
                check(audio::post_race_speech(native.event,true)==native.request,"original4CD3E0 event/speech request table");
                check(audio::post_race_speech(native.event,false)==-1,"native49BA10 never requests victory speech after a loss");
            }
            {
                audio::SpeechMusicDuck relative;std::vector<std::int16_t> pcm(96000*2,10000);
                relative.apply(pcm.data(),1,false,true);check(pcm[0]==10000,"relative speech duck preserves existing race music master");
                relative.apply(pcm.data(),12000,true,true);check(pcm[23998]>0&&pcm[23998]<10000,"speech attacks with original envelope while music stays audible");
                std::fill(pcm.begin(),pcm.end(),10000);relative.apply(pcm.data(),96000,false,true);
                check(pcm.back()==10000,"speech release restores the unchanged race music master");
            }
            RacePauseMenu tournament(false,true);check(tournament.tournament()&&!tournament.practice(),"tournament confirmation context is distinct from practice");
            RacePauseMenu pause;
            check(pause.update({false,false,false,false,false,true})==RacePauseAction::resume,"pause back resumes, never exits race");
            pause.open();pause.update({false,true});pause.update({false,false,false,false,true});
            check(pause.page()==RacePausePage::restart&&!pause.confirmed(),"restart requires explicit confirmation");
            check(pause.update({false,false,false,false,true})==RacePauseAction::none&&pause.page()==RacePausePage::menu,"default cancel preserves active race");
            pause.update({false,false,false,false,true});pause.update({false,false,true});
            check(pause.update({false,false,false,false,true})==RacePauseAction::restart,"restart only after OK");
            pause.open();for(unsigned i=0;i<3;++i)pause.update({false,true});
            pause.update({false,false,false,false,true});check(pause.page()==RacePausePage::leave,"leave has separate dialog");
            pause.update({false,false,false,false,false,true});check(pause.page()==RacePausePage::menu,"cancel leave restores pause menu");
            PostRaceMenu placed(false,false,0,4,2);check(placed.racer()==2,"native49BEE0 initializes the player's result-order row");
            placed.update({true});placed.update({false,false,true});FrontendInput choose;choose.right=true;placed.update(choose);
            placed.update({false,false,false,true});placed.update({false,false,true});
            check(placed.statistics()&&placed.racer()==2,"fresh statistics controller returns to the human row");
            PostRaceMenu post(false,false,0,4);post.update({true});post.update({false,false,true});
            check(post.statistics()&&post.racer()==0,"statistics opens at human row");
            FrontendInput right;right.right=true;for(unsigned i=0;i<8;++i)post.update(right);
            check(post.racer()==3,"native49CBD0 racer upper clamp");
            FrontendInput left;left.left=true;for(unsigned i=0;i<8;++i)post.update(left);
            check(post.racer()==0,"native49CBD0 racer lower clamp");
        }
        {
            Frame reflected,output;RenderLimits limits;std::string error;
            const unsigned char reflected_color[4]={100,80,60,255};
            const unsigned char black[4]={0,0,0,255};
            check(reflected.reset(1,1,reflected_color,limits,error),"synthetic original floor target");
            check(output.reset(8,8,black,limits,error),"floor equation target");
            RenderTexture texture;texture.width=texture.height=1;texture.valid=true;
            texture.rgba={128,64,32,128};RasterMaterial state;state.texture=&texture;
            state.frontend_floor=true;state.reflected_scene=&reflected;
            state.alpha_test=true;state.alpha_ref=170;
            RenderOptions options;options.vertex_color=true;RenderStats stats;
            Rasterizer raster(output,options,limits,stats);
            ClipVertex triangle[3];triangle[0].x=-1;triangle[0].y=-1;
            triangle[1].x=1;triangle[1].y=-1;triangle[2].x=-1;triangle[2].y=1;
            for(auto& vertex:triangle) {vertex.z=.5;vertex.r=64;vertex.g=128;vertex.b=192;vertex.a=32;}
            check(raster.draw_triangle(triangle,state,0,error),"actual-source native floor equation");
            check(stats.fragments_written!=0,"native floor pixelalpha1 passes alpha test despite diffusealpha128");
            // Independent worldreflect.efx arithmetic, including original
            // vertex mask32 (undoubled) and final UNORM rounding.
            const unsigned char native_pixel[4]={71,69,52,255};
            for(std::size_t i=0;i<output.ids().size();++i)if(output.ids()[i]!=kNoTriangle)
                for(unsigned channel=0;channel<4;++channel)
                    check(output.rgba()[i*4+channel]==native_pixel[channel],
                        "native float floor modulation and independent reflection-alpha mask");
        }
        {
            // Independent ps_1_1 register limits followed by the literal
            // car.efx pixel product. A negative lighting color cannot
            // subtract from a positive highlight after COLOR saturation.
            Frame output;RenderLimits limits;std::string error;
            const unsigned char black[4]={0,0,0,255};
            check(output.reset(8,8,black,limits,error),"FE color register target");
            RenderTexture texture;texture.width=texture.height=1;texture.valid=true;
            texture.rgba={128,128,128,128};RasterMaterial state;state.texture=&texture;
            state.car_shading=state.frontend_lighting=true;
            RenderOptions options;RenderStats stats;Rasterizer raster(output,options,limits,stats);
            ClipVertex triangle[3];triangle[0].x=-1;triangle[0].y=-1;
            triangle[1].x=1;triangle[1].y=-1;triangle[2].x=-1;triangle[2].y=1;
            const double inputs[7]={-1,.25,3,.5,-.5,1.5,3};
            for(auto& vertex:triangle){vertex.z=.5;std::copy_n(inputs,7,vertex.surface);}
            check(raster.draw_triangle(triangle,state,0,error),"FE saturated COLOR pixel product");
            check(stats.fragments_written!=0,"FE register test draws real fragments");
            const unsigned char expected[3]={128,32,255};
            for(std::size_t i=0;i<output.ids().size();++i)if(output.ids()[i]!=kNoTriangle)
                for(unsigned channel=0;channel<3;++channel)
                    check(output.rgba()[i*4+channel]==expected[channel],"ps_1_1 COLOR limits precede texture/specular product");
        }
        {
            #include "frontend_magazine_camera_native.inc"
            for(const auto& sample:kNativeMagazineCameras){
                FrontendBrowserCamera camera;camera.magazine(sample.values);const auto frame=camera.frame(0,1280,720);const auto& v=sample.matrix;
                const Vec3 actual[3]={frame.right,frame.up,frame.forward};
                const Vec3 wanted[3]={{v[0],v[8],-v[4]},{-v[1],-v[9],v[5]},{v[2],v[10],-v[6]}};
                for(unsigned j=0;j<3;++j)check(std::abs(actual[j].x-wanted[j].x)<.00005&&std::abs(actual[j].y-wanted[j].y)<.00005&&std::abs(actual[j].z-wanted[j].z)<.00005,"native magazine camera basis including roll");
                const double translation[3]={-(frame.right.x*frame.eye.x+frame.right.y*frame.eye.y+frame.right.z*frame.eye.z),
                    frame.up.x*frame.eye.x+frame.up.y*frame.eye.y+frame.up.z*frame.eye.z,
                    -(frame.forward.x*frame.eye.x+frame.forward.y*frame.eye.y+frame.forward.z*frame.eye.z)};
                for(unsigned j=0;j<3;++j)check(std::abs(translation[j]-v[12+j])<.0005,"native magazine camera translation");
            }
        }
        // Independent original 542C20/544390 witness: actual mode-0 CView
        // and whole-car root. Shader expectations below follow authenticated
        // car.efx equations, with supplied startup-light boundary state.
        const double raw_eye[3]={2.7437283992767334,1.8576174974441528,.7651561498641968};
        float placement[16]={1,0,0,0,0,1,0,0,0,0,1,0,0,0,.20027998089790344F,1};
        float eye[4];frontend_local_eye(placement,raw_eye,eye);
        const double native_eye[4]={2.269277334213257,1.536394476890564,.46719667315483093,1};
        for(unsigned i=0;i<4;++i)check(std::abs(eye[i]-native_eye[i])<1e-6,"native FE clamped LocalEyePos");
        CarShadingMaterial material;material.gates[1]=1;
        for(unsigned i=0;i<3;++i) {
            material.diffuse_min[i]=.2F;material.diffuse_range[i]=.4F;
            material.specular_min[i]=.3F;material.specular_range[i]=.5F;
        }
        material.diffuse_min[3]=.6F;material.diffuse_range[3]=.2F;
        material.specular_min[3]=2;
        const float position[3]={0,0,0};
        const float normals[3][3]={{0,0,1},{0,0,-1},{2,0,0}};
        const double expected[3][3]={
            {.16250151481665143,.00020057602170786372,.6336112707115158},
            {-.009559216976165772,5.485728244575609e-10,.602},
            {.5759352854850635,.14024445869762775,.9265147171733882}};
        // Original4AE1F0/5411B0/542E40/544390 populated-state witness.
        // RGB/lateral/attenuation are deliberately nonzero test inputs,
        // not retail defaults; all channel vectors are independently native.
        const float marker_positions[4][3]={
            {41.37820053100586F,-1.0800316333770752F,13.673964500427246F},
            {16.810354232788086F,-1.0800316333770752F,13.673964500427246F},
            {-9.519950866699219F,-1.0800316333770752F,13.673964500427246F},
            {-35.60659408569336F,-1.0800316333770752F,13.673964500427246F}};
        const float rgb[3]={17,39,71},attenuation[3]={.4F,.03F,.001F},car_position[3]={0,0,.20027998089790344F};
        FrontendRoad light_road;light_road.offset=99;std::array<FrontendPointLight,32> pool;
        const auto count=frontend_road_lights(light_road,marker_positions,.125F,rgb,attenuation,pool);
        check(count==20,"native moving road fills20 of32 slots");
        check(std::abs(pool[0].position[0]-142.37820434570312)<.00002 &&
            std::abs(pool[5].position[0]-17.810354232788086)<.00002 &&
            std::abs(pool[6].position[0]+8.519950866699219)<.00002 &&
            std::abs(pool[5].position[1]+.9550316333770752)<.000001 &&
            std::abs(pool[5].position[2]-13.964964866638184)<.000001,
            "native moving pool includes original lateral, vertical and segment offsets");
        const float native_h[10][4]={
            {.37331944704055786F,.3836084306240082F,.39857423305511475F,.4948277175426483F},
            {-.03231409192085266F,-.027825508266687393F,-.02129666693508625F,-.03618179261684418F},
            {.03231409192085266F,.027825508266687393F,.02129666693508625F,.03618179261684418F},
            {-.06558123975992203F,-.050790976732969284F,-.02927788533270359F,-.06640803813934326F},
            {-.028355663642287254F,-.027030684053897858F,-.025103433057665825F,-.03498077765107155F},
            {.028719954192638397F,.009623236022889614F,-.018153807148337364F,.013481257483363152F},
            {.09651527553796768F,.09517118334770203F,.09321616590023041F,.12297910451889038F},
            {.22620627284049988F,.2138376086950302F,.19584684073925018F,.27683430910110474F},
            {.1727355718612671F,.17082083225250244F,.16803576052188873F,.22070521116256714F},
            {.3578336238861084F,.3854305148124695F,.42557138204574585F,.4962257146835327F}};
        const auto populated=frontend_point_lighting(pool.data(),count,car_position);
        for(unsigned term=0;term<10;++term)for(unsigned channel=0;channel<4;++channel)
            check(std::abs(populated.harmonic[term][channel]-native_h[term][channel])<1e-6,
                "all40 populated moving road harmonic values match genuine native consumer");
        const auto lighting=frontend_startup_lighting();
        for(unsigned n=0;n<3;++n) {
            double surface[7];frontend_surface(lighting,material,position,normals[n],eye,surface);
            for(unsigned i=0;i<3;++i) {
                check(std::abs(surface[i]-expected[n][0])<1e-6,"native harmonic diffuse without normal flip");
                check(std::abs(surface[i+3]-expected[n][1])<1e-6,"native harmonic specular without upper dot clamp");
            }
            check(std::abs(surface[6]-expected[n][2])<1e-6,"native FE diffuse alpha remains separate");
        }
        // Independently executed544390/542400/541C90 with the same native
        // FE input and quarter-turn wheel placement. This coefficient table
        // is native output, not the source polynomial transformation.
        FrontendLighting native_wheel;
        const float native_wheel_h[10][4]={
            {0.35557228326797485F,0.35557228326797485F,0.35557228326797485F,0.49166592955589294F},
            {-0.074945211410522461F,-0.074945211410522461F,-0.074945211410522461F,-0.10363014787435532F},
            {0.034823097288608551F,0.034823097288608551F,0.034823097288608551F,0.048151478171348572F},
            {-0.034823097288608551F,-0.034823097288608551F,-0.034823097288608551F,-0.048151478171348572F},
            {-0.094938158988952637F,-0.094938158988952637F,-0.094938158988952637F,-0.13127529621124268F},
            {-0.042310785502195358F,-0.042310785502195358F,-0.042310785502195358F,-0.058505043387413025F},
            {-0.028591757640242577F,-0.028591757640242577F,-0.028591757640242577F,-0.039535120129585266F},
            {-0.32748597860336304F,-0.32748597860336304F,-0.32748597860336304F,-0.45282971858978271F},
            {0.16954396665096283F,0.16954396665096283F,0.16954396665096283F,0.23443613946437836F},
            {0.22944235801696777F,0.22944235801696777F,0.22944235801696777F,0.31726035475730896F},
        };
        for(unsigned t=0;t<10;++t)for(unsigned c=0;c<4;++c)
            native_wheel.harmonic[t][c]=native_wheel_h[t][c];
        const float quarter_wheel[16]={0,0,-1,0,0,1,0,0,1,0,0,0,0,0,.20027998089790344F,1};
        bool old_basis_differs=false;
        for(const auto& normal:normals) {
            double original[7],placed[7],old[7];
            frontend_surface(native_wheel,material,position,normal,eye,original);
            frontend_surface(lighting,material,position,normal,eye,placed,quarter_wheel);
            frontend_surface(lighting,material,position,normal,eye,old);
            for(unsigned c=0;c<7;++c) {
                check(std::abs(original[c]-placed[c])<2e-6,"placed FE wheel lighting matches independently native rotated harmonic");
                old_basis_differs |= std::abs(original[c]-old[c])>.001;
            }
        }
        check(old_basis_differs,"shared unrotated FE harmonic is a negative control for rolling wheel highlights");
        {
            #include "frontend_camera_lighting_native.inc"
            const float origin[3]={0,0,.20027998089790344F};
            bool changes=false;
            for(const auto& sample:kNativeCameraLights) {
                const auto* v=sample.view;CameraFrame camera;
                camera.right={v[0],v[8],-v[4]};camera.up={-v[1],-v[9],v[5]};camera.forward={v[2],v[10],-v[6]};
                const auto actual=frontend_point_lighting(nullptr,0,origin,&camera);
                for(unsigned t=0;t<10;++t)for(unsigned c=0;c<4;++c) {
                    check(std::abs(actual.harmonic[t][c]-sample.harmonic[t*4+c])<2e-6,"active CView FE lighting matches native542E40/544390");
                    changes|=std::abs(actual.harmonic[t][c]-lighting.harmonic[t][c])>.001;
                }
            }
            check(changes,"fixed startup lights are a negative control for changed garage cameras");
        }
        const double cube_directions[6][3]={{1,.25,.5},{-1,.25,.5},{.2,1,.6},{.2,-1,.6},{.2,.4,1},{.2,.4,-1}};
        // Asymmetric projected markers from original40AD60 native face bases,
        // not the appearance of a symmetric tunnel. Native Y faces differ.
        const double cube_uv[6][2]={{.25,.375},{.75,.375},{.6,.8},{.6,.2},{.6,.3},{.4,.3}};
        for(unsigned expected_face=0;expected_face<6;++expected_face) {
            unsigned face;double u,v;
            check(frontend_cube_coordinates(cube_directions[expected_face],face,u,v) && face==expected_face &&
                std::abs(u-cube_uv[expected_face][0])<1e-9 && std::abs(v-cube_uv[expected_face][1])<1e-9,
                "native asymmetric cube marker handedness and orientation");
            for(unsigned corner=0;corner<4;++corner) {
                double edge[3]{};unsigned component=0;
                for(unsigned axis=0;axis<3;++axis)if(axis!=expected_face/2)
                    edge[axis]=(corner&(1U<<component++))?1.:-1.;
                edge[expected_face/2]=expected_face&1?-1.000001:1.000001;
                check(frontend_cube_coordinates(edge,face,u,v) && face==expected_face,
                    "native cube edge keeps intended face");
                for(double coordinate:{u,v}) {
                    const double pixel=coordinate*128-.5;
                    const auto lo=static_cast<unsigned>(std::floor(pixel));
                    const auto hi=std::min(lo+1,127U);
                    check(pixel>=0 && pixel<=127 && lo<=127 && hi<=127,
                        "all bilinear atlas edge taps stay inside original128-square face");
                    check(coordinate==.5/128 || coordinate==127.5/128,
                        "face corners use native edge texel clamp instead of adjacent atlas tile");
                }
            }
        }
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
        {
            // Independent genuine5443F0 stack quad, actual view4 road marker.
            const float point[3]={-9.519950866699219F,-1.0800316333770752F,-13.673964500427246F};
            const float eye_direction[3]={.63970947265625F,.15323638916015625F,.753186821937561F};
            const float right[3]={-.7047351002693176F,.7000333666801453F,.11533286422491074F};
            const float up[3]={-.0988493338227272F,.06409282982349396F,-.9930362105369568F};
            FrontendRoadGlow quad;
            check(frontend_road_glow(point,eye_direction,right,up,.7531868815422058F,
                .017617058008909225F,2,quad),"native floor streetlamp billboard");
            for(unsigned i=0;i<4;++i)for(unsigned axis=0;axis<3;++axis)
                check(std::abs(quad.corners[i][axis]-native_floor_sprite[i][axis])<.000004F,
                    "all native floor sprite corners and vertical stretch");
            check(quad.rgba[0]==110 && quad.rgba[1]==100 && quad.rgba[2]==75 &&
                quad.rgba[3]==140,"native descriptor interpolation and truncation");
            check(!frontend_road_glow(point,eye_direction,right,up,1,1,3,quad),
                "unknown sprite view refuses publication");
        }
        {
            // Genuine5447C0 ->5443F0 ->40C300, main CView and supplied marker.
            const float point[3]={1.5F,-.6000000238418579F,.700279951095581F};
            const float eye[3]={.4514164924621582F,.8920027017593384F,.023547090590000153F};
            const float right[3]={-.7047351002693176F,.7000333666801453F,.11533286422491074F};
            const float up[3]={-.0988493338227272F,.06409282982349396F,-.9930362105369568F};
            const float expected[3][4][3]={
                {{2.04470229149F,-.07907266915F,.383308231831F},{1.61766624451F,.345114171505F,.453194439411F},{1.67756426334F,.306276977062F,1.054927110672F},{2.10460019112F,-.11790986359F,.985040843487F}},
                {{2.11560511589F,-.153492882848F,.25360262394F},{1.52362775803F,.434535175562F,.350482225418F},{1.60666108131F,.380697220564F,1.18463265896F},{2.1986386776F,-.207330867648F,1.08775305748F}},
                {{1.81307506561F,-.655442774296F,.29742822051F},{1.29479527473F,-.140620738268F,.382246911526F},{1.36749148369F,-.187756210566F,1.112550497055F},{1.88577127457F,-.702578246593F,1.027731776237F}}};
            const unsigned char colors[3][4]={{31,37,53,25},{9,11,12,1},{57,0,0,36}};
            for(unsigned descriptor=0;descriptor<3;++descriptor) {
                FrontendRoadGlow q;check(frontend_car_glow(point,eye,right,up,eye[0],
                    .002785184420645237F,0,descriptor,1,q),"native car sprite descriptor accepted");
                for(unsigned c=0;c<4;++c) {
                    check(q.rgba[c]==colors[descriptor][c],"native car sprite all color channels");
                    for(unsigned a=0;a<3;++a)check(std::abs(q.corners[c][a]-expected[descriptor][c][a])<.000004F,
                        "native car sprite all four corners");
                }
            }
        }
        {
            FrontendBrowserCamera camera;unsigned sample=0;
            for(unsigned stage=0;stage<5;++stage) {
                if(stage==1)camera.prepare();else if(stage==2)camera.depart();else if(stage==3)camera.arrive();
                else if(stage==4)camera.tracking=false;
                for(unsigned i=0;i<(stage?91U:1U);++i) {
                    if(camera.tracking)camera.update(i?1.F/60:0);
                    const float t=float(i)/60;
                    const float x=stage==2?25*(t/1.5F)*(t/1.5F):stage==3?-15*(1-t/1.5F):0;
                    if(sample>=sizeof(browser_camera_samples)/sizeof(browser_camera_samples[0]))break;
                    const auto& expected=browser_camera_samples[sample];
                    if(expected.stage!=stage || expected.frame!=i)continue;
                    const auto frame=camera.frame(x,1280,720);
                    for(unsigned j=0;j<5;++j)check(std::abs(camera.pose[j]-expected.pose[j])<.0002F,"native browser camera interpolation");
                    const auto& v=expected.view;
                    // Native view columns use down-positive raw Z; port maps
                    // raw(x,y,z) to world(x,z,-y), with up-positive screen Y.
                    const Vec3 basis[3]={frame.right,frame.up,frame.forward};
                    const Vec3 wanted[3]={{v[0],v[8],-v[4]},{-v[1],-v[9],v[5]},{v[2],v[10],-v[6]}};
                    for(unsigned j=0;j<3;++j)check(std::abs(basis[j].x-wanted[j].x)<.00002 &&
                        std::abs(basis[j].y-wanted[j].y)<.00002 && std::abs(basis[j].z-wanted[j].z)<.00002,"native browser view basis");
                    const double translation[3]={-(frame.right.x*frame.eye.x+frame.right.y*frame.eye.y+frame.right.z*frame.eye.z),
                        frame.up.x*frame.eye.x+frame.up.y*frame.eye.y+frame.up.z*frame.eye.z,
                        -(frame.forward.x*frame.eye.x+frame.forward.y*frame.eye.y+frame.forward.z*frame.eye.z)};
                    for(unsigned j=0;j<3;++j)check(std::abs(translation[j]-v[12+j])<.0002,"native browser view translation");
                    ++sample;
                }
            }
            check(sample==sizeof(browser_camera_samples)/sizeof(browser_camera_samples[0]),"all native browser camera checkpoints");
        }
        {
            unsigned sample=0;
            for(unsigned entry=0;entry<std::size(garage_camera_samples);entry+=4) {
                const auto slot=garage_camera_samples[entry].slot;
                FrontendBrowserCamera camera;check(camera.focus(slot),"native garage slot focus exists");
                for(unsigned i=0;i<=180;++i) {
                    camera.update(i?1.F/60:0);
                    const auto& expected=garage_camera_samples[sample];
                    if(expected.slot!=slot||expected.frame!=i)continue;
                    const auto frame=camera.frame(0,1280,720);
                    for(unsigned j=0;j<5;++j)if(std::abs(camera.pose[j]-expected.pose[j])>=.001F)
                        throw std::runtime_error("garage camera pose slot="+std::to_string(slot)+" frame="+std::to_string(i)+" field="+std::to_string(j));
                    const auto& v=expected.view;
                    const Vec3 basis[3]={frame.right,frame.up,frame.forward};
                    const Vec3 wanted[3]={{v[0],v[8],-v[4]},{-v[1],-v[9],v[5]},{v[2],v[10],-v[6]}};
                    for(unsigned j=0;j<3;++j)if(std::abs(basis[j].x-wanted[j].x)>=.00003||
                        std::abs(basis[j].y-wanted[j].y)>=.00003||std::abs(basis[j].z-wanted[j].z)>=.00003)
                        throw std::runtime_error("garage camera basis slot="+std::to_string(slot)+" frame="+std::to_string(i));
                    const double translation[3]={-(frame.right.x*frame.eye.x+frame.right.y*frame.eye.y+frame.right.z*frame.eye.z),
                        frame.up.x*frame.eye.x+frame.up.y*frame.eye.y+frame.up.z*frame.eye.z,
                        -(frame.forward.x*frame.eye.x+frame.forward.y*frame.eye.y+frame.forward.z*frame.eye.z)};
                    for(unsigned j=0;j<3;++j)if(std::abs(translation[j]-v[12+j])>=.0003)
                        throw std::runtime_error("garage camera translation slot="+std::to_string(slot)+" frame="+std::to_string(i));
                    ++sample;if(sample==std::size(garage_camera_samples))break;
                }
            }
            check(sample==std::size(garage_camera_samples),"all original garage camera checkpoints");
        }
        {
            unsigned sample=0;
            for(unsigned entry=0;entry<std::size(manual_camera_samples);entry+=11) {
                const auto slot=manual_camera_samples[entry].slot;
                FrontendBrowserCamera camera;check(camera.focus(slot),"manual orbit starts from original detail preset");
                for(unsigned i=0;i<600;++i)camera.update(1.F/60);
                for(unsigned i=0;i<420;++i) {
                    camera.orbit(i<300?3.F:0.F,i<180?-1.F:i<300?1.F:0.F);
                    camera.update(1.F/60);
                    check(camera.manual&&!camera.tracking,"manual orbit stays active after releasing stick");
                    const auto frame=camera.frame(0,1280,720);
                    check(frame.eye.y>.05,"manual orbit never enters the road or near plane");
                    const auto& expected=manual_camera_samples[sample];
                    if(expected.slot!=slot||expected.frame!=i)continue;
                    float actual[11];
                    std::copy(camera.pose.begin(),camera.pose.end(),actual);
                    std::copy(camera.translation.begin(),camera.translation.end(),actual+5);
                    std::copy(camera.target.begin(),camera.target.end(),actual+8);
                    for(unsigned j=0;j<11;++j)if(std::abs(actual[j]-expected.state[j])>=.001F)
                        throw std::runtime_error("manual camera state slot="+std::to_string(slot)+" frame="+std::to_string(i)+" field="+std::to_string(j));
                    const auto& v=expected.view;
                    const Vec3 basis[3]={frame.right,frame.up,frame.forward};
                    const Vec3 wanted[3]={{v[0],v[8],-v[4]},{-v[1],-v[9],v[5]},{v[2],v[10],-v[6]}};
                    for(unsigned j=0;j<3;++j)check(std::abs(basis[j].x-wanted[j].x)<.00004&&
                        std::abs(basis[j].y-wanted[j].y)<.00004&&std::abs(basis[j].z-wanted[j].z)<.00004,"native manual orbit view basis");
                    const double translation[3]={-(frame.right.x*frame.eye.x+frame.right.y*frame.eye.y+frame.right.z*frame.eye.z),
                        frame.up.x*frame.eye.x+frame.up.y*frame.eye.y+frame.up.z*frame.eye.z,
                        -(frame.forward.x*frame.eye.x+frame.forward.y*frame.eye.y+frame.forward.z*frame.eye.z)};
                    for(unsigned j=0;j<3;++j)check(std::abs(translation[j]-v[12+j])<.0004,"native manual orbit view translation");
                    ++sample;if(sample==std::size(manual_camera_samples))break;
                }
                check(camera.focus(slot)&&!camera.manual&&camera.tracking,"return to detail resumes original focus transition");
            }
            check(sample==std::size(manual_camera_samples),"all native manual camera checkpoints");
            FrontendBrowserCamera idle;idle.focus(31);const auto destination=idle.destination;
            idle.orbit(0,0);check(idle.tracking&&!idle.manual&&idle.destination==destination,"neutral input preserves detail transition");
        }
        check(frontend_wheel_phase(0,.231441572F,.31F)==7787 &&
              frontend_wheel_phase(0,.229F,.31F)==7705 &&
              frontend_wheel_phase(0,1.F,.31F)==33646,
              "native wheel phase uses original constants and truncation");
        check(frontend_wheel_phase(65000,1.F,.31F)==33110,
              "native wheel phase wraps 16-bit accumulator");
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
        FrontendClock frontend_clock;
        check(frontend_clock.sample(1000000000)==0,"menu clock first anchor has no elapsed animation");
        check(frontend_clock.sample(1033333333)==33333333 && frontend_clock.original_ticks()==133,
            "menu clock retains ordinary frame delta and native phase");
        check(frontend_clock.sample(62033333333ULL)==0 && frontend_clock.original_ticks()==133,
            "long applet or blocked-I/O gap does not advance menu phase or throw");
        check(frontend_clock.sample(62533333333ULL)==500000000,
            "ordinary half-second slow frame is not clamped");
        const auto resume_phase=frontend_clock.original_ticks();frontend_clock.resume();
        check(frontend_clock.sample(125533333333ULL)==0 && frontend_clock.original_ticks()==resume_phase,
            "explicit applet resume resets anchor while keeping scene phase");
        check(frontend_clock.sample(125550000000ULL)==16666667,
            "normal frames resume from fresh applet anchor");
        {
            FrontendPresentationStats stats;FrontendPresentationWindow window;
            check(!stats.sample(0,window) && !stats.sample(500000000,window) &&
                !stats.sample(1000000000,window),"presentation startup warmup emits no partial result");
            for(unsigned interval=1;interval<=120;++interval) {
                const bool ready=stats.sample(1000000000ULL+interval*16666667ULL,window);
                check(ready==(interval==120),"presentation result waits for complete two-second window");
            }
            check(window.intervals==120 && window.elapsed_ns==2000000040ULL && window.max_ns==16666667 &&
                std::abs(window.mean_ms()-16.666667)<.000001 && std::abs(window.fps()-60)<.00001,
                "normal presentation window measures interval count elapsed mean maximum and FPS");
            std::uint64_t now=3000000040ULL;
            for(unsigned interval=0;interval<6;++interval) {
                now+=interval<2?500000000:250000000;
                check(stats.sample(now,window)==(interval==5),"second presentation window is independent");
            }
            check(window.intervals==6 && window.elapsed_ns==2000000000ULL && window.max_ns==500000000 &&
                window.fps()==3,"slow normal frames remain measured, including maximum interval");
            stats.reset();
            check(!stats.sample(now+100000000000ULL,window) &&
                !stats.sample(now+101000000000ULL,window),"menu reentry restarts anchor and warmup");
            check(stats.sample(now+103500000000ULL,window) && window.intervals==1 &&
                window.elapsed_ns==2500000000ULL && window.max_ns==2500000000ULL && window.fps()==.4,
                "long actual frame is measured rather than hidden as suspension");
            check(!stats.sample(0,window),"backward clock resets presentation anchor");
        }
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
            const auto original_materials=skyline.scene.draws;
            check(bind_frontend_skyline_runtime_skin(skyline,skyline_request,error),error.c_str());
            check(original_materials.size()==skyline.scene.draws.size(),"texture-only FE rebind preserves material groups");
            bool shaded_material=false;
            for(std::size_t i=0;i<original_materials.size();++i) {
                const auto& expected=original_materials[i].shading;
                const auto& actual=skyline.scene.draws[i].shading;
                check(std::memcmp(&expected,&actual,sizeof(expected))==0,
                    "GLOBAL_SKIN1 rebind preserves decoded material gates and vectors");
                shaded_material|=actual.gates[0]!=0 && actual.gates[1]!=0 &&
                    (actual.diffuse_min[0]!=0 || actual.diffuse_range[0]!=0);
            }
            check(shaded_material,"original FE materials retain nonzero diffuse gate and vectors");
            unsigned global_draws=0;
            for(const auto& draw:skyline.scene.draws)if(draw.texture_hash==0xDDA11455U) {
                check(draw.binding==BindingKind::resolved &&
                    skyline.scene.textures.at(draw.texture).source==skyline.skin_binding.source,
                    "native GLOBAL_SKIN1 uses existing generated body surface");++global_draws;
            }
            check(global_draws!=0,"authored GLOBAL_SKIN1 material retained");
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
            FrontendGpuScene gpu_scene;
            if(!gpu_scene.load(root,1280,720,error))throw std::runtime_error(error);
            // Independent original 486ECD/487BA0/4870C0/487680 execution:
            // mode0 saved entry, VIEW inverse and raw -> world basis conversion.
            // Independently captured genuine40AD60 six input VIEW matrices,
            // raw -> world basis and original negative vertical projection.
            // FOV follows genuine CRT initializer689920 and original40DD00.
            const double native_cube_basis[6][9]={
                {0.7025555372238159,-0.02402954176068306,-0.7112331986427307,-0.0988493338227272,-0.9930362105369568,-0.06409282982349396,-0.7047401070594788,0.1153336837887764,-0.7000383734703064},
                {-0.7025555372238159,0.02402954176068306,0.7112331986427307,-0.0988493338227272,-0.9930362105369568,-0.06409282982349396,0.7047401070594788,-0.1153336837887764,0.7000383734703064},
                {-0.7047401070594788,0.1153336837887764,-0.7000383734703064,0.7025580406188965,-0.024029627442359924,-0.711235761642456,-0.09884968400001526,-0.9930397868156433,-0.06409306079149246},
                {-0.7047401070594788,0.1153336837887764,-0.7000383734703064,-0.7025580406188965,0.024029627442359924,0.711235761642456,0.09884968400001526,0.9930397868156433,0.06409306079149246},
                {-0.7047401070594788,0.1153336837887764,-0.7000383734703064,-0.0988493338227272,-0.9930362105369568,-0.06409282982349396,-0.7025555372238159,0.02402954176068306,0.7112331986427307},
                {0.7047376036643982,-0.11533327400684357,0.7000358700752258,-0.0988493338227272,-0.9930362105369568,-0.06409282982349396,0.7025530338287354,-0.024029456079006195,-0.7112306356430054}};
            const auto& cube_commands=gpu_scene.commands();
            check(!cube_commands.frontend_cube_draws.empty(),"original Y-family cube resources resident");
            check(validate_recorded_commands(cube_commands,error),error.c_str());
            for(unsigned face=0;face<6;++face) {
                const auto& frame=cube_commands.frontend_cube_frames[face];
                check(frame.projection[0]==1 && frame.projection[1]==1 &&
                    frame.projection[2]==.5F && frame.projection[3]==64,
                    "all six native cube projections use original CRT FOV and clipping planes");
                for(unsigned axis=0;axis<3;++axis)
                    check(std::abs(frame.right[axis]-native_cube_basis[face][axis])<1e-5 &&
                        std::abs(frame.up[axis]-native_cube_basis[face][3+axis])<1e-5 &&
                        std::abs(frame.forward[axis]-native_cube_basis[face][6+axis])<1e-5,
                        "all six cube capture bases match native asymmetric orientation");
            }
            const auto native_road_region=parse_track_region(front.data(),381824,0);
            const TrackMesh* native_cube_mesh=nullptr;
            for(const auto& mesh:native_road_region.meshes)if(mesh.name=="TRFE_ROAD02_Y")native_cube_mesh=&mesh;
            check(native_cube_mesh && native_cube_mesh->valid,"authenticated original Y solid is decoded");
            unsigned cube_segments=0;
            for(const auto& draw:cube_commands.frontend_cube_draws) {
                if(draw.frontend_sprite_view)continue;
                check(draw.instance<3,"original cube view uses native three translated copies");
                const auto& vertices=cube_commands.resource_table().resources.at(draw.vertex_resource);
                check(vertices.elements==native_cube_mesh->vertices.size(),"cube geometry vertex count matches original Y solid");
                const auto* decoded=reinterpret_cast<const MeshVertex*>(vertices.data);
                for(std::size_t v=0;v<vertices.elements;++v) {
                    const auto& original=native_cube_mesh->vertices[v];
                    check(decoded[v].px==original.px && decoded[v].py==original.py && decoded[v].pz==original.pz &&
                        decoded[v].u==original.u && decoded[v].v==original.v,
                        "every resident cube vertex and UV comes from authenticated Y solid");
                }
                cube_segments|=1U<<draw.instance;
                check(draw.texture_resource!=kNoHandle,"original cube diffuse texture is bound");
            }
            check(cube_segments==7,"all original cube road segments are represented");
            unsigned car_sprite_count=0,car_descriptor_mask=0;
            const std::uint32_t native_car_flare_hashes[3]={0x8868656FU,0x88D8E8A2U,0x3394FE62U};
            for(const auto& draw:cube_commands.owning_draws())if(draw.frontend_sprite_kind) {
                ++car_sprite_count;car_descriptor_mask|=1U<<(draw.frontend_sprite_kind-1);
                check(draw.frontend_sprite_kind<=3 && draw.frontend_sprite_view==1,
                    "original bounded car flare descriptors belong to primary view");
                const auto& texture=cube_commands.resource_table().resources.at(draw.texture_resource);
                check(texture.bytes!=0 && texture.texture_hash==native_car_flare_hashes[draw.frontend_sprite_kind-1],
                    "original inner outer and tail flare textures bound to actual car markers");
                check(!draw.pipeline.depth_write && draw.material.environment_range[3]==2 && draw.material.alpha[3]==8,
                    "native car flare additive no-write, original color and RGBA2x opt-in");
            }
            check(car_sprite_count==6 && car_descriptor_mask==7,
                "all original installed preset head pairs and tail markers retained");
            unsigned native_glows[6]={2,2,0,2,2,1},active_glows[6]={};
            for(const auto& draw:cube_commands.frontend_cube_draws)if(draw.frontend_sprite_kind==0 && draw.frontend_sprite_view && draw.material.alpha[0]>0)
                ++active_glows[draw.frontend_sprite_view-10];
            for(unsigned face=0;face<6;++face)check(active_glows[face]==native_glows[face],
                "native actual cube frustum streetlamp visibility per face");
            unsigned floor_glows=0,primary_glows=0;
            const RecordedDraw* native_floor_glow=nullptr;
            for(const auto& draw:cube_commands.reflections)if(draw.frontend_sprite_kind==0 && draw.frontend_sprite_view && draw.material.alpha[0]>0) {
                ++floor_glows;if(!native_floor_glow)native_floor_glow=&draw;
            }
            for(const auto& draw:cube_commands.owning_draws())if(draw.frontend_sprite_kind==0 && draw.frontend_sprite_view && draw.material.alpha[0]>0)++primary_glows;
            check(floor_glows==5 && primary_glows==0,"initial native primary point/floor box culling, not mode suppression");
            check(native_floor_glow && native_floor_glow->instance==6,"native first visible floor marker and segment");
            if(native_floor_glow) {
                const auto& resource=cube_commands.resource_table().resources.at(native_floor_glow->texture_resource);
                check(resource.texture_hash==0x3394FE62U && resource.bytes!=0,
                    "authored HEADLIGHTGLOW receives resident original pixels");
                check(!native_floor_glow->pipeline.depth_write,
                    "native sprite additive pass has no depth write");
                const int signs[4][2]={{-1,1},{1,1},{1,-1},{-1,-1}};
                for(unsigned i=0;i<4;++i)for(unsigned axis=0;axis<3;++axis) {
                    const auto& rows=native_floor_glow->placement.rows;
                    const float point=rows[12+axis]+signs[i][0]*rows[axis]+signs[i][1]*rows[8+axis];
                    check(std::abs(point-native_floor_sprite1280[i][axis])<.0002F,
                        "actual resident floor sprite placement matches all native corners");
                }
                check(std::abs(native_floor_glow->material.alpha[0]-140.F/255)<.000001F,
                    "actual native floor sprite opacity survives publication");

            }

            const TrackMesh* native_floor=nullptr;const TrackMesh* native_z=nullptr;
            for(const auto& mesh:native_road_region.meshes) {
                if(mesh.name=="TRFE_ROAD02_A")native_floor=&mesh;
                if(mesh.name=="TRFE_ROAD02_Z")native_z=&mesh;
            }
            check(native_floor && native_floor->vertex_format_flags==0x40 && native_z && native_z->valid,
                "original worldreflect selector belongs to authored base floor and Z capture is valid");
            check((cube_commands.frame.options[2]&8U)!=0 && cube_commands.reflection_draw_count()!=0,
                "native FE floor target is separately enabled beside car cube");
            const auto& floor_frame=cube_commands.frontend_floor_frame;
            check(std::abs(floor_frame.projection[0]-1.2961782217025757)<1e-6 &&
                std::abs(floor_frame.projection[1]-1.660086750984192)<1e-6,
                "original320x240 view4 projection uses shared main CView");
            for(unsigned axis=0;axis<3;++axis)
                check(floor_frame.eye[axis]==cube_commands.frame.eye[axis] &&
                    floor_frame.right[axis]==cube_commands.frame.right[axis] &&
                    floor_frame.up[axis]==cube_commands.frame.up[axis],"floor capture shares native primary pose");
            unsigned z_segments=0,floor_receivers=0;
            for(const auto& draw:cube_commands.reflections) {
                if(draw.frontend_sprite_view)continue;
                check(draw.instance<3 && draw.material.surface.gates[0]==0,
                    "native FE reflected mode3 uses three Z solids without invented mirrored car");
                z_segments|=1U<<draw.instance;
                const auto& resource=cube_commands.resource_table().resources.at(draw.vertex_resource);
                check(resource.elements==native_z->vertices.size(),"resident reflection geometry has original Z vertex count");
                const auto* vertices=reinterpret_cast<const MeshVertex*>(resource.data);
                for(std::size_t v=0;v<resource.elements;++v)
                    check(vertices[v].px==native_z->vertices[v].px && vertices[v].py==native_z->vertices[v].py &&
                        vertices[v].pz==native_z->vertices[v].pz && vertices[v].u==native_z->vertices[v].u &&
                        vertices[v].v==native_z->vertices[v].v,"every Z capture vertex/UV is authored retail geometry");
            }
            check(z_segments==7,"all native Z-family segments resident");
            for(const auto& draw:cube_commands.owning_draws())if(draw.material.environment_min[3]==1) {
                ++floor_receivers;const auto& resource=cube_commands.resource_table().resources.at(draw.vertex_resource);
                check(resource.elements==native_floor->vertices.size(),"worldreflect mask only binds authenticated floor solid");
                const auto* vertices=reinterpret_cast<const MeshVertex*>(resource.data);
                for(std::size_t v=0;v<resource.elements;++v)for(unsigned c=0;c<4;++c)
                    check(vertices[v].color[c]==native_floor->vertices[v].color[c],
                        "floor retains native RGB and independent undoubled reflection-alpha mask");
            }
            check(floor_receivers!=0,"worldreflect receiver connected");
            const auto& entry_frame=gpu_scene.commands().frame;
            const double native_eye[]={2.743728462142889,.7651561591125922,-1.8576175162722057};
            const double native_right[]={-.7047351002693176,.11533286422491074,-.7000333666801453};
            const double native_up[]={.0988493338227272,.9930362105369568,.06409282982349396};
            const double native_forward[]={-.7025505304336548,.02402937039732933,.71122807264328};
            for(unsigned axis=0;axis<3;++axis) {
                check(std::abs(entry_frame.eye[axis]-native_eye[axis])<1e-5,
                    "main entry native camera eye, not constructor default");
                check(std::abs(entry_frame.right[axis]-native_right[axis])<1e-5 &&
                    std::abs(entry_frame.up[axis]-native_up[axis])<1e-5 &&
                    std::abs(entry_frame.forward[axis]-native_forward[axis])<1e-5,
                    "main entry native camera basis preserves original roll");
            }
            // Genuine original 40DD00 primary view1, 1280x720 viewport.
            check(std::abs(entry_frame.projection[0]-1.2961782217025757)<1e-6 &&
                std::abs(entry_frame.projection[1]-2.32236385345459)<1e-6,
                "main entry original independent fixed-angle projection scales");
            const auto initial_commands=gpu_scene.commands().owning_draws();
            check(!initial_commands.empty(),"original frontend resident GPU submission");
            const auto shadow=std::find_if(initial_commands.begin(),initial_commands.end(),
                [](const auto& d){return d.name.find("_FE_SHADOW")!=std::string::npos;});
            check(shadow!=initial_commands.end(),"original SHADOWFE resident draw connected");
            if(shadow!=initial_commands.end()) {
                const auto& resources=gpu_scene.commands().resource_table().resources;
                const auto& vertices=resources.at(shadow->vertex_resource);
                const auto& texture=resources.at(shadow->texture_resource);
                check(vertices.elements==16 && texture.texture_hash==bin_hash("SKYLINE_SHADOWFE"),
                    "FE shadow uses original texture and sixteen native samples");
                // Independently executed original 567890/56AB99/56EB33 slices,
                // authenticated FE_SKYLINE parts and original solid headers.
                const double x[]={-2.746955156326294,-.8996659517288208,.9476232528686523,2.794912338256836};
                const double y[]={-1.1095234155654907,-.3698313236236572,.36986076831817627,1.1095528602600098};
                const auto* v=reinterpret_cast<const MeshVertex*>(vertices.data);
                for(unsigned i=0;i<16;++i) {
                    const double z=(((i>>1)^i)&5)==5?.09999999403953552:.019999999552965164;
                    check(std::abs(v[i].px-x[i%4])<.00001 && std::abs(v[i].pz-y[i/4])<.00001 &&
                        std::abs(v[i].py-z)<.000001,"all FE shadow vertices match original footprint and ground consumer");
                    check(std::abs(v[i].u-(i%4)/3.)<.000001 && std::abs(v[i].v-(i/4)/3.)<.000001,
                        "original FE shadow UV orientation retained");
                }
                check(std::abs(shadow->material.alpha[0]-184./255)<.000001 && !shadow->pipeline.depth_write,
                    "native fixed FE alpha92 receives original Modulate2x and no depth writes");
            }
            for(const auto& draw:initial_commands)
                check(draw.program && std::string(draw.program->name).find("fallback")==std::string::npos,
                    "native frontend submits no placeholder fallback material");
            // Independent native 4B17F5..4B1825 witness with original
            // SKYLINE wheel-0 radius/vertical and normal main-view flag.
            for(const auto& draw:initial_commands) {
                if(draw.frontend_sprite_view || draw.name.find("_FE_")!=std::string::npos)continue;
                const auto& instance=skyline.scene.instances.at(draw.instance);
                check(std::abs(draw.placement.rows[14]-instance.transform[14]-.20027998089790344)<.00001,
                    "whole FE car receives original contact-plane root height");
            }
            check(gpu_scene.frame(1.0F/60,4000,error),error.c_str());
            const auto& moving_commands=gpu_scene.commands().owning_draws();
            check(moving_commands.size()==initial_commands.size() &&
                moving_commands.front().placement.rows[12]!=initial_commands.front().placement.rows[12],
                "resident original road translates without rebuilding draw definitions");
            // Independently executed 0x40BA40 at native phase 7253, with
            // authenticated SKYLINE wheel-0 radius and the frame above.
            const double native_cosine=.7678149938583374;
            const double native_sine=.6406716108322144;
            std::array<bool,4> rolling_copies{},stationary_brakes{};
            for(std::size_t i=0;i<moving_commands.size();++i) {
                const auto& draw=moving_commands[i];
                if(draw.frontend_sprite_view || draw.name.find("_FE_")!=std::string::npos)continue;
                const auto& instance=skyline.scene.instances.at(draw.instance);
                if(instance.part_slot==17) {
                    check(instance.copy<4,"original rolling wheel index");
                    rolling_copies[instance.copy]=true;
                    const auto& mesh=skyline.scene.meshes.at(instance.mesh);
                    check(!mesh.vertices.empty(),"original wheel vertex retained");
                    for(std::size_t v:{std::size_t(0),mesh.vertices.size()-1}) {
                        const auto& point=mesh.vertices[v];
                        const double slot[3]={point.px,point.pz,point.py};
                        double before[3]{},actual[3]{};
                        for(unsigned c=0;c<3;++c) {
                            before[c]=instance.transform[12+c];actual[c]=draw.placement.rows[12+c];
                            for(unsigned k=0;k<3;++k) {
                                before[c]+=slot[k]*instance.transform[k*4+c];
                                actual[c]+=slot[k]*draw.placement.rows[k*4+c];
                            }
                        }
                        const double x=before[0]-instance.animation_pivot[0];
                        const double z=before[2]-instance.animation_pivot[2];
                        const double expected[3]={x*native_cosine+z*native_sine+instance.animation_pivot[0],
                            before[1],-x*native_sine+z*native_cosine+instance.animation_pivot[2]+.20027998089790344};
                        for(unsigned c=0;c<3;++c)
                            check(std::abs(actual[c]-expected[c])<.00001,
                                "all four original wheel vertices match native spin axis/sign/pivot");
                    }
                } else if(instance.part_slot==16) {
                    check(instance.copy<4,"original stationary brake index");
                    stationary_brakes[instance.copy]=true;
                    for(unsigned c=0;c<16;++c)
                        check(draw.placement.rows[c]==initial_commands[i].placement.rows[c],
                            "brake matrices do not inherit wheel rolling");
                }
            }
            check(std::all_of(rolling_copies.begin(),rolling_copies.end(),[](bool b){return b;}) &&
                std::all_of(stationary_brakes.begin(),stationary_brakes.end(),[](bool b){return b;}),
                "native rolling witness covers all four wheels and all four stationary brakes");
            // Render the actual moving scene at the same phases as the menu
            // diagnostic. This reaches road/car sprites in cube, floor and
            // primary views, including their borrowed Rasterizer limits.
            {
                FrontendGpuScene reference_scene;
                if(!reference_scene.load(root,1280,720,error))
                    throw std::runtime_error("reference scene load: "+error);
                std::vector<unsigned char> first_frame,reference_pixels;
                for(unsigned frame=1;frame<=420;++frame) {
                    if(!reference_scene.frame(1.F/60,frame*4000ULL/60,error))
                        throw std::runtime_error("reference scene advance: "+error);
                    if(frame!=60 && frame!=420)continue;
                    if(!reference_scene.render_reference(reference_pixels,error))
                        throw std::runtime_error("moving reference render: "+error);
                    // Independently render the same real camera/time with
                    // only equation-proven no-output draws removed. Other
                    // passes, placements, textures and uniforms stay intact.
                    auto& recorded=const_cast<RecordedCommands&>(reference_scene.commands());
                    auto saved_draws=recorded.owning_draws();
                    unsigned fast=0,removed=0;
                    for(std::size_t i=0;i<recorded.draw_count();++i)
                        fast+=frontend_glow_fast(recorded.draw_view(i),recorded.frame);
                    auto& candidates=recorded.owning_draws();
                    candidates.erase(std::remove_if(candidates.begin(),candidates.end(),[&](const RecordedDraw& d) {
                        const RecordedDrawView view{d,d.instance,d.placement,d.material,false};
                        const bool skip=frontend_glow_no_output(view,recorded.frame);removed+=skip;return skip;
                    }),candidates.end());
                    std::vector<unsigned char> candidate_pixels;
                    const bool rendered=reference_scene.render_reference(candidate_pixels,error);
                    candidates=std::move(saved_draws);
                    check(rendered,error.c_str());
                    check(fast>0 && removed>0,"real menu exercises specialization and zero-output selection");
                    check(candidate_pixels==reference_pixels,"real menu frame60/420 no-output elimination RGBA parity");
                    check(reference_pixels.size()==1280ULL*720*4,
                        "moving reference scene writes a complete RGBA frame");
                    bool visible=false;
                    for(std::size_t pixel=0;pixel<reference_pixels.size();pixel+=4) {
                        check(reference_pixels[pixel+3]==255,
                            "moving reference scene writes opaque composed pixels");
                        visible|=reference_pixels[pixel]!=0 || reference_pixels[pixel+1]!=0 ||
                            reference_pixels[pixel+2]!=0;
                    }
                    check(visible,"moving reference scene includes visible authored geometry");
                    if(frame==60)first_frame=reference_pixels;
                    else check(reference_pixels!=first_frame,
                        "advanced reference scene changes its rendered pixels");
                }
                std::cout << "PASS: moving menu reference renders at frames 60 and 420\n";
            }
            {
                FrontendGpuScene garage_orbit;
                if(!garage_orbit.load_browser(root,1280,720,{"CIVIC"},error))throw std::runtime_error("orbit browser load: "+error);
                for(unsigned slot:{31U,32U,33U,34U,17U,39U}) {
                    garage_orbit.focus_part(slot);
                    if(!garage_orbit.frame(3,1000,error))throw std::runtime_error("orbit focus frame: "+error);
                    for(unsigned i=0;i<420;++i) {
                        garage_orbit.orbit(i<300?3.F:0.F,i<180?-1.F:i<300?1.F:0.F);
                        if(!garage_orbit.frame(1.F/60,1000,error))throw std::runtime_error("orbit sweep frame: "+error);
                        check(garage_orbit.commands().frame.eye[1]>.05F,"live garage orbit eye remains above floor");
                    }
                    if(!validate_recorded_commands(garage_orbit.commands(),error))throw std::runtime_error("orbit commands: "+error);
                    const auto& uniform=garage_orbit.commands().frame;
                    const double delta[3]={-uniform.eye[0],.75-uniform.eye[1],-uniform.eye[2]};
                    const auto dot=[&](const float* basis){return delta[0]*basis[0]+delta[1]*basis[1]+delta[2]*basis[2];};
                    const double depth=dot(uniform.forward);
                    check(depth>uniform.projection[2]&&std::abs(dot(uniform.right)*uniform.projection[0])<depth&&
                        std::abs(dot(uniform.up)*uniform.projection[1])<depth,"live car center stays inside returned orbit frustum");
                    if(slot==31||slot==34) {
                        std::vector<unsigned char> pixels;
                        if(!garage_orbit.render_reference(pixels,error))throw std::runtime_error("orbit reference render: "+error);
                        check(pixels.size()==1280*720*4,"actual vinyl orbit reference frame rendered");
                    }
                }
                std::cout<<"PASS: live garage manual orbit through all vinyl layers, rims and caliper-paint focus\n";
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

            {
                audio::FrontendSpeech speech;check(speech.play(root,audio::post_race_speech(19,true),error),error.c_str());
                std::vector<std::int16_t> pcm(48000*2);speech.mix(pcm.data(),48000);
                check(std::any_of(pcm.begin(),pcm.end(),[](auto v){return v!=0;}),"authored post-victory speech group decodes audible genuine PCM");
                speech.stop();check(!speech.active(),"post voice can stop when leaving the post-race context");
            }
            std::array<std::vector<std::int16_t>,3> extra_cues;
            const audio::FrontendSound extra_names[]={audio::FrontendSound::left,audio::FrontendSound::right,audio::FrontendSound::dialog};
            for(unsigned i=0;i<3;++i) {
                sfx.reset();sfx.play(extra_names[i]);check(sfx.active(),"original post cue voice becomes active");
                extra_cues[i].resize(48000*2);sfx.mix(extra_cues[i].data(),48000);
                check(!sfx.active()&&std::any_of(extra_cues[i].begin(),extra_cues[i].end(),[](auto v){return v!=0;}),"post cue produces audible bounded PCM and drains its tail");
            }
            check(extra_cues[0]!=extra_cues[1]&&extra_cues[0]!=extra_cues[2]&&extra_cues[1]!=extra_cues[2],"left/right/dialog use distinct native common-bank samples");

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
            // GPU-only loading must retain authored textures/font coordinates
            // and update selection without a CPU background or texture reload.
            {
                RaceLoadingArt generic;
                const bool generic_loaded=generic.load(root,error);check(generic_loaded,error.c_str());
                std::vector<unsigned char> base(1280*720*4),shown(base.size()),cached(base.size());
                generic.draw({base.data(),1280,720,1280*4},.4,false);
                for(unsigned tip:{2U,3U,4U,5U,6U,7U,8U,9U,11U,14U,15U,16U,17U,18U,19U,20U}) {
                    RaceLoadingArt art;
                    const bool loaded=art.load(root,error,tip);check(loaded,error.c_str());
                    art.draw({shown.data(),1280,720,1280*4},.4,false);
                    art.draw({cached.data(),1280,720,1280*4},.4,true);
                    check(shown!=base&&shown==cached,"native loading card contributes visible cached artwork");
                }
            }
            FrontendArt gpu_art;
            check(gpu_art.load_startup(root,error,true),error.c_str());
            std::fill(pixels.begin(),pixels.end(),0);
            gpu_art.draw_startup_splash({pixels.data(),1280,720,1280*4});
            std::size_t lit_splash_pixels=0;
            for(std::size_t p=0;p<pixels.size();p+=4)
                if(pixels[p]||pixels[p+1]||pixels[p+2])++lit_splash_pixels;
            check(pixels[3]==255&&lit_splash_pixels>1280*100,"retail splash sprite is visible");
            gpu_art.release_startup_splash();
            check(gpu_art.load_post_race(root,error),error.c_str());
            {
                FrontendUiFrame pause_ui,profile_ui,welcome_ui;RacePauseMenu pause;
                gpu_art.record_underground_welcome(1280,720,false,welcome_ui);
                check(!welcome_ui.vertices.empty(),"shared startup load retains the welcome page");
                gpu_art.record_pause(1280,720,pause,pause_ui);
                check(!pause_ui.vertices.empty(),"retail MU_Pause_PC renders actual objects");
                FrontendArt pause_animation;check(gpu_art.prepare_post_race(pause_animation,error),error.c_str());
                FrontendUiFrame begin,later;pause.open();pause_animation.advance(0);pause_animation.record_pause(1280,720,pause,begin);
                pause_animation.advance(.1F);pause_animation.record_pause(1280,720,pause,later);
                check(begin.vertices.size()!=later.vertices.size()||std::memcmp(begin.vertices.data(),later.vertices.data(),begin.vertices.size()*sizeof(TrackVertex)),"authored pause Init advances with UI time");
                gpu_art.record_profiles(1280,720,{{0,"PLAYER",{}},{1,"NEW CAREER",{}}},1,"",profile_ui);
                check(!profile_ui.vertices.empty(),"retail PC_LoadSave renders named profiles");
            }
            FrontendArt result_art,empty_art;
            check(!empty_art.prepare_post_race(result_art,error),"empty post source refused");
            {
                FrontendArt statistics_art;check(statistics_art.load_startup(root,error,true),error.c_str());
                statistics_art.release_startup_splash();
                FrontendMenu stats_row(true,true,true);FrontendInput down;down.down=true;
                stats_row.update(down);stats_row.update(down);
                FrontendUiFrame frame;statistics_art.advance(.016F);
                statistics_art.record_ui(1280,720,stats_row,frame);
                check(!frame.vertices.empty(),"authored Statistics main-menu highlight renders");
                bool atlas_changed=false;
                const bool loaded=statistics_art.load_statistics(root,atlas_changed,error);check(loaded,error.c_str());
                check(atlas_changed,"first Statistics visit adds the missing sprites to the menu atlas");
                const bool reopened=statistics_art.load_statistics(root,atlas_changed,error);check(reopened,error.c_str());
                check(!atlas_changed,"returning to Statistics reuses the resident atlas");
                CareerCatalog catalog;check(catalog.load(root,error),error.c_str());
                CareerProfile profile;CareerInfoMenu statistics;statistics.open(CareerInfoScreen::statistics);
                do {
                    statistics_art.advance(1.F);
                    statistics_art.record_career_info(1280,720,catalog,profile,statistics,frame);
                    check(!frame.vertices.empty(),"main-menu Statistics category renders without garage assets");
                }while(statistics.move(1));
            }
            check(!gpu_art.prepare_post_race(gpu_art,error),"self post replacement refused");
            const auto source_pixels=gpu_art.atlas().rgba;
            check(gpu_art.prepare_post_race(result_art,error),error.c_str());
            check(gpu_art.atlas().rgba==source_pixels,"preparing results preserves career atlas");
            check(result_art.atlas().rgba.size()<=8*1024*1024,"result overlay fits remaining race GPU budget");
            PostRaceMenu results(true,true,1);
            const PostRaceValues result_values={{"CareerStyleDisplayed","1500"},{"StyleStage","2"},{"Tournament","1"}};
            for(unsigned page=0;page<5;++page) {
                if(page==1)results.update({false,false,true,false});
                if(page==2)results.update({false,false,true,false});
                if(page==3)results.update({false,false,true,false});
                if(page==4){results.update({true,false,false,false});results.update({false,false,true,false});}
                std::vector<unsigned char> original(1280*720*4,0),compact(original.size(),0);
                gpu_art.draw_post_race({original.data(),1280,720,1280*4},results,result_values);
                result_art.draw_post_race({compact.data(),1280,720,1280*4},results,result_values);
                check(original==compact,"compact post atlas preserves all five original pages");
                FrontendUiFrame full_ui,small_ui;
                gpu_art.record_post_race(1280,720,results,result_values,full_ui);
                result_art.record_post_race(1280,720,results,result_values,small_ui);
                check(!small_ui.vertices.empty() && full_ui.additive==small_ui.additive && full_ui.vertices.size()==small_ui.vertices.size(),"compact results keep GPU submissions");
                for(std::size_t i=0;i<full_ui.vertices.size();++i) {
                    const auto& a=full_ui.vertices[i];const auto& b=small_ui.vertices[i];
                    check(a.px==b.px && a.py==b.py && a.pz==b.pz && std::memcmp(a.color,b.color,sizeof a.color)==0,"compact result vertex placement and color");
                    if(i%4==0) {
                        const auto sample=[&](const auto& atlas,const auto& vertices) {
                            // The resident HUD sampler is linear. A nearest-texel
                            // floor at an integer edge changes by a full pixel
                            // when normalized UVs are rounded for a shorter atlas.
                            double u=0,v=0;for(unsigned k=0;k<4;++k){u+=double(vertices[i+k].u)*.25;v+=double(vertices[i+k].v)*.25;}
                            const double x=u*atlas.width-.5,y=v*atlas.height-.5;
                            const int ix=int(std::floor(x)),iy=int(std::floor(y));
                            const double fx=x-ix,fy=y-iy;std::array<double,4> result{};
                            for(unsigned row=0;row<2;++row)for(unsigned col=0;col<2;++col) {
                                const unsigned sx=unsigned(std::clamp(ix+int(col),0,int(atlas.width)-1));
                                const unsigned sy=unsigned(std::clamp(iy+int(row),0,int(atlas.height)-1));
                                const auto* pixel=atlas.rgba.data()+4*(std::size_t(sy)*atlas.width+sx);
                                const double weight=(col?fx:1-fx)*(row?fy:1-fy);
                                for(unsigned c=0;c<4;++c)result[c]+=pixel[c]*weight;
                            }
                            return result;
                        };
                        const auto full_sample=sample(gpu_art.atlas(),full_ui.vertices);
                        const auto compact_sample=sample(result_art.atlas(),small_ui.vertices);
                        for(unsigned c=0;c<4;++c)check(std::abs(full_sample[c]-compact_sample[c])<.5,
                            "compact results preserve original linear-filtered sprite/font samples");
                    }
                }
            }
            {
                FrontendArt animated;check(gpu_art.prepare_post_race(animated,error),error.c_str());
                PostRaceMenu entry(true,true,1);entry.update({false,false,true,false});FrontendUiFrame first,later,held;
                animated.advance(0);animated.record_post_race(1280,720,entry,result_values,first);
                animated.advance(.4F);animated.record_post_race(1280,720,entry,result_values,later);
                animated.record_post_race(1280,720,entry,result_values,held);
                const auto equal=[](const auto& a,const auto& b){return a.vertices.size()==b.vertices.size() &&
                    !std::memcmp(a.vertices.data(),b.vertices.data(),a.vertices.size()*sizeof(TrackVertex));};
                check(!equal(first,later),"original style Init animation advances rather than jumping to endpoint");
                check(equal(later,held),"repeated result draw does not advance time");
            }
            {
                FrontendArt animation;check(gpu_art.prepare_post_race(animation,error),error.c_str());
                PostRaceMenu pages(true,false,0);pages.animate();FrontendUiFrame opened,closing,reopened,confirmation;
                animation.advance(.4F);animation.record_post_race(1280,720,pages,result_values,opened);
                pages.update({false,false,true});pages.advance(.1);animation.advance(.1F);
                animation.record_post_race(1280,720,pages,result_values,closing);
                check(opened.vertices.size()==closing.vertices.size()&&std::memcmp(opened.vertices.data(),closing.vertices.data(),opened.vertices.size()*sizeof(TrackVertex)),"native results FadeOut changes actual submitted UI vertex colors");
                pages.advance(.07);pages.update({false,false,true});
                check(pages.confirming(),"continue opens native post-race confirmation package");
                animation.record_post_race(1280,720,pages,result_values,confirmation);
                check(!confirmation.vertices.empty(),"original MU_PostRaceConfirm renders from compact resident atlas");
                pages.update({false,false,false,true});pages.update({true});pages.update({false,false,true});
                check(pages.statistics(),"cancel confirmation then open statistics");
                animation.advance(.3F);animation.record_post_race(1280,720,pages,result_values,reopened);
                check(!reopened.vertices.empty(),"statistics returns with original entry animation");
            }
            std::cout<<"PASS: compact post-race atlas bytes="<<result_art.atlas().rgba.size()<<" all five result pages preserved\n";
            const auto& atlas=gpu_art.atlas();
            check(atlas.width && atlas.height && atlas.rgba.size()==atlas.width*atlas.height*4ULL,
                "resident original frontend atlas contains complete pixel storage");
            FrontendUiFrame ui;
            FrontendMenu gpu_menu;
            gpu_art.record_ui(1280,720,gpu_menu,ui);
            check(!ui.additive.empty() && ui.vertices.size()==ui.additive.size()*4 &&
                ui.additive.size()<=kFrontendUiMaxQuads,"authored UI fits resident overlay capacity");
            check(std::any_of(ui.vertices.begin(),ui.vertices.end(),[](const TrackVertex& vertex) {
                return vertex.pz<kFrontendFontVertexZ;
            }) && std::any_of(ui.vertices.begin(),ui.vertices.end(),[](const TrackVertex& vertex) {
                return vertex.pz==-1.F;
            }),"menu glyphs carry a pixel footprint and sprites retain nearest sampling");
            // Independent MainMenu.fng resource5 circle submissions: authored
            // positions and alpha, before any temporal-state approximation.
            // Missing CIRCLE atlas binding used to silently omit all four.
            const double native_circles[4][4][2]={
                {{556.4586791992188,33.027889251708984},{580.9721069335938,12.458686828613281},{601.5413208007812,36.972110748291016},{577.0278930664062,57.54131317138672}},
                {{522.4586791992188,33.027889251708984},{546.9721069335938,12.458686828613281},{567.5413208007812,36.972110748291016},{543.0278930664062,57.54131317138672}},
                {{533.8563842773438,29.14358901977539},{517.8563842773438,56.85640335083008},{490.1435852050781,40.85641098022461},{506.1435852050781,13.143595695495605}},
                {{456.1435852050781,29.143592834472656},{483.8564147949219,13.143593788146973},{499.8564147949219,40.856407165527344},{472.1435852050781,56.856407165527344}}};
            const unsigned circle_alpha[4]={60,104,74,68};
            for(unsigned circle=0;circle<4;++circle) {
                bool found=false;
                for(std::size_t i=0;i<ui.vertices.size();i+=4) {
                    bool same=true;
                    for(unsigned corner=0;corner<4;++corner) {
                        const auto& v=ui.vertices[i+corner];
                        same &= std::abs(v.px-native_circles[circle][corner][0]*2)<.001 &&
                                std::abs(v.py-native_circles[circle][corner][1]*1.5)<.001;
                    }
                    if(!same)continue;
                    found=true;
                    for(unsigned corner=0;corner<4;++corner) {
                        const auto& v=ui.vertices[i+corner];
                        check(v.color[0]==206 && v.color[1]==232 && v.color[2]==242 &&
                              v.color[3]==circle_alpha[circle],
                              "original header circle native color and alpha");
                    }
                    check(ui.vertices[i+2].u>ui.vertices[i].u && ui.vertices[i+2].v>ui.vertices[i].v,
                        "original circle binds nonempty resident texture coordinates");
                }
                check(found,"original rotated header circle matches genuine FEng submission");
            }
            unsigned outside_markers=0;
            for(std::size_t i=0;i<ui.vertices.size();i+=4) {
                if(ui.vertices[i].color[0]!=254 || ui.vertices[i].color[1]!=0 ||
                    ui.vertices[i].color[2]!=0) continue;
                // Genuine original FEng image submit places the three authored
                // red control markers wholly outside clipX [-1,1]. Keep their
                // geometry; production viewport clipping excludes them.
                for(unsigned corner=0;corner<4;++corner) {
                    const double clip_x=2*double(ui.vertices[i+corner].px)/1280-1;
                    const double native_x=(corner==0 || corner==3)?-1.084375023841858:-1.0343749523162842;
                    check(std::abs(clip_x-native_x)<1e-6 && clip_x<-1,
                        "authored menu marker remains outside original viewport");
                }
                ++outside_markers;
            }
            check(outside_markers==3,"native off-canvas controls retained without visible red squares");
            const double native_logo[4][2]={{-1.1467323303222656,.9117122888565063},
                {.29381564259529114,.9117122888565063},
                {.29381564259529114,.6716209650039673},
                {-1.1467323303222656,.6716209650039673}};
            const double native_text[4][2]={{.40000003576278687,.2124999612569809},
                {.4437500238418579,.2124999612569809},
                {.4437500238418579,.14583328366279602},
                {.40000003576278687,.14583328366279602}};
            const auto native_quad_present=[&](const double (&pin)[4][2]) {
                for(std::size_t i=0;i<ui.vertices.size();i+=4) {
                    bool same=true;
                    for(unsigned corner=0;corner<4;++corner)
                        same &= std::abs(2*double(ui.vertices[i+corner].px)/1280-1-pin[corner][0])<1e-6 &&
                            std::abs(1-2*double(ui.vertices[i+corner].py)/720-pin[corner][1])<1e-6;
                    if(same)return true;
                }
                return false;
            };
            check(native_quad_present(native_logo),"onscreen authored logo matches original FEng clip corners");
            check(native_quad_present(native_text),"onscreen original font glyph matches original FEng clip corners");
            bool atlas_pixels=false,restored_color=false;
            for(const auto& vertex:ui.vertices) {
                check(std::isfinite(vertex.px) && std::isfinite(vertex.py) &&
                    vertex.u>=0 && vertex.u<=1 && vertex.v>=0 && vertex.v<=1,
                    "original font and FEng quads have valid atlas coordinates");
                const auto x=std::min(atlas.width-1,static_cast<unsigned>(vertex.u*atlas.width));
                const auto y=std::min(atlas.height-1,static_cast<unsigned>(vertex.v*atlas.height));
                restored_color |= vertex.color[0]>127 || vertex.color[1]>127 || vertex.color[2]>127;
                check(vertex.color[0]%2==0 && vertex.color[1]%2==0 && vertex.color[2]%2==0 && vertex.color[3]%2==0,
                    "GPU overlay preserves native half-byte quantization and restores Modulate2x");
                atlas_pixels |= atlas.rgba[(y*atlas.width+x)*4+3]!=0;
            }
            check(restored_color,"native modulation restored before dedicated GPU UI shader");
            check(atlas_pixels,"UI vertices bind decoded original texture pixels");
            const auto initial_ui=ui.vertices;
            const auto* resident_pixels=atlas.rgba.data();
            FrontendInput navigation;navigation.down=true;gpu_menu.update(navigation);
            gpu_art.record_ui(1280,720,gpu_menu,ui);
            bool highlight_changed=ui.vertices.size()!=initial_ui.size();
            if(!highlight_changed)for(std::size_t i=0;i<ui.vertices.size();++i) {
                const auto& a=ui.vertices[i];const auto& b=initial_ui[i];
                highlight_changed |= a.color[0]!=b.color[0] || a.color[1]!=b.color[1] || a.color[2]!=b.color[2] ||
                                     a.px!=b.px || a.py!=b.py;
            }
            check(highlight_changed && atlas.rgba.data()==resident_pixels,
                "selection updates original highlight quads with resident texture storage");
            gpu_menu.song("Lil Jon","Get Low","Kings of Crunk");
            gpu_art.record_ui(1280,720,gpu_menu,ui);
            check(ui.vertices.size()==ui.additive.size()*4 && !ui.vertices.empty(),
                "current song text reaches GPU original font submission");
            const double native_temporal[4][4][2]={
                {{462,19},{494,19},{494,51},{462,51}},
                {{462,50.9936408996582},{462.00634765625,18.999996185302734},{494,19.006359100341797},{493.99365234375,51.000003814697266}},
                {{494,51.000003814697266},{462,50.999996185302734},{462,18.9999942779541},{494,19.0000057220459}},
                {{493.99365234375,19.00000762939453},{494,50.99365234375},{462.00634765625,50.99999237060547},{462,19.00634765625}}};
            const unsigned temporal_alpha[4]={80,46,10,44};
            for(unsigned phase=0;phase<5;++phase) {
                gpu_art.advance(phase?.5F:0.F);gpu_art.record_ui(1280,720,gpu_menu,ui);
                const unsigned expected=phase%4;bool found=false;
                for(std::size_t i=0;i<ui.vertices.size();i+=4) {
                    bool same=true;
                    for(unsigned corner=0;corner<4;++corner) {
                        const auto& v=ui.vertices[i+corner];
                        same &= std::abs(v.px-native_temporal[expected][corner][0]*2)<.002 &&
                                std::abs(v.py-native_temporal[expected][corner][1]*1.5)<.002 &&
                                v.color[3]==temporal_alpha[expected];
                    }
                    found |= same;
                }
                check(found,"original timed circle quaternion/color and two-second loop match genuine FEng");
                check(atlas.rgba.data()==resident_pixels,"authored temporal UI reuses resident texture atlas");
            }
            // Circle(58) has the sole authored 45-degree quaternion pair.
            // Genuine 4FC640 uses sine phases4095/4095/8191 at its midpoint.
            gpu_art.advance(.75F);gpu_art.record_ui(1280,720,gpu_menu,ui);
            const double native_short_arc[4][2]={{489.3743896484375,34.99565887451172},
                {512.0043334960938,12.374380111694336},{534.6256103515625,35.00434112548828},
                {511.99566650390625,57.62561798095703}};
            bool short_arc_found=false;
            for(std::size_t i=0;i<ui.vertices.size();i+=4) {
                bool same=true;
                for(unsigned corner=0;corner<4;++corner) {
                    const auto& v=ui.vertices[i+corner];
                    same &= std::abs(v.px-native_short_arc[corner][0]*2)<.002 &&
                            std::abs(v.py-native_short_arc[corner][1]*1.5)<.002 && v.color[3]==60;
                }
                short_arc_found |= same;
            }
            check(short_arc_found,"authored circle short-arc interpolation matches independent native image corners");
            // Genuine4FC100 nonloop scripts and4F1810 parent modulation:
            // actual two-mode child records, four corners, packed colors.
            // Actual nonloop title Init and Description FadeIn color tracks,
            // independently executed through original4FC100. Native byte /2
            // packing is retained. Alpha0 must submit no resurrected glyph.
            for(unsigned tick:{0U,1100U,1110U,1120U,1400U}) {
                FrontendArt entry_art;FrontendMenu entry_menu;
                check(entry_art.load(root,error,false),error.c_str());
                entry_art.advance((float(tick)+.25F)/1200);
                entry_art.record_ui(1280,720,entry_menu,ui);
                unsigned title_alpha=0;bool title_present=false;
                for(std::size_t i=0;i<ui.vertices.size();i+=4) {
                    const auto& v=ui.vertices[i];
                    if(std::abs(v.px-891.9F)<.01F && std::abs(v.py-97.35F)<.01F) {
                        title_present=true;title_alpha=v.color[3];
                        for(unsigned corner=0;corner<4;++corner)
                            check(ui.vertices[i+corner].color[3]==title_alpha,
                                  "title Init alpha applies to all glyph corners");
                    }
                }
                const unsigned expected=tick<=1100?0:tick==1110?80:160;
                check(title_present==(expected!=0) && title_alpha==expected,
                      "native title Init zero, midpoint and endpoint alpha survive UI draw");
            }
            // Original Chyron text keys: genuine4FC100 at840/870/900 returns
            // alpha0/110/220, Hide0/40/80 returns255/128/0. Parent255
            // modulation then native half-byte packing gives0/110/218 and
            // 254/128/0. The controller clocks are separately witnessed.
            FrontendArt chyron_art;FrontendMenu chyron_menu;
            check(chyron_art.load(root,error,false),error.c_str());
            const auto chyron_alpha=[&]() {
                chyron_art.record_ui(1280,720,chyron_menu,ui);
                unsigned alpha=0;
                for(const auto& v:ui.vertices)if(v.py>560 &&
                    v.color[0]==206 && v.color[1]==232 && v.color[2]==240)
                    alpha=std::max(alpha,unsigned(v.color[3]));
                return alpha;
            };
            check(chyron_alpha()==0,"Chyron Show starts with invisible original text");
            chyron_art.advance(840.25F/1200);check(chyron_alpha()==0,"Chyron Show original delayed text key");
            chyron_art.advance(30.F/1200);check(chyron_alpha()==110,"Chyron Show native text midpoint");
            chyron_art.advance(30.F/1200);check(chyron_alpha()==218,"Chyron Show native text endpoint");
            chyron_art.advance(4.451F);check(chyron_alpha()==254,"native controller starts Chyron Hide with authored alpha");
            chyron_art.advance(40.F/1200);check(chyron_alpha()==128,"Chyron Hide native text midpoint");
            chyron_art.advance(40.F/1200);check(chyron_alpha()==0,"Chyron Hide original zero-alpha endpoint");
            chyron_art.advance(1.F);check(chyron_alpha()==0,"closed Chyron removes metadata glyphs");
            chyron_menu.announce_song();check(chyron_alpha()==0,"same-song reentry restarts Show invisible key");
            chyron_art.advance(.75F);check(chyron_alpha()==218,"same-song reentry follows authored Show keys again");
            // Genuine 4D76A0 -> 4D7580 binds TrackTitle to artist and
            // ArtistName to quoted title. Inspect actual font quads in authored
            // package order, rather than inferring semantics from object names.
            FrontendArt binding_art;FrontendMenu binding_menu;
            if(!binding_art.load(root,error,false))throw std::runtime_error(error);
            binding_menu.song("A","B","C");
            const auto metadata_glyphs=[&]() {
                binding_art.record_ui(1280,720,binding_menu,ui);
                binding_art.advance(.75F);
                binding_art.record_ui(1280,720,binding_menu,ui);
                std::vector<std::array<float,4>> glyphs;
                for(std::size_t i=0;i<ui.vertices.size();i+=4) {
                    const auto& v=ui.vertices[i];
                    if(v.py>560 && v.color[0]==206 && v.color[1]==232 && v.color[2]==240)
                        glyphs.push_back({v.u,v.v,ui.vertices[i+2].u,ui.vertices[i+2].v});
                }
                return glyphs;
            };
            const auto bound=metadata_glyphs();
            check(bound.size()==5,"native metadata emits artist, quoted title and album glyphs");
            check(bound[1]==bound[3] && bound[1]!=bound[2],"title has matching surrounding quote glyphs");
            binding_menu.song("C","B","A");const auto swapped=metadata_glyphs();
            check(swapped.size()==5 && swapped[0]==bound[4] && swapped[4]==bound[0] &&
                swapped[1]==bound[1] && swapped[2]==bound[2] && swapped[3]==bound[3],
                "artist is the authored first row, quoted title second and album third");
            binding_menu.song("A","\"B\"","C");
            check(metadata_glyphs()==bound,"authenticated already-quoted title is not double quoted");
            FrontendArt description_art;FrontendMenu description_menu;
            check(description_art.load(root,error,false),error.c_str());
            const auto description_alpha=[&]() {
                description_art.record_ui(1280,720,description_menu,ui);
                unsigned alpha=0;
                for(const auto& v:ui.vertices)if(v.py>540 && v.py<580 &&
                    v.color[0]==84 && v.color[1]==154 && v.color[2]==180) {
                    check(!alpha || alpha==v.color[3],"Description glyphs share native FadeIn alpha");
                    alpha=v.color[3];
                }
                return alpha;
            };
            description_art.advance(0);
            check(description_alpha()==0,"entry HighLight starts Description FadeIn at invisible key");
            description_art.advance(.1F);
            check(description_alpha()==128,"Description native FadeIn midpoint alpha");
            description_art.advance(.1F);
            check(description_alpha()==254,"Description native FadeIn endpoint with half-byte packing");
            description_menu.update({false,true,false,false});
            check(description_alpha()==0,"selection event replaces settled Description state with FadeIn start");
            description_art.advance(.1F);
            check(description_alpha()==128,"new selection Description uses authored FadeIn midpoint");
            description_menu.update({true,false,false,false});
            check(description_alpha()==0,"immediate reverse restarts native non-rebased FadeIn keys");
            FrontendArt selection_art;FrontendMenu selection_menu;
            check(selection_art.load(root,error,false),error.c_str());
            const auto selection_quad=[&](double x,double y,unsigned alpha) {
                for(std::size_t i=0;i<ui.vertices.size();i+=4) {
                    const double xs[]={x,x+247.25,x+247.25,x};
                    const double ys[]={y,y,y+20,y+20};bool same=true;
                    for(unsigned c=0;c<4;++c) {const auto& v=ui.vertices[i+c];
                        same &= std::abs(v.px-xs[c]*2)<.001 && std::abs(v.py-ys[c]*1.5)<.001 &&
                            v.color[0]==4 && v.color[1]==134 && v.color[2]==196 && v.color[3]==alpha;
                    }
                    if(same)return true;
                }
                return false;
            };
            const unsigned times[]={0,20,100,200};
            const double xs[]={323,324.5,330.5,338};const unsigned alphas[]={4,16,64,124};
            for(unsigned phase=0;phase<4;++phase) {
                selection_art.advance(float(phase?times[phase]-times[phase-1]:0)/1200);
                selection_art.record_ui(1280,720,selection_menu,ui);
                check(selection_quad(xs[phase],104,alphas[phase]),"original UG backing all corners and parent alpha during HighLight");
            }
            selection_menu.update({false,true,false,false});
            selection_art.record_ui(1280,720,selection_menu,ui);
            check(selection_quad(323,146,4) && selection_quad(338,104,124),
                "immediate selection forwards native start events to both backing groups");
            selection_art.advance(20.F/1200);selection_art.record_ui(1280,720,selection_menu,ui);
            check(selection_quad(324.5,146,16) && selection_quad(338,104,112),
                "original selected fade-in and previous fade-out run simultaneously");
            selection_menu.update({true,false,false,false});
            selection_art.record_ui(1280,720,selection_menu,ui);
            check(selection_quad(323,104,4) && selection_quad(338,146,124),
                "rapid reverse restarts authored mode1 keys without optional rebasing or delay");
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
