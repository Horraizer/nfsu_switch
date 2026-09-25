// The stock 240SX on the Nintendo Switch, drawn by the CPU reference
// renderer running on the console (issue #141). docs/SWITCH_BUILD.md.
//
// It reads the six files of the pinned desktop frame from the SD card -
// sdmc:/nfsu/ by default, laid out as the game lays out its installation -
// builds the stock scene with the same library calls the desktop viewer
// makes for that frame (nfsu_stock_frame), renders it at 1280x720 with
// nfsu_render, and shows it through a libnx framebuffer. It reports the
// render time and the SHA-256 of the frame encoded exactly as the desktop
// TGA writer encodes it, on screen and on stdout, which is the PC when the
// application was started with `nxlink -s`. Equal to the pinned desktop
// hash or not, the result is reported; a difference is not hidden.
//
// It opens its inputs for reading only and writes no file anywhere. A
// missing or unreadable input is reported by its path and nothing is
// rendered; a parser's complaint about an input is reported with the
// input's path. + exits, and so does a request from the system to close.

#include "nfsu/screen_text.h"
#include "nfsu/stock_frame.h"

#include <switch.h>

#include <arpa/inet.h>
#include <unistd.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <exception>
#include <memory>
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
constexpr const char* kDefaultRoot = "sdmc:/nfsu";

constexpr nfsu::ScreenColor kPaper{0, 0, 0, 255};
constexpr nfsu::ScreenColor kPlain{230, 230, 230, 255};
constexpr nfsu::ScreenColor kGood{120, 230, 120, 255};
constexpr nfsu::ScreenColor kBad{255, 110, 90, 255};
// The background before there is a frame: the renderer's own default
// background, so the screen does not change colour when the frame appears.
constexpr nfsu::ScreenColor kBackground{40, 44, 52, 255};

// The display. Until there is a frame it shows the report so far from the
// top of the screen down. Once there is one it shows the frame, with the
// lines added after it anchored to the bottom edge, below the car.
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

    // The frame is borrowed, not copied, and must outlive every present().
    // A buffer of any other size than 1280x720 RGBA8 is not shown.
    void show_frame(const std::vector<unsigned char>& rgba) {
        if (rgba.size() == std::size_t{kScreenWidth} * kScreenHeight * 4U) {
            frame_ = &rgba;
        }
    }

    void add(const std::string& text, nfsu::ScreenColor ink) {
        std::vector<Line>& lines = frame_ != nullptr ? caption_ : log_;
        for (const std::string& line : nfsu::wrap_screen_text(text, kColumns)) {
            lines.push_back(Line{line, ink});
        }
    }

    // Composes the picture into the linear buffer and queues it. The
    // buffer's rows are stride bytes apart, as framebufferBegin reports,
    // which need not be the width times four.
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
        if (frame_ != nullptr) {
            for (std::uint32_t y = 0; y < kScreenHeight; ++y) {
                unsigned char* row = pixels + std::size_t{y} * stride;
                std::memcpy(row, frame_->data() + std::size_t{y} * kScreenWidth * 4U,
                            std::size_t{kScreenWidth} * 4U);
                // Shown opaque whatever the frame's alpha holds; the frame
                // itself, and so its hash, stays as rendered.
                for (std::uint32_t x = 0; x < kScreenWidth; ++x) {
                    row[std::size_t{x} * 4U + 3U] = 255;
                }
            }
            draw(image, caption_, kScreenLines > caption_.size()
                                      ? kScreenLines - caption_.size()
                                      : 0);
        } else {
            nfsu::fill_screen_rect(image, 0, 0, kScreenWidth, kScreenHeight, kBackground);
            draw(image, log_, 0);
        }
        framebufferEnd(&framebuffer_);
    }

private:
    struct Line {
        std::string text;
        nfsu::ScreenColor ink;
    };

    // Draws lines from screen row first_row down. When there are more than
    // fit, the last ones win, so the outcome is always on screen.
    static void draw(const nfsu::ScreenImage& image, const std::vector<Line>& lines,
                     std::size_t first_row) {
        const std::size_t room = kScreenLines - first_row;
        const std::size_t first = lines.size() > room ? lines.size() - room : 0;
        for (std::size_t i = first; i < lines.size(); ++i) {
            const std::size_t row = first_row + (i - first);
            nfsu::draw_screen_text(image, 0, static_cast<std::int64_t>(row * kLineHeight),
                                   lines[i].text, kTextScale, lines[i].ink, kPaper);
        }
    }

    Framebuffer framebuffer_{};
    bool open_ = false;
    const std::vector<unsigned char>* frame_ = nullptr;
    std::vector<Line> log_;
    std::vector<Line> caption_;
};

// One line of the report, to stdout and to the screen.
class Report {
public:
    explicit Report(Screen& screen) : screen_(screen) {}

    void line(const std::string& text, nfsu::ScreenColor ink = kPlain) {
        std::printf("%s\n", text.c_str());
        std::fflush(stdout);
        screen_.add(text, ink);
    }

    // stdout only: the listings the screen has no room for.
    void detail(const std::string& text) {
        std::printf("%s\n", text.c_str());
        std::fflush(stdout);
    }

private:
    Screen& screen_;
};

std::string milliseconds(std::uint64_t start, std::uint64_t end) {
    const std::uint64_t nanoseconds = armTicksToNs(end - start);
    char text[32];
    std::snprintf(text, sizeof text, "%llu.%03llu",
                  static_cast<unsigned long long>(nanoseconds / 1000000U),
                  static_cast<unsigned long long>((nanoseconds / 1000U) % 1000U));
    return text;
}

