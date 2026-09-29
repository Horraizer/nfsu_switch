#include "nfsu/car_shadow.h"
#include "nfsu/texture_decode.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace nfsu {
namespace {
constexpr std::uint32_t kShadowHash = 0xF36596F5U; // 240SX_SHADOWIG

VehicleVec3 add(VehicleVec3 a, VehicleVec3 b) { return {a.x+b.x,a.y+b.y,a.z+b.z}; }
VehicleVec3 sub(VehicleVec3 a, VehicleVec3 b) { return {a.x-b.x,a.y-b.y,a.z-b.z}; }
VehicleVec3 mul(VehicleVec3 a, double s) { return {a.x*s,a.y*s,a.z*s}; }
double dot(VehicleVec3 a, VehicleVec3 b) { return a.x*b.x+a.y*b.y+a.z*b.z; }
VehicleVec3 cross(VehicleVec3 a, VehicleVec3 b) {
    return {a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x};
}
VehicleVec3 unit(VehicleVec3 a, VehicleVec3 fallback) {
    const double length = std::sqrt(dot(a,a));
    return length > 1.0e-6 ? mul(a,1.0/length) : fallback;
}
VehicleVec3 rotate(const VehicleQuat& q, VehicleVec3 v) {
    const VehicleVec3 u{q.x,q.y,q.z};
    const VehicleVec3 t = mul(cross(u,v),2.0);
    return add(add(v,mul(t,q.w)),cross(u,t));
}
void row(float* values, std::size_t index, VehicleVec3 world) {
    values[4*index] = static_cast<float>(world.x);
    values[4*index+1] = static_cast<float>(world.y);
    values[4*index+2] = static_cast<float>(world.z);
}
} // namespace

bool prepare_car_shadow(const StockFrameInputs& inputs, const TexturePack& pack,
                        CarShadowAsset& out, std::string& error) {
    error.clear();
    std::vector<std::uint32_t> wanted{kShadowHash};
    RenderTextureSource source;
    source.bytes = &inputs.file(StockFrameFile::car_textures).bytes;
    source.packs.push_back(&pack);
    source.wanted = &wanted;
    RenderTextureSet decoded;
    if (!decode_render_texture_sources({source}, {}, decoded, error)) return false;
    if (decoded.textures.size() != 1 || !decoded.textures[0].usable() ||
        decoded.textures[0].hash != kShadowHash) {
        error = "240SX_SHADOWIG is missing or unusable"; return false;
    }
    out.texture = std::move(decoded.textures[0]);
    // The retail path has four samples on each axis and three eight-vertex
    // strips. Triangle-list form keeps its eighteen covered triangles in
    // one resident draw; per-point road sampling remains an approximation.
    for (std::size_t y = 0; y < 4; ++y) for (std::size_t x = 0; x < 4; ++x) {
        auto& v = out.vertices[y*4+x];
        v.px = -1.0F + 2.0F*static_cast<float>(x)/3.0F;
        v.pz = -1.0F + 2.0F*static_cast<float>(y)/3.0F;
        v.py = 0.0F;
        // Retail world.efx doubles 0x80 RGB, effectively unity. Both render
        // paths apply that unity directly and ignore this diagnostic color.
        v.color[0] = v.color[1] = v.color[2] = 128;
        v.color[3] = 255;
        v.u = static_cast<float>(x)/3.0F;
        v.v = static_cast<float>(y)/3.0F;
    }
    std::size_t n = 0;
    for (std::uint16_t y = 0; y < 3; ++y) for (std::uint16_t x = 0; x < 3; ++x) {
        const std::uint16_t a = static_cast<std::uint16_t>(y*4+x);
        out.triangles[n++] = {a,static_cast<std::uint16_t>(a+1),static_cast<std::uint16_t>(a+4)};
        out.triangles[n++] = {static_cast<std::uint16_t>(a+1),static_cast<std::uint16_t>(a+5),static_cast<std::uint16_t>(a+4)};
    }
    return true;
}

CarShadowPlacement place_car_shadow(const RaceSession& session) noexcept {
    return place_car_shadow(session.car(),session.spec());
}

