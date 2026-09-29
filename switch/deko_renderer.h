#pragma once

#include "nfsu/draw_commands.h"
#include <memory>

namespace nfsu {
struct DekoPresentTiming {
    double acquire_ms = 0;
    double fence_wait_ms = 0;
    double record_ms = 0;
    double submit_ms = 0;
};

// Switch-only owner of the device, queue, resident uploads and swapchain.
// The borrowed commands and their scene must stay alive through close().
// All work uses one queue. Presentation keeps two command and uniform slots;
// each slot's fence completes before its storage is reused. Updates replace
// only draw selection; the uploaded scene remains resident.
class DekoRenderer {
public:
    DekoRenderer();
    ~DekoRenderer();
    DekoRenderer(const DekoRenderer&) = delete;
    DekoRenderer& operator=(const DekoRenderer&) = delete;
    // Offscreen mode owns no native window, so a diagnostic framebuffer may
    // stay open. Both modes record exactly the same draw path.
    bool open(const RecordedCommands& commands, std::string& error, bool presentation = true,
              std::size_t draw_capacity = 0);
    // Reuses resident GPU resources. Presentation may have a frame in flight.
    bool update(const RecordedCommands& commands, std::string& error);
    std::uint64_t gpu_bytes() const noexcept;
    // Offscreen only. Tight top-first RGBA8 after a completed GPU copy and
    // fence. Times include command recording/submission/wait; upload and
    // allocation are excluded. No CPU rasterizer is invoked here.
    bool capture(std::vector<unsigned char>& rgba, double& render_ms,
                 double& readback_ms, std::string& error);
    bool present(std::string& error, DekoPresentTiming* timing = nullptr);
    bool close(std::string& error);
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace nfsu
