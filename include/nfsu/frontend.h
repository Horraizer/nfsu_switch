#pragma once
#include "nfsu/screen_text.h"
#include <filesystem>
#include <string>
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
private:
    unsigned selected_=0;
    std::string notice_;
};
// Only original texture pixels are retained. Layout and text are port UI;
// this does not claim to interpret the original compressed FNG animations.
class FrontendArt {
public:
    bool load(const std::filesystem::path& root, std::string& error);
    void draw(ScreenImage image, const FrontendMenu& menu) const;
private:
    struct Image { unsigned width=0,height=0; std::vector<unsigned char> rgba; };
    Image background_,selection_,logo_;
    static void blit(ScreenImage target, const Image& source,
                     unsigned x,unsigned y,unsigned w,unsigned h);
};
}
