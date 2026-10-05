#pragma once
#include "nfsu/screen_text.h"
#include "nfsu/track.h"
#include <filesystem>
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
    void song(const char* artist,const char* title,const char* album) {artist_=artist;title_=title;album_=album;}
    const std::string& artist() const {return artist_;}
    const std::string& title() const {return title_;}
    const std::string& album() const {return album_;}
private:
    unsigned selected_=0;
    std::string notice_;
    std::string artist_="Lil Jon & the Eastside Boyz",title_="Get Low",album_="Kings of Crunk";
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
