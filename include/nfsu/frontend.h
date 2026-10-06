#pragma once
#include "nfsu/screen_text.h"
#include "nfsu/track.h"
#include <filesystem>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>
namespace nfsu {
enum class FrontendAction { none, underground, free_drive, exit };
struct FrontendInput { bool up=false, down=false, accept=false, back=false; };
class FrontendMenu {
public:
    FrontendAction update(FrontendInput input);
    unsigned selected() const { return selected_; }
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
struct FrontendUiAtlas {
    unsigned width=0,height=0;
    std::vector<unsigned char> rgba;
};
struct FrontendUiFrame {
    std::vector<TrackVertex> vertices;
    std::vector<unsigned char> additive;
};
// Original MU_Main_PC object transforms, sprite art and proportional FNTF text.
class FrontendArt {
public:
    FrontendArt();
    ~FrontendArt();
    bool load(const std::filesystem::path& root, std::string& error,bool background=true);
    void advance(float seconds);
    void draw(ScreenImage image, const FrontendMenu& menu) const;
    void draw_overlay(ScreenImage image,const FrontendMenu& menu) const;
    const FrontendUiAtlas& atlas() const;
    void record_ui(unsigned width,unsigned height,const FrontendMenu& menu,FrontendUiFrame& frame) const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
