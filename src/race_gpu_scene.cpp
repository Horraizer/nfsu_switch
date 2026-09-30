#include "nfsu/race_gpu_scene.h"
#include "nfsu/draw_execution.h"
#include <algorithm>
#include <array>
#include <cstring>
#include <cmath>
#include <limits>
#include <map>
#include <stdexcept>

namespace nfsu {
namespace {
RenderOptions race_render_options(bool sky) {
    RenderOptions options;
    options.sky = sky;
    options.width = 1280; options.height = 720;
    return options;
}
void append_car_resources(UploadTable& track, const UploadTable& car) {
    for (auto resource : car.resources) {
        resource.name = "race-car/" + resource.name;
        track.resources.push_back(std::move(resource));
    }
    auto& a = track.totals; const auto& b = car.totals;
    a.resources += b.resources; a.vertex_buffers += b.vertex_buffers;
    a.index_buffers += b.index_buffers; a.textures += b.textures;
    a.generated_textures += b.generated_textures; a.vertex_bytes += b.vertex_bytes;
    a.index_bytes += b.index_bytes; a.texture_bytes += b.texture_bytes;
    a.generated_texture_bytes += b.generated_texture_bytes; a.empty_buffers += b.empty_buffers;
    a.largest_texture_bytes = std::max(a.largest_texture_bytes, b.largest_texture_bytes);
}
void rebase_car_draw(RecordedDraw& draw, std::size_t offset) {
    draw.name = "race-car/" + draw.name;
    draw.vertex_resource += offset; draw.index_resource += offset;
    if (draw.texture_resource != kNoHandle) draw.texture_resource += offset;
}
void append_car_commands(RecordedCommands& track, const RecordedCommands& car) {
    const auto offset = track.owning_uploads().resources.size();
    append_car_resources(track.owning_uploads(), car.owning_uploads());
    for (auto draw : car.owning_draws()) {
        rebase_car_draw(draw, offset);
        track.owning_draws().push_back(std::move(draw));
    }
}

const std::array<MeshVertex,4>& particle_vertices() {
    static const auto vertices = [] {
        std::array<MeshVertex,4> result{};
        constexpr float x[4] = {-0.5F,0.5F,0.5F,-0.5F};
        constexpr float y[4] = {0.5F,0.5F,-0.5F,-0.5F};
        constexpr float u[4] = {0,1,1,0};
        constexpr float v[4] = {0,0,1,1};
        for (std::size_t i=0; i<4; ++i) {
            result[i].px=x[i]; result[i].py=y[i];
            result[i].u=u[i]; result[i].v=v[i];
            result[i].color[0]=result[i].color[1]=result[i].color[2]=255;
            result[i].color[3]=255;
        }
        return result;
    }();
    return vertices;
}

const std::array<std::uint16_t,6>& particle_indices() {
    static const std::array<std::uint16_t,6> indices{0,1,2,0,2,3};
    return indices;
}

const std::array<std::uint8_t,1024>& particle_texture(RaceParticleKind kind) {
    static const auto smoke = [] {
        std::array<std::uint8_t,1024> result{};
        for (unsigned y=0; y<16; ++y) for (unsigned x=0; x<16; ++x) {
            const auto rgba=race_particle_texel(RaceParticleKind::smoke,x,y);
            std::copy(rgba.begin(),rgba.end(),result.begin()+(y*16+x)*4);
        }
        return result;
    }();
    static const auto spark = [] {
        std::array<std::uint8_t,1024> result{};
        for (unsigned y=0; y<16; ++y) for (unsigned x=0; x<16; ++x) {
            const auto rgba=race_particle_texel(RaceParticleKind::spark,x,y);
            std::copy(rgba.begin(),rgba.end(),result.begin()+(y*16+x)*4);
        }
        return result;
    }();
    return kind == RaceParticleKind::smoke ? smoke : spark;
}

bool append_particle_commands(RecordedCommands& commands, std::string& error) {
    auto& table=commands.owning_uploads();
    const auto first=table.resources.size();
    const auto& vertices=particle_vertices();
    const auto& indices=particle_indices();
    UploadResource vertex;
    vertex.kind=UploadResourceKind::vertex_buffer;
    vertex.name="particle/quad-vertices";
    vertex.data=reinterpret_cast<const unsigned char*>(vertices.data());
    vertex.bytes=sizeof vertices; vertex.alignment=kCarVertexAlignment;
    vertex.element_bytes=sizeof(MeshVertex); vertex.elements=vertices.size();
    table.resources.push_back(vertex);
    UploadResource index;
    index.kind=UploadResourceKind::index_buffer;
    index.name="particle/quad-indices";
    index.data=reinterpret_cast<const unsigned char*>(indices.data());
    index.bytes=sizeof indices; index.alignment=kIndexAlignment;
    index.element_bytes=sizeof(std::uint16_t); index.elements=indices.size();
    table.resources.push_back(index);
    for (unsigned k=0; k<2; ++k) {
        const auto kind=k ? RaceParticleKind::spark : RaceParticleKind::smoke;
        const auto& texels=particle_texture(kind);
        UploadResource texture;
        texture.kind=UploadResourceKind::texture;
        texture.lifetime=UploadLifetime::generated;
        texture.memory=UploadMemory::image;
        texture.name=k ? "particle/spark-disc" : "particle/smoke-disc";
        texture.data=texels.data(); texture.bytes=texels.size();
        texture.alignment=kTextureStagingAlignment;
        texture.element_bytes=4; texture.elements=256;
        if (!check_texture_upload_format(TextureUploadFormat::rgba8,16,16,
                                         texture.bytes,texture.texture,error)) return false;
        table.resources.push_back(texture);
    }
    auto& totals=table.totals;
    totals.resources+=4; totals.vertex_buffers+=1; totals.index_buffers+=1;
    totals.textures+=2; totals.generated_textures+=2;
    totals.vertex_bytes+=sizeof vertices; totals.index_bytes+=sizeof indices;
    totals.texture_bytes+=2048; totals.generated_texture_bytes+=2048;
    totals.largest_texture_bytes=std::max<std::uint64_t>(totals.largest_texture_bytes,1024);
    const shader_manifest::Program* program=nullptr;
    for (const auto& candidate : shader_manifest::kPrograms)
        if (std::strcmp(candidate.name,"car_textured")==0) program=&candidate;
    if (!program) { error="particle shader program missing"; return false; }
    for (std::size_t slot=0; slot<kRaceParticleLimit; ++slot) {
        const bool spark=slot>=kRaceSmokeLimit;
        RecordedDraw draw;
        draw.name="particle/"+std::string(spark?"spark/":"smoke/")+std::to_string(slot);
        draw.group=0x186000+slot;
        draw.vertex_resource=first; draw.index_resource=first+1;
        draw.texture_resource=first+(spark?3:2);
        draw.vertex_count=4; draw.index_count=6;
        draw.program=program;
        const char* variant=spark?"additive":"source_over";
        bool found=false;
        for (std::size_t i=0; i<program->variant_count; ++i)
            if (std::strcmp(program->variants[i].name,variant)==0) {
                draw.pipeline=program->variants[i]; found=true; break;
            }
        if (!found) { error="particle blend variant missing"; return false; }
        draw.pipeline.depth_test=shader_manifest::CompareOp::less_equal;
        draw.pipeline.depth_write=false;
        // Textured shaders use fallback.w == 2 for an unlit particle draw.
        draw.material.fallback[3]=2.0F;
        draw.material.alpha[0]=0;
        commands.owning_draws().push_back(std::move(draw));
    }
    return true;
}

void place_particle_commands(RecordedCommands& commands, std::size_t first,
                             const RaceParticles& particles, const CameraFrame& camera) {
    for (std::size_t slot=0; slot<kRaceParticleLimit; ++slot) {
        auto& draw=commands.owning_draws()[first+slot];
        std::fill(std::begin(draw.placement.rows),std::end(draw.placement.rows),0.0F);
        draw.placement.rows[15]=1;
        draw.material.alpha[0]=0;
        const bool spark=slot>=kRaceSmokeLimit;
        const auto offset=spark?slot-kRaceSmokeLimit:slot;
        if (offset >= (spark?particles.spark_count():particles.smoke_count())) continue;
        const auto& particle=spark?particles.spark(offset):particles.smoke(offset);
        VehicleVec3 at;
        double diameter=0, opacity=0;
        if (!race_particle_visual(particle,particles.step(),at,diameter,opacity)) continue;
        const Vec3 world{at.x,at.z,-at.y};
        const Vec3 delta{world.x-camera.eye.x,world.y-camera.eye.y,
                         world.z-camera.eye.z};
        const double depth=delta.x*camera.forward.x+delta.y*camera.forward.y+
                           delta.z*camera.forward.z;
        if (depth <= camera.near_plane || depth > camera.far_plane) continue;
        diameter=std::min(diameter,48.0*2.0*depth/(camera.focal_y*720.0));
        const Vec3 right=camera.right, up=camera.up;
        draw.placement.rows[0]=static_cast<float>(right.x*diameter);
        draw.placement.rows[1]=static_cast<float>(-right.z*diameter);
        draw.placement.rows[2]=static_cast<float>(right.y*diameter);
        draw.placement.rows[8]=static_cast<float>(up.x*diameter);
        draw.placement.rows[9]=static_cast<float>(-up.z*diameter);
        draw.placement.rows[10]=static_cast<float>(up.y*diameter);
        draw.placement.rows[12]=static_cast<float>(at.x);
        draw.placement.rows[13]=static_cast<float>(at.y);
        draw.placement.rows[14]=static_cast<float>(at.z);
        draw.material.alpha[0]=static_cast<float>(opacity);
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
    if (track.owning_draws().size() + car.owning_draws().size() > kTrackSliceDrawLimit ||
        track.owning_uploads().resources.size() + car.owning_uploads().resources.size() > kTrackSliceResourceLimit) {
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
    // Car source files and their decoded preparation are temporary. Complete
    // that work before streaming the track so neither source set overlaps the
    // other's allocation peak. Only the prepared car scene survives this scope.
    if (loading) loading->checkpoint("car input");
    {
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
    }
    if (loading) loading->checkpoint("car source released");
    if (!load_track_slice(files, root, track_, error, observer, loading)) return false;
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
    if (!prepare_templates(error)) return false;
    if (loading) loading->checkpoint("render templates prepared");
    if (!prepare_frame_cache(error)) return false;
    if (particles_enabled_)
        car_bytes_ += sizeof(particles_) + sizeof(particle_vertices()) +
                      sizeof(particle_indices()) + 2*particle_texture(RaceParticleKind::smoke).size();
    if (loading) loading->checkpoint("render prepared");
    if (cache_bytes() > 8*kMiB) { error = "race command cache exceeds 8 MiB"; return false; }
    ready_ = true;
    // Track parsing temporaries die here; car source copies died before it.
    return true;
}
std::uint64_t RaceGpuScene::cache_bytes() const {
    std::uint64_t bytes = sizeof(track_templates_) + template_of_group_.capacity()*sizeof(std::size_t) +
        track_templates_.owning_draws().capacity()*sizeof(RecordedDraw) +
        track_templates_.owning_uploads().resources.capacity()*sizeof(UploadResource);
    for (const auto& draw : track_templates_.owning_draws()) bytes += draw.name.capacity()+1;
    for (const auto& resource : track_templates_.owning_uploads().resources) bytes += resource.name.capacity()+1;
    bytes += culler_.memory_bytes() + sizeof(combined_uploads_) + sizeof(car_templates_) +
        combined_uploads_.resources.capacity()*sizeof(UploadResource) +
        car_templates_.owning_draws().capacity()*sizeof(RecordedDraw) +
        (mesh_first_.capacity()+section_first_.capacity())*sizeof(std::size_t) +
        mesh_groups_.capacity()*sizeof(std::uint32_t) + selection_.capacity()*sizeof(SelectedTrackDraw) +
        retail_groups_.capacity()*sizeof(RetailGroup) + bucket_first_.capacity()*sizeof(std::size_t) +
        track_placements_.capacity()*sizeof(DrawInstanceUniform);
    for (const auto& r : combined_uploads_.resources) bytes += r.name.capacity()+1;
    for (const auto& d : car_templates_.owning_draws()) bytes += d.name.capacity()+1;
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
    RenderOptions options = race_render_options(sky_);
    options.material_state = MaterialStateMode::retail;
    TrackDrawListOptions list_options;
    list_options.order = TrackDrawOrder::retail_bucket;
    if (!build_track_draw_list(track_.scene,witnesses,list_options,list,error) ||
        !record_track_commands(track_.scene,list,options,{},track_templates_,error) ||
        !validate_recorded_commands(track_templates_,error)) return false;
    sky_draws_ = 0;
    std::stable_partition(track_templates_.owning_draws().begin(), track_templates_.owning_draws().end(),
        [&](const RecordedDraw& d) { return d.mesh == track_.scene.sky_mesh; });
    for (const auto& draw : track_templates_.owning_draws())
        if (draw.mesh == track_.scene.sky_mesh) ++sky_draws_;
    template_of_group_.assign(track_.scene.region.draws.size(),kNoHandle);
    for (std::size_t i = 0; i < track_templates_.owning_draws().size(); ++i)
        template_of_group_[track_templates_.owning_draws()[i].group] = i;
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
    // Match build_track_draw_list's retail layer / first-visible-texture
    // buckets. Resolve keys once; only their first-use rank changes per view.
    std::map<std::pair<std::size_t, std::uint32_t>, std::size_t> buckets;
    retail_groups_.resize(scene.region.draws.size());
    for (std::size_t i = 0; i < scene.region.draws.size(); ++i) {
        const auto& draw = scene.region.draws[i];
        auto& group = retail_groups_[i];
        std::pair<std::size_t, std::uint32_t> key{kNoHandle, draw.texture_hash};
        if (draw.binding == BindingKind::resolved) {
            const auto& texture = scene.region.textures.at(draw.texture);
            group.layer = static_cast<unsigned char>(render_draw_layer(texture.layer_group, texture.layer_order));
            key = {draw.texture, 0};
        }
        group.bucket = buckets.emplace(key, buckets.size()).first->second;
    }
    bucket_first_.resize(buckets.size(), kNoHandle);
    section_first_.assign(track_.objects.sections.size(), kNoHandle);
    for (const auto& section : scene.sections) section_first_.at(section.section_index) = section.first_handle;
    // The sky's placement follows the scenery instances; see record_track_commands.
    track_placements_.resize(scene.instances.size() + (sky_draws_ ? 1 : 0));
    for (std::size_t n = 0; n < scene.instances.size(); ++n) {
        const auto& transform = scene.instances[n].transform;
        auto& placement = track_placements_[n];
        for (std::size_t i = 0; i < 3; ++i) {
            for (std::size_t j = 0; j < 3; ++j) placement.rows[i*4+j] = transform.linear[i*3+j];
            placement.rows[12+i] = transform.translation[i];
        }
    }
    if (sky_draws_) track_placements_.back() = track_templates_.owning_draws().front().placement;
    if (!record_car_commands(car_, race_render_options(sky_), {}, skin_source_, car_templates_, error)) return false;
    car_draw_count_=car_templates_.owning_draws().size();
    if (particles_enabled_ && !append_particle_commands(car_templates_,error)) return false;
    if (!validate_recorded_commands(car_templates_,error)) return false;
    const auto offset = track_templates_.owning_uploads().resources.size();
    if (offset + car_templates_.owning_uploads().resources.size() > kTrackSliceResourceLimit) {
        error = "prepared resource namespace exceeds capacity"; return false;
    }
    // Move the track table and rebase the car definitions in place. Copying
    // the entire track recording here needlessly duplicates every template.
    combined_uploads_ = std::move(track_templates_.owning_uploads());
    append_car_resources(combined_uploads_, car_templates_.owning_uploads());
    for (auto& draw : car_templates_.owning_draws()) rebase_car_draw(draw, offset);
    track_templates_.owning_uploads() = {}; car_templates_.owning_uploads() = {};
    selection_.reserve(kTrackSliceDrawLimit);
    return true;
}

RecordedDrawView RaceGpuScene::selected_draw(std::size_t i) const {
    if (i >= sky_position_ && i < sky_position_ + sky_draws_) {
        const auto& d = track_templates_.owning_draws().at(i - sky_position_);
        return {d, d.instance, track_placements_.at(d.instance), d.material, true};
    }
    if (i >= sky_position_ + sky_draws_) i -= sky_draws_;
    if (i < selection_.size()) {
        const auto& selected = selection_[i];
        const auto& draw = track_templates_.owning_draws().at(template_of_group_.at(selected.draw));
        return {draw, selected.instance, track_placements_.at(selected.instance), draw.material, true};
    }
    const auto& draw = car_templates_.owning_draws().at(i-selection_.size());
    return {draw, draw.instance, draw.placement, draw.material, false};
}

bool RaceGpuScene::select_track(const TrackCullReport& visible, RaceGpuFrame& out,
                               std::size_t& entries, std::string& error) {
    selection_.clear(); entries = 0; out.unplaced = 0; out.triangles = 0;
    std::fill(bucket_first_.begin(), bucket_first_.end(), kNoHandle);
    std::size_t next_bucket = 0;
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
        if (sky_draws_ + entries + car_templates_.owning_draws().size() > kTrackSliceDrawLimit) return true;
        for (auto k = first; k < last; ++k) {
            const auto group = mesh_groups_[k];
            if (group >= template_of_group_.size()) { error = "prepared group is out of range"; return false; }
            auto& first_use = bucket_first_.at(retail_groups_.at(group).bucket);
            // Empty groups influence first use in the reference draw list,
            // even though record_track_commands emits no draw for them.
            if (first_use == kNoHandle) first_use = next_bucket++;
            const auto index = template_of_group_[group];
            if (index == kNoHandle) {
                if (!scene.region.draws[group].index_count) continue;
                error = "prepared group has no validated template"; return false;
            }
            if (index >= track_templates_.owning_draws().size() || selection_.size() >= kTrackSliceDrawLimit) {
                error = "prepared draw capacity exceeded"; return false;
            }
            selection_.push_back({static_cast<std::uint32_t>(handle), group, selection_.size()});
            out.triangles += track_templates_.owning_draws()[index].index_count/3;
        }
    }
    // The ordinal makes equal bucket keys stable without stable_sort's
    // temporary allocation. Selection capacity is reserved once at load.
    std::sort(selection_.begin(), selection_.end(), [this](const SelectedTrackDraw& a, const SelectedTrackDraw& b) {
        const auto& ga = retail_groups_.at(a.draw);
        const auto& gb = retail_groups_.at(b.draw);
        if (ga.layer != gb.layer) return ga.layer < gb.layer;
        const auto fa = bucket_first_.at(ga.bucket), fb = bucket_first_.at(gb.bucket);
        if (fa != fb) return fa < fb;
        return a.ordinal < b.ordinal;
    });
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
        RenderOptions options = race_render_options(sky_);
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
            sky_position_ = selection_.size();
            for (std::size_t i = 0; i < selection_.size(); ++i) {
                const auto& d = track_templates_.owning_draws()[template_of_group_[selection_[i].draw]];
                if (d.pipeline.blend) { sky_position_ = i; break; }
            }
            stage(&RaceFrameProfile::list_ms);
            if (sky_draws_ + entries + car_templates_.owning_draws().size() > kTrackSliceDrawLimit) continue;
            for (auto& draw : car_templates_.owning_draws()) {
                if (draw.instance != kNoHandle) {
                    if (draw.instance >= car_.instances.size()) { error = "prepared car instance is out of range"; return false; }
                    const auto& instance = car_.instances[draw.instance];
                    std::copy(std::begin(instance.transform),std::end(instance.transform),draw.placement.rows);
                    draw.material.alpha[3] = static_cast<float>(instance.lamp_mode);
                }
            }
            if (particles_enabled_)
                place_particle_commands(car_templates_,car_draw_count_,particles_,basis);
            if (!record_frame_state(options,{},out.commands,error)) return false;
            stage(&RaceFrameProfile::car_ms);
            out.track_draws = sky_draws_ + selection_.size(); out.car_draws = car_draw_count_;
            out.particle_draws = particles_enabled_ ? kRaceParticleLimit : 0;
            out.visible = culler_.result().visible.size(); out.far_plane = far;
            stage(&RaceFrameProfile::track_ms);
            for (std::size_t i = 0; i < sky_draws_; ++i)
                out.triangles += track_templates_.owning_draws()[i].index_count/3;
            if (out.triangles > RenderLimits{}.max_submitted_triangles) {
                error = "prepared submitted triangle capacity exceeded"; return false;
            }
            for (const auto& draw : car_templates_.owning_draws()) out.triangles += draw.index_count/3;
            out.commands.source_ = this; out.commands.generation_ = generation_;
            stage(&RaceFrameProfile::combine_ms);
            return true;
        }
        const auto visible = cull_track_region(track_.objects, frustum);
        if (!visible.ok() || !visible.policy_held) { error = "race visibility policy degraded"; return false; }
        stage(&RaceFrameProfile::cull_ms);
        TrackDrawList list;
        TrackDrawListOptions list_options;
        list_options.order = TrackDrawOrder::retail_bucket;
        if (!build_track_draw_list(track_.scene, visible, list_options, list, error)) return false;
        stage(&RaceFrameProfile::list_ms);
        RecordedCommands car_commands;
        if (!record_car_commands(car_, options, {}, skin_source_, car_commands, error)) return false;
        if (particles_enabled_) {
            if (!append_particle_commands(car_commands,error)) return false;
            place_particle_commands(car_commands,car_draw_count_,particles_,basis);
        }
        stage(&RaceFrameProfile::car_ms);
        if (sky_draws_ + list.entries.size() + car_commands.owning_draws().size() > kTrackSliceDrawLimit) continue;
        RecordedCommands track_commands;
        options.material_state = MaterialStateMode::retail;
        if (!record_track_commands(track_.scene, list, options, {}, track_commands, error)) return false;
        stage(&RaceFrameProfile::track_ms);
        RaceGpuFrame result;
        result.car_draws = car_draw_count_; result.track_draws = track_commands.owning_draws().size();
        result.particle_draws = particles_enabled_ ? kRaceParticleLimit : 0;
        result.visible = visible.visible.size(); result.unplaced = list.cost.visible_unplaced;
        result.far_plane = far; result.triangles = 0;
        for (const auto& draw : track_commands.owning_draws()) result.triangles += draw.index_count/3;
        for (const auto& draw : car_commands.owning_draws()) result.triangles += draw.index_count/3;
        if (!combine_race_commands(std::move(track_commands), car_commands, result.commands, error)) return false;
        stage(&RaceFrameProfile::combine_ms);
        out = std::move(result); return true;
    }
    error = "complete track plus car list exceeds 2047 draws at minimum far plane"; return false;
}
} // namespace nfsu
