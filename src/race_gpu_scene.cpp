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
                        std::string& error, AssetCheckObserver* observer) {
    if (!load_track_slice(files, root, track, error, observer)) return false;
    StockFrameInputs inputs;
    if (!read_stock_frame_inputs(root, inputs)) {
        error = "stock 240SX inputs could not be read";
        for (const auto& file : inputs.files) if (!file.read) error += "\n" + file.path + ": " + file.error;
        return false;
    }
    StockFrame stock;
    if (!prepare_stock_frame(inputs, stock_240sx_request(), stock_240sx_skin(), stock, error)) return false;
    skin_source_ = stock.skin_binding.source;
    car_ = std::move(stock.scene); base_ = car_.instances;
    UploadTable table;
    if (!build_car_upload_table(car_, skin_source_, table, error)) return false;
    car_bytes_ = table.totals.vertex_bytes + table.totals.index_bytes + table.totals.texture_bytes +
        2 * base_.size() * sizeof(PreparedInstance);
    if (car_bytes_ + track.contacts.memory.resident_bytes > kPersistentBudgetPlaceholder) {
        error = "race car and collision exceed the persistent payload plan"; return false;
    }
    // RaceRun loads the route and collision course itself; discard the
    // diagnostic path and contact copy before allocating that course.
    track.contacts = {};
    std::vector<TrackResidencyPathPoint>().swap(track.path);
    // Inputs, parser geometry and duplicate decoded car textures die here.
    return true;
}
bool RaceGpuScene::frame(const RaceSession& session, RaceGpuFrame& out, std::string& error) {
    out = {}; error.clear();
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
        const auto visible = cull_track_region(track.objects, frustum);
        if (!visible.ok() || !visible.policy_held) { error = "race visibility policy degraded"; return false; }
        TrackDrawList list;
        if (!build_track_draw_list(track.scene, visible, {}, list, error)) return false;
        RecordedCommands car_commands;
        if (!record_car_commands(car_, options, {}, skin_source_, car_commands, error)) return false;
        if (list.entries.size() + car_commands.draws.size() > kTrackSliceDrawLimit) continue;
        RecordedCommands track_commands;
        if (!record_track_commands(track.scene, list, options, {}, track_commands, error)) return false;
        RaceGpuFrame result;
        result.car_draws = car_commands.draws.size(); result.track_draws = track_commands.draws.size();
        result.visible = visible.visible.size(); result.unplaced = list.cost.visible_unplaced;
        result.far_plane = far; result.triangles = list.cost.triangles;
        for (const auto& draw : car_commands.draws) result.triangles += draw.index_count/3;
        if (!combine_race_commands(std::move(track_commands), car_commands, result.commands, error)) return false;
        out = std::move(result); return true;
    }
    error = "complete track plus car list exceeds 2047 draws at minimum far plane"; return false;
}
} // namespace nfsu
