#include "nfsu/race_gpu_scene.h"
#include "nfsu/draw_execution.h"
#include <algorithm>
#include <cstring>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace nfsu {
namespace {
RenderOptions race_render_options() {
    RenderOptions options;
    options.width = 1280; options.height = 720;
    return options;
}
void append_car_commands(RecordedCommands& track, const RecordedCommands& car) {
    const auto offset = track.uploads.resources.size();
    for (auto resource : car.uploads.resources) {
        resource.name = "race-car/" + resource.name;
        track.uploads.resources.push_back(std::move(resource));
    }
    auto& a = track.uploads.totals; const auto& b = car.uploads.totals;
    a.resources += b.resources; a.vertex_buffers += b.vertex_buffers;
    a.index_buffers += b.index_buffers; a.textures += b.textures;
    a.generated_textures += b.generated_textures; a.vertex_bytes += b.vertex_bytes;
    a.index_bytes += b.index_bytes; a.texture_bytes += b.texture_bytes;
    a.generated_texture_bytes += b.generated_texture_bytes; a.empty_buffers += b.empty_buffers;
    a.largest_texture_bytes = std::max(a.largest_texture_bytes, b.largest_texture_bytes);
    for (auto draw : car.draws) {
        draw.name = "race-car/" + draw.name;
        draw.vertex_resource += offset; draw.index_resource += offset;
        if (draw.texture_resource != kNoHandle) draw.texture_resource += offset;
        track.draws.push_back(std::move(draw));
    }
}

}
bool combine_race_commands(RecordedCommands track, const RecordedCommands& car,
                           RecordedCommands& out, std::string& error) {
    out = {}; error.clear();
    if (track.borrowed() || car.borrowed()) {
        error = "cannot append to an already combined borrowed frame"; return false;
    }
    if (!validate_recorded_commands(track, error) || !validate_recorded_commands(car, error)) return false;
    if (track.width != car.width || track.height != car.height ||
        std::memcmp(&track.frame, &car.frame, sizeof track.frame) != 0) {
        error = "track/car frame camera differs"; return false;
    }
    if (track.draws.size() + car.draws.size() > kTrackSliceDrawLimit ||
        track.uploads.resources.size() + car.uploads.resources.size() > kTrackSliceResourceLimit) {
        error = "combined race exceeds draw/resource capacity"; return false;
    }
    append_car_commands(track,car);
    DrawExecutionPlan plan;
    if (!plan_draw_execution(track, plan, error)) return false;
    out = std::move(track); return true;
}
bool RaceGpuScene::load(FileSystemService& files, const std::filesystem::path& root,
                        std::string& error, AssetCheckObserver* observer,
                        TrackSliceLoadObserver* loading) {
    if (ready_) { error = "race scene is already loaded"; return false; }
    if (!load_track_slice(files, root, track_, error, observer, loading)) return false;
    if (loading) loading->checkpoint("car input");
    StockFrameInputs inputs;
    if (!read_stock_frame_inputs(root, inputs)) {
        error = "stock 240SX inputs could not be read";
        for (const auto& file : inputs.files) if (!file.read) error += "\n" + file.path + ": " + file.error;
        return false;
    }
    if (loading) loading->checkpoint("car preparation");
    StockFrame stock;
    if (!prepare_stock_frame(inputs, stock_240sx_request(), stock_240sx_skin(), stock, error)) return false;
    if (loading) loading->checkpoint("car prepared");
    skin_source_ = stock.skin_binding.source;
    car_ = std::move(stock.scene); base_ = car_.instances;
    UploadTable table;
    if (!build_car_upload_table(car_, skin_source_, table, error)) return false;
    car_bytes_ = table.totals.vertex_bytes + table.totals.index_bytes + table.totals.texture_bytes +
        2 * base_.size() * sizeof(PreparedInstance);
    if (car_bytes_ + track_.contacts.memory.resident_bytes > kPersistentBudgetPlaceholder) {
        error = "race car and collision exceed the persistent payload plan"; return false;
    }
    // RaceRun loads the route and collision course itself; discard the
    // diagnostic path and contact copy before allocating that course.
    track_.contacts = {};
    std::vector<TrackResidencyPathPoint>().swap(track_.path);
    // Validate command templates once while this privately owned scene is
    // immutable. GPU upload pointers stay valid for this owner's lifetime.
    if (loading) loading->checkpoint("render preparation");
    if (!prepare_templates(error) || !prepare_frame_cache(error)) return false;
    if (loading) loading->checkpoint("render prepared");
    if (cache_bytes() > 8*kMiB) { error = "race command cache exceeds 8 MiB"; return false; }
    ready_ = true;
    // Inputs, parser geometry and duplicate decoded car textures die here.
    return true;
}
std::uint64_t RaceGpuScene::cache_bytes() const {
    std::uint64_t bytes = sizeof(track_templates_) + template_of_group_.capacity()*sizeof(std::size_t) +
        track_templates_.draws.capacity()*sizeof(RecordedDraw) +
        track_templates_.uploads.resources.capacity()*sizeof(UploadResource);
    for (const auto& draw : track_templates_.draws) bytes += draw.name.capacity()+1;
    for (const auto& resource : track_templates_.uploads.resources) bytes += resource.name.capacity()+1;
    bytes += culler_.memory_bytes() + sizeof(combined_uploads_) + sizeof(car_templates_) +
        combined_uploads_.resources.capacity()*sizeof(UploadResource) +
        car_templates_.draws.capacity()*sizeof(RecordedDraw) +
        (mesh_first_.capacity()+section_first_.capacity())*sizeof(std::size_t) +
        mesh_groups_.capacity()*sizeof(std::uint32_t) + selection_.capacity()*sizeof(TrackDrawListEntry) +
        track_placements_.capacity()*sizeof(DrawInstanceUniform);
    for (const auto& r : combined_uploads_.resources) bytes += r.name.capacity()+1;
    for (const auto& d : car_templates_.draws) bytes += d.name.capacity()+1;
    return bytes;
}
bool RaceGpuScene::prepare_templates(std::string& error) {
    std::vector<TrackVisibleInstance> witnesses;
    for (const auto first : track_.scene.first_instance_of_mesh) {
        if (first == kNoHandle) continue;
        const auto& instance = track_.scene.instances[first];
        TrackVisibleInstance item;
        item.section_index = instance.section_index; item.instance_index = instance.instance_index;
        witnesses.push_back(item);
    }
    TrackDrawList list;
    const RenderOptions options = race_render_options();
    if (!build_track_draw_list(track_.scene,witnesses,{},list,error) ||
        !record_track_commands(track_.scene,list,options,{},track_templates_,error) ||
        !validate_recorded_commands(track_templates_,error)) return false;
    template_of_group_.assign(track_.scene.region.draws.size(),kNoHandle);
    for (std::size_t i = 0; i < track_templates_.draws.size(); ++i)
        template_of_group_[track_templates_.draws[i].group] = i;
    return true;
}
bool RaceGpuScene::prepare_frame_cache(std::string& error) {
    if (!culler_.prepare(track_.objects, error)) return false;
    const auto& scene = track_.scene;
    mesh_first_.assign(scene.region.meshes.size()+1, 0);
    for (const auto& draw : scene.region.draws) if (draw.drawable) ++mesh_first_.at(draw.mesh+1);
    for (std::size_t i = 1; i < mesh_first_.size(); ++i) mesh_first_[i] += mesh_first_[i-1];
    mesh_groups_.resize(mesh_first_.back());
    auto cursor = mesh_first_;
    for (std::size_t i = 0; i < scene.region.draws.size(); ++i) {
        const auto& draw = scene.region.draws[i];
        if (draw.drawable) mesh_groups_.at(cursor.at(draw.mesh)++) = static_cast<std::uint32_t>(i);
    }
    section_first_.assign(track_.objects.sections.size(), kNoHandle);
    for (const auto& section : scene.sections) section_first_.at(section.section_index) = section.first_handle;
    track_placements_.resize(scene.instances.size());
    for (std::size_t n = 0; n < scene.instances.size(); ++n) {
        const auto& transform = scene.instances[n].transform;
        auto& placement = track_placements_[n];
        for (std::size_t i = 0; i < 3; ++i) {
            for (std::size_t j = 0; j < 3; ++j) placement.rows[i*4+j] = transform.linear[i*3+j];
            placement.rows[12+i] = transform.translation[i];
        }
    }
    if (!record_car_commands(car_, race_render_options(), {}, skin_source_, car_templates_, error)) return false;
    RecordedCommands combined = track_templates_;
    const auto track_count = track_templates_.draws.size();
    if (!validate_recorded_commands(car_templates_,error)) return false;
    append_car_commands(combined,car_templates_);
    if (combined.uploads.resources.size() > kTrackSliceResourceLimit) {
        error = "prepared resource namespace exceeds capacity"; return false;
    }
    for (std::size_t i = 0; i < car_templates_.draws.size(); ++i)
        car_templates_.draws[i] = std::move(combined.draws[track_count+i]);
    combined_uploads_ = std::move(combined.uploads);
    // Only the final namespace owns metadata. Geometry remains in track_/car_.
    track_templates_.uploads = {}; car_templates_.uploads = {};
    selection_.reserve(kTrackSliceDrawLimit);
    return true;
}