CarShadowPlacement place_car_shadow(const VehicleState& state,
                                    const VehicleSpec& spec) noexcept {
    CarShadowPlacement out;
    const auto& handling = spec.handling;
    const VehicleVec3 com{handling.centre_of_mass[0],handling.centre_of_mass[1],handling.centre_of_mass[2]};
    const VehicleVec3 origin = sub(state.position,rotate(state.orientation,com));
    const VehicleVec3 body_up = unit(rotate(state.orientation,{0,0,1}),{0,0,1});
    std::array<VehicleVec3,kVehicleWheelCount> wheel_centers{};
    std::array<bool,kVehicleWheelCount> active{};
    unsigned hits = 0;
    for (std::size_t i = 0; i < kVehicleWheelCount; ++i) {
        if (!state.wheels[i].contact) continue;
        const auto& w = handling.wheels[i];
        const VehicleVec3 local{w.longitudinal,w.lateral,w.vertical+state.wheels[i].compression};
        wheel_centers[i] = add(origin,rotate(state.orientation,local));
        active[i] = true;
        ++hits;
    }
    if (!hits) {
        // Keep the invisible resident draw at the car, never at world zero.
        row(out.transform.rows,3,origin);
        return out;
    }
    // The current collision API allocates per query. Fit a plane through
    // three contact-wheel centres, falling back to body up on fewer hits.
    // Suspension heights are a road-normal approximation, not terrain taps.
    VehicleVec3 normal = body_up;
    if (hits >= 3) {
        std::array<std::size_t,3> picked{};
        std::size_t count = 0;
        for (std::size_t i = 0; i < kVehicleWheelCount && count < 3; ++i)
            if (active[i]) picked[count++] = i;
        const VehicleVec3 a = sub(wheel_centers[picked[1]],wheel_centers[picked[0]]);
        const VehicleVec3 b = sub(wheel_centers[picked[2]],wheel_centers[picked[0]]);
        normal = unit(cross(a,b),body_up);
        if (dot(normal,body_up) < 0.0) normal = mul(normal,-1.0);
    }
    VehicleVec3 contacts{};
    for (std::size_t i = 0; i < kVehicleWheelCount; ++i)
        if (active[i]) contacts = add(contacts,sub(wheel_centers[i],mul(normal,handling.wheels[i].radius)));
    const VehicleVec3 road = mul(contacts,1.0/hits);
    const VehicleVec3 body_forward = rotate(state.orientation,{1,0,0});
    const VehicleVec3 forward = unit(sub(body_forward,mul(normal,dot(body_forward,normal))),{1,0,0});
    const VehicleVec3 side = unit(cross(normal,forward),{0,1,0});
    const double altitude = std::max(0.0,dot(sub(origin,road),normal));
    const double fade = std::min(1.0,altitude/3.0);
    const double expansion = 1.05*(1.0+0.75*fade);
    const double half_length = 0.5*handling.length*expansion;
    const double half_width = 0.5*handling.width*expansion;
    // Contact wheels determine the road plane, but the body origin determines
    // horizontal placement. Project it onto that plane, then apply the traced
    // depth lift. A partial contact set must not drag the grid toward a wheel.
    const VehicleVec3 center = add(sub(origin,mul(normal,dot(sub(origin,road),normal))),
                                   mul(normal,0.04));
    row(out.transform.rows,0,mul(forward,half_length));
    row(out.transform.rows,1,mul(side,half_width));
    row(out.transform.rows,2,normal);
    row(out.transform.rows,3,center);
    // With fewer than three contacts the plane normal falls back to body up.
    // Use world up for alignment then, so a tilted airborne car still fades.
    const VehicleVec3 alignment_normal = hits >= 3 ? normal : VehicleVec3{0,0,1};
    out.alpha = static_cast<float>((228.0/255.0)*std::abs(dot(body_up,alignment_normal))*(1.0-fade));
    return out;
}

