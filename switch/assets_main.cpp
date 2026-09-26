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
// written anywhere. + exits, and so does a request from the system to close,
// during the check as well as after it; the HOME button suspends it there
// too.

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
// the status after it. When there are more lines than fit, the first ones
// win and the last line that fits says how many do not: the report comes
// summary first (nfsu::AssetReportForm::console), so what a full screen
// cuts off is the tail of the per-file lines, never the title, the config,
// the verdict or the files to copy. The status - the progress, then the
// time and "Press + to exit." - is always shown.
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
        const std::size_t room =
            kScreenLines > status_.size() ? kScreenLines - status_.size() : 0;
        if (lines_.size() <= room) {
            shown_.assign(lines_.begin(), lines_.end());
        } else if (room > 0) {
            const std::size_t kept = room - 1;
            shown_.assign(lines_.begin(), lines_.begin() + static_cast<std::ptrdiff_t>(kept));
            shown_.push_back(Line{"... " + std::to_string(lines_.size() - kept) +
                                      " more lines do not fit on the screen",
                                  kNote});
        } else {
            shown_.clear();
        }
        shown_.insert(shown_.end(), status_.begin(), status_.end());
        for (std::size_t i = 0; i < shown_.size() && i < kScreenLines; ++i) {
            nfsu::draw_screen_text(image, 0, static_cast<std::int64_t>(i * kLineHeight),
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

// Thrown out of the check when + is pressed or the system asks the
// application to close while it runs, and caught in main(), which then
// closes the screen and returns. The check holds nothing but memory - no
// file stays open between the service's calls - so leaving it this way
// frees everything it held.
struct ExitRequested {};

// Keeps the application alive while the check runs: the system's messages
// and the + button between files and after every chunk hashed, which is at
// most kAssetReadChunkBytes, and on screen which file and how far through a
// large one.
class Progress final : public nfsu::AssetCheckObserver {
public:
    Progress(Screen& screen, PadState& pad) : screen_(screen), pad_(pad) {}

    void file_started(std::size_t index, std::size_t total, const nfsu::AssetFile& file) override {
        keep_alive();
        current_ = "checking " + std::to_string(index + 1) + " of " + std::to_string(total) +
                   ": " + file.path + " (" + std::to_string(file.size) + " bytes)";
        next_ = kProgressStep;
        screen_.set_status(current_, kNote);
        screen_.present();
    }

    void file_progress(std::size_t, std::uint64_t hashed, std::uint64_t size) override {
        keep_alive();
        if (hashed < next_ || size == 0) {
            return;
        }
        next_ = hashed + kProgressStep;
        screen_.set_status(current_ + "  " + std::to_string(hashed * 100U / size) + "%", kNote);
        screen_.present();
    }

private:
    // appletMainLoop services the system: it returns false once the system
    // asks the application to close, and it holds the application, as the
    // wait for + does, while the HOME menu has the screen.
    void keep_alive() {
        if (!appletMainLoop()) {
            throw ExitRequested{};
        }
        padUpdate(&pad_);
        if ((padGetButtonsDown(&pad_) & HidNpadButton_Plus) != 0) {
            throw ExitRequested{};
        }
    }

    Screen& screen_;
    PadState& pad_;
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

// Runs the check and puts its report on the screen. Returns the time the
// check took, as the status line shows it, or nothing when there was no
// check.
std::string check_first_lap(Screen& screen, PadState& pad) {
    nfsu::AssetManifest manifest;
    if (!nfsu::retail_asset_manifest("first-lap", manifest)) {
        screen.add("this build holds no first-lap set", kBad);
        return "";
    }
    Progress progress(screen, pad);
    const std::uint64_t start = armGetSystemTick();
    const nfsu::ConsoleAssetCheck result = nfsu::run_console_asset_check(
        nfsu::posix_file_system(), nfsu::ConsoleAssetSetup{}, manifest, &progress);
    const std::uint64_t end = armGetSystemTick();
    screen.set_status("", kPlain);
    for (const nfsu::AssetLine& line : result.lines) {
        screen.add(line.text, ink_for(line.tone));
    }
    return result.checked ? "checked in " + seconds(start, end) + " s. " : std::string();
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

    std::string timing;
    bool leave = false;
    try {
        timing = check_first_lap(screen, pad);
    } catch (const ExitRequested&) {
        leave = true;
    } catch (const std::bad_alloc&) {
        screen.add("stopped: out of memory", kBad);
    } catch (const std::exception& caught) {
        screen.add(std::string("stopped: ") + caught.what(), kBad);
    } catch (...) {
        screen.add("stopped: an unknown exception", kBad);
    }

    // appletMainLoop returns false when the system asks the application to
    // close, so either way out of this loop reaches the shutdown below.
    screen.set_status(timing + "Press + to exit.", kPlain);
    while (!leave && appletMainLoop()) {
        padUpdate(&pad);
        if ((padGetButtonsDown(&pad) & HidNpadButton_Plus) != 0) {
            break;
        }
        screen.present();
    }

    screen.close();
    return 0;
}
