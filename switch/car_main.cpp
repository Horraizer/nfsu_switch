// The stock 240SX on the Nintendo Switch, drawn by the CPU reference
// renderer running on the console (issue #141), painted with the runtime
// body skin and orbited with the left stick (issue #148).
// docs/SWITCH_BUILD.md.
//
// It reads the six files of the pinned desktop frame from the SD card -
// sdmc:/nfsu/ by default, laid out as the game lays out its installation -
// builds the stock scene with the same library calls the desktop viewer
// makes for that frame (nfsu_stock_frame), composes and binds the body skin
// the way the viewer's --skin-car does, renders it at 1280x720 with
// nfsu_render, and shows it through a libnx framebuffer. At the default
// camera it reports the SHA-256 of the frame encoded exactly as the desktop
// TGA writer encodes it and compares it with the pinned desktop hash, on
// screen and on stdout, which is the PC when the application was started
// with `nxlink -s`. Equal or not, the result is reported; a difference is
// not hidden.
//
// Then the controller takes over, through the controller model of
// docs/INPUT.md and the control table of nfsu_stock_view: the left stick
// orbits the camera and the frame is rendered again whenever the camera
// moved, - returns the camera to the default and the hash is compared
// again, + exits. Away from the default camera there is no pinned frame to
// compare with, and the screen says so instead of showing a result. The
// frame time is shown throughout.
//
// With the argument --unpainted it prepares the frame of issue #141
// instead, without the skin, and compares it with that frame's pin. With
// --car NAME (issue #315) it draws any playable racer of the retail car
// type table, resolved against the root's Global/GLOBALB.BUN, reading that
// car's own Cars/<NAME> files; a car other than the 240SX has no pinned
// desktop frame, and the application reports its hash and says so. Any
// other argument replaces sdmc:/nfsu as the root.
//
// It opens its inputs for reading only and writes no file anywhere. A
// missing or unreadable input is reported by its path and nothing is
// rendered; a parser's complaint about an input is reported with the
// input's path. A request from the system to close ends it as + does.

#include "nfsu/input.h"
#include "nfsu/screen_text.h"
#include "nfsu/stock_frame.h"
#include "nfsu/stock_view.h"

#include <switch.h>

#include <arpa/inet.h>
#include <unistd.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <exception>
#include <filesystem>
#include <memory>
#include <new>
#include <string>
#include <vector>

// The button mapping in nfsu_stock_view is written with libnx's bit values
// so that a desktop test can run it; these are the values libnx itself
// declares, checked where both are visible.
static_assert(nfsu::kNpadButtonA == static_cast<std::uint64_t>(HidNpadButton_A), "A");
static_assert(nfsu::kNpadButtonB == static_cast<std::uint64_t>(HidNpadButton_B), "B");
static_assert(nfsu::kNpadButtonX == static_cast<std::uint64_t>(HidNpadButton_X), "X");
static_assert(nfsu::kNpadButtonY == static_cast<std::uint64_t>(HidNpadButton_Y), "Y");
static_assert(nfsu::kNpadButtonStickL == static_cast<std::uint64_t>(HidNpadButton_StickL), "StickL");
static_assert(nfsu::kNpadButtonStickR == static_cast<std::uint64_t>(HidNpadButton_StickR), "StickR");
static_assert(nfsu::kNpadButtonL == static_cast<std::uint64_t>(HidNpadButton_L), "L");
static_assert(nfsu::kNpadButtonR == static_cast<std::uint64_t>(HidNpadButton_R), "R");
static_assert(nfsu::kNpadButtonZL == static_cast<std::uint64_t>(HidNpadButton_ZL), "ZL");
static_assert(nfsu::kNpadButtonZR == static_cast<std::uint64_t>(HidNpadButton_ZR), "ZR");
static_assert(nfsu::kNpadButtonPlus == static_cast<std::uint64_t>(HidNpadButton_Plus), "Plus");
static_assert(nfsu::kNpadButtonMinus == static_cast<std::uint64_t>(HidNpadButton_Minus), "Minus");
static_assert(nfsu::kNpadButtonLeft == static_cast<std::uint64_t>(HidNpadButton_Left), "Left");
static_assert(nfsu::kNpadButtonUp == static_cast<std::uint64_t>(HidNpadButton_Up), "Up");
static_assert(nfsu::kNpadButtonRight == static_cast<std::uint64_t>(HidNpadButton_Right), "Right");
static_assert(nfsu::kNpadButtonDown == static_cast<std::uint64_t>(HidNpadButton_Down), "Down");
// The sticks pass through unscaled: libnx's range is the model's.
static_assert(JOYSTICK_MAX == nfsu::kAxisMax, "stick range");

