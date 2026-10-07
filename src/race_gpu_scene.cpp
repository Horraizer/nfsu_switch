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
    options.material_state = MaterialStateMode::retail;
    return options;
}
void append_car_resources(UploadTable& track, const UploadTable& car,
                          const char* prefix = "race-car/") {
    for (auto resource : car.resources) {
        resource.name = prefix + resource.name;
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
void rebase_car_draw(RecordedDraw& draw, std::size_t offset,
                     const char* prefix = "race-car/") {
    draw.name = prefix + draw.name;
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
void append_shadow_commands(RecordedCommands& track, const RecordedCommands& shadow) {
    const auto offset = track.owning_uploads().resources.size();
    append_car_resources(track.owning_uploads(), shadow.owning_uploads(), "race-shadow/");
    for (auto draw : shadow.owning_draws()) {
        rebase_car_draw(draw, offset, "race-shadow/");
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

void append_glow_resources(UploadTable& into, const UploadTable& prepared,
                           std::size_t first) {
    auto& totals = into.totals;
    for (std::size_t i = first; i < prepared.resources.size(); ++i) {
        const auto& resource = prepared.resources[i];
        into.resources.push_back(resource);
        ++totals.resources;
        if (resource.kind == UploadResourceKind::vertex_buffer) {
            ++totals.vertex_buffers; totals.vertex_bytes += resource.bytes;
        } else if (resource.kind == UploadResourceKind::index_buffer) {
            ++totals.index_buffers; totals.index_bytes += resource.bytes;
        } else if (resource.kind == UploadResourceKind::texture) {
            ++totals.textures; totals.texture_bytes += resource.bytes;
            totals.largest_texture_bytes = std::max(totals.largest_texture_bytes,
                                                    resource.bytes);
        }
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
                        TrackSliceLoadObserver* loading, bool tuned, bool glows,
                        bool car_shading, const CarAssets* car, const RaceCarDress* dress) {
    if (ready_) { error = "race scene is already loaded"; return false; }
    // The misuse refusals run before any file is read, so a caller that
    // cannot reach retail data still sees the sentence.
    if (tuned && dress != nullptr) {
        error = "the tuned preset and an explicit dress are exclusive; pass one";
        return false;
    }
    if (dress != nullptr && (dress->request == nullptr || dress->skin == nullptr)) {
        error = "a dress needs both a request and a skin";
        return false;
    }
    if (dress != nullptr && dress->request->car_type_name.empty()) {
        error = "a dress request needs the car type it was authored for";
        return false;
    }
    tuned_ = tuned;
    dressed_ = dress != nullptr;
    glows_enabled_ = glows;
    car_shading_=car_shading;
    CarAssets selection;
    if (car != nullptr) {
        selection = *car;
    } else if (!load_car_assets(root / "Global/GLOBALB.BUN",
                                dress != nullptr ? dress->request->car_type_name : "240SX",
                                selection, error)) {
        return false;
    }
    if (tuned && selection.car_type_name != "240SX") {
        error = "the tuned preset is the 240SX's own part set; it cannot dress car type " +
                selection.car_type_name;
        return false;
    }
    if (dress != nullptr && dress->request->car_type_name != selection.car_type_name) {
        error = "the dress was authored for car type " + dress->request->car_type_name +
                " and cannot dress car type " + selection.car_type_name;
        return false;
    }
    // Car source files and their decoded preparation are temporary. Complete
    // that work before streaming the track so neither source set overlaps the
    // other's allocation peak. Only the prepared car scene survives this scope.
    if (loading) loading->checkpoint("car input");
    {
        StockFrameInputs inputs;
        // A false return covers both a refused selection and a pack that was
        // not copied; either way the message names the cause (review F1 of
        // issue #315's first round). A dress reads the tuned extras as well:
        // its authored wheels and vinyls are not optional inputs.
        if (!read_car_frame_inputs(root, selection, inputs, tuned || dressed_, error)) {
            error = car_frame_inputs_error(inputs, error, root);
            return false;
        }
        // A dress can install parts whose solids live only in the shared
        // EXHAUST family file (the FE_SKYLINE ROCKET exhaust); the family
        // pair is the dress's own input, refused by name when absent.
        if (dress != nullptr && !read_exhaust_family_inputs(root, inputs)) {
            const StockFrameInput& missing = inputs.exhaust_geometry.read
                ? inputs.exhaust_textures : inputs.exhaust_geometry;
            error = "the dress needs the shared exhaust family: " + missing.path + ": " +
                    (missing.error.empty() ? std::string("not read") : missing.error);
            return false;
        }
        if (loading) loading->checkpoint("car preparation");
        StockFrame stock;
        if (dress != nullptr) {
            if (!prepare_race_car_frame(inputs, root, *dress->request, *dress->skin,
                                        stock, error, car_shading_)) return false;
            if (dress->bind != nullptr &&
                !dress->bind(stock, inputs, *dress->request, error, dress->user)) return false;
        } else if (!prepare_race_car_frame(inputs, root,
                tuned ? tuned_240sx_request() : stock_car_request(selection.car_type_name),
                tuned ? tuned_240sx_skin() : stock_car_skin(selection.car_type_name),
                stock, error, car_shading_)) return false;
        if (!prepare_car_shadow(selection.car_type_name, inputs, stock.car_pack,
                                shadow_asset_, error)) return false;
        if (glows_enabled_) glow_anchors_ = collect_car_glow_anchors(stock);
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
    root_ = root;
    if (wet_road_reflections_) {
        if (!load_wet_road_lamps(root, track_.contacts, lamps_, error)) return false;
        lamps_loaded_ = true;
    }
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
    if (glows_enabled_ && !prepare_glows(error)) return false;
    if (loading) loading->checkpoint("render prepared");
    if (cache_bytes() > 8*kMiB) { error = "race command cache exceeds 8 MiB"; return false; }
    if (lamps_loaded_) {
        wet_uploads_ = combined_uploads_;
        if (!append_wet_lamp_uploads(lamps_, wet_uploads_, error) || !prepare_reflections(error)) return false;
    }
    if (cache_bytes() > 8*kMiB) { error = "wet race command cache exceeds 8 MiB"; return false; }
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
        shadow_templates_.owning_draws().capacity()*sizeof(RecordedDraw) +
        sizeof(shadow_asset_) + shadow_asset_.texture.rgba.capacity() +
        (mesh_first_.capacity()+section_first_.capacity())*sizeof(std::size_t) +
        mesh_groups_.capacity()*sizeof(std::uint32_t) + selection_.capacity()*sizeof(SelectedTrackDraw) +
        retail_groups_.capacity()*sizeof(RetailGroup) + bucket_first_.capacity()*sizeof(std::size_t) +
        track_placements_.capacity()*sizeof(DrawInstanceUniform);
    bytes += wet_uploads_.resources.capacity()*sizeof(UploadResource);
    for (const auto& r : wet_uploads_.resources) bytes += r.name.capacity()+1;
    bytes += lamps_.lights.capacity()*sizeof(WetRoadLamp) + lamps_.texture.rgba.capacity();
    bytes += (reflection_templates_.capacity()+lamp_draws_.capacity())*sizeof(RecordedDraw);
    bytes += (reflection_first_.capacity()+reflection_selection_.capacity())*sizeof(std::size_t);
    bytes += reflection_entries_.capacity()*sizeof(TrackDrawListEntry);
    bytes += reflection_cache_.bounds.capacity()*sizeof(std::array<float,6>);
    bytes += reflection_cache_.groups.capacity()*sizeof(std::vector<std::uint32_t>);
    for (const auto& g : reflection_cache_.groups) bytes += g.capacity()*sizeof(std::uint32_t);
    for (const auto& d : reflection_templates_) bytes += d.name.capacity()+1;
    for (const auto& d : lamp_draws_) bytes += d.name.capacity()+1;
    for (const auto& r : combined_uploads_.resources) bytes += r.name.capacity()+1;
    for (const auto& d : car_templates_.owning_draws()) bytes += d.name.capacity()+1;
    for (const auto& d : shadow_templates_.owning_draws()) bytes += d.name.capacity()+1;
    if (glows_enabled_) bytes += sizeof(glow_templates_) + sizeof(glow_selected_) +
        sizeof(glow_vertices_) + sizeof(glow_indices_) +
        glow_head_.rgba.capacity() + glow_tail_.rgba.capacity();
    return bytes;
}
bool RaceGpuScene::prepare_reflections(std::string& error) {
    auto options = race_render_options(sky_); options.material_state = MaterialStateMode::retail;
    prepare_track_reflection_cache(track_.scene, reflection_cache_);
    if (!record_track_reflections(track_.scene, options, combined_uploads_, reflection_templates_, error, false)) return false;
    reflection_first_.assign(track_.scene.instances.size(), kNoHandle);
    for (std::size_t i=0;i<reflection_templates_.size();++i) {
        const auto n=reflection_templates_[i].instance;
        if (reflection_first_[n]==kNoHandle) reflection_first_[n]=i;
    }
    reflection_selection_.reserve(4096); reflection_entries_.reserve(4096); lamp_draws_.reserve(lamps_.lights.size());
    return true;
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
    options.texture_filter = texture_filter_;
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
    auto car_options=race_render_options(sky_);
    car_options.texture_filter=texture_filter_;
    car_options.car_shading=car_shading_;
    if (car_shading_) car_options.material_state=MaterialStateMode::retail;
    if (!record_car_commands(car_, car_options, {}, skin_source_, car_templates_, error)) return false;
    car_draw_count_=car_templates_.owning_draws().size();
    if (particles_enabled_ && !append_particle_commands(car_templates_,error)) return false;
    if (!validate_recorded_commands(car_templates_,error)) return false;
    if (!record_car_shadow_commands(shadow_asset_, {}, race_render_options(sky_), shadow_templates_, error)) return false;
    if (tuned_ || dressed_) for (const auto& draw : car_templates_.owning_draws()) {
        if (draw.index_count && draw.texture_resource == kNoHandle) {
            error = tuned_ ? "tuned race car has fallback triangles: " + draw.name
                           : "dressed race car has fallback triangles: " + draw.name;
            return false;
        }
    }
    const auto offset = track_templates_.owning_uploads().resources.size();
    if (offset + shadow_templates_.owning_uploads().resources.size() +
        car_templates_.owning_uploads().resources.size() > kTrackSliceResourceLimit) {
        error = "prepared resource namespace exceeds capacity"; return false;
    }
    // Move the track table and rebase the car definitions in place. Copying
    // the entire track recording here needlessly duplicates every template.
    combined_uploads_ = std::move(track_templates_.owning_uploads());
    append_car_resources(combined_uploads_, shadow_templates_.owning_uploads(), "race-shadow/");
    for (auto& draw : shadow_templates_.owning_draws()) rebase_car_draw(draw, offset, "race-shadow/");
    const auto car_offset = combined_uploads_.resources.size();
    append_car_resources(combined_uploads_, car_templates_.owning_uploads());
    for (auto& draw : car_templates_.owning_draws()) rebase_car_draw(draw, car_offset);
    track_templates_.owning_uploads() = {}; shadow_templates_.owning_uploads() = {}; car_templates_.owning_uploads() = {};
    selection_.reserve(kTrackSliceDrawLimit);
    return true;
}

bool RaceGpuScene::prepare_glows(std::string& error) {
    namespace sm = shader_manifest;
    glow_head_ = make_car_glow_texture(true);
    glow_tail_ = make_car_glow_texture(false);
    for (std::size_t i = 0; i < glow_vertices_.size(); ++i) {
        auto& v = glow_vertices_[i];
        v.px = (i == 0 || i == 3) ? -1.0F : 1.0F;
        v.py = i < 2 ? -1.0F : 1.0F;
        v.u = v.px < 0 ? 0.0F : 1.0F;
        v.v = v.py < 0 ? 0.0F : 1.0F;
        std::fill(std::begin(v.color), std::end(v.color), 255);
    }
    auto& table = combined_uploads_;
    glow_first_resource_ = table.resources.size();
    if (glow_first_resource_ + 4 > kTrackSliceResourceLimit) {
        error = "glow resources exceed race resource capacity"; return false;
    }
    const auto buffer = [&](UploadResourceKind kind, const char* name,
                            const unsigned char* data, std::size_t bytes,
                            std::size_t element_bytes, std::size_t elements) {
        UploadResource resource;
        resource.kind = kind; resource.name = name; resource.data = data;
        resource.bytes = bytes; resource.element_bytes = element_bytes;
        resource.elements = elements; resource.alignment = kind == UploadResourceKind::vertex_buffer ?
            kCarVertexAlignment : kIndexAlignment;
        table.resources.push_back(std::move(resource));
        ++table.totals.resources;
        if (kind == UploadResourceKind::vertex_buffer) {
            ++table.totals.vertex_buffers; table.totals.vertex_bytes += bytes;
        } else {
            ++table.totals.index_buffers; table.totals.index_bytes += bytes;
        }
    };
    buffer(UploadResourceKind::vertex_buffer, "race-glow/quad",
           reinterpret_cast<const unsigned char*>(glow_vertices_.data()),
           sizeof(glow_vertices_), sizeof(MeshVertex), glow_vertices_.size());
    buffer(UploadResourceKind::index_buffer, "race-glow/indices",
           reinterpret_cast<const unsigned char*>(glow_indices_.data()),
           sizeof(glow_indices_), sizeof(std::uint16_t), glow_indices_.size());
    const auto texture = [&](const RenderTexture& source, const char* name) -> bool {
        TextureUploadShape shape;
        if (!check_texture_upload_format(TextureUploadFormat::rgba8, source.width,
                                         source.height, source.rgba.size(), shape, error)) return false;
        UploadResource resource;
        resource.kind = UploadResourceKind::texture; resource.memory = UploadMemory::image;
        resource.name = name; resource.data = source.rgba.data();
        resource.bytes = shape.bytes; resource.alignment = kTextureStagingAlignment;
        resource.element_bytes = shape.texel_bytes;
        resource.elements = shape.bytes/shape.texel_bytes;
        resource.texture_hash = source.hash; resource.texture = shape;
        table.resources.push_back(std::move(resource));
        ++table.totals.resources; ++table.totals.textures;
        table.totals.texture_bytes += shape.bytes;
        table.totals.largest_texture_bytes =
            std::max(table.totals.largest_texture_bytes, shape.bytes);
        return true;
    };
    if (!texture(glow_head_, "race-glow/head") ||
        !texture(glow_tail_, "race-glow/tail")) return false;
    const sm::Program* program = nullptr;
    for (const auto& candidate : sm::kPrograms)
        if (std::strcmp(candidate.name, "car_textured") == 0) program = &candidate;
    if (!program) { error = "glow shader program is absent"; return false; }
    for (std::size_t i = 0; i < glow_templates_.size(); ++i) {
        auto& draw = glow_templates_[i];
        draw.name = i == 0 ? "race-glow/head" : "race-glow/tail";
        draw.group = track_.scene.region.draws.size() + i;
        draw.vertex_resource = glow_first_resource_;
        draw.index_resource = glow_first_resource_ + 1;
        draw.texture_resource = glow_first_resource_ + 2 + i;
        draw.vertex_count = 4; draw.index_count = 6;
        draw.program = program;
        bool found = false;
        for (std::size_t v = 0; v < program->variant_count; ++v)
            if (std::strcmp(program->variants[v].name, "additive") == 0) {
                draw.pipeline = program->variants[v]; found = true; break;
            }
        if (!found) { error = "glow additive variant is absent"; return false; }
        draw.pipeline.depth_write = false;
    }
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
    if (i == selection_.size()) {
        const auto& draw = shadow_templates_.owning_draws().at(0);
        return {draw, draw.instance, draw.placement, draw.material, false};
    }
    i -= selection_.size() + 1;
    if (i >= car_templates_.owning_draws().size()) {
        i -= car_templates_.owning_draws().size();
        if (i >= glow_count_) throw std::out_of_range("selected glow draw is out of range");
        const auto& glow = glow_selected_[i];
        return {glow, kNoHandle, glow.placement, glow.material, false};
    }
    const auto& draw = car_templates_.owning_draws().at(i);
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
        if (sky_draws_ + entries + 1 + car_templates_.owning_draws().size() +
            glow_count_ > kTrackSliceDrawLimit) return true;
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
    if (wet_road_reflections_ && !lamps_loaded_) {
        if (!load_wet_road_lamps(root_, session.course().world, lamps_, error)) return false;
        wet_uploads_ = combined_uploads_;
        if (!append_wet_lamp_uploads(lamps_, wet_uploads_, error)) return false;
        if (!prepare_reflections(error)) return false;
        if (cache_bytes() > 8*kMiB) { error = "wet race command cache exceeds 8 MiB"; return false; }
        lamps_loaded_ = true;
    }
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
    const auto shadow = place_car_shadow(session);
    for (double far = 250; far >= 31.25; far *= 0.5) {
        RenderOptions options = race_render_options(sky_);
        options.texture_filter = texture_filter_;
        options.car_shading=car_shading_;
        if (car_shading_) options.material_state=MaterialStateMode::retail;
        options.wet_road_reflections = wet_road_reflections_;
        options.wet_lamps = wet_road_reflections_ ? &lamps_ : nullptr;
        if (!drive_render_camera(session.camera(), ChaseFovMode::vertical_4_3,
                                 options.width, options.height, options.camera, error)) return false;
        options.camera.far_plane = far;
        CameraFrame basis;
        if (!make_camera_frame(options.camera, options.width, options.height, basis, error)) return false;
        glow_count_ = 0;
        if (glows_enabled_) {
            const auto sprites = place_car_glows(glow_anchors_, car_, basis,
                                                 session.animation().braking);
            // Keep the package draw count invariant even when the view-facing
            // gate fades a sprite out. Zero-radius slots rasterize no pixels.
            for (std::size_t i = 0; i < sprites.size(); ++i) {
                const auto& sprite = sprites[i];
                auto& draw = glow_selected_[i];
                draw = glow_templates_[sprite.head ? 0 : 1];
                draw.material.alpha[0] = static_cast<float>(sprite.opacity);
                car_glow_transform(sprite, basis, draw.placement.rows);
                ++glow_count_;
            }
        }
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
            if (sky_draws_ + entries + 1 + car_templates_.owning_draws().size() + glow_count_ > kTrackSliceDrawLimit) continue;
            auto& shadow_draw = shadow_templates_.owning_draws()[0];
            shadow_draw.placement = shadow.transform;
            shadow_draw.material.alpha[0] = shadow.alpha;
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
            out.glow_draws = glow_count_;
            out.visible = culler_.result().visible.size(); out.far_plane = far;
            stage(&RaceFrameProfile::track_ms);
            for (std::size_t i = 0; i < sky_draws_; ++i)
                out.triangles += track_templates_.owning_draws()[i].index_count/3;
            if (out.triangles > RenderLimits{}.max_submitted_triangles) {
                error = "prepared submitted triangle capacity exceeded"; return false;
            }
            for (const auto& draw : car_templates_.owning_draws()) out.triangles += draw.index_count/3;
            out.triangles += shadow_asset_.triangles.size();
            out.triangles += 2*glow_count_;
            reflection_selection_.clear(); lamp_draws_.clear();
            if (wet_road_reflections_) {
                options.material_state = MaterialStateMode::retail;
                if (!track_reflection_entries(track_.scene, options, reflection_entries_, error, &reflection_cache_)) return false;
                for (const auto& entry : reflection_entries_) {
                    const auto first = reflection_first_.at(entry.instance);
                    if (first == kNoHandle) { error = "reflection template missing"; return false; }
                    auto index = first;
                    while (index < reflection_templates_.size() && reflection_templates_[index].instance == entry.instance &&
                           reflection_templates_[index].group != entry.draw) ++index;
                    if (index == reflection_templates_.size() || reflection_templates_[index].instance != entry.instance) {
                        error = "reflection group missing"; return false;
                    }
                    reflection_selection_.push_back(index);
                }
                if (!record_lamp_reflections(options, wet_uploads_, lamp_draws_, error)) return false;
                if (reflection_selection_.size()+lamp_draws_.size() > 4096) { error = "reflection capacity exceeded"; return false; }
            }
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
        RecordedCommands shadow_commands;
        if (!record_car_shadow_commands(shadow_asset_, shadow, options, shadow_commands, error)) return false;
        stage(&RaceFrameProfile::car_ms);
        if (sky_draws_ + list.entries.size() + 1 + car_commands.owning_draws().size() + glow_count_ > kTrackSliceDrawLimit) continue;
        RecordedCommands track_commands;
        options.material_state = MaterialStateMode::retail;
        // Keep the reference resource namespace in the same order as the
        // prepared path: track, shadow, car, glows, then wet lamp uploads.
        auto track_options = options;
        track_options.wet_road_reflections = false;
        track_options.wet_lamps = nullptr;
        if (!record_track_commands(track_.scene, list, track_options, {}, track_commands, error)) return false;
        if (wet_road_reflections_) track_commands.frame.options[2] |= 2U;
        stage(&RaceFrameProfile::track_ms);
        RaceGpuFrame result;
        result.car_draws = car_draw_count_; result.track_draws = track_commands.owning_draws().size();
        result.particle_draws = particles_enabled_ ? kRaceParticleLimit : 0;
        result.glow_draws = glow_count_;
        result.visible = visible.visible.size(); result.unplaced = list.cost.visible_unplaced;
        result.far_plane = far; result.triangles = 0;
        for (const auto& draw : track_commands.owning_draws()) result.triangles += draw.index_count/3;
        for (const auto& draw : car_commands.owning_draws()) result.triangles += draw.index_count/3;
        result.triangles += shadow_asset_.triangles.size();
        append_shadow_commands(track_commands, shadow_commands);
        if (!combine_race_commands(std::move(track_commands), car_commands, result.commands, error)) return false;
        if (glows_enabled_) {
            auto& uploads = result.commands.owning_uploads();
            if (uploads.resources.size() != glow_first_resource_) {
                error = "reference glow resource offset differs"; return false;
            }
            append_glow_resources(uploads, combined_uploads_, glow_first_resource_);
            const auto reference_sprites = place_car_glows(glow_anchors_, car_, basis,
                                                            session.animation().braking);
            for (std::size_t i = 0; i < reference_sprites.size(); ++i) {
                RecordedDraw draw = glow_templates_[reference_sprites[i].head ? 0 : 1];
                draw.material.alpha[0] = static_cast<float>(reference_sprites[i].opacity);
                car_glow_transform(reference_sprites[i], basis, draw.placement.rows);
                result.commands.owning_draws().push_back(std::move(draw));
            }
            result.triangles += 2*glow_count_;
            DrawExecutionPlan plan;
            if (!validate_recorded_commands(result.commands,error) ||
                !plan_draw_execution(result.commands,plan,error)) return false;
        }
        if (wet_road_reflections_) {
            if (!append_wet_lamp_uploads(lamps_, result.commands.owning_uploads(), error) ||
                !record_track_reflections(track_.scene, options,
                                          result.commands.owning_uploads(),
                                          result.commands.reflections, error)) return false;
        }
        stage(&RaceFrameProfile::combine_ms);
        out = std::move(result); return true;
    }
    error = "complete track plus car list exceeds 2047 draws at minimum far plane"; return false;
}
} // namespace nfsu
