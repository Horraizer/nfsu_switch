#pragma once

#include "nfsu/draw_commands.h"
#include "nfsu/gpu_scene_profile.h"
#include "nfsu/deko_gpfifo_budget.h"
#include <memory>

namespace nfsu {
struct DekoPresentTiming {
    double acquire_ms = 0;
    double fence_wait_ms = 0;
    double uniform_write_ms = 0;
    double record_ms = 0;
    double finish_ms = 0;
    double submit_ms = 0;
};
struct DekoUpdateTiming {
    double validation_ms = 0;
    double uniform_write_ms = 0;
};
struct DekoMemoryUsage {
    std::uint64_t frame_uniform_bytes = 0;
    std::uint64_t static_uniform_bytes = 0;
    std::uint64_t prepared_command_bytes = 0;
};
struct DekoGpfifoUsage {
    std::uint32_t last_entries = 0, max_entries = 0;
    std::uint32_t last_lists = 0;
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
    // opaque_first submits depth-writing opaque track draws first, so the
    // sky that follows them is depth-rejected wherever the world covers it.
    // sky_with_opaque is a diagnostic only: it moves the sky into the opaque
    // pass at its list position, as before issue 227, for console A/B runs.
    bool open(const RecordedCommands& commands, std::string& error, bool presentation = true,
              std::size_t draw_capacity = 0, bool profile_frame = false,
              bool opaque_first = false, bool sky_with_opaque = false);
    // Reuses resident GPU resources. Presentation may have a frame in flight.
    bool update(const RecordedCommands& commands, std::string& error,
                DekoUpdateTiming* timing = nullptr);
    std::uint64_t gpu_bytes() const noexcept;
    DekoMemoryUsage memory_usage() const noexcept;
    DekoGpfifoUsage gpfifo_usage() const noexcept;
    // Offscreen only. Tight top-first RGBA8 after a completed GPU copy and
    // fence. Times include command recording/submission/wait; upload and
    // allocation are excluded. No CPU rasterizer is invoked here.
    bool capture(std::vector<unsigned char>& rgba, double& render_ms,
                 double& readback_ms, std::string& error);
    bool present(std::string& error, DekoPresentTiming* timing = nullptr,
                 const DekoSceneFrame* scene_frame = nullptr);
    void reset_gpu_profile();
    const DekoGpuProfile& gpu_profile() const noexcept;
    std::uint64_t profile_storage_bytes() const noexcept;
    bool close(std::string& error);
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace nfsu
