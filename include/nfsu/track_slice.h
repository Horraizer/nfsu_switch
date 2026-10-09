#pragma once

#include "nfsu/assets.h"
#include "nfsu/draw_commands.h"
#include "nfsu/track_contact.h"
#include "nfsu/track_residency.h"

namespace nfsu {

// Event 1, track 1003, STREAML1RA. The recovered permanent set stays resident
// on the #157 hand path. Each frame selects a bounded frustum from that set.
// This is a diagnostic path, not a race or the recovered route of #61.
constexpr std::size_t kTrackSliceDrawLimit = 2047;
constexpr std::size_t kTrackSliceResourceLimit = 8192;
struct TrackSlice {
    std::uint32_t event_number = 1;
    std::uint32_t track_number = 1003;
    PreparedTrackScene scene;
    TrackObjectReport objects;
    TrackContactWorld contacts;
    std::vector<TrackResidencyPathPoint> path;
    AssetCheckReport assets;
    std::size_t sections = 0;
    std::uint64_t input_bytes = 0;
    std::size_t texture_spillover_sections = 0;
    std::uint64_t texture_spillover_bytes = 0;
    std::uint64_t streaming_bytes = 0;
    std::uint64_t texture_bytes = 0;
};

// Synchronous parsing checkpoints. Exceptions propagate for cancellation and
// unwind temporary buffers; callbacks may sample the platform allocator.
class TrackSliceLoadObserver {
public:
    virtual ~TrackSliceLoadObserver() = default;
    virtual void checkpoint(const char* phase) = 0;
};

// Runs the #47 first-lap asset check before parsing. All paths are resolved
// from that manifest, read-only. Failure leaves slice empty.
bool load_track_slice(FileSystemService& files, const std::filesystem::path& root,
                      TrackSlice& slice, std::string& error,
                      AssetCheckObserver* observer = nullptr,
                      TrackSliceLoadObserver* loading = nullptr,
                      std::uint32_t event_number = 1, bool world_mips = false);

struct TrackSliceFrame {
    RecordedCommands commands;
    std::size_t sample = 0;
    std::size_t visible = 0;
    std::uint64_t triangles = 0;
    std::uint64_t unplaced = 0;
    double far_plane = 0;
    TrackContactStatus contact_status = TrackContactStatus::refused;
    TrackContact contact;
};

// Both camera and frustum derive from the same orbit basis. Used by the
// console and the desktop tests, including the raw-slot coordinate map.
bool track_slice_view(const TrackResidencyPathPoint& point, double yaw_offset,
                      double far_plane, RenderOptions& options,
                      TrackFrustum& frustum, std::string& error);

// Try far planes 250, 125, 62.5, 31.25 until the complete culled draw list
// fits 2047 entries. Never truncate a list. Show the selected far plane.
// A refused collision query or unrenderable frame fails with a diagnostic.
bool make_track_slice_frame(const TrackSlice& slice, std::size_t sample,
                            double yaw_offset, TrackSliceFrame& frame,
                            std::string& error);

} // namespace nfsu
