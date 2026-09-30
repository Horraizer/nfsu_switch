#include "nfsu/race_gpu_scene.h"
#include "nfsu/car_parts.h"
#include "nfsu/posix_file_system.h"
#include "nfsu/draw_execution.h"
#include "nfsu/deko_gpfifo_budget.h"
#include "nfsu/platform_desktop.h"
#include "nfsu/arg_path.h"
#include <fstream>
#include <iostream>
#include <algorithm>
#include <set>
#include <stdexcept>
#include <cstring>
#include <cstdlib>
#include <new>
#include <limits>
#include <cmath>

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

void require(bool okay, const std::string& error) { if (!okay) throw std::runtime_error(error); }
bool same_upload_resource(const nfsu::UploadResource& a, const nfsu::UploadResource& b) {
    return a.kind == b.kind && a.lifetime == b.lifetime && a.memory == b.memory &&
        a.name == b.name && a.source == b.source && a.data == b.data &&
        a.bytes == b.bytes && a.alignment == b.alignment &&
        a.element_bytes == b.element_bytes && a.elements == b.elements &&
        a.texture_hash == b.texture_hash &&
        a.texture.format == b.texture.format && a.texture.width == b.texture.width &&
        a.texture.height == b.texture.height && a.texture.texel_bytes == b.texture.texel_bytes &&
        a.texture.row_bytes == b.texture.row_bytes && a.texture.bytes == b.texture.bytes;
}
bool same_upload_totals(const nfsu::UploadTotals& a, const nfsu::UploadTotals& b) {
    return a.resources == b.resources && a.vertex_buffers == b.vertex_buffers &&
        a.index_buffers == b.index_buffers && a.textures == b.textures &&
        a.generated_textures == b.generated_textures && a.vertex_bytes == b.vertex_bytes &&
        a.index_bytes == b.index_bytes && a.texture_bytes == b.texture_bytes &&
        a.generated_texture_bytes == b.generated_texture_bytes &&
        a.largest_texture_bytes == b.largest_texture_bytes && a.empty_buffers == b.empty_buffers;
}
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
        require(deko_track_list_calls(65) == 2 &&
                deko_frame_gpfifo_bound(deko_track_list_calls(2047)) < kDekoGpfifoRingEntries &&
                deko_frame_gpfifo_bound(1828) > kDekoGpfifoRingEntries,
                "per-draw prepared calls would overflow the Switch GPFIFO ring");
        RaceReplay replay; GamepadFrame pad;
        require(!replay.load("NFSU_RACE_INPUT 1\n1 0 0\n", error), "unverified replay accepted");
        require(!replay.next(0, pad, error), "empty replay accepted");
        require(argc >= 2, "recording argument missing");
        require(check_arguments(argc, argv, 1, error), error);
        std::ifstream in(argument_path(argv[1]), std::ios::binary);
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
        fixture.owning_uploads().resources = {v, ix}; fixture.owning_uploads().totals.resources = 2;
        fixture.owning_uploads().totals.vertex_buffers = fixture.owning_uploads().totals.index_buffers = 1;
        fixture.owning_uploads().totals.vertex_bytes = sizeof vertices; fixture.owning_uploads().totals.index_bytes = sizeof indices;
        RecordedDraw draw; draw.vertex_resource = 0; draw.index_resource = 1; draw.vertex_count = draw.index_count = 3;
        for (const auto& p : shader_manifest::kPrograms) if (std::string(p.name) == "car_fallback") {
            draw.program = &p; draw.pipeline = p.variants[0];
        }
        fixture.owning_draws().push_back(draw);
        RecordedCommands joined;
        require(combine_race_commands(fixture, fixture, joined, error), error);
        require(joined.owning_draws().size() == 2 && joined.owning_uploads().resources.size() == 4 &&
                joined.owning_draws()[1].vertex_resource == 2 && joined.owning_draws()[1].index_resource == 3 &&
                joined.owning_draws()[1].texture_resource == kNoHandle, "car upload references not rebased");
        DrawValidationAudit checked_audit;
        auto* previous_checked_audit = set_draw_validation_audit(&checked_audit);
        const bool checked_ok = validate_recorded_commands(joined,error);
        set_draw_validation_audit(previous_checked_audit);
        require(checked_ok && checked_audit.index_visits == 6, "index audit missed reference validation");
        auto many = fixture;
        many.owning_uploads().resources.resize(4126, v);
        many.owning_uploads().resources[1] = ix;
        DrawExecutionPlan slot_plan;
        require(plan_draw_execution(many, slot_plan, error) && slot_plan.buffer_allocations == 4126,
                "combined scene buffer slots truncated at 4096");
        auto empty = fixture; empty.owning_uploads().resources.clear(); empty.owning_draws().clear();
        require(plan_draw_execution(empty, slot_plan, error) && slot_plan.buffer_allocations == 1,
                "empty scene lost its valid arena configuration");
        unsigned char rgba[4] = {10,20,30,255};
        auto textured = fixture;
        UploadResource texture; texture.name = "pixel"; texture.kind = UploadResourceKind::texture;
        texture.memory = UploadMemory::image; texture.data = rgba; texture.bytes = 4;
        require(check_texture_upload_format(TextureUploadFormat::rgba8,1,1,4,texture.texture,error),error);
        texture.elements = 1; texture.element_bytes = 4;
        textured.owning_uploads().resources.push_back(texture); textured.owning_uploads().totals.resources = 3;
        textured.owning_uploads().totals.textures = 1; textured.owning_uploads().totals.texture_bytes = 4;
        textured.owning_uploads().totals.largest_texture_bytes = 4;
        textured.owning_draws()[0].texture_resource = 2;
        for (const auto& p : shader_manifest::kPrograms) if (std::string(p.name) == "car_textured") {
            textured.owning_draws()[0].program = &p; textured.owning_draws()[0].pipeline = p.variants[0];
        }
        require(combine_race_commands(textured,textured,joined,error),error);
        require(joined.owning_draws()[1].texture_resource == 5 && joined.owning_uploads().resources[5].data == rgba,
                "car texture reference not rebased");
        auto other = fixture; other.frame.eye[0] = 1;
        require(!combine_race_commands(fixture, other, joined, error) && joined.owning_draws().empty(), "mixed cameras accepted");
        other = fixture; other.owning_draws().resize(2047, draw);
        require(!combine_race_commands(fixture, other, joined, error), "combined draw overflow accepted");
        std::cout << "PASS: fixed clock, exact replay cursor, refusal and combined draw guards\n";
        if (argc == 2) return 0;
        const std::filesystem::path root = argument_path(argv[2]);
        RaceGpuScene scene;
        scene.set_particles_enabled(true);
        RaceRun race;
        require(race.prepare_metadata(root, error), error);
        require(!race.prepare_metadata(root, error) && error == "race metadata already prepared",
                "prepared race metadata accepted a second load");
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
        require(race.start(root, scene.track(), error), error);
        RaceGpuScene unloaded_scene;
        RaceGpuFrame unloaded_frame;
        require(!unloaded_scene.frame(race.session(), unloaded_frame, error) &&
                error == "race scene is not loaded", "unloaded race scene did not refuse a frame");
        require(!race.commit(error), "commit without step accepted");
        require(replay.load(text, error), error);
        require(scene.cache_bytes() <= 8*kMiB,"command cache exceeded metadata limit");
        std::cout << "command cache metadata estimate " << scene.cache_bytes() << " / " << 8*kMiB << '\n';
        // #186 particles and #184 sky are both present in the merged cache.
        require(scene.cache_bytes() == 3826234,"event 1 command cache metadata estimate differs from pin");
        std::cout << "payload stream " << scene.track().streaming_bytes << " car " << scene.car_bytes()
                  << " collision " << race.persistent_bytes() << '\n';
        require(scene.car_bytes()+race.persistent_bytes()+scene.track().contacts.memory.resident_bytes+scene.cache_bytes() <=
                kPersistentBudgetPlaceholder, "persistent payload over budget");
        RaceGpuFrame frame;
        require(scene.frame(race.session(), frame, error), error);
        require(frame.commands.package_identity() != nullptr &&
                frame.commands.static_track_draw_count() == frame.track_draws &&
                frame.commands.static_track_instance_count() == scene.track().scene.instances.size() + 1 &&  // #184: the sky placement
                frame.commands.static_track_instance_count() <= 65536 &&
                frame.commands.static_track_definition_count() <= 4096,
                "prepared static package identity or instance count changed");
        std::vector<const RecordedDraw*> static_by_group;
        for (std::size_t i = 0; i < frame.commands.static_track_definition_count(); ++i) {
            const auto& definition = frame.commands.static_track_definition(i);
            require(definition.group < 65536, "static track group exceeds Switch slot table");
            if (definition.group >= static_by_group.size())
                static_by_group.resize(definition.group + 1, nullptr);
            require(!static_by_group[definition.group], "duplicate static track group");
            static_by_group[definition.group] = &definition;
        }
        require(!scene.set_sky(false), "sky mode changed after load");
        {
            // #227: --sky off records no sky draws; the package then holds
            // only the scenery placements, at the same pose and far plane.
            RaceGpuScene no_sky; RaceGpuFrame plain;
            require(no_sky.set_sky(false), "sky mode refused before load");
            require(no_sky.load(posix_file_system(), root, error), error);
            require(no_sky.frame(race.session(), plain, error), error);
            std::size_t sky_views = 0;
            for (std::size_t i = 0; i < frame.commands.draw_count(); ++i)
                if (std::strcmp(frame.commands.draw_view(i).definition.program->name, "sky_textured") == 0) ++sky_views;
            for (std::size_t i = 0; i < plain.commands.draw_count(); ++i)
                require(std::strcmp(plain.commands.draw_view(i).definition.program->name, "sky_textured") != 0,
                        "sky drawn with the sky off");
            require(sky_views == 2 && plain.track_draws + 2 == frame.track_draws &&
                    plain.car_draws == frame.car_draws && plain.far_plane == frame.far_plane &&
                    plain.commands.static_track_instance_count() == scene.track().scene.instances.size(),
                    "sky switch changed more than the two sky draws");
            std::cout << "PASS: sky off omits exactly the two sky draws and their placement\n";
        }
        const auto& const_commands = frame.commands;
        unsigned owning_refusals = 0;
        try { (void)frame.commands.owning_draws(); }
        catch (const std::logic_error&) { ++owning_refusals; }
        try { (void)frame.commands.owning_uploads(); }
        catch (const std::logic_error&) { ++owning_refusals; }
        try { (void)const_commands.owning_draws(); }
        catch (const std::logic_error&) { ++owning_refusals; }
        try { (void)const_commands.owning_uploads(); }
        catch (const std::logic_error&) { ++owning_refusals; }
        require(owning_refusals == 4, "borrowed frame exposed owning storage");
        RecordedCommands refused_append;
        require(!combine_race_commands(frame.commands,fixture,refused_append,error) && refused_append.owning_draws().empty(),
                "borrowed combined frame was appended as an owning recording");
        auto expired = frame.commands;
        require(scene.frame(race.session(), frame, error), error);
        require(!expired.generation_valid(), "old frame generation remained valid");
        DrawExecutionPlan expired_plan;
        require(!plan_draw_execution(expired,expired_plan,error), "planner accepted an expired frame");
        require(!validate_draw_update(frame.commands.resource_table(),expired,kTrackSliceDrawLimit,error),
                "update accepted an expired frame");
        bool range_refused = false;
        try { (void)frame.commands.draw_view(frame.commands.draw_count()); }
        catch (const std::out_of_range&) { range_refused = true; }
        require(range_refused, "prepared draw handle escaped capacity guard");
        UploadTable uploaded = frame.commands.resource_table();
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
            RaceFrameProfile total; DesktopTiming timer;
            std::size_t retained = 0;
            for (unsigned i = 0; i < 121; ++i) {
                RaceFrameProfile sample;
                require(scene.frame(race.session(),frame,error,&sample,std::string(argv[3]) == "--profile-reference",&timer),error);
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
        const std::set<std::uint64_t> inspect = {1,163,180,600,1164,6000,11344,12459,13128,24300,35396,36510,37177,48336};
        const bool full = argc > 3 && std::string(argv[3]) == "--full-draws";
        const bool sampled = argc > 3 && std::string(argv[3]) == "--sample-draws";
        require(argc <= 3 || full || sampled, "unknown retail draw sampling option");
        while (race.session().timing().outcome == RaceOutcome::running) {
            require(replay.next(race.session().timing().step, pad, error), error);
            require(race.simulate(pad, error), error);
            require(!race.simulate(pad, error), "second simulation before commit accepted");
            require(race.commit(error), error);
            scene.advance_particles(race.session());
            const auto tick = race.session().timing().step;
            if (full || (sampled && tick%60 == 0) || inspect.count(tick)) {
                DrawValidationAudit audit;
                auto* previous_audit = set_draw_validation_audit(&audit);
                const auto allocations_before = allocation_calls;
                const bool frame_ok = scene.frame(race.session(), frame, error);
                const auto frame_allocations = allocation_calls - allocations_before;
                set_draw_validation_audit(previous_audit);
                require(frame_ok, error);
                require(audit.index_visits == 0, "prepared frame rescanned indices");
                require(frame_allocations == 0, "prepared frame allocated after load");
                require(frame.commands.borrowed(), "prepared frame reconstructed owning commands");
                RaceGpuFrame reference;
                require(scene.frame(race.session(),reference,error,nullptr,true),error);
                require(!reference.commands.borrowed(), "reference path returned a borrowed recording");
                require(reference.commands.width == frame.commands.width && reference.commands.height == frame.commands.height &&
                    std::memcmp(reference.commands.clear_color,frame.commands.clear_color,4) == 0 &&
                    reference.commands.draw_count() == frame.commands.draw_count() &&
                    reference.commands.resource_table().resources.size() == frame.commands.resource_table().resources.size() &&
                    same_upload_totals(reference.commands.resource_table().totals,frame.commands.resource_table().totals) &&
                    std::memcmp(&reference.commands.frame,&frame.commands.frame,sizeof(DrawFrameUniform)) == 0 &&
                    reference.triangles == frame.triangles && reference.far_plane == frame.far_plane &&
                    reference.visible == frame.visible && reference.unplaced == frame.unplaced,
                    "cached frame metadata differs from reference");
                for (std::size_t i = 0; i < frame.commands.resource_table().resources.size(); ++i)
                    require(same_upload_resource(reference.commands.resource_table().resources[i],frame.commands.resource_table().resources[i]),
                            "cached upload resource differs from reference at index " + std::to_string(i));
                for (std::size_t i = 0; i < frame.commands.draw_count(); ++i) {
                    const auto selected = frame.commands.draw_view(i);
                    const auto& a = selected.definition; const auto& b = reference.commands.owning_draws()[i];
                    if (i < frame.commands.static_track_draw_count()) {
                        require(a.group < static_by_group.size() &&
                                static_by_group[a.group] == &a && selected.track_instance &&
                                selected.instance < frame.commands.static_track_instance_count() &&
                                &selected.placement == &frame.commands.static_track_instance(selected.instance) &&
                                &selected.material == &a.material,
                                "selected track draw escaped its static package");
                    } else require(!selected.track_instance, "car draw entered static track range");
                    require(selected.diagnostic_name() == b.name && a.group == b.group && a.mesh == b.mesh && selected.instance == b.instance &&
                        a.vertex_resource == b.vertex_resource && a.index_resource == b.index_resource &&
                        a.texture_resource == b.texture_resource && a.first_vertex == b.first_vertex &&
                        a.vertex_count == b.vertex_count && a.first_index == b.first_index && a.index_count == b.index_count &&
                        a.program == b.program && a.pipeline.name == b.pipeline.name &&
                        a.pipeline.cull == b.pipeline.cull && a.pipeline.alpha_test == b.pipeline.alpha_test &&
                        a.pipeline.alpha_ref == b.pipeline.alpha_ref && a.pipeline.blend == b.pipeline.blend &&
                        a.pipeline.blend_src == b.pipeline.blend_src && a.pipeline.blend_dst == b.pipeline.blend_dst &&
                        a.pipeline.blend_op == b.pipeline.blend_op && a.pipeline.depth_test == b.pipeline.depth_test &&
                        a.pipeline.depth_write == b.pipeline.depth_write && a.pipeline.color_write == b.pipeline.color_write &&
                        std::memcmp(&selected.placement,&b.placement,sizeof b.placement) == 0 &&
                        std::memcmp(&selected.material,&b.material,sizeof b.material) == 0,
                        "cached ordered draw differs from reference");
                }
                require(frame.track_draws > 0 && frame.car_draws == 74 &&
                        frame.particle_draws == kRaceParticleLimit,
                        "missing track/car/particle commands");
                if (tick == 1164) {
                    unsigned visible_smoke=0, visible_sparks=0;
                    bool smoke_color_checked=false, spark_color_checked=false;
                    for (std::size_t i=frame.commands.static_track_draw_count()+frame.car_draws;
                         i<frame.commands.draw_count(); ++i) {
                        const auto view=frame.commands.draw_view(i);
                        const auto slot=i-frame.commands.static_track_draw_count()-frame.car_draws;
                        const bool spark=slot>=kRaceSmokeLimit;
                        if (view.material.alpha[0] > 0)
                            (spark ? visible_sparks : visible_smoke)++;
                        if ((!spark && view.material.alpha[0] > 0 && !smoke_color_checked) ||
                            (slot == kRaceSmokeLimit && !spark_color_checked)) {
                                // Evaluate the recorded textured shader state at two
                                // opposed billboard headings against the CPU texel.
                                const auto texel=race_particle_texel(
                                    spark ? RaceParticleKind::spark : RaceParticleKind::smoke,8,8);
                                const auto& state=frame.commands.frame;
                                require(state.options[2] == 1 && view.material.fallback[3] == 2.0F &&
                                        view.material.alpha[2] == 0.0F && view.material.alpha[3] == 0.0F,
                                        "particle draw is not unlit under flat race shading");
                                const float length=std::sqrt(state.light[0]*state.light[0]+state.light[1]*state.light[1]+
                                                             state.light[2]*state.light[2]);
                                require(length > 0.0F,"race light direction is zero");
                                for (float heading : {-1.0F,1.0F}) {
                                    const float dot=heading*length;
                                    const unsigned shade=state.options[2] && view.material.fallback[3] < 1.5F
                                        ? static_cast<unsigned>(std::floor((0.35F+0.65F*std::max(0.0F,dot))*256.0F+0.5F))
                                        : 256U;
                                    for (unsigned channel=0; channel<3; ++channel)
                                        require(((static_cast<unsigned>(texel[channel])*shade+128U)>>8U)==texel[channel],
                                                "GPU particle source color differs from CPU reference");
                                }
                                (spark ? spark_color_checked : smoke_color_checked)=true;
                        }
                    }
                    require(visible_smoke >= 4 && visible_smoke <= kRaceSmokeLimit &&
                            visible_sparks == 0 && smoke_color_checked && spark_color_checked,
                            "slide frame does not contain bounded smoke and no sparks");
                }
                require(deko_frame_gpfifo_bound(deko_track_list_calls(
                            static_cast<std::uint32_t>(frame.track_draws))) < kDekoGpfifoRingEntries,
                        "selected track draws exceed GPFIFO call budget");
                if (tick == 13128) {
                    require(race.session().animation().braking, "animation guard tick is not braking");
                    StockFrameInputs inputs;
                    require(read_stock_frame_inputs(root, inputs), "animation guard retail car inputs unavailable");
                    StockFrame stock;
                    require(prepare_stock_frame(inputs, stock_240sx_request(), stock_240sx_skin(), stock, error), error);
                    std::vector<PreparedInstance> animated = stock.scene.instances;
                    std::vector<PreparedInstance> static_placed = stock.scene.instances;
                    require(drive_place_instances(stock.scene.instances, race.session().car(),
                                race.session().spec(), animated, &race.session().animation()),
                            "animation guard placement failed");
                    require(drive_place_instances(stock.scene.instances, race.session().car(),
                                race.session().spec(), static_placed),
                            "animation guard static placement failed");
                    std::size_t wheels = 0, moving_wheels = 0, headlights = 0, brakelights = 0;
                    for (std::size_t i = 0; i < frame.commands.draw_count(); ++i) {
                        const auto draw = frame.commands.draw_view(i);
                        if (draw.definition.name.rfind("race-car/car group ",0) != 0) continue;
                        require(draw.instance < stock.scene.instances.size(), "animation guard instance out of range");
                        const auto slot = stock.scene.instances[draw.instance].part_slot;
                        if (slot == kSlotWheel) {
                            ++wheels;
                            require(std::memcmp(draw.placement.rows, animated[draw.instance].transform,
                                        sizeof animated[draw.instance].transform) == 0,
                                    "animated race wheel placement differs from car state");
                            if (std::memcmp(animated[draw.instance].transform,
                                            static_placed[draw.instance].transform,
                                            sizeof animated[draw.instance].transform) != 0) ++moving_wheels;
                        } else if (slot == kSlotHeadlight) {
                            ++headlights;
                            require(draw.material.alpha[3] == 1.0F, "race headlight mode is not on");
                        } else if (slot == kSlotBrakelight) {
                            ++brakelights;
                            require(draw.material.alpha[3] == 3.0F, "braking race lamp mode is not bright");
                        }
                    }
                    require(wheels > 0 && moving_wheels > 0 && headlights > 0 && brakelights > 0,
                            "animation guard did not inspect moving wheels and both lamp slots");
                }
                require(validate_draw_update(uploaded, frame.commands, kTrackSliceDrawLimit, error), error);
                max_draws = std::max(max_draws, frame.commands.draw_count()); ++frames;
                if (inspect.count(tick)) std::cout << "frame " << tick << " draws " << frame.commands.draw_count()
                    << " car " << frame.car_draws << " far " << frame.far_plane << '\n';
            }
        }
        for (const auto& line : race.result_lines()) std::cout << line << '\n';
        require(replay.exhausted() && race.matches_event1_replay(), "Switch race runner differs from desktop pins");
        std::cout << "PASS: exact per-step stream and result; frames " << frames << " max draws " << max_draws << '\n';
        return 0;
    } catch (const std::exception& e) { std::cerr << "FAIL: " << e.what() << '\n'; return 1; }
}
