#include "nfsu/race_gpu_scene.h"
#include "nfsu/posix_file_system.h"
#include "nfsu/draw_execution.h"
#include <fstream>
#include <iostream>
#include <algorithm>
#include <set>
#include <stdexcept>
#include <cstring>
#include <cstdlib>
#include <new>
#include <limits>

// Count live C++ allocation bytes without allocating inside the counter.
// This is a per-frame retention check, not a claim about the newlib heap.
namespace {
std::size_t live_bytes = 0;
struct Allocation { void* raw; std::size_t bytes; };
void* allocate_counted(std::size_t bytes, std::size_t alignment) {
    if (!bytes) bytes = 1;
    alignment = std::max(alignment,alignof(std::max_align_t));
    if (bytes > std::numeric_limits<std::size_t>::max()-alignment-sizeof(Allocation)) throw std::bad_alloc();
    void* raw = std::malloc(bytes+alignment+sizeof(Allocation));
    if (!raw) throw std::bad_alloc();
    auto address = (reinterpret_cast<std::uintptr_t>(raw)+sizeof(Allocation)+alignment-1)&~(alignment-1);
    auto* header = reinterpret_cast<Allocation*>(address)-1;
    header->raw = raw; header->bytes = bytes; live_bytes += bytes;
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

void require(bool okay, const std::string& error) { if (!okay) throw std::runtime_error(error); }
int main(int argc, char** argv) {
    using namespace nfsu;
    try {
        std::string error;
        RaceTickDebt clock;
        clock.add(16666); require(!clock.take(), "clock rounds early");
        clock.add(1); require(clock.take() && !clock.take(), "clock loses fractional debt");
        clock.add(1000000); for (int i = 0; i < 8; ++i) require(clock.take(), "catch-up tick missing");
        require(clock.debt_ticks() == 52, "clock discarded slow-frame debt");
        clock.reset(); require(!clock.take(), "clock reset failed");
        RaceReplay replay; GamepadFrame pad;
        require(!replay.load("NFSU_RACE_INPUT 1\n1 0 0\n", error), "unverified replay accepted");
        require(!replay.next(0, pad, error), "empty replay accepted");
        require(argc >= 2, "recording argument missing");
        std::ifstream in(argv[1], std::ios::binary);
        const std::string text((std::istreambuf_iterator<char>(in)), {});
        require(bool(in) && replay.load(text, error), error);
        require(!replay.next(1, pad, error), "out of order replay tick accepted");
        std::vector<RaceInputRun> runs; require(parse_race_input(text, runs, error), error);
        std::uint64_t ticks = 0;
        for (const auto& run : runs) for (std::uint32_t i = 0; i < run.ticks; ++i, ++ticks) {
            require(replay.next(ticks, pad, error), error);
            require(pad.pads[0].connected && pad.pads[0].buttons == run.buttons &&
                    pad.pads[0].axes[0] == run.steer && pad.time_us == ticks*1000000/60,
                    "replay pad differs from desktop");
        }
        require(ticks == 48336 && replay.exhausted() && !replay.next(ticks, pad, error), "replay extent differs");
        RaceRun unopened;
        require(!unopened.commit(error) && !unopened.simulate({}, error), "unstarted race accepted");
        MeshVertex vertices[3]{}; std::uint16_t indices[] = {0,1,2};
        RecordedCommands fixture; fixture.width = 1280; fixture.height = 720; fixture.clear_color[3] = 255;
        UploadResource v; v.name = "vertices"; v.data = reinterpret_cast<const unsigned char*>(vertices);
        v.bytes = sizeof vertices; v.elements = 3; v.element_bytes = sizeof(MeshVertex);
        UploadResource ix; ix.name = "indices"; ix.kind = UploadResourceKind::index_buffer;
        ix.data = reinterpret_cast<const unsigned char*>(indices); ix.bytes = sizeof indices; ix.elements = 3; ix.element_bytes = 2;
        fixture.uploads.resources = {v, ix}; fixture.uploads.totals.resources = 2;
        fixture.uploads.totals.vertex_buffers = fixture.uploads.totals.index_buffers = 1;
        fixture.uploads.totals.vertex_bytes = sizeof vertices; fixture.uploads.totals.index_bytes = sizeof indices;
        RecordedDraw draw; draw.vertex_resource = 0; draw.index_resource = 1; draw.vertex_count = draw.index_count = 3;
        for (const auto& p : shader_manifest::kPrograms) if (std::string(p.name) == "car_fallback") {
            draw.program = &p; draw.pipeline = p.variants[0];
        }
        fixture.draws.push_back(draw);
        RecordedCommands joined;
        require(combine_race_commands(fixture, fixture, joined, error), error);
        require(joined.draws.size() == 2 && joined.uploads.resources.size() == 4 &&
                joined.draws[1].vertex_resource == 2 && joined.draws[1].index_resource == 3 &&
                joined.draws[1].texture_resource == kNoHandle, "car upload references not rebased");
        auto many = fixture;
        many.uploads.resources.resize(4126, v);
        many.uploads.resources[1] = ix;
        DrawExecutionPlan slot_plan;
        require(plan_draw_execution(many, slot_plan, error) && slot_plan.buffer_allocations == 4126,
                "combined scene buffer slots truncated at 4096");
        auto empty = fixture; empty.uploads.resources.clear(); empty.draws.clear();
        require(plan_draw_execution(empty, slot_plan, error) && slot_plan.buffer_allocations == 1,
                "empty scene lost its valid arena configuration");
        unsigned char rgba[4] = {10,20,30,255};
        auto textured = fixture;
        UploadResource texture; texture.name = "pixel"; texture.kind = UploadResourceKind::texture;
        texture.memory = UploadMemory::image; texture.data = rgba; texture.bytes = 4;
        require(check_texture_upload_format(TextureUploadFormat::rgba8,1,1,4,texture.texture,error),error);
        texture.elements = 1; texture.element_bytes = 4;
        textured.uploads.resources.push_back(texture); textured.uploads.totals.resources = 3;
        textured.uploads.totals.textures = 1; textured.uploads.totals.texture_bytes = 4;
        textured.uploads.totals.largest_texture_bytes = 4;
        textured.draws[0].texture_resource = 2;
        for (const auto& p : shader_manifest::kPrograms) if (std::string(p.name) == "car_textured") {
            textured.draws[0].program = &p; textured.draws[0].pipeline = p.variants[0];
        }
        require(combine_race_commands(textured,textured,joined,error),error);
        require(joined.draws[1].texture_resource == 5 && joined.uploads.resources[5].data == rgba,
                "car texture reference not rebased");
        auto other = fixture; other.frame.eye[0] = 1;
        require(!combine_race_commands(fixture, other, joined, error) && joined.draws.empty(), "mixed cameras accepted");
        other = fixture; other.draws.resize(2047, draw);
        require(!combine_race_commands(fixture, other, joined, error), "combined draw overflow accepted");
        std::cout << "PASS: fixed clock, exact replay cursor, refusal and combined draw guards\n";
        if (argc == 2) return 0;
        const std::filesystem::path root(argv[2]);
        RaceGpuScene scene;
        struct PollCount : TrackSliceLoadObserver {
            std::size_t sections = 0; bool car_prepared = false;
            void checkpoint(const char* phase) override {
                if (std::string(phase) == "stream sections") ++sections;
                if (std::string(phase) == "car prepared") car_prepared = true;
            }
        } polled;
        require(scene.load(posix_file_system(), root, error, nullptr, &polled), error);
        require(polled.sections >= 4*scene.track().sections && polled.car_prepared,
                "race scene disconnected the shared loader checkpoints");
        require(!scene.load(posix_file_system(),root,error),"loaded race scene allowed cache-invalidating reload");
        RaceRun race; require(race.start(root, scene.track(), error), error);
        require(!race.commit(error), "commit without step accepted");
        require(replay.load(text, error), error);
        require(scene.cache_bytes() <= 8*kMiB,"command cache exceeded metadata limit");
        std::cout << "command cache metadata estimate " << scene.cache_bytes() << " / " << 8*kMiB << '\n';
        std::cout << "payload stream " << scene.track().streaming_bytes << " car " << scene.car_bytes()
                  << " collision " << race.persistent_bytes() << '\n';
        require(scene.car_bytes()+race.persistent_bytes()+scene.track().contacts.memory.resident_bytes+scene.cache_bytes() <=
                kPersistentBudgetPlaceholder, "persistent payload over budget");
        RaceGpuFrame frame;
        require(scene.frame(race.session(), frame, error), error);
        UploadTable uploaded = frame.commands.uploads;
        SceneMemoryRequirement need;
        require(upload_table_requirement(uploaded, bounded_image_layout, need, error), error);
        std::cout << "resources " << uploaded.resources.size() << " buffer allocations " <<
            uploaded.totals.vertex_buffers + uploaded.totals.index_buffers << " GPU image bound " << need.image_extent << '\n';
        DrawExecutionPlan plan;
        require(plan_draw_execution(frame.commands,plan,error),error);
        const auto gpu_bound = need.buffer_extent + need.image_extent + plan.staging_bytes + 64*kMiB;
        require(gpu_bound <= kGpuBudgetPlaceholder, "combined GPU bound exceeds 256 MiB");
        std::cout << "GPU conservative bound " << gpu_bound << " / " << kGpuBudgetPlaceholder << '\n';
        MemoryArena buffers, images, staging;
        ArenaConfig config; config.category = MemoryCategory::gpu; config.max_alignment = kMaxArenaAlignment;
        config.capacity = need.buffer_extent; config.max_allocations = plan.buffer_allocations;
        require(buffers.create(config,error),error);
        config.capacity = need.image_extent; config.max_allocations = 4096;
        require(images.create(config,error),error);
        config.capacity = plan.staging_bytes;
        require(staging.create(config,error),error);
        UploadLedger ledger;
        require(ledger.create({&buffers,&images,&staging},bounded_image_layout,8192,error),error);
        std::vector<UploadHandle> handles;
        require(ledger.upload_table(uploaded,1,handles,error),error);
        ledger.retire(1);
        require(ledger.release_all(handles,2,error),error); ledger.retire(2);
        require(!buffers.stats().used_bytes && !images.stats().used_bytes && !staging.stats().used_bytes,
                "combined upload ledger leaked allocations");
        if (argc > 3 && (std::string(argv[3]) == "--profile" || std::string(argv[3]) == "--profile-reference")) {
            RaceFrameProfile total;
            std::size_t retained = 0;
            for (unsigned i = 0; i < 121; ++i) {
                RaceFrameProfile sample;
                require(scene.frame(race.session(),frame,error,&sample,std::string(argv[3]) == "--profile-reference"),error);
                const auto observed = live_bytes;
                if (!i) retained = observed;
                else require(observed == retained,"fixed-pose frames retain growing C++ allocations");
                if (i) {
                    total.cull_ms += sample.cull_ms; total.list_ms += sample.list_ms;
                    total.car_ms += sample.car_ms; total.track_ms += sample.track_ms;
                    total.combine_ms += sample.combine_ms;
                }
            }
            std::cout << "PASS: fixed-pose live C++ bytes stable at " << retained << " after warmup\n";
            std::cout << "PROFILE 120 fixed-pose frames mean ms: cull " << total.cull_ms/120
                << " list " << total.list_ms/120 << " car " << total.car_ms/120
                << " track " << total.track_ms/120 << " combine " << total.combine_ms/120 << '\n';
            return 0;
        }
        std::size_t frames = 0, max_draws = 0;
        const std::set<std::uint64_t> inspect = {1,163,180,600,6000,11344,12459,13128,24300,35396,36510,37177,48336};
        const bool full = argc > 3 && std::string(argv[3]) == "--full-draws";
        const bool sampled = argc > 3 && std::string(argv[3]) == "--sample-draws";
        require(argc <= 3 || full || sampled, "unknown retail draw sampling option");
        while (race.session().timing().outcome == RaceOutcome::running) {
            require(replay.next(race.session().timing().step, pad, error), error);
            require(race.simulate(pad, error), error);
            require(!race.simulate(pad, error), "second simulation before commit accepted");
            require(race.commit(error), error);
            const auto tick = race.session().timing().step;
            if (full || (sampled && tick%60 == 0) || inspect.count(tick)) {
                require(scene.frame(race.session(), frame, error), error);
                RaceGpuFrame reference;
                require(scene.frame(race.session(),reference,error,nullptr,true),error);
                require(reference.commands.width == frame.commands.width && reference.commands.height == frame.commands.height &&
                    std::memcmp(reference.commands.clear_color,frame.commands.clear_color,4) == 0 &&
                    reference.commands.draws.size() == frame.commands.draws.size() &&
                    reference.commands.uploads.resources.size() == frame.commands.uploads.resources.size() &&
                    std::memcmp(&reference.commands.frame,&frame.commands.frame,sizeof(DrawFrameUniform)) == 0 &&
                    reference.triangles == frame.triangles && reference.far_plane == frame.far_plane &&
                    reference.visible == frame.visible && reference.unplaced == frame.unplaced,
                    "cached frame metadata differs from reference");
                for (std::size_t i = 0; i < frame.commands.draws.size(); ++i) {
                    const auto& a = frame.commands.draws[i]; const auto& b = reference.commands.draws[i];
                    require(a.name == b.name && a.group == b.group && a.mesh == b.mesh && a.instance == b.instance &&
                        a.vertex_resource == b.vertex_resource && a.index_resource == b.index_resource &&
                        a.texture_resource == b.texture_resource && a.first_vertex == b.first_vertex &&
                        a.vertex_count == b.vertex_count && a.first_index == b.first_index && a.index_count == b.index_count &&
                        a.program == b.program && a.pipeline.name == b.pipeline.name &&
                        a.pipeline.cull == b.pipeline.cull && a.pipeline.alpha_test == b.pipeline.alpha_test &&
                        a.pipeline.alpha_ref == b.pipeline.alpha_ref && a.pipeline.blend == b.pipeline.blend &&
                        a.pipeline.blend_src == b.pipeline.blend_src && a.pipeline.blend_dst == b.pipeline.blend_dst &&
                        a.pipeline.blend_op == b.pipeline.blend_op && a.pipeline.depth_test == b.pipeline.depth_test &&
                        a.pipeline.depth_write == b.pipeline.depth_write && a.pipeline.color_write == b.pipeline.color_write &&
                        std::memcmp(&a.placement,&b.placement,sizeof a.placement) == 0 &&
                        std::memcmp(&a.material,&b.material,sizeof a.material) == 0,
                        "cached ordered draw differs from reference");
                }
                require(frame.track_draws > 0 && frame.car_draws == 94, "missing track/car commands");
                require(validate_draw_update(uploaded, frame.commands, kTrackSliceDrawLimit, error), error);
                max_draws = std::max(max_draws, frame.commands.draws.size()); ++frames;
                if (inspect.count(tick)) std::cout << "frame " << tick << " draws " << frame.commands.draws.size()
                    << " car " << frame.car_draws << " far " << frame.far_plane << '\n';
            }
        }
        for (const auto& line : race.result_lines()) std::cout << line << '\n';
        require(replay.exhausted() && race.matches_event1_replay(), "Switch race runner differs from desktop pins");
        std::cout << "PASS: exact per-step stream and result; frames " << frames << " max draws " << max_draws << '\n';
        return 0;
    } catch (const std::exception& e) { std::cerr << "FAIL: " << e.what() << '\n'; return 1; }
}
