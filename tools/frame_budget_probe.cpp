#include "nfsu/race_gpu_scene.h"
#include "nfsu/posix_file_system.h"
#include "nfsu/platform_desktop.h"
#include "nfsu/draw_execution.h"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <set>
#include <stdexcept>
#include <tuple>

#include <cstdlib>
#include <new>
#include <limits>
// Count live C++ allocation bytes without allocating inside the counter.
// This is a per-frame retention check, not a claim about the newlib heap.
namespace {
std::size_t live_bytes = 0, allocation_calls = 0;
struct Allocation { void* raw; std::size_t bytes; };
void* allocate_counted(std::size_t bytes, std::size_t alignment) {
    if (!bytes) bytes = 1;
    alignment = std::max(alignment,alignof(std::max_align_t));
    if (bytes > std::numeric_limits<std::size_t>::max()-alignment-sizeof(Allocation)) throw std::bad_alloc();
    void* raw = std::malloc(bytes+alignment+sizeof(Allocation));
    if (!raw) throw std::bad_alloc();
    auto address = (reinterpret_cast<std::uintptr_t>(raw)+sizeof(Allocation)+alignment-1)&~(alignment-1);
    auto* header = reinterpret_cast<Allocation*>(address)-1;
    ++allocation_calls; header->raw = raw; header->bytes = bytes; live_bytes += bytes;
    return reinterpret_cast<void*>(address);
}
void free_counted(void* memory) noexcept {
    if (!memory) return;
    auto* header = static_cast<Allocation*>(memory)-1;
    live_bytes -= header->bytes; std::free(header->raw);
}
}
void* operator new(std::size_t n) { return allocate_counted(n,alignof(std::max_align_t)); }
void* operator new[](std::size_t n) { return ::operator new(n); }
void operator delete(void* p) noexcept { free_counted(p); }
void operator delete[](void* p) noexcept { free_counted(p); }
void operator delete(void* p,std::size_t) noexcept { free_counted(p); }
void operator delete[](void* p,std::size_t) noexcept { free_counted(p); }
void* operator new(std::size_t n,std::align_val_t a) { return allocate_counted(n,static_cast<std::size_t>(a)); }
void* operator new[](std::size_t n,std::align_val_t a) { return ::operator new(n,a); }
void operator delete(void* p,std::align_val_t) noexcept { free_counted(p); }
void operator delete[](void* p,std::align_val_t) noexcept { free_counted(p); }
void operator delete(void* p,std::size_t,std::align_val_t) noexcept { free_counted(p); }
void operator delete[](void* p,std::size_t,std::align_val_t) noexcept { free_counted(p); }

void* operator new(std::size_t n,const std::nothrow_t&) noexcept { try { return ::operator new(n); } catch (...) { return nullptr; } }
void* operator new[](std::size_t n,const std::nothrow_t& t) noexcept { return ::operator new(n,t); }
void operator delete(void* p,const std::nothrow_t&) noexcept { free_counted(p); }
void operator delete[](void* p,const std::nothrow_t&) noexcept { free_counted(p); }
void* operator new(std::size_t n,std::align_val_t a,const std::nothrow_t&) noexcept { try { return ::operator new(n,a); } catch (...) { return nullptr; } }
void* operator new[](std::size_t n,std::align_val_t a,const std::nothrow_t& t) noexcept { return ::operator new(n,a,t); }
void operator delete(void* p,std::align_val_t,const std::nothrow_t&) noexcept { free_counted(p); }
void operator delete[](void* p,std::align_val_t,const std::nothrow_t&) noexcept { free_counted(p); }