bool record_car_shadow_commands(const CarShadowAsset& asset,
                                const CarShadowPlacement& placement,
                                const RenderOptions& options,
                                RecordedCommands& out, std::string& error) {
    out = {};
    if (!record_frame_state(options,{},out,error)) return false;
    auto& table = out.owning_uploads();
    UploadResource vertices;
    vertices.name = "shadow grid vertices";
    vertices.kind = UploadResourceKind::vertex_buffer;
    vertices.memory = UploadMemory::buffer;
    vertices.data = reinterpret_cast<const unsigned char*>(asset.vertices.data());
    vertices.bytes = sizeof(asset.vertices);
    vertices.alignment = alignof(MeshVertex);
    vertices.element_bytes = sizeof(MeshVertex);
    vertices.elements = asset.vertices.size();
    table.resources.push_back(vertices);
    UploadResource indices;
    indices.name = "shadow grid indices";
    indices.kind = UploadResourceKind::index_buffer;
    indices.memory = UploadMemory::buffer;
    indices.data = reinterpret_cast<const unsigned char*>(asset.triangles.data());
    indices.bytes = sizeof(asset.triangles);
    indices.alignment = alignof(std::uint16_t);
    indices.element_bytes = sizeof(std::uint16_t);
    indices.elements = asset.triangles.size()*3;
    table.resources.push_back(indices);
    UploadResource texture;
    texture.name = "240SX_SHADOWIG";
    texture.kind = UploadResourceKind::texture;
    texture.memory = UploadMemory::image;
    texture.data = asset.texture.rgba.data();
    texture.bytes = asset.texture.rgba.size();
    texture.alignment = kTextureStagingAlignment;
    texture.element_bytes = 4;
    texture.elements = std::uint64_t{asset.texture.width}*asset.texture.height;
    texture.texture_hash = kShadowHash;
    if (!check_texture_upload_format(TextureUploadFormat::rgba8,asset.texture.width,
                                     asset.texture.height,texture.bytes,texture.texture,error)) return false;
    table.resources.push_back(texture);
    table.totals.resources = 3;
    table.totals.vertex_buffers = table.totals.index_buffers = table.totals.textures = 1;
    table.totals.vertex_bytes = vertices.bytes;
    table.totals.index_bytes = indices.bytes;
    table.totals.texture_bytes = table.totals.largest_texture_bytes = texture.bytes;
    RecordedDraw draw;
    draw.name = "shadow ground";
    draw.vertex_resource = 0; draw.index_resource = 1; draw.texture_resource = 2;
    draw.vertex_count = static_cast<std::uint32_t>(asset.vertices.size());
    draw.index_count = static_cast<std::uint32_t>(asset.triangles.size()*3);
    bool blend_variant_found = false;
    for (const auto& p : shader_manifest::kPrograms) if (std::strcmp(p.name,"car_textured") == 0) {
        draw.program = &p;
        for (std::size_t i = 0; i < p.variant_count; ++i)
            if (std::strcmp(p.variants[i].name,"source_over") == 0) {
                draw.pipeline = p.variants[i];
                blend_variant_found = true;
            }
    }
    if (!draw.program) { error = "shadow shader program missing"; return false; }
    if (!blend_variant_found) { error = "shadow source-over variant missing"; return false; }
    draw.pipeline.depth_write = false;
    draw.pipeline.cull = shader_manifest::Cull::none;
    draw.placement = placement.transform;
    draw.material.alpha[0] = placement.alpha;
    draw.material.alpha[3] = 4.0F; // the world-effect shadow branch
    out.owning_draws().push_back(draw);
    return validate_recorded_commands(out,error);
}

bool render_car_shadow(const CarShadowAsset& asset,
                       const CarShadowPlacement& placement,
                       const RenderOptions& options, Frame& frame,
                       RenderStats& stats, std::string& error) {
    if (placement.alpha <= 0.0F) return true;
    CameraFrame camera;
    if (!make_camera_frame(options.camera,frame.width(),frame.height(),camera,error)) return false;
    RasterMaterial material;
    material.texture = &asset.texture;
    material.blend = RenderBlendMode::source_over;
    material.depth_write = false;
    material.alpha_scale = placement.alpha;
    Rasterizer raster(frame,options,{},stats);
    for (const auto& triangle : asset.triangles) {
        const std::uint16_t indices[3] = {triangle.a,triangle.b,triangle.c};
        ClipVertex clip[3];
        for (std::size_t k = 0; k < 3; ++k) {
            const auto& vertex = asset.vertices[indices[k]];
            clip[k] = project_point(camera,placed_vertex(vertex,placement.transform.rows));
            clip[k].u = vertex.u; clip[k].v = vertex.v;
        }
        if (!raster.draw_triangle(clip,material,
                static_cast<std::uint32_t>(stats.triangles_submitted++),error)) return false;
    }
    return true;
}
} // namespace nfsu
