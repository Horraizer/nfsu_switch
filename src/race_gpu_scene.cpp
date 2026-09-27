#include "nfsu/race_gpu_scene.h"
#include "nfsu/draw_execution.h"
#include <algorithm>
#include <cstring>
#include <cmath>

namespace nfsu {
bool combine_race_commands(RecordedCommands track, const RecordedCommands& car,
                           RecordedCommands& out, std::string& error) {
    out = {}; error.clear();
    if (!validate_recorded_commands(track, error) || !validate_recorded_commands(car, error)) return false;
    if (track.width != car.width || track.height != car.height ||
        std::memcmp(&track.frame, &car.frame, sizeof track.frame) != 0) {
        error = "track/car frame camera differs"; return false;
    }
    if (track.draws.size() + car.draws.size() > kTrackSliceDrawLimit ||
        track.uploads.resources.size() + car.uploads.resources.size() > kTrackSliceResourceLimit) {
        error = "combined race exceeds draw/resource capacity"; return false;
    }
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
    if (!prepare_templates(error)) return false;
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
    RenderOptions options; options.width = 1280; options.height = 720;
    if (!build_track_draw_list(track_.scene,witnesses,{},list,error) ||
        !record_track_commands(track_.scene,list,options,{},track_templates_,error) ||
        !validate_recorded_commands(track_templates_,error)) return false;
    template_of_group_.assign(track_.scene.region.draws.size(),kNoHandle);
    for (std::size_t i = 0; i < track_templates_.draws.size(); ++i)
        template_of_group_[track_templates_.draws[i].group] = i;
    return true;
}
bool RaceGpuScene::record_cached_track(const TrackDrawList& list, const RecordedCommands& car,
                                       RecordedCommands& out, std::string& error) const {
    out = {};
    out.frame = car.frame; out.width = car.width; out.height = car.height;
    std::copy(std::begin(car.clear_color),std::end(car.clear_color),out.clear_color);
    out.uploads = track_templates_.uploads;
    out.draws.reserve(list.entries.size());
    // build_track_draw_list already checked every (instance,group) pair and
    // the list order/cost. Templates contain only fully validated geometry.
    for (const auto& entry : list.entries) {
        if (entry.draw >= template_of_group_.size() || entry.instance >= track_.scene.instances.size()) {
            error = "cached track selection is out of range"; return false;
        }
        const auto index = template_of_group_[entry.draw];
        if (index == kNoHandle) {
            if (!track_.scene.region.draws[entry.draw].index_count) continue;
            error = "cached track selection has no template"; return false;
        }
        auto draw = track_templates_.draws[index];
        draw.instance = entry.instance;
        draw.name = "track group " + std::to_string(entry.draw) + " copy " + std::to_string(entry.instance);
        const auto& transform = track_.scene.instances[entry.instance].transform;
        for (std::size_t i = 0; i < 3; ++i) {
            for (std::size_t j = 0; j < 3; ++j) draw.placement.rows[i*4+j] = transform.linear[i*3+j];
            draw.placement.rows[12+i] = transform.translation[i];
        }
        out.draws.push_back(std::move(draw));
    }
    return true;
}
bool RaceGpuScene::frame(const RaceSession& session, RaceGpuFrame& out, std::string& error, RaceFrameProfile* profile, bool reference, TimingService* timer) {
    out = {}; error.clear();
    if (!ready_) { error = "race scene is not loaded"; return false; }
    if (profile) *profile = {};
    if (profile && !timer) { error = "race profiling requires a timing service"; return false; }
    auto start = profile ? timer->now_us() : 0;
    auto stage = [&](double RaceFrameProfile::* field) {
        if (!profile) return;
        const auto end = timer->now_us();
        profile->*field += (end-start)/1000.0;
        start = end;
    };
    if (!drive_place_instances(base_, session.car(), session.spec(), car_.instances)) {
        error = "race car placement failed"; return false;
    }
    for (double far = 250; far >= 31.25; far *= 0.5) {
        RenderOptions options; options.width = 1280; options.height = 720;
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
        if (reference) {
            if (!record_track_commands(track_.scene, list, options, {}, track_commands, error)) return false;
        } else if (!record_cached_track(list, car_commands, track_commands, error)) return false;
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
