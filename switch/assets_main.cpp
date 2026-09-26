// nfsu_switch_assets: whether the SD card holds the retail files the first
// lap needs (issue #47), shown on screen. docs/SWITCH_ASSETS.md.
//
// It reads the config file sdmc:/config/nfsu_switch/assets.cfg when there
// is one, then checks the first-lap set under the root in force -
// sdmc:/nfsu unless the config file names another - with the check
// `nfsu_probe asset-check` runs on the desktop, through the console's file
// system service, and shows the report: the config, one line per file, the
// verdict, the files still to copy and how long the check took. Nothing
// goes to stdout or the network: the report is read, and photographed,
// from the screen. Every file is opened for reading only and nothing is
// written anywhere. + exits.

#include "nfsu/assets.h"
#include "nfsu/posix_file_system.h"
#include "nfsu/screen_text.h"

#include <switch.h>

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <new>
#include <string>
#include <vector>

namespace {

constexpr std::uint32_t kScreenWidth = 1280;
constexpr std::uint32_t kScreenHeight = 720;
constexpr std::uint32_t kTextScale = 2;
constexpr std::uint32_t kLineHeight = nfsu::kScreenCellHeight * kTextScale;
constexpr std::size_t kColumns = kScreenWidth / (nfsu::kScreenCellWidth * kTextScale);
constexpr std::size_t kScreenLines = kScreenHeight / kLineHeight;
// How often the progress line is redrawn while a file is hashed.
constexpr std::uint64_t kProgressStep = 8U * 1024U * 1024U;

constexpr nfsu::ScreenColor kPaper{0, 0, 0, 255};
constexpr nfsu::ScreenColor kPlain{230, 230, 230, 255};
constexpr nfsu::ScreenColor kGood{120, 230, 120, 255};
constexpr nfsu::ScreenColor kBad{255, 110, 90, 255};
constexpr nfsu::ScreenColor kNote{240, 210, 110, 255};
constexpr nfsu::ScreenColor kBackground{40, 44, 52, 255};

nfsu::ScreenColor ink_for(nfsu::AssetTone tone) {
    switch (tone) {
    case nfsu::AssetTone::good:
        return kGood;
    case nfsu::AssetTone::bad:
        return kBad;
    case nfsu::AssetTone::note:
        return kNote;
    case nfsu::AssetTone::plain:
        break;
    }
    return kPlain;
}

struct Line {
    std::string text;
    nfsu::ScreenColor ink;
};

// The report from the top of the screen down, wrapped to its width, with
// one status line after it. When there are more lines than fit, the last
// ones win, so the verdict and the files to copy stay on screen.
class Screen {
public:
    bool open() {
        if (R_FAILED(framebufferCreate(&framebuffer_, nwindowGetDefault(), kScreenWidth,
                                       kScreenHeight, PIXEL_FORMAT_RGBA_8888, 2))) {
            return false;
        }
        if (R_FAILED(framebufferMakeLinear(&framebuffer_))) {
            framebufferClose(&framebuffer_);
            return false;
        }
        open_ = true;
        return true;
    }

    void close() {
        if (open_) {
            framebufferClose(&framebuffer_);
            open_ = false;
        }
    }

    void add(const std::string& text, nfsu::ScreenColor ink) {
        for (const std::string& line : nfsu::wrap_screen_text(text, kColumns)) {
            lines_.push_back(Line{line, ink});
        }
    }

    void set_status(const std::string& text, nfsu::ScreenColor ink) {
        status_.clear();
        if (!text.empty()) {
            for (const std::string& line : nfsu::wrap_screen_text(text, kColumns)) {
                status_.push_back(Line{line, ink});
            }
        }
    }