using namespace nfsu;
namespace {
volatile std::uint64_t observed = 0;
void require(bool ok, const std::string& error) {
    if (!ok) throw std::runtime_error(error);
}
template<class F> double measure(F fn) {
    fn();
    const auto start = std::chrono::steady_clock::now();
    for (unsigned i = 0; i < 120; ++i) fn();
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now()-start).count()/120;
}
auto state_key(const RecordedDraw& d) {
    const auto& p = d.pipeline;
    return std::make_tuple(d.program, p.cull, p.alpha_test, p.alpha_ref, p.blend,
        p.blend_src, p.blend_dst, p.blend_op, p.depth_test, p.depth_write, p.color_write);
}
TrackFrustum frustum_for(const RaceSession& session, double far) {
    RenderOptions options;
    std::string error;
    require(drive_render_camera(session.camera(), ChaseFovMode::vertical_4_3,
        1280, 720, options.camera, error), error);
    options.camera.far_plane = far;
    CameraFrame basis;
    require(make_camera_frame(options.camera, 1280, 720, basis, error), error);
    TrackCamera camera;
    camera.eye[0] = float(basis.eye.x); camera.eye[1] = float(basis.eye.y); camera.eye[2] = float(basis.eye.z);
    for (unsigned i = 0; i < 3; ++i) camera.up[i] = i == 1 ? 1.0F : 0.0F;
    camera.target[0] = float(options.camera.target.x);
    camera.target[1] = float(options.camera.target.y); camera.target[2] = float(options.camera.target.z);
    camera.aspect = float(1280)/720;
    camera.vertical_fov = float(options.camera.fov_y_deg * (3.14159265358979323846/180));
    camera.near_plane = float(options.camera.near_plane); camera.far_plane = float(far);
    TrackFrustum result;
    require(track_frustum_look_at(camera, result), "frustum failed");
    return result;
}
void sample(RaceGpuScene& scene, RaceRun& race) {
    std::string error;
    RaceGpuFrame frame;
    DesktopTiming timer;
    RaceFrameProfile sums;
    std::size_t allocations = 0, max_allocations = 0;
    std::uint64_t index_visits = 0, max_index_visits = 0;
    for (unsigned i = 0; i < 121; ++i) {
        RaceFrameProfile p;
        DrawValidationAudit audit;
        auto* previous_audit = set_draw_validation_audit(&audit);
        const auto before_allocations = allocation_calls;
        const bool okay = scene.frame(race.session(), frame, error, &p, false, &timer);
        const auto count = allocation_calls - before_allocations;
        set_draw_validation_audit(previous_audit);
        require(okay, error);
        if (i) {
            allocations += count; max_allocations = std::max(max_allocations, count);
            index_visits += audit.index_visits; max_index_visits = std::max(max_index_visits,audit.index_visits);
        }
        if (i) {
            sums.cull_ms += p.cull_ms; sums.list_ms += p.list_ms;
            sums.car_ms += p.car_ms; sums.track_ms += p.track_ms; sums.combine_ms += p.combine_ms;
        }
    }
    const auto& source = scene.track().scene;
    const auto frustum = frustum_for(race.session(), frame.far_plane);
    const auto visible = cull_track_region(scene.track().objects, frustum);
    require(visible.ok() && visible.policy_held, "visibility failed");
    TrackDrawList list;
    require(build_track_draw_list(source, visible, {}, list, error), error);
    // A measurement prototype only: precompute mesh adjacency, preserve the
    // current ordered visible set, and compare every selected pair.
    std::vector<std::vector<std::uint32_t>> adjacency(source.region.meshes.size());
    for (std::size_t i = 0; i < source.region.draws.size(); ++i)
        if (source.region.draws[i].drawable)
            adjacency.at(source.region.draws[i].mesh).push_back(static_cast<std::uint32_t>(i));
    std::vector<TrackDrawListEntry> selected;
    selected.reserve(list.entries.size());
    auto select = [&] {
        selected.clear();
        for (const auto& item : visible.visible) {
            const auto handle = track_scene_instance_of(source, item.section_index, item.instance_index);
            if (handle == kNoHandle) continue;
            for (auto group : adjacency.at(source.instances.at(handle).mesh))
                selected.push_back({static_cast<std::uint32_t>(handle), group});
        }
        observed += selected.size();
    };
    const auto select_ms = measure(select);
    const auto full_list_ms = measure([&] {
        TrackDrawList measured;
        require(build_track_draw_list(source, visible, {}, measured, error), error);
        observed += measured.entries.size();
    });
    require(selected.size() == list.entries.size(), "prototype selection size mismatch");
    for (std::size_t i = 0; i < selected.size(); ++i)
        require(selected[i].instance == list.entries[i].instance && selected[i].draw == list.entries[i].draw,
                "prototype selection order mismatch");
    TrackCullOptions trusted;
    trusted.verify_containment = false;
    const auto unchecked = cull_track_region(scene.track().objects, frustum, trusted);
    require(unchecked.ok() && unchecked.policy_held && unchecked.visible.size() == visible.visible.size(),
            "containment selection size mismatch");
    for (std::size_t i = 0; i < visible.visible.size(); ++i)
        require(unchecked.visible[i].section_index == visible.visible[i].section_index &&
                unchecked.visible[i].instance_index == visible.visible[i].instance_index,
                "containment selection order mismatch");
    const auto checked_cull_ms = measure([&] { observed += cull_track_region(scene.track().objects, frustum).visible.size(); });
    const auto cull_ms = measure([&] { observed += cull_track_region(scene.track().objects, frustum, trusted).visible.size(); });
    const auto list_validation_ms = measure([&] { require(validate_track_draw_list(source,list,error),error); });
    const auto command_validation_ms = measure([&] { require(validate_recorded_commands(frame.commands,error),error); });
    DrawExecutionPlan plan;
    const auto execution_plan_ms = measure([&] { require(plan_draw_execution(frame.commands,plan,error),error); });
    const auto update_ms = measure([&] { require(validate_draw_update(frame.commands.resource_table(),frame.commands,kTrackSliceDrawLimit,error),error); });
    std::vector<unsigned char> uniforms((1+2*kTrackSliceDrawLimit)*256);
    const auto uniform_ms = measure([&] {
        std::memcpy(uniforms.data(),&frame.commands.frame,sizeof(DrawFrameUniform));
        for (std::size_t i = 0; i < frame.commands.draw_count(); ++i) {
            std::memcpy(uniforms.data()+(1+2*i)*256,&frame.commands.draw_view(i).placement,sizeof(DrawInstanceUniform));
            std::memcpy(uniforms.data()+(2+2*i)*256,&frame.commands.draw_view(i).material,sizeof(DrawMaterialUniform));
        }
        std::atomic_signal_fence(std::memory_order_seq_cst);
        observed += uniforms[256];
    });
    const auto table_copy_ms = measure([&] { auto copy = frame.commands.resource_table(); observed += copy.resources.size(); });
    std::set<std::size_t> textures, meshes, sections, instances;
    std::set<decltype(state_key(RecordedDraw{}))> states;
    std::uint64_t track_triangles = 0, texture_runs = 0, state_runs = 0, vb_runs = 0, ib_runs = 0;
    std::size_t textured = 0;
    for (std::size_t i = 0; i < frame.track_draws; ++i) {
        const auto view = frame.commands.draw_view(i);
        const auto& d = view.definition;
        textures.insert(d.texture_resource); meshes.insert(d.mesh); instances.insert(view.instance);
        sections.insert(source.instances.at(view.instance).section_index); states.insert(state_key(d));
        track_triangles += d.index_count/3; textured += d.texture_resource != kNoHandle;
        if (!i || d.texture_resource != frame.commands.draw_view(i-1).definition.texture_resource) ++texture_runs;
        if (!i || state_key(d) != state_key(frame.commands.draw_view(i-1).definition)) ++state_runs;
        if (!i || d.vertex_resource != frame.commands.draw_view(i-1).definition.vertex_resource) ++vb_runs;
        if (!i || d.index_resource != frame.commands.draw_view(i-1).definition.index_resource) ++ib_runs;
    }
    std::cout << "POSE tick=" << race.session().timing().step << " track=" << frame.track_draws
        << " car=" << frame.car_draws << " track_triangles=" << track_triangles << " all_triangles=" << frame.triangles
        << " visible=" << frame.visible << " unplaced=" << frame.unplaced << " far=" << frame.far_plane
        << " resources=" << frame.commands.resource_table().resources.size() << '\n';
    std::cout << "COUNTS textures=" << textures.size() << " states=" << states.size() << " meshes=" << meshes.size()
        << " sections=" << sections.size() << " instances=" << instances.size() << " textured=" << textured
        << " texture_runs=" << texture_runs << " state_runs=" << state_runs << " vb_runs=" << vb_runs
        << " ib_runs=" << ib_runs << " index_visits_per_validation=" << frame.triangles*3 << '\n';
    std::uint64_t reflected_triangles = 0, reflected_lamps = 0;
    for (std::size_t i = 0; i < frame.commands.reflection_draw_count(); ++i) {
        const auto& d = frame.commands.reflection_draw(i);
        reflected_triangles += d.index_count/3;
        reflected_lamps += d.material.environment_range[3] > 0.5F;
    }
    std::cout << "REFLECTION_SOURCE draws=" << frame.commands.reflection_draw_count()
        << " triangles=" << reflected_triangles << " lamps=" << reflected_lamps
        << " cache_bytes=" << scene.cache_bytes() << " target=160x90 update=every-frame\n";
    std::cout << "ALLOCATIONS mean=" << double(allocations)/120 << " max=" << max_allocations << '\n';
    std::cout << "FRAME_INDEX_VISITS mean=" << double(index_visits)/120 << " max=" << max_index_visits << '\n';
    std::cout << "FRAME_MS cull=" << sums.cull_ms/120 << " list=" << sums.list_ms/120 << " car=" << sums.car_ms/120
        << " cached_track=" << sums.track_ms/120 << " combine=" << sums.combine_ms/120 << '\n';
    std::cout << "ISOLATED_MS checked_cull=" << checked_cull_ms << " trusted_cull=" << cull_ms
        << " full_list=" << full_list_ms << " adjacency_select=" << select_ms
        << " list_validation=" << list_validation_ms << " command_validation=" << command_validation_ms
        << " execution_plan=" << execution_plan_ms << " update_validation=" << update_ms
        << " uniform_memcpy=" << uniform_ms << " resource_table_copy=" << table_copy_ms << '\n';
}
}
int main(int argc, char** argv) {
    try {
        require(argc == 3 || (argc == 5 && std::strcmp(argv[3], "--wet-road-reflections") == 0 &&
            (std::strcmp(argv[4], "off") == 0 || std::strcmp(argv[4], "on") == 0)),
            "usage: nfsu_frame_budget_probe RETAIL_ROOT RECORDING [--wet-road-reflections off|on]");
        std::string error;
        RaceGpuScene scene;
        scene.set_wet_road_reflections(argc == 5 && std::strcmp(argv[4], "on") == 0);
        struct Loading : TrackSliceLoadObserver {
            std::chrono::steady_clock::time_point begin;
            double preparation_ms = 0;
            void checkpoint(const char* phase) override {
                if (std::strcmp(phase,"render preparation") == 0) begin = std::chrono::steady_clock::now();
                if (std::strcmp(phase,"render prepared") == 0)
                    preparation_ms = std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-begin).count();
            }
        } loading;
        const auto load_start = std::chrono::steady_clock::now();
        require(scene.load(posix_file_system(),std::filesystem::path(argv[1]),error,nullptr,&loading),error);
        std::cout << "LOAD_MS " << std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-load_start).count() << '\n';
        std::cout << "LOAD_PREPARATION_MS " << loading.preparation_ms << '\n';
        RaceRun race;
        require(race.start(std::filesystem::path(argv[1]),scene.track(),error),error);
        std::ifstream input(argv[2],std::ios::binary);
        const std::string text((std::istreambuf_iterator<char>(input)),{});
        require(bool(input),"recording read failed");
        RaceReplay replay;
        require(replay.load(text,error),error);
        std::cout << std::fixed << std::setprecision(6);
        std::cout << "METHOD warmup=1 samples=120 sizeof_draw=" << sizeof(RecordedDraw)
                  << " sizeof_resource=" << sizeof(UploadResource) << '\n';
        sample(scene,race);
        const std::set<std::uint64_t> ticks{6000,11344,24300,36510};
        while (race.session().timing().outcome == RaceOutcome::running) {
            GamepadFrame pad;
            require(replay.next(race.session().timing().step,pad,error),error);
            require(race.simulate(pad,error) && race.commit(error),error);
            if (ticks.count(race.session().timing().step)) sample(scene,race);
        }
        require(replay.exhausted() && race.matches_event1_replay(),"replay pin mismatch");
        std::cout << "PASS: selection pairs equal; exact replay pins; sink=" << observed << '\n';
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "FAIL: " << e.what() << '\n'; return 1;
    }
}