// Reads, prepares, renders, encodes and reports. The frame stays in `frame`
// for the screen to show after this returns.
void run(const std::string& root, nfsu::StockFrame& frame, Report& report, Screen& screen) {
    const std::uint64_t read_start = armGetSystemTick();
    auto inputs = std::make_unique<nfsu::StockFrameInputs>();
    nfsu::read_stock_frame_inputs(root, *inputs);
    const std::uint64_t read_end = armGetSystemTick();

    std::size_t missing = 0;
    for (const nfsu::StockFrameInput& input : inputs->files) {
        if (input.read) {
            report.line("read " + input.path + ": " + std::to_string(input.bytes.size()) +
                        " bytes");
        } else {
            ++missing;
            report.line("NOT READ " + root + "/" + input.path + ": " + input.error, kBad);
        }
    }
    if (missing != 0) {
        report.line("nothing rendered: " + std::to_string(missing) + " of " +
                        std::to_string(nfsu::kStockFrameFileCount) +
                        " inputs could not be read. Copy each from the game's own "
                        "installation to the same path under " + root + ".",
                    kBad);
        return;
    }
    report.line("preparing the stock scene...");
    screen.present();

    std::string error;
    const std::uint64_t prepare_start = armGetSystemTick();
    const bool prepared =
        nfsu::prepare_stock_frame(*inputs, nfsu::stock_240sx_request(), frame, error);
    const std::uint64_t prepare_end = armGetSystemTick();
    for (const std::string& issue : frame.issues) {
        report.line("issue: " + issue, kBad);
    }
    if (!prepared) {
        report.line("nothing rendered: " + error, kBad);
        return;
    }
    report.line("rendering 1280x720 with nfsu_render...");
    screen.present();

    const std::uint64_t render_start = armGetSystemTick();
    const bool rendered = nfsu::render_stock_frame(frame, nfsu::RenderOptions{}, error);
    const std::uint64_t render_end = armGetSystemTick();
    if (!rendered) {
        report.line("nothing rendered: " + error, kBad);
        return;
    }

    const std::uint64_t encode_start = armGetSystemTick();
    nfsu::StockFrameImage image;
    const bool encoded = nfsu::encode_stock_frame(frame.frame, image, error);
    const std::uint64_t encode_end = armGetSystemTick();

    // From here on the screen shows the frame, and every line below is its
    // caption.
    screen.show_frame(frame.frame.rgba());
    report.line("nfsu_switch_car: the stock 240SX from " + root +
                ", CPU reference renderer, 1280x720");
    report.line("render_ms " + milliseconds(render_start, render_end) + "  prepare_ms " +
                milliseconds(prepare_start, prepare_end) + "  read_ms " +
                milliseconds(read_start, read_end) + "  encode_sha256_ms " +
                milliseconds(encode_start, encode_end));
    report.detail(nfsu::stock_frame_slots_line(frame.report));
    for (const std::string& line : nfsu::stock_frame_stats_lines(frame.stats)) {
        report.detail(line);
    }
    if (!frame.inputs_ok) {
        report.line(std::to_string(frame.issues.size()) +
                        " issues were reported with the inputs; they are listed above "
                        "and on stdout",
                    kBad);
    }
    if (!encoded) {
        report.line("the frame could not be encoded: " + error, kBad);
        return;
    }
    const std::string hash = nfsu::sha256_hex(image.sha256);
    report.line("tga_bytes " + std::to_string(image.tga.size()) + "  sha256 " + hash);
    if (hash == nfsu::kStock240sxFrameSha256 &&
        image.tga.size() == nfsu::kStock240sxFrameBytes) {
        report.line("result: EQUAL to the pinned desktop frame", kGood);
    } else {
        report.line("result: DIFFERENT from the pinned desktop frame, " +
                        std::to_string(nfsu::kStock240sxFrameBytes) + " bytes, sha256 " +
                        std::string(nfsu::kStock240sxFrameSha256),
                    kBad);
    }
}

} // namespace

int main(int argc, char** argv) {
    // nxlink sets the host address when it starts the application; without
    // it there is nobody to send stdout to, and no socket is opened.
    bool sockets = false;
    int nxlink_socket = -1;
    if (__nxlink_host.s_addr != 0 && R_SUCCEEDED(socketInitializeDefault())) {
        sockets = true;
        nxlink_socket = nxlinkStdio();
    }

    padConfigureInput(1, HidNpadStyleSet_NpadStandard);
    PadState pad;
    padInitializeDefault(&pad);

    Screen screen;
    const bool have_screen = screen.open();
    Report report(screen);
    report.line("nfsu_switch_car: the stock 240SX, drawn by the CPU reference renderer");
    if (!have_screen) {
        report.detail("the framebuffer could not be created; reporting on stdout only");
    }
    const std::string root =
        argc >= 2 && argv[1] != nullptr && argv[1][0] != '\0' ? argv[1] : kDefaultRoot;
    report.line("inputs: " + root);
    screen.present();

    // Kept until exit: the screen shows its frame.
    std::unique_ptr<nfsu::StockFrame> frame;
    try {
        frame = std::make_unique<nfsu::StockFrame>();
        run(root, *frame, report, screen);
    } catch (const std::bad_alloc&) {
        report.line("stopped: out of memory", kBad);
    } catch (const std::exception& caught) {
        report.line(std::string("stopped: ") + caught.what(), kBad);
    } catch (...) {
        report.line("stopped: an unknown exception", kBad);
    }

    report.line("Press + to exit.");
    while (appletMainLoop()) {
        padUpdate(&pad);
        if ((padGetButtonsDown(&pad) & HidNpadButton_Plus) != 0) {
            break;
        }
        screen.present();
    }

    screen.close();
    if (nxlink_socket >= 0) {
        close(nxlink_socket);
    }
    if (sockets) {
        socketExit();
    }
    return 0;
}
