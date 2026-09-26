#include "nfsu/track_slice.h"
#include "nfsu/posix_file_system.h"
#include <cmath>
#include <iostream>
#include <limits>

int main(int argc, char** argv) {
    using namespace nfsu;
    std::string error;
    RenderOptions options;
    TrackFrustum frustum;
    TrackResidencyPathPoint point;
    point.slot[0] = 100; point.slot[1] = 200; point.slot[2] = 30;
    if (!track_slice_view(point, 0, 250, options, frustum, error) ||
        options.camera.target.x != 120 || options.camera.target.y != 33 ||
        options.camera.target.z != -200 || options.camera.far_plane != 250) return 1;
    const float low[3] = {119, 199, 32}, high[3] = {121, 201, 34};
    if (track_classify_box(frustum, low, high) == TrackBoxClass::outside) return 9;
    if (track_slice_view(point, 0, 0, options, frustum, error)) return 2;
    if (track_slice_view(point, std::numeric_limits<double>::quiet_NaN(), 250, options, frustum, error)) return 3;
    TrackSlice empty;
    TrackSliceFrame frame;
    if (make_track_slice_frame(empty, 0, 0, frame, error) || error.empty()) return 4;
    if (argc == 1) { std::cout << "PASS: track slice camera and refusal guards\n"; return 0; }
    TrackSlice slice;
    if (!load_track_slice(posix_file_system(), argv[1], slice, error)) {
        std::cerr << error << '\n'; return 5;
    }
    std::cout << "sections " << slice.sections << " path " << slice.path.size()
              << " CPU " << slice.streaming_bytes << " textures " << slice.texture_bytes
              << " collision " << slice.contacts.memory.resident_bytes << '\n';
    const bool full = argc == 3 && std::string(argv[2]) == "--full";
    if (argc > 2 && !full) return 10;
    std::size_t hits = 0, max_draws = 0, frames = 0;
    for (std::size_t sample = 0; sample < slice.path.size(); sample += full ? 1 : 120) {
        if (!make_track_slice_frame(slice, sample, 0, frame, error)) {
            std::cerr << "sample " << sample << ": " << error << '\n'; return 6;
        }
        if (frame.commands.draws.empty()) { std::cerr << "empty track frame\n"; return 7; }
        max_draws = std::max(max_draws, frame.commands.draws.size());
        hits += frame.contact_status == TrackContactStatus::hit;
        ++frames;
        std::cout << "sample " << sample << " draws " << frame.commands.draws.size()
                  << " resources " << frame.commands.uploads.resources.size()
                  << " visible " << frame.visible << " triangles " << frame.triangles
                  << " far " << frame.far_plane << " contact " << track_contact_status_name(frame.contact_status) << '\n';
    }
    std::cout << "PASS: frames " << frames << " max draws " << max_draws << " road hits " << hits << '\n';
    if (full) return frames == slice.path.size() && hits > 0 && max_draws <= kTrackSliceDrawLimit ? 0 : 8;
    return frames == 57 && hits == 55 && max_draws == 1769 ? 0 : 8;
}