    void present() {
        if (!open_) {
            return;
        }
        std::uint32_t stride = 0;
        unsigned char* pixels =
            static_cast<unsigned char*>(framebufferBegin(&framebuffer_, &stride));
        if (pixels == nullptr) {
            return;
        }
        const nfsu::ScreenImage image{pixels, kScreenWidth, kScreenHeight, stride};
        nfsu::fill_screen_rect(image, 0, 0, kScreenWidth, kScreenHeight, kBackground);
        shown_.assign(lines_.begin(), lines_.end());
        shown_.insert(shown_.end(), status_.begin(), status_.end());
        const std::size_t first = shown_.size() > kScreenLines ? shown_.size() - kScreenLines : 0;
        for (std::size_t i = first; i < shown_.size(); ++i) {
            nfsu::draw_screen_text(image, 0, static_cast<std::int64_t>((i - first) * kLineHeight),
                                   shown_[i].text, kTextScale, shown_[i].ink, kPaper);
        }
        framebufferEnd(&framebuffer_);
    }

private:
    Framebuffer framebuffer_{};
    bool open_ = false;
    std::vector<Line> lines_;
    std::vector<Line> status_;
    std::vector<Line> shown_;
};

// Keeps the screen alive while the check runs: which file, and how far
// through a large one.
class Progress final : public nfsu::AssetCheckObserver {
public:
    explicit Progress(Screen& screen) : screen_(screen) {}

    void file_started(std::size_t index, std::size_t total, const nfsu::AssetFile& file) override {
        current_ = "checking " + std::to_string(index + 1) + " of " + std::to_string(total) +
                   ": " + file.path + " (" + std::to_string(file.size) + " bytes)";
        next_ = kProgressStep;
        screen_.set_status(current_, kNote);
        screen_.present();
    }

    void file_progress(std::size_t, std::uint64_t hashed, std::uint64_t size) override {
        if (hashed < next_ || size == 0) {
            return;
        }
        next_ = hashed + kProgressStep;
        screen_.set_status(current_ + "  " + std::to_string(hashed * 100U / size) + "%", kNote);
        screen_.present();
    }

private:
    Screen& screen_;
    std::string current_;
    std::uint64_t next_ = kProgressStep;
};

std::string seconds(std::uint64_t start, std::uint64_t end) {
    const std::uint64_t nanoseconds = armTicksToNs(end - start);
    char text[32];
    std::snprintf(text, sizeof text, "%llu.%03llu",
                  static_cast<unsigned long long>(nanoseconds / 1000000000U),
                  static_cast<unsigned long long>((nanoseconds / 1000000U) % 1000U));
    return text;
}

void check_first_lap(Screen& screen) {
    nfsu::AssetManifest manifest;
    if (!nfsu::retail_asset_manifest("first-lap", manifest)) {
        screen.add("this build holds no first-lap set", kBad);
        return;
    }
    Progress progress(screen);
    const std::uint64_t start = armGetSystemTick();
    const nfsu::ConsoleAssetCheck result = nfsu::run_console_asset_check(
        nfsu::posix_file_system(), nfsu::ConsoleAssetSetup{}, manifest, &progress);
    const std::uint64_t end = armGetSystemTick();
    screen.set_status("", kPlain);
    for (const nfsu::AssetLine& line : result.lines) {
        screen.add(line.text, ink_for(line.tone));
    }
    if (result.checked) {
        screen.add("checked in " + seconds(start, end) + " s", kPlain);
    }
}

} // namespace

int main() {
    padConfigureInput(1, HidNpadStyleSet_NpadStandard);
    PadState pad;
    padInitializeDefault(&pad);

    Screen screen;
    screen.open();
    screen.add("nfsu_switch_assets: the retail files the first lap needs, on this SD card "
               "(issue #47)",
               kPlain);
    screen.present();

    try {
        check_first_lap(screen);
    } catch (const std::bad_alloc&) {
        screen.add("stopped: out of memory", kBad);
    } catch (const std::exception& caught) {
        screen.add(std::string("stopped: ") + caught.what(), kBad);
    } catch (...) {
        screen.add("stopped: an unknown exception", kBad);
    }

    // appletMainLoop returns false when the system asks the application to
    // close, so either way out of this loop reaches the shutdown below.
    screen.set_status("Press + to exit.", kPlain);
    while (appletMainLoop()) {
        padUpdate(&pad);
        if ((padGetButtonsDown(&pad) & HidNpadButton_Plus) != 0) {
            break;
        }
        screen.present();
    }

    screen.close();
    return 0;
}