namespace {

constexpr std::uint32_t kScreenWidth = 1280;
constexpr std::uint32_t kScreenHeight = 720;
constexpr std::uint32_t kTextScale = 2;
constexpr std::uint32_t kLineHeight = nfsu::kScreenCellHeight * kTextScale;
constexpr std::size_t kColumns = kScreenWidth / (nfsu::kScreenCellWidth * kTextScale);
constexpr std::size_t kScreenLines = kScreenHeight / kLineHeight;
constexpr const char* kDefaultRoot = "sdmc:/nfsu";
constexpr const char* kUnpaintedArgument = "--unpainted";

constexpr nfsu::ScreenColor kPaper{0, 0, 0, 255};
constexpr nfsu::ScreenColor kPlain{230, 230, 230, 255};
constexpr nfsu::ScreenColor kGood{120, 230, 120, 255};
constexpr nfsu::ScreenColor kBad{255, 110, 90, 255};
constexpr nfsu::ScreenColor kNote{240, 210, 110, 255};
// The background before there is a frame: the renderer's own default
// background, so the screen does not change colour when the frame appears.
constexpr nfsu::ScreenColor kBackground{40, 44, 52, 255};

struct Line {
    std::string text;
    nfsu::ScreenColor ink;
};

// The display. Until there is a frame it shows the report so far from the
// top of the screen down. Once there is one it shows the frame, with the
// caption - the lines reported after it - and then the status, which is
// replaced every frame, anchored to the bottom edge, below the car.
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

    // Replaces the status lines drawn below the caption.
    void set_status(const std::vector<Line>& status) {
        status_.clear();
        for (const Line& line : status) {
            for (const std::string& wrapped : nfsu::wrap_screen_text(line.text, kColumns)) {
                status_.push_back(Line{wrapped, line.ink});
            }
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
            bottom_.assign(caption_.begin(), caption_.end());
            bottom_.insert(bottom_.end(), status_.begin(), status_.end());
            draw(image, bottom_, kScreenLines > bottom_.size() ? kScreenLines - bottom_.size()
                                                               : 0);
        } else {
            nfsu::fill_screen_rect(image, 0, 0, kScreenWidth, kScreenHeight, kBackground);
            draw(image, log_, 0);
        }
        framebufferEnd(&framebuffer_);
    }

private:
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
    std::vector<Line> status_;
    // The caption and the status together, kept between presents so that a
    // frame allocates nothing once it has grown.
    std::vector<Line> bottom_;
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

std::string degrees(double value) {
    char text[32];
    std::snprintf(text, sizeof text, "%.2f", value);
    return text;
}

// Which frame this run shows, and the desktop pin it is compared with.
struct Choice {
    std::string root = kDefaultRoot;
    bool painted = true;
    bool tuned = false;
    std::string ignored;
    // The car type name to load; resolved against the retail car type table
    // in main, which reports a name no record holds.
    std::string car_name = "240SX";
    nfsu::CarAssets car;

