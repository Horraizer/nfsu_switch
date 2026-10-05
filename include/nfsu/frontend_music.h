#pragma once
#include "nfsu/audio.h"
#include "nfsu/frontend_sfx.h"
#include <filesystem>
#include <memory>
#include <string>
#include <functional>
namespace nfsu::audio {
// Fresh-profile native defaults: FE songs 19..25, IG songs 0..18.
// Bank IDs are metadata indices plus one; entry zero is not a song.
std::vector<Segment> original_music_playlist(const Bank& bank,
    const std::vector<Segment>& indexed, bool frontend);
struct MenuSong { const char* title; const char* artist; const char* album; };
MenuSong original_menu_song(unsigned index);
class FrontendMusic {
public:
    FrontendMusic();
    ~FrontendMusic();
    bool start(const std::filesystem::path& root, std::string& error,
        const std::function<bool()>& thread_setup = {});
    // The caller keeps servicing input/applet/presentation while the device
    // plays the fade and any original cue. stop() then releases the device.
    void begin_stop();
    bool stop_ready() const;
    void stop();
    void sound(FrontendSound cue);
    std::string error() const;
    MenuSong now_playing() const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
