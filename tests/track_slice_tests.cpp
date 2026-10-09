#include "nfsu/track_slice.h"
#include "nfsu/draw_execution.h"
#include "nfsu/posix_file_system.h"
#include "nfsu/platform_fakes.h"
#include "nfsu/heap_usage.h"
#include "nfsu/arg_path.h"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>

namespace {
int fail(int code, const char* message) {
    std::cerr << "FAIL: " << message << '\n';
    return code;
}
}

int main(int argc, char** argv) {
    using namespace nfsu;
    if(argc==3 && std::string(argv[2])=="--mips") {
        for(unsigned event:{1U,8U}) {
            TrackSlice base,mipped;std::string error;
            if(!load_track_slice(posix_file_system(),argument_path(argv[1]),base,error,nullptr,nullptr,event,false) ||
               !load_track_slice(posix_file_system(),argument_path(argv[1]),mipped,error,nullptr,nullptr,event,true)) {
                std::cerr<<error<<'\n';return fail(30,"world mip retail loading");
            }
            const auto& a=base.scene.region.textures;const auto& b=mipped.scene.region.textures;
            if(a.size()!=b.size() || base.scene.instances.size()!=mipped.scene.instances.size())
                return fail(31,"mips changed resource or instance selection");
            std::size_t chains=0,cutouts=0;std::uint64_t extra=0;
            for(std::size_t i=0;i<a.size();++i) {
                if(!b[i].usable() || a[i].hash!=b[i].hash || a[i].width!=b[i].width ||
                   a[i].height!=b[i].height || a[i].storage!=b[i].storage ||
                   a[i].alpha_test_mode!=b[i].alpha_test_mode || a[i].alpha_blend_mode!=b[i].alpha_blend_mode ||
                   b[i].payload().size()<a[i].payload().size() ||
                   !std::equal(a[i].payload().begin(),a[i].payload().end(),b[i].payload().begin()))
                    return fail(32,"mip path changed top level, alpha state or binding");
                if(b[i].mip_count>1){++chains;if(b[i].alpha_test_mode==1)++cutouts;}
                extra+=b[i].payload().size()-a[i].payload().size();
            }
            TrackSliceFrame frame;
            if(!chains || !cutouts || !extra || !make_track_slice_frame(mipped,0,0,frame,error)) {
                std::cerr<<error<<'\n';return fail(33,"mip retail frame missing chains/cutouts");
            }
            DrawExecutionPlan plan;
            if(!plan_draw_execution(frame.commands,plan,error)){
                std::cerr<<error<<'\n';return fail(34,"mip frame upload/copy plan");
            }
            std::cout<<"PASS world mips event="<<event<<" textures="<<b.size()<<" chains="<<chains
                     <<" cutouts="<<cutouts<<" extra_payload_bytes="<<extra<<" staging="<<plan.staging_bytes<<'\n';
        }
        return 0;
    }
    HeapUsage heap;
    heap.begin(100, 200); heap.observe(180, 300); heap.observe(120, 300);
    heap.finish_load(140, 300); heap.observe(500, 600); heap.observe(90, 600);
    if (heap.before_load != 100 || heap.after_load != 140 || heap.current != 90 ||
        heap.load_peak != 180 || heap.load_delta() != 80 || heap.peak != 500 ||
        heap.extent_peak != 600 || heap.load_overrun(79) != 1 || heap.load_overrun(80) != 0)
        return fail(11, "allocator observations preserve separate load/run high-water marks and overrun");
    heap.begin(50, 100); heap.finish_load(40, 100);
    if (heap.load_delta() != 0 || heap.peak != 50 || heap.after_load != 40)
        return fail(12, "heap baseline resets and freeing cannot underflow the delta");
    FakeFileSystem incomplete;
    incomplete.add_file("slice-missing/Tracks/TRACKB1003.lzc", {1, 2, 3});
    TrackSlice refused;
    refused.sections = 123;
    refused.path.resize(1);
    std::string error;
    if (load_track_slice(incomplete, "slice-missing", refused, error) ||
        error.find("first-lap asset check: missing files") == std::string::npos ||
        error.find("Tracks/TRACKB1003.lzc") == std::string::npos ||
        !refused.path.empty() || refused.sections != 0 || !incomplete.reads().empty())
        return fail(13, "incomplete install stops at the asset verdict before parsing and empties the slice");
    RenderOptions options;
    TrackFrustum frustum;
    TrackResidencyPathPoint point;
    point.slot[0] = 100; point.slot[1] = 200; point.slot[2] = 30;
    if (!track_slice_view(point, 0, 250, options, frustum, error) ||
        options.camera.target.x != 120 || options.camera.target.y != 33 ||
        options.camera.target.z != -200 || options.camera.far_plane != 250) return fail(1, "track slice camera mapping");
    const float low[3] = {119, 199, 32}, high[3] = {121, 201, 34};
    if (track_classify_box(frustum, low, high) == TrackBoxClass::outside) return fail(9, "camera and culler share the raw-slot mapping");
    if (track_slice_view(point, 0, 0, options, frustum, error)) return fail(2, "zero far plane refused");
    if (track_slice_view(point, std::numeric_limits<double>::quiet_NaN(), 250, options, frustum, error)) return fail(3, "non-finite yaw refused");
    TrackSlice empty;
    TrackSliceFrame frame;
    if (make_track_slice_frame(empty, 0, 0, frame, error) || error.empty()) return fail(4, "empty slice sample refused with a diagnostic");
    if (argc == 1) { std::cout << "PASS: track slice camera and refusal guards\n"; return 0; }
    struct PollCount : TrackSliceLoadObserver {
        std::size_t sections = 0;
        void checkpoint(const char* phase) override {
            if (std::string(phase) == "stream sections") ++sections;
        }
    } polled;
    TrackSlice slice;
    if (!check_arguments(argc, argv, 1, error)) return fail(18, error.c_str());
    if (!load_track_slice(posix_file_system(), argument_path(argv[1]), slice, error, nullptr, &polled)) {
        std::cerr << error << '\n'; return 5;
    }
    if (polled.sections < 4 * slice.sections)
        return fail(14, "slice loader connects parsing checkpoints to its observer");
    std::cout << "sections " << slice.sections << " path " << slice.path.size()
              << " CPU " << slice.streaming_bytes << " textures " << slice.texture_bytes
              << " collision " << slice.contacts.memory.resident_bytes
              << " spillover " << slice.texture_spillover_sections
              << " spillover-bytes " << slice.texture_spillover_bytes
              << " unresolved " << slice.scene.region.unresolved_hashes.size() << '\n';
    if (slice.texture_spillover_sections != 19 ||
        slice.texture_spillover_bytes != 3512968 ||
        slice.scene.region.unresolved_hashes.size() != 2)
        return fail(15, "event-1 spillover resolves 46 of 48 texture hashes");
    for (const std::uint32_t hash : {0x3394FE62U, 0x008A6835U}) {
        const auto found = std::find_if(slice.scene.region.textures.begin(),
                                        slice.scene.region.textures.end(),
            [hash](const RenderTexture& texture) {
                return texture.usable() && texture.claims_hash(hash);
            });
        if (found == slice.scene.region.textures.end())
            return fail(16, "GLOBALB P8 track texture did not decode");
        for (const auto& draw : slice.scene.region.draws)
            if (draw.texture_hash == hash && draw.binding != BindingKind::resolved)
                return fail(17, "GLOBALB P8 track draw still uses fallback");
    }
    const bool full = argc == 3 && std::string(argv[2]) == "--full";
    if (argc > 2 && !full) return fail(10, "unknown track slice test argument");
    std::size_t hits = 0, max_draws = 0, frames = 0;
    for (std::size_t sample = 0; sample < slice.path.size(); sample += full ? 1 : 120) {
        if (!make_track_slice_frame(slice, sample, 0, frame, error)) {
            std::cerr << "sample " << sample << ": " << error << '\n'; return 6;
        }
        if (frame.commands.draw_count() == 0) { std::cerr << "empty track frame\n"; return 7; }
        max_draws = std::max(max_draws, frame.commands.draw_count());
        hits += frame.contact_status == TrackContactStatus::hit;
        ++frames;
        std::cout << "sample " << sample << " draws " << frame.commands.draw_count()
                  << " resources " << frame.commands.resource_table().resources.size()
                  << " visible " << frame.visible << " triangles " << frame.triangles
                  << " far " << frame.far_plane << " contact " << track_contact_status_name(frame.contact_status) << '\n';
    }
    std::cout << "PASS: frames " << frames << " max draws " << max_draws << " road hits " << hits << '\n';
    if (full) return frames == slice.path.size() && hits > 0 && max_draws <= kTrackSliceDrawLimit ? 0 : fail(8, "full path frame/contact/draw count");
    // #184: the two borrowed SKYDOME groups add two background submissions.
    return frames == 57 && hits == 55 && max_draws == 1771 ? 0 : fail(8, "sampled path frame/contact/draw pin");
}
