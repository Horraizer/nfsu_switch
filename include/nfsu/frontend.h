#pragma once
#include "nfsu/screen_text.h"
#include "nfsu/career.h"
#include "nfsu/track.h"
#include <filesystem>
#include <algorithm>
#include <cmath>
#include "nfsu/career_profiles.h"
#include "nfsu/race_pause.h"
#include <cstdint>
#include <memory>
#include <map>
#include <string>
#include <utility>
#include <vector>
namespace nfsu {
enum class FrontendAction { none, underground, free_drive, profiles, exit, multiplayer, options, statistics };
struct FrontendInput { bool up=false, down=false, accept=false, back=false, left=false, right=false, profiles=false; };
struct RacePresentationOptions {
    bool motion_blur=true;
    bool low_car_reflections=false;
    bool road_reflections=true;
    bool metric_speed=false;
};
class FrontendMenu {
public:
    explicit FrontendMenu(bool multiplayer=false,bool options=false,bool statistics=false)
        :multiplayer_(multiplayer),options_(options),statistics_(statistics){}
    FrontendAction update(FrontendInput input);
    unsigned selected() const { return selected_; }
    bool multiplayer_enabled()const{return multiplayer_;}
    bool options_enabled()const{return options_;}
    bool statistics_enabled()const{return statistics_;}
    FrontendAction selected_action() const;
    void notice(std::string text) { notice_=std::move(text); }
    const std::string& notice() const { return notice_; }
    void song(const char* artist,const char* title,const char* album) {
        if(artist_!=artist || title_!=title || album_!=album)++song_revision_;
        artist_=artist;title_=title;album_=album;
    }
    void announce_song() {++song_revision_;}
    unsigned song_revision() const {return song_revision_;}
    const std::string& artist() const {return artist_;}
    const std::string& title() const {return title_;}
    const std::string& album() const {return album_;}
private:
    unsigned selected_=0,song_revision_=1;
    bool multiplayer_=false,options_=false,statistics_=false;
    std::string notice_;
    std::string artist_="Lil Jon & the Eastside Boyz",title_="Get Low",album_="Kings of Crunk";
};
// Match the live race pause policy: gaps above one second are suspension,
// while ordinary slow frames retain their full elapsed time. Resume hooks
// reset only the wall-clock anchor; authored animation phase stays resident.
class FrontendClock {
public:
    void resume() { anchored_=false; }
    std::uint64_t sample(std::uint64_t now_ns) {
        const auto elapsed=anchored_ && now_ns>=previous_?now_ns-previous_:0;
        previous_=now_ns;anchored_=true;
        if(elapsed>1000000000ULL)return 0;
        active_ns_+=elapsed;return elapsed;
    }
    std::uint32_t original_ticks() const {return static_cast<std::uint32_t>(active_ns_/250000);}
private:
    std::uint64_t previous_=0,active_ns_=0;bool anchored_=false;
};
// Normal-path intervals between successful presentation returns. This measures
// sustained CPU presentation cadence, not scanout timestamps or GPU clocks.
struct FrontendPresentationWindow {
    std::uint64_t intervals=0,elapsed_ns=0,max_ns=0;
    double mean_ms() const {return intervals?double(elapsed_ns)/intervals/1000000:0;}
    double fps() const {return elapsed_ns?double(intervals)*1000000000/elapsed_ns:0;}
};
class FrontendPresentationStats {
public:
    void reset() {anchored_=false;warming_=true;window_={};}
    bool sample(std::uint64_t now_ns,FrontendPresentationWindow& result) {
        if(!anchored_ || now_ns<previous_) {
            reset();anchored_=true;previous_=warmup_start_=now_ns;return false;
        }
        const auto interval=now_ns-previous_;previous_=now_ns;
        if(warming_) {
            if(now_ns-warmup_start_>=1000000000ULL)warming_=false;
            return false;
        }
        if(!interval)return false;
        ++window_.intervals;window_.elapsed_ns+=interval;
        if(interval>window_.max_ns)window_.max_ns=interval;
        if(window_.elapsed_ns<2000000000ULL)return false;
        result=window_;window_={};return true;
    }
private:
    std::uint64_t previous_=0,warmup_start_=0;
    bool anchored_=false,warming_=true;
    FrontendPresentationWindow window_;
};
// Original Chyron controller4D74C0: supplied timestamp uses4000Hz game ticks.
// A fresh package starts with constructor4D7450 flags cleared. Native Show
// refreshes the timestamp; an interruption during Hide retains its flag.
class FrontendChyron {
public:
    enum class Action {none,hide,close};
    void show(std::uint32_t ticks) {
        if(!visible_)hiding_=false;
        shown_=ticks;visible_=true;
    }
    Action update(std::uint32_t ticks) {
        const float elapsed=float(static_cast<std::int32_t>(ticks-shown_))*.00025F;
        if(!visible_ || !(elapsed>5.2F))return Action::none;
        if(!hiding_){hiding_=true;return Action::hide;}
        if(elapsed>6.2F){visible_=false;return Action::close;}
        return Action::none;
    }
    bool visible() const {return visible_;}
private:
    std::uint32_t shown_=0;bool visible_=false,hiding_=false;
};
constexpr std::size_t kFrontendUiMaxQuads=2048;
// Frontend-only Z values at or below this tag carry two 10-bit footprints.
// frontend_ui.vert restores the ordinary overlay depth before projection.
constexpr float kFrontendFontVertexZ=-2.0F;
// Original4F0660 admits only positive transformed depth into the UI queue.
inline bool frontend_info_depth_visible(float depth)noexcept{return depth>0;}
// XY projection of the authored quaternion, including non-planar half-turns
// on the original left/right/up/down menu arrows. Column composition stays
// local to these flat information packages; other frontend paths are unchanged.
inline std::array<float,4> frontend_info_rotation(float x,float y,float z,float w)noexcept {
    return {{1-2*(y*y+z*z),2*(x*y-z*w),2*(x*y+z*w),1-2*(x*x+z*z)}};
}
struct FrontendUiAtlas {
    unsigned width=0,height=0;
    std::vector<unsigned char> rgba;
};
struct FrontendUiFrame {
    std::size_t background_quads=0;
    std::vector<TrackVertex> vertices;
    std::vector<unsigned char> additive;
};
// Retail loading hints share one display-count table during a game session.
class RaceLoadingTips {
public:
    unsigned prologue();
    unsigned career(const RaceEventRecord&,const CareerProfile&);
    unsigned quick(QuickRaceMode,unsigned nitrous_level);
    unsigned current()const;
    void shown(unsigned tip);
private:
    std::uint32_t enabled_=0;
    std::array<unsigned char,22> shown_{};
    unsigned career_nitrous_=0,quick_nitrous_=0;
};
// Original PC_Loading presentation; texels remain retail-owned.
class RaceLoadingArt {
public:
    RaceLoadingArt();
    ~RaceLoadingArt();
    bool load(const std::filesystem::path& root,std::string& error,unsigned tip=0);
    // Reference mode is for host diagnostics; runtime always uses the cache.
    void draw(ScreenImage image,double seconds,bool cached=true) const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
enum class PostRaceAction { none, leave, restart, quit_tournament, restart_tournament, next_race };
enum class PostRacePage { menu, statistics, results, style, reward, confirmation };
// Original FE_SOUND notification IDs, not ABK entry ordinals.
enum class PostRaceCue { none=0,left=1,right=2,up=3,down=4,select=5,back=6,dialog=12 };
//49E100: ordered packages;49E040 walks back;496E00/497110/497180/
//4971F0 confirm terminal actions. Static callers may omit animate().
class PostRaceMenu {
public:
    PostRaceMenu()=default;
    PostRaceMenu(bool results,bool style,unsigned rewards,unsigned racers=1,unsigned human=0):
        results_(results),style_(style),reward_count_(rewards),racer_count_(std::clamp(racers,1U,4U)) {
        human_=racer_=std::min(human,racer_count_-1);
        page_=results?PostRacePage::results:style?PostRacePage::style:rewards?PostRacePage::reward:PostRacePage::menu;
    }
    void tournament(bool next) {tournament_=true;next_race_=next;}
    void animate(bool enabled=true) {animated_=enabled;}
    PostRaceAction advance(double seconds) {
        if(!fading_||!std::isfinite(seconds)||seconds<=0||seconds>1)return PostRaceAction::none;
        fade_ticks_+=seconds*1200;
        if(fade_ticks_<200)return PostRaceAction::none;
        fading_=false;page_=pending_page_;reward_index_=pending_reward_;++revision_;
        const auto action=pending_action_;pending_action_=PostRaceAction::none;return action;
    }
    PostRaceAction update(FrontendInput input) {
        cue_=PostRaceCue::none;dialog_sound_=false;
        if(fading_)return PostRaceAction::none;
        if(confirming()) {
            if(input.back){cue_=PostRaceCue::back;page_=PostRacePage::menu;++revision_;return PostRaceAction::none;}
            if(input.left!=input.right||input.up!=input.down) {
                confirm_=!confirm_;cue_=input.left||input.up?PostRaceCue::up:PostRaceCue::down;
            }
            if(input.accept) {cue_=PostRaceCue::select;return transition(PostRacePage::menu,confirm_?confirmation_:PostRaceAction::none);}
            return PostRaceAction::none;
        }
        if(statistics()) {
            const auto before=racer_;
            if(input.left!=input.right) {
                if(input.left&&racer_)--racer_;
                else if(input.right&&racer_+1<racer_count_)++racer_;
            }
            if(before!=racer_)cue_=input.left?PostRaceCue::left:PostRaceCue::right;
            if(input.back||input.accept){cue_=input.back?PostRaceCue::back:PostRaceCue::select;return transition(PostRacePage::menu);}
            return PostRaceAction::none;
        }
        if(page_!=PostRacePage::menu) {
            if(input.back) {
                if(page_==PostRacePage::style&&results_){cue_=PostRaceCue::back;return transition(PostRacePage::results);}
                if(page_==PostRacePage::reward) {
                    if(reward_index_){cue_=PostRaceCue::back;return transition(PostRacePage::reward,PostRaceAction::none,reward_index_-1);}
                    if(style_||results_){cue_=PostRaceCue::back;return transition(style_?PostRacePage::style:PostRacePage::results);}
                }
                return PostRaceAction::none;
            }
            if(input.accept) {
                cue_=PostRaceCue::select;
                if(page_==PostRacePage::results&&style_)return transition(PostRacePage::style);
                if(page_!=PostRacePage::reward&&reward_count_)return transition(PostRacePage::reward);
                if(page_==PostRacePage::reward&&reward_index_+1<reward_count_)return transition(PostRacePage::reward,PostRaceAction::none,reward_index_+1);
                return transition(PostRacePage::menu);
            }
            return PostRaceAction::none;
        }
        if(input.back){cue_=PostRaceCue::back;confirm(tournament_&&next_race_?PostRaceAction::quit_tournament:PostRaceAction::leave);return PostRaceAction::none;}
        if(input.up!=input.down){selected_=(selected_+(input.up?1:count()-1))%count();cue_=input.up?PostRaceCue::up:PostRaceCue::down;}
        if(!input.accept)return PostRaceAction::none;
        cue_=PostRaceCue::select;
        if(selected_==1){racer_=human_;return transition(PostRacePage::statistics);}
        confirm(selected_==0?(tournament_&&next_race_?PostRaceAction::quit_tournament:PostRaceAction::leave):
            tournament_?(selected_==2?PostRaceAction::restart_tournament:selected_==3?PostRaceAction::restart:PostRaceAction::next_race):PostRaceAction::restart);
        return PostRaceAction::none;
    }
    unsigned racer()const{return racer_;}
    unsigned racer_count()const{return racer_count_;}
    unsigned selected()const{return selected_;}
    bool statistics()const{return page_==PostRacePage::statistics;}
    PostRacePage page()const{return page_;}
    unsigned reward_index()const{return reward_index_;}
    unsigned count()const{return tournament_?(next_race_?5:4):3;}
    unsigned revision()const{return revision_;}
    bool tournament()const{return tournament_;}
    bool next_race()const{return next_race_;}
    bool confirming()const{return page_==PostRacePage::confirmation;}
    bool confirmed()const{return confirm_;}
    bool fading()const{return fading_;}
    unsigned fade_tick()const{return unsigned(std::min(fade_ticks_,200.0));}
    PostRaceCue cue()const{return cue_;}
    bool dialog_sound()const{return dialog_sound_;}
    PostRaceAction confirmation()const{return confirmation_;}
private:
    void confirm(PostRaceAction action){confirmation_=action;confirm_=false;page_=PostRacePage::confirmation;dialog_sound_=true;++revision_;}
    PostRaceAction transition(PostRacePage page,PostRaceAction action=PostRaceAction::none,unsigned reward=0) {
        // PostRaceResults authors a200-tick FadeOut. No invented fade
        // is inserted in the confirmation or other packages without it.
        if(animated_&&page_==PostRacePage::results) {
            fading_=true;fade_ticks_=0;pending_page_=page;pending_reward_=reward;pending_action_=action;return PostRaceAction::none;
        }
        page_=page;reward_index_=reward;++revision_;return action;
    }
    unsigned selected_=0;
    bool results_=false,style_=false,tournament_=false,next_race_=false,animated_=false;
    bool fading_=false,confirm_=false,dialog_sound_=false;
    unsigned revision_=0,pending_reward_=0;double fade_ticks_=0;
    PostRaceCue cue_=PostRaceCue::none;
    PostRaceAction confirmation_=PostRaceAction::none,pending_action_=PostRaceAction::none;
    PostRacePage pending_page_=PostRacePage::menu;
    unsigned reward_count_=0,reward_index_=0,racer_=0,racer_count_=1,human_=0;
    PostRacePage page_=PostRacePage::menu;
};

using PostRaceValues=std::map<std::string,std::string>;
// Original MU_Main_PC object transforms, sprite art and proportional FNTF text.
class CareerGarage;
class FrontendArt {
public:
    FrontendArt();
    ~FrontendArt();
    bool load(const std::filesystem::path& root, std::string& error,bool background=true);
    bool load_startup(const std::filesystem::path& root,std::string& error,bool splash=false);
    void draw_startup_splash(ScreenImage image)const;
    void release_startup_splash();
    bool load_underground_welcome(const std::filesystem::path& root,std::string& error);
    void draw_underground_welcome(ScreenImage image,bool exit_selected=false,unsigned tick=600)const;
    void record_underground_welcome(unsigned width,unsigned height,bool exit_selected,FrontendUiFrame& frame,unsigned tick=600)const;
    bool load_profile_pages(const std::filesystem::path&,std::string& error);
    void draw_profiles(ScreenImage,const std::vector<CareerProfileEntry>&,unsigned selected,const std::string& notice)const;
    void record_profiles(unsigned width,unsigned height,const std::vector<CareerProfileEntry>&,unsigned selected,
                         const std::string& notice,FrontendUiFrame&)const;
    void record_options(unsigned width,unsigned height,const RacePresentationOptions&,unsigned selected,
                        const std::string& notice,FrontendUiFrame&)const;
    void draw_pause(ScreenImage,const RacePauseMenu&)const;
    void record_pause(unsigned width,unsigned height,const RacePauseMenu&,FrontendUiFrame&,unsigned fade_out=~0U)const;
    bool load_post_race(const std::filesystem::path& root,std::string& error);
    // Independent result-screen atlas. Career maps and garage textures stay
    // out of the race renderer; source art and its UVs remain unchanged.
    bool prepare_post_race(FrontendArt& target,std::string& error) const;
    void draw_post_race(ScreenImage image,const PostRaceMenu& menu,const PostRaceValues& values)const;
    void record_post_race(unsigned width,unsigned height,const PostRaceMenu& menu,
                          const PostRaceValues& values,FrontendUiFrame& frame)const;
    bool load_career_event(const std::filesystem::path&,unsigned event,std::string& error);
    bool load_magazine(const std::filesystem::path&,const CareerMagazineRecipe&,std::string& error);
    void draw_magazine(ScreenImage,const CareerMagazineRecipe&,const std::string& car,bool background)const;
    void record_magazine(unsigned width,unsigned height,const CareerMagazineRecipe&,
                         const std::string& car,FrontendUiFrame&)const;
    bool load_statistics(const std::filesystem::path& root,bool& atlas_changed,std::string& error);
    bool load_career_pages(const std::filesystem::path& root,const CareerCatalog&,std::string& error);
    void draw_career_info(ScreenImage,const CareerCatalog&,const CareerProfile&,const CareerInfoMenu&)const;
    void record_career_info(unsigned width,unsigned height,const CareerCatalog&,const CareerProfile&,
                           const CareerInfoMenu&,FrontendUiFrame&)const;
    void draw_career(ScreenImage,const CareerCatalog&,const CareerProfile&,unsigned selection,
                     const std::string& notice={},const CareerGarage* garage=nullptr)const;
    void record_career(unsigned width,unsigned height,const CareerCatalog&,const CareerProfile&,
                       unsigned selection,const std::string& notice,FrontendUiFrame&,const CareerGarage* garage=nullptr)const;
    void draw_free_drive(ScreenImage,const CareerCatalog&,const CareerProfile&,unsigned selection,unsigned course,const std::string& notice,const QuickRaceOptions* options=nullptr,bool choose_mode=false)const;
    void record_free_drive(unsigned width,unsigned height,const CareerCatalog&,const CareerProfile&,
                           unsigned selection,unsigned course,const std::string& notice,FrontendUiFrame&,const QuickRaceOptions* options=nullptr,bool choose_mode=false)const;
    void append_fps(unsigned width,unsigned height,const FrontendPresentationWindow&,FrontendUiFrame&)const;
    void advance(float seconds);
    void draw(ScreenImage image, const FrontendMenu& menu) const;
    void draw_overlay(ScreenImage image,const FrontendMenu& menu) const;
    const FrontendUiAtlas& atlas() const;
    void record_ui(unsigned width,unsigned height,const FrontendMenu& menu,FrontendUiFrame& frame) const;
private:
    bool load_assets(const std::filesystem::path&,std::string&,bool background,bool startup,bool splash=false);
    void decode_startup_splash(const std::vector<unsigned char>&,std::string&);
    void decode_welcome(const std::vector<unsigned char>&,const std::vector<unsigned char>&,
                        const std::vector<unsigned char>&,std::string&);
    void decode_profiles(const std::vector<unsigned char>&,const std::vector<unsigned char>&,std::string&);
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