    // Only the 240SX has a pinned desktop frame (issue #315); every other
    // car reports its own hash with no pin to compare with.
    bool has_pin() const { return car.car_type_name == "240SX"; }
    const char* pinned_hash() const {
        return tuned ? nfsu::kTuned240sxFrameSha256 :
               painted ? nfsu::kStock240sxPaintedFrameSha256 : nfsu::kStock240sxFrameSha256;
    }
    std::uint64_t pinned_bytes() const {
        return tuned ? nfsu::kTuned240sxFrameBytes :
               painted ? nfsu::kStock240sxPaintedFrameBytes : nfsu::kStock240sxFrameBytes;
    }
    const char* name() const { return tuned ? "tuned" : painted ? "painted" : "unpainted"; }
};

Choice parse_arguments(int argc, char** argv) {
    Choice choice;
    bool have_root = false;
    for (int i = 1; i < argc; ++i) {
        if (argv[i] == nullptr || argv[i][0] == '\0') {
            continue;
        }
        const std::string argument = argv[i];
        if (argument == "--car" && i + 1 < argc && argv[i + 1] != nullptr) {
            choice.car_name = argv[i + 1];
            ++i;
            continue;
        }
        if (argument == kUnpaintedArgument) {
            choice.painted = false;
        } else if (argument == "--tuned") {
            choice.tuned = true;
        } else if (!have_root) {
            choice.root = argument;
            have_root = true;
        } else {
            choice.ignored += (choice.ignored.empty() ? "" : " ") + argument;
        }
    }
    if (choice.tuned) {
        choice.painted = true;
    }
    return choice;
}

// The hash of the frame now in `frame`, compared with the pin, as status
// lines and on stdout. Only called at the default camera. A car with no
// pinned desktop frame reports its hash and says so.
std::vector<Line> compare_with_pin(const nfsu::Frame& frame, const Choice& choice,
                                   std::string& encode_ms) {
    std::vector<Line> lines;
    const std::uint64_t encode_start = armGetSystemTick();
    nfsu::StockFrameImage image;
    std::string error;
    const bool encoded = nfsu::encode_stock_frame(frame, image, error);
    encode_ms = milliseconds(encode_start, armGetSystemTick());
    if (!encoded) {
        lines.push_back(Line{"the frame could not be encoded: " + error, kBad});
    } else {
        const std::string hash = nfsu::sha256_hex(image.sha256);
        lines.push_back(Line{"tga_bytes " + std::to_string(image.tga.size()) + "  sha256 " + hash,
                             kPlain});
        if (!choice.has_pin()) {
            lines.push_back(Line{"no pinned desktop frame for " + choice.car.car_type_name +
                                     "; record this hash to pin it",
                                 kNote});
        } else if (hash == choice.pinned_hash() && image.tga.size() == choice.pinned_bytes()) {
            lines.push_back(Line{"result: EQUAL to the pinned desktop frame", kGood});
        } else {
            lines.push_back(Line{"result: DIFFERENT from the pinned desktop frame, " +
                                     std::to_string(choice.pinned_bytes()) + " bytes, sha256 " +
                                     std::string(choice.pinned_hash()),
                                 kBad});
        }
    }
    for (const Line& line : lines) {
        std::printf("%s\n", line.text.c_str());
    }
    std::fflush(stdout);
    return lines;
}

// Reads, prepares and starts the view. False, with the reason reported,
// when there is nothing to show.
bool prepare(const Choice& choice, nfsu::StockFrame& frame, nfsu::StockView& view,
             Report& report, Screen& screen, std::string& timings) {
    const std::uint64_t read_start = armGetSystemTick();
    auto inputs = std::make_unique<nfsu::StockFrameInputs>();
    std::string read_error;
    nfsu::read_car_frame_inputs(choice.root, choice.car, *inputs, choice.tuned, read_error);
    const std::uint64_t read_end = armGetSystemTick();

    const auto note_input = [&](const nfsu::StockFrameInput& input) {
        if (input.read) {
            report.line("read " + input.path + ": " + std::to_string(input.bytes.size()) +
                        " bytes");
        }
    };
    for (const nfsu::StockFrameInput& input : inputs->files) note_input(input);
    if (choice.tuned) {
        for (const nfsu::StockFrameInput* input :
             {&inputs->wheel_geometry, &inputs->wheel_textures, &inputs->vinyls})
            note_input(*input);
    }
    if (!read_error.empty()) {
        report.line("nothing rendered: " + read_error, kBad);
        return false;
    }
    if (!inputs->all_read()) {
        const std::string error = inputs->read_error(choice.root);
        std::size_t first = 0;
        while (first < error.size()) {
            const std::size_t end = error.find('\n', first);
            const bool summary = end == std::string::npos;
            report.line((summary ? "nothing rendered: " : "") +
                            error.substr(first, summary ? std::string::npos : end - first),
                        kBad);
            if (summary) break;
            first = end + 1;
        }
        return false;
    }
    report.line(std::string("preparing the ") + choice.name() + " car scene...");
    screen.present();

    std::string error;
    const std::uint64_t prepare_start = armGetSystemTick();
    const nfsu::StockFrameSkin skin = choice.tuned ? nfsu::tuned_240sx_skin() :
        choice.painted ? nfsu::stock_car_skin(choice.car.car_type_name) : nfsu::StockFrameSkin{};
    const nfsu::StockSceneRequest request = choice.tuned ? nfsu::tuned_240sx_request() :
        nfsu::stock_car_request(choice.car.car_type_name);
    const bool prepared =
        nfsu::prepare_stock_frame(*inputs, request, skin, frame, error);
    const std::uint64_t prepare_end = armGetSystemTick();
    for (const std::string& issue : frame.issues) {
        report.line("issue: " + issue, kBad);
    }
    if (!prepared) {
        report.line("nothing rendered: " + error, kBad);
        return false;
    }
    if (!view.start(frame.scene, error)) {
        report.line("nothing rendered: " + error, kBad);
        return false;
    }
    timings = "prepare_ms " + milliseconds(prepare_start, prepare_end) + "  read_ms " +
              milliseconds(read_start, read_end);
    report.line("rendering 1280x720 with nfsu_render...");
    screen.present();
    return true;
}

// One controller frame from player 1 and the handheld controls, which
// padInitializeDefault reads as one. Buttons are the held state: the model
// computes its own presses, so padGetButtonsDown is not used here.
nfsu::GamepadFrame sample(const PadState& pad, std::uint64_t tick) {
    nfsu::GamepadFrame input;
    input.time_us = armTicksToNs(tick) / 1000U;
    nfsu::GamepadState& state = input.pads[0];
    state.connected = padIsConnected(&pad);
    state.buttons = nfsu::gamepad_buttons_from_npad(padGetButtons(&pad));
    const HidAnalogStickState left = padGetStickPos(&pad, 0);
    const HidAnalogStickState right = padGetStickPos(&pad, 1);
    state.axes[0] = left.x;
    state.axes[1] = left.y;
    state.axes[2] = right.x;
    state.axes[3] = right.y;
    return input;
}

// The frame loop: sample, step the camera, render when the camera moved,
// compare with the pin at the default camera, present. True when + was
// pressed or the system asked the application to close; false, with the
// reason reported, when a render failed.
bool orbit(const Choice& choice, nfsu::StockFrame& frame, nfsu::StockView& view,
           PadState& pad, Report& report, Screen& screen, const std::string& timings) {
    std::vector<Line> result;
    std::string render_ms = "-";
    std::string frame_ms = "-";
    std::uint64_t frames = 0;
    bool first = true;
    std::uint64_t last_tick = armGetSystemTick();
    while (appletMainLoop()) {
        const std::uint64_t tick = armGetSystemTick();
        if (!first) {
            frame_ms = milliseconds(last_tick, tick);
        }
        last_tick = tick;
        padUpdate(&pad);
        const nfsu::StockViewStep step = view.step(sample(pad, tick));
        if (step.exit) {
            break;
        }
        if (view.needs_render()) {
            std::string error;
            const std::uint64_t render_start = armGetSystemTick();
            const bool rendered = view.render(frame.frame, frame.stats, error);
            render_ms = milliseconds(render_start, armGetSystemTick());
            if (!rendered) {
                // Rendering the same scene again would fail the same way,
                // so the loop stops here rather than retrying every frame.
                report.line("nothing rendered: " + error, kBad);
                report.detail("frames rendered: " + std::to_string(frames));
                return false;
            } else {
                ++frames;
                if (first) {
                    // From here on the screen shows the frame, and every
                    // line reported is its caption.
                    screen.show_frame(frame.frame.rgba());
                    report.line("nfsu_switch_car: the " + std::string(choice.name()) +
                                " " + choice.car.car_type_name + " from " + choice.root +
                                ", CPU reference renderer, 1280x720");
                    report.detail(nfsu::stock_frame_slots_line(frame.report));
                    for (const std::string& line : nfsu::stock_frame_stats_lines(frame.stats)) {
                        report.detail(line);
                    }
                    for (const std::string& line : nfsu::stock_frame_skin_lines(frame)) {
                        report.detail(line);
                    }
                    if (!frame.inputs_ok) {
                        report.line(std::to_string(frame.issues.size()) +
                                        " issues were reported with the inputs; they are "
                                        "listed on stdout",
                                    kBad);
                    }
                }
                if (view.at_default()) {
                    std::string encode_ms;
                    result = compare_with_pin(frame.frame, choice, encode_ms);
                    if (first) {
                        report.line("render_ms " + render_ms + "  " + timings +
                                    "  encode_sha256_ms " + encode_ms);
                    }
                } else {
                    result = {Line{"the hash is compared only at the default camera; - "
                                   "returns to it",
                                   kNote}};
                }
                first = false;
            }
        }
        std::vector<Line> status = result;
        const nfsu::OrbitCamera& camera = view.options().camera;
        status.push_back(Line{std::string("camera: ") +
                                  (view.at_default() ? "default" : "moved") + ", yaw " +
                                  degrees(camera.yaw_deg) + " pitch " +
                                  degrees(camera.pitch_deg) + "  frame_ms " + frame_ms +
                                  "  render_ms " + render_ms,
                              kPlain});
        status.push_back(Line{"Left stick orbits, - resets the camera, + exits.", kPlain});
        screen.set_status(status);
        screen.present();
    }
    report.detail("frames rendered: " + std::to_string(frames));
    return true;
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
    Choice choice = parse_arguments(argc, argv);
    report.line("nfsu_switch_car: the " + choice.car_name +
                ", drawn by the CPU reference renderer");
    if (!have_screen) {
        report.detail("the framebuffer could not be created; reporting on stdout only");
    }
    // Resolve the selected car through the retail car type table before any
    // input is read, so a bad name stops with the playable list rather than
    // a missing file (issue #315).
    bool car_ok = true;
    {
        std::string error;
        car_ok = nfsu::load_car_assets(std::filesystem::path(choice.root) / "Global/GLOBALB.BUN",
                                       choice.car_name, choice.car, error);
        if (!car_ok) {
            report.line("stopped: car selection: " + error, kBad);
        } else if (choice.tuned && !nfsu::tuned_preset_available_for(choice.car)) {
            car_ok = false;
            report.line("stopped: the --tuned preset is the 240SX's own part set; it cannot "
                        "dress car type " + choice.car.car_type_name, kBad);
        }
    }
    report.line("inputs: " + choice.root + "  frame: " + choice.name() + "  car: " +
                choice.car.car_type_name + (choice.car.brand_name.empty()
                                                ? std::string()
                                                : " (" + choice.car.brand_name + ")"));
    if (!choice.ignored.empty()) {
        report.line("ignored arguments: " + choice.ignored, kBad);
    }
    screen.present();
    if (!car_ok) {
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

    // Kept until exit: the screen shows the frame, and the view reads the
    // scene inside it.
    std::unique_ptr<nfsu::StockFrame> frame;
    std::unique_ptr<nfsu::StockView> view;
    bool ready = false;
    try {
        frame = std::make_unique<nfsu::StockFrame>();
        view = std::make_unique<nfsu::StockView>();
        std::string timings;
        ready = prepare(choice, *frame, *view, report, screen, timings);
        if (ready) {
            ready = orbit(choice, *frame, *view, pad, report, screen, timings);
        }
    } catch (const std::bad_alloc&) {
        report.line("stopped: out of memory", kBad);
        ready = false;
    } catch (const std::exception& caught) {
        report.line(std::string("stopped: ") + caught.what(), kBad);
        ready = false;
    } catch (...) {
        report.line("stopped: an unknown exception", kBad);
        ready = false;
    }

    // Nothing to orbit: wait for + as issue #141's application did.
    if (!ready) {
        report.line("Press + to exit.");
        while (appletMainLoop()) {
            padUpdate(&pad);
            if ((padGetButtonsDown(&pad) & HidNpadButton_Plus) != 0) {
                break;
            }
            screen.present();
        }
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