RecordedDrawView RaceGpuScene::selected_draw(std::size_t i) const {
    if (i < selection_.size()) {
        const auto& selected = selection_[i];
        const auto& draw = track_templates_.draws.at(template_of_group_.at(selected.draw));
        return {draw, selected.instance, track_placements_.at(selected.instance), draw.material, true};
    }
    const auto& draw = car_templates_.draws.at(i-selection_.size());
    return {draw, draw.instance, draw.placement, draw.material, false};
}

bool RaceGpuScene::select_track(const TrackCullReport& visible, RaceGpuFrame& out,
                               std::size_t& entries, std::string& error) {
    selection_.clear(); entries = 0; out.unplaced = 0; out.triangles = 0;
    const auto& scene = track_.scene;
    for (const auto& item : visible.visible) {
        if (item.section_index >= section_first_.size() ||
            section_first_[item.section_index] == kNoHandle ||
            item.instance_index >= track_.objects.sections[item.section_index].instances.size()) {
            error = "prepared visible instance is out of range"; return false;
        }
        const auto position = section_first_[item.section_index]+item.instance_index;
        if (position >= scene.instance_handles.size()) { error = "prepared handle is out of range"; return false; }
        const auto handle = scene.instance_handles[position];
        if (handle == kNoHandle) { ++out.unplaced; continue; }
        if (handle >= scene.instances.size()) { error = "prepared instance is out of range"; return false; }
        const auto mesh = scene.instances[handle].mesh;
        if (mesh >= scene.region.meshes.size()) { error = "prepared mesh is out of range"; return false; }
        const auto first = mesh_first_[mesh], last = mesh_first_[mesh+1];
        entries += last-first;
        // Keep the reference path's far-plane retry, including empty groups
        // in the list capacity test even though they emit no hardware draw.
        if (entries + car_templates_.draws.size() > kTrackSliceDrawLimit) return true;
        for (auto k = first; k < last; ++k) {
            const auto group = mesh_groups_[k];
            if (group >= template_of_group_.size()) { error = "prepared group is out of range"; return false; }
            const auto index = template_of_group_[group];
            if (index == kNoHandle) {
                if (!scene.region.draws[group].index_count) continue;
                error = "prepared group has no validated template"; return false;
            }
            if (index >= track_templates_.draws.size() || selection_.size() >= kTrackSliceDrawLimit) {
                error = "prepared draw capacity exceeded"; return false;
            }
            selection_.push_back({static_cast<std::uint32_t>(handle), group});
            out.triangles += track_templates_.draws[index].index_count/3;
        }
    }
    return true;
}
bool RaceGpuScene::frame(const RaceSession& session, RaceGpuFrame& out, std::string& error, RaceFrameProfile* profile, bool reference, TimingService* timer) {
    out = {}; error.clear();
    if (!ready_) { error = "race scene is not loaded"; return false; }
    if (profile) *profile = {};
    if (profile && !timer) { error = "race profiling requires a timing service"; return false; }
    if (!reference && generation_ == std::numeric_limits<std::uint64_t>::max()) {
        error = "command generation exhausted"; return false;
    }
    if (!reference) ++generation_;
    auto start = profile ? timer->now_us() : 0;
    auto stage = [&](double RaceFrameProfile::* field) {
        if (!profile) return;
        const auto end = timer->now_us();
        profile->*field += (end-start)/1000.0;
        start = end;
    };
    if (!drive_place_instances(base_, session.car(), session.spec(), car_.instances,
                               &session.animation())) {
        error = "race car placement failed"; return false;
    }
    for (double far = 250; far >= 31.25; far *= 0.5) {
        RenderOptions options = race_render_options();
        if (!drive_render_camera(session.camera(), ChaseFovMode::vertical_4_3,
                                 options.width, options.height, options.camera, error)) return false;
        options.camera.far_plane = far;
        CameraFrame basis;
        if (!make_camera_frame(options.camera, options.width, options.height, basis, error)) return false;
        TrackCamera camera;
        camera.eye[0] = float(basis.eye.x); camera.eye[1] = float(basis.eye.y); camera.eye[2] = float(basis.eye.z);
        camera.target[0] = float(options.camera.target.x); camera.target[1] = float(options.camera.target.y);
        camera.target[2] = float(options.camera.target.z);
        camera.up[0] = 0; camera.up[1] = 1; camera.up[2] = 0;
        camera.aspect = float(options.width)/options.height;
        camera.vertical_fov = float(options.camera.fov_y_deg * (3.14159265358979323846/180));
        camera.near_plane = float(options.camera.near_plane); camera.far_plane = float(far);
        TrackFrustum frustum;
        if (!track_frustum_look_at(camera, frustum)) { error = "race camera frustum invalid"; return false; }
        if (!reference) {
            if (!culler_.cull(frustum,error)) return false;
            stage(&RaceFrameProfile::cull_ms);
            std::size_t entries = 0;
            if (!select_track(culler_.result(),out,entries,error)) return false;
            stage(&RaceFrameProfile::list_ms);
            if (entries + car_templates_.draws.size() > kTrackSliceDrawLimit) continue;
            for (auto& draw : car_templates_.draws) {
                if (draw.instance != kNoHandle) {
                    if (draw.instance >= car_.instances.size()) { error = "prepared car instance is out of range"; return false; }
                    const auto& instance = car_.instances[draw.instance];
                    std::copy(std::begin(instance.transform),std::end(instance.transform),draw.placement.rows);
                    draw.material.alpha[3] = static_cast<float>(instance.lamp_mode);
                }
            }
            if (!record_frame_state(options,{},out.commands,error)) return false;
            stage(&RaceFrameProfile::car_ms);
            out.track_draws = selection_.size(); out.car_draws = car_templates_.draws.size();
            out.visible = culler_.result().visible.size(); out.far_plane = far;
            stage(&RaceFrameProfile::track_ms);
            if (out.triangles > RenderLimits{}.max_submitted_triangles) {
                error = "prepared submitted triangle capacity exceeded"; return false;
            }
            for (const auto& draw : car_templates_.draws) out.triangles += draw.index_count/3;
            out.commands.source_ = this; out.commands.generation_ = generation_;
            stage(&RaceFrameProfile::combine_ms);
            return true;
        }
        const auto visible = cull_track_region(track_.objects, frustum);
        if (!visible.ok() || !visible.policy_held) { error = "race visibility policy degraded"; return false; }
        stage(&RaceFrameProfile::cull_ms);
        TrackDrawList list;
        if (!build_track_draw_list(track_.scene, visible, {}, list, error)) return false;
        stage(&RaceFrameProfile::list_ms);
        RecordedCommands car_commands;
        if (!record_car_commands(car_, options, {}, skin_source_, car_commands, error)) return false;
        stage(&RaceFrameProfile::car_ms);
        if (list.entries.size() + car_commands.draws.size() > kTrackSliceDrawLimit) continue;
        RecordedCommands track_commands;
        if (!record_track_commands(track_.scene, list, options, {}, track_commands, error)) return false;
        stage(&RaceFrameProfile::track_ms);
        RaceGpuFrame result;
        result.car_draws = car_commands.draws.size(); result.track_draws = track_commands.draws.size();
        result.visible = visible.visible.size(); result.unplaced = list.cost.visible_unplaced;
        result.far_plane = far; result.triangles = list.cost.triangles;
        for (const auto& draw : car_commands.draws) result.triangles += draw.index_count/3;
        if (!combine_race_commands(std::move(track_commands), car_commands, result.commands, error)) return false;
        stage(&RaceFrameProfile::combine_ms);
        out = std::move(result); return true;
    }
    error = "complete track plus car list exceeds 2047 draws at minimum far plane"; return false;
}
} // namespace nfsu
