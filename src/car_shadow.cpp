#include "nfsu/car_shadow.h"
#include "nfsu/ai_driver.h"
#include "nfsu/texture.h"
#include "nfsu/texture_decode.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>

namespace nfsu {
namespace {

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

bool prepare_car_shadow(const std::string& car_type_name, const StockFrameInputs& inputs,
                        const TexturePack& pack, CarShadowAsset& out, std::string& error) {
    error.clear();
    // The retail shadow texture of the selected car: <name>_SHADOWIG in the
    // car's own TEXTURES.BIN. Format and authored mip count vary by car.
    // The 240SX's hash was 0xF36596F5.
    const std::string shadow_name = car_type_name + "_SHADOWIG";
    const std::uint32_t shadow_hash = bin_hash(shadow_name);
    std::vector<std::uint32_t> wanted{shadow_hash};
    RenderTextureSource source;
    source.bytes = &inputs.file(StockFrameFile::car_textures).bytes;
    source.packs.push_back(&pack);
    source.wanted = &wanted;
    RenderTextureSet decoded;
    RenderTextureLimits limits; limits.retain_mips = true;
    if (!decode_render_texture_sources({source}, limits, decoded, error)) return false;
    if (decoded.textures.size() != 1 || !decoded.textures[0].usable() ||
        decoded.textures[0].hash != shadow_hash) {
        error = shadow_name + " is missing or unusable"; return false;
    }
    out.texture = std::move(decoded.textures[0]);
    out.texture_name = shadow_name;
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

bool prepare_car_shadow_ramp(const StockFrameInputs& inputs, const StockFrame& stock,
                             RenderTexture& out, std::string& error) {
    constexpr std::uint32_t hash = 0xbadb4475; // 56A130, SHADOWRAMP
    const std::vector<std::uint32_t> wanted{hash};
    RenderTextureSource source;
    source.bytes = &inputs.file(StockFrameFile::global_bun).bytes;
    source.wanted = &wanted;
    if (stock.shared_sets.empty()) { error = "shadow ramp texture pack absent"; return false; }
    const auto& globals = stock.shared_sets.front();
    source.complete = globals.complete;
    for (const auto& pack : globals.packs) source.packs.push_back(&pack);
    RenderTextureSet decoded;
    if (!decode_render_texture_sources({source}, {}, decoded, error)) return false;
    for (auto& texture : decoded.textures) {
        if (texture.claims_hash(hash) && texture.usable()) {
            out = std::move(texture);
            return true;
        }
    }
    error = "original SHADOWRAMP texture unavailable";
    return false;
}

bool CarShadowSilhouette::initialize(const VehicleHull& hull, std::string& error) {
    if (hull.points.size() < 3 || hull.points.size() > kHullLimit) {
        error = "shadow collision hull outside native projection bounds";
        return false;
    }
    hull_ = hull.points;
    const auto segments = hull_.size() * 3;
    vertices_.resize(1 + 2 * (segments + 1));
    triangles_.reserve(segments * 3);
    for (std::size_t i = 0; i < segments; ++i) {
        const auto inner = static_cast<std::uint16_t>(1 + 2*i);
        const auto outer = static_cast<std::uint16_t>(inner + 1);
        const auto next = static_cast<std::uint16_t>(inner + 2);
        const auto next_outer = static_cast<std::uint16_t>(inner + 3);
        triangles_.push_back({0, inner, next});
        triangles_.push_back({inner, outer, next_outer});
        triangles_.push_back({inner, next_outer, next});
    }
    return true;
}

bool CarShadowSilhouette::append(UploadTable& table, const RenderTexture& ramp,
    std::size_t ramp_resource, RecordedDraw& draw, std::string& error) const {
    if (vertices_.empty() || !ramp.usable()) { error = "shadow silhouette is not prepared"; return false; }
    if (ramp_resource == kNoHandle) {
        UploadResource texture;
        texture.name = "SHADOWRAMP";
        texture.kind = UploadResourceKind::texture;
        texture.memory = UploadMemory::image;
        texture.data = ramp.rgba.data();
        texture.bytes = ramp.rgba.size();
        texture.alignment = kTextureStagingAlignment;
        texture.element_bytes = 4;
        texture.elements = std::uint64_t{ramp.width} * ramp.height;
        texture.texture_hash = ramp.hash;
        if (!check_texture_upload_format(TextureUploadFormat::rgba8, ramp.width, ramp.height,
                                         texture.bytes, texture.texture, error, ramp.mip_count)) return false;
        ramp_resource = table.resources.size();
        table.resources.push_back(texture);
        ++table.totals.textures;
        table.totals.texture_bytes += texture.bytes;
        table.totals.largest_texture_bytes = std::max(table.totals.largest_texture_bytes, texture.bytes);
    }

    UploadResource vertices;
    vertices.name = "shadow silhouette vertices";
    vertices.kind = UploadResourceKind::vertex_buffer;
    vertices.data = reinterpret_cast<const unsigned char*>(vertices_.data());
    vertices.bytes = vertices_.size() * sizeof(MeshVertex);
    vertices.elements = vertices_.size();
    vertices.element_bytes = sizeof(MeshVertex);
    vertices.alignment = kCarVertexAlignment;
    vertices.streamed_vertices = true;
    UploadResource indices;
    indices.name = "shadow silhouette indices";
    indices.kind = UploadResourceKind::index_buffer;
    indices.data = reinterpret_cast<const unsigned char*>(triangles_.data());
    indices.bytes = triangles_.size() * sizeof(MeshTriangle);
    indices.elements = triangles_.size() * 3;
    indices.element_bytes = 2;
    indices.alignment = kIndexAlignment;
    draw.name = "shadow silhouette";
    draw.texture_resource = ramp_resource;
    draw.vertex_resource = table.resources.size();
    draw.index_resource = draw.vertex_resource + 1;
    draw.vertex_count = static_cast<std::uint32_t>(vertices.elements);
    draw.index_count = static_cast<std::uint32_t>(indices.elements);
    for (const auto& program : shader_manifest::kPrograms) {
        if (std::strcmp(program.name, "car_textured") != 0) continue;
        for (std::size_t i = 0; i < program.variant_count; ++i) {
            if (std::strcmp(program.variants[i].name, "source_over") != 0) continue;
            draw.program = &program;
            draw.pipeline = program.variants[i];
        }
    }
    if (!draw.program) { error = "shadow ramp shader variant absent"; return false; }
    draw.pipeline.cull = shader_manifest::Cull::none;
    draw.pipeline.depth_write = false;
    // Reuse the unlit world-effect vertex RGBA x2 material used by skids.
    draw.material.fallback[3] = 9;
    draw.material.alpha[0] = 0;
    table.resources.push_back(vertices);
    table.resources.push_back(indices);
    table.totals.resources = table.resources.size();
    ++table.totals.vertex_buffers;
    ++table.totals.index_buffers;
    table.totals.vertex_bytes += vertices.bytes;
    table.totals.index_bytes += indices.bytes;
    return true;
}

VehicleVec3 car_shadow_light_ray(const VehicleVec3& origin, const VehicleVec3& light_position) noexcept {
    // 56D799 passes horizontal length as X and negative Z as Y to 564DB0.
    // The resulting angle is elevation above the road, not from vertical.
    auto light = sub(origin, light_position);
    const auto horizontal = std::sqrt(light.x*light.x + light.y*light.y);
    constexpr std::uint16_t min_elevation = 0x1a90;
    if (track_angle(float(horizontal),float(-light.z)) < min_elevation) {
        const double sum = std::abs(light.x) + std::abs(light.y);
        if (sum > 0) {
            const double length = std::sqrt(dot(light,light));
            const auto direction = ai_driver_heading(min_elevation);
            const double scale = length * direction[0] / sum;
            light.x *= scale;
            light.y *= scale;
            light.z = -length * direction[1];
        }
    }
    return light;
}

void CarShadowSilhouette::update(const VehicleState& state, const VehicleSpec& spec,
    const TrackContactWorld& world, const VehicleVec3& eye, const VehicleVec3& light_position,
    bool visible, RecordedDraw& draw) {
    draw.material.alpha[0] = 0;
    if (!visible || hull_.empty()) return;
    const auto relative = sub(state.position, eye);
    const double distance2 = dot(relative, relative);
    if (!(distance2 < 45.0*45.0)) return;
    auto q = state.orientation;
    const double norm = std::sqrt(q.x*q.x + q.y*q.y + q.z*q.z + q.w*q.w);
    if (!(norm > 0) || !std::isfinite(norm)) return;
    q.x /= norm; q.y /= norm; q.z /= norm; q.w /= norm;
    const auto& com = spec.handling.centre_of_mass;
    const auto origin = sub(state.position, rotate(q, {com[0], com[1], com[2]}));
    TrackContact reference;
    if (!query_track_ground(world, origin, ground_cache_, reference)) return;
    const auto height = std::clamp((origin.z - reference.point[2]) / 3.0, 0.0, 1.0);
    const double distance = std::sqrt(distance2);
    const double distance_fade = std::clamp((45.0 - distance) / 25.0, 0.0, 1.0);
    const auto opacity = static_cast<unsigned char>(55.0 * (1.0 - height) * distance_fade);
    if (!opacity) return;

    const auto light = car_shadow_light_ray(origin, light_position);
    if (!(light.z < 0)) return;

    // 56E591 projects collision vertices into the render-origin plane.
    std::array<VehicleVec3,kHullLimit> points;
    for (std::size_t i = 0; i < hull_.size(); ++i) {
        const auto& v = hull_[i];
        const auto p = rotate(q, {v[0], v[1], v[2] + 0.275});
        points[i] = {origin.x + p.x - p.z * light.x / light.z,
                     origin.y + p.y - p.z * light.y / light.z, 0};
    }
    const auto end = points.begin() + hull_.size();
    std::sort(points.begin(), end, [](const auto& a, const auto& b) {
        return a.x < b.x || (a.x == b.x && a.y < b.y);
    });
    std::array<VehicleVec3,2*kHullLimit> contour;
    std::size_t count = 0;
    const auto push = [&](const VehicleVec3& p, std::size_t floor) {
        while (count >= floor + 2 && cross(sub(contour[count-1],contour[count-2]),
               sub(p,contour[count-1])).z <= 0) --count;
        contour[count++] = p;
    };
    for (auto p = points.begin(); p != end; ++p) push(*p, 0);
    const auto lower = count - 1;
    for (auto p = end - 1; p != points.begin();) push(*--p, lower);
    --count;

    // 56CF10 and 56CEB0 keep road hits near the reference plane only.
    std::size_t kept = 0;
    const VehicleVec3 ref_point{origin.x, origin.y, reference.point[2]};
    const VehicleVec3 normal{reference.normal[0], reference.normal[1], reference.normal[2]};
    for (std::size_t i = 0; i < count; ++i) {
        auto point = contour[i];
        point.z = reference.point[2] + 0.06;
        TrackContact ground;
        if (!query_track_ground(world, point, ground_cache_, ground)) continue;
        point.z = ground.point[2] + 0.06;
        if (std::abs(point.z - ref_point.z - 0.06) > 4.0 ||
            std::abs(dot(sub(point, ref_point), normal)) >= 1.5) continue;
        contour[kept++] = point;
    }
    if (kept < 3 || kept > hull_.size()) return;

    // 56D0C0 cuts each corner at one third of its incoming/outgoing edges.
    std::array<VehicleVec3,3*kHullLimit> smooth;
    for (std::size_t i = 0; i < kept; ++i) {
        const auto p = contour[i];
        const auto a = mul(sub(contour[(i+kept-1)%kept],p), 0.333);
        const auto b = mul(sub(contour[(i+1)%kept],p), 0.333);
        smooth[3*i] = add(p, a);
        smooth[3*i+1] = add(p, mul(add(a,b), 0.333));
        smooth[3*i+2] = add(p, b);
    }
    count = kept * 3;
    const auto centre = mul(add(smooth[0], smooth[count/2]), 0.5);
    const auto store_vertex = [opacity](MeshVertex& vertex, const VehicleVec3& p, float u) {
        vertex.px = static_cast<float>(p.x);
        vertex.pz = static_cast<float>(p.y);
        vertex.py = static_cast<float>(p.z);
        vertex.u = u;
        vertex.v = 0;
        vertex.color[0] = vertex.color[1] = vertex.color[2] = 128;
        vertex.color[3] = opacity;
    };
    store_vertex(vertices_[0], centre, 0);
    for (std::size_t i = 0; i <= hull_.size()*3; ++i) {
        const auto point = smooth[i < count ? i : 0];
        auto rim = add(centre, mul(sub(point, centre), 1.4 - 0.3*height));
        rim.z = point.z; // 56D870 expands XY but retains contour height.
        store_vertex(vertices_[1+2*i], point, 0.155F);
        store_vertex(vertices_[2+2*i], rim, 1);
    }
    draw.material.alpha[0] = 1;
}

std::size_t CarShadowSilhouette::resident_bytes() const noexcept {
    return sizeof(*this) + hull_.capacity()*sizeof(hull_[0]) +
           vertices_.capacity()*sizeof(MeshVertex) + triangles_.capacity()*sizeof(MeshTriangle);
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
    // horizontal placement. Project it onto that plane, then apply clearance.
    // A partial contact set must not drag the grid toward a wheel.
    // Retail lifts its sampled road grid by 0.04 and selected vertices by
    // another 0.08. This single-plane approximation needs the full 0.12
    // clearance: at the event-1 grid pose its rear edge otherwise sinks
    // below the road and the CPU frame hides the entire shadow until GO.
    const VehicleVec3 center = add(sub(origin,mul(normal,dot(sub(origin,road),normal))),
                                   mul(normal,0.12));
    row(out.transform.rows,0,mul(forward,half_length));
    row(out.transform.rows,1,mul(side,half_width));
    row(out.transform.rows,2,normal);
    row(out.transform.rows,3,center);
    // With fewer than three contacts the plane normal falls back to body up.
    // Use world up for alignment then, so a tilted airborne car still fades.
    const VehicleVec3 alignment_normal = hits >= 3 ? normal : VehicleVec3{0,0,1};
    out.strength=static_cast<float>(std::abs(dot(body_up,alignment_normal))*(1.0-fade));
    out.alpha = static_cast<float>((228.0/255.0)*std::abs(dot(body_up,alignment_normal))*(1.0-fade));
    return out;
}

bool prepare_car_shadow_bounds(const StockFrameInputs&, const StockFrame& car,
    CarShadowAsset& shadow, std::string& error) {
    std::vector<std::uint32_t> loaded;
    for(const auto& mesh:car.car_geometry.meshes)if(mesh.has_directory_entry)loaded.push_back(mesh.directory_hash);
    for(const auto& source:car.geometry_sources())for(const auto& mesh:source.meshes)
        if(mesh.has_directory_entry)loaded.push_back(mesh.directory_hash);
    std::sort(loaded.begin(),loaded.end());loaded.erase(std::unique(loaded.begin(),loaded.end()),loaded.end());
    float low[3]={std::numeric_limits<float>::max(),std::numeric_limits<float>::max(),std::numeric_limits<float>::max()};
    float high[3]={-low[0],-low[1],-low[2]};
    // Constructor 0x56AB08..0x56AB70 reduces LOD-A solid-header bounds
    // for slots 0..11, including disabled models and zero absent models.
    for(unsigned slot=0;slot<12;++slot) {
        float a[3]{},b[3]{};const auto record=car.report.slots[slot].record;
        if(record!=kNoCarPart) {
            std::array<PartModel,kCarPartLodCount> models;
            if(!resolve_part_models(car.parts.records.at(record),loaded,models,error))return false;
            if(models[0].resolved) {
                auto mesh=std::find_if(car.car_geometry.meshes.begin(),car.car_geometry.meshes.end(),
                    [&](const auto& m){return m.has_directory_entry && m.directory_hash==models[0].hash;});
                if(mesh==car.car_geometry.meshes.end()){error="shadow footprint model outside car geometry";return false;}
                if(!mesh->has_solid_bounds){error="shadow footprint solid bounds unavailable";return false;}
                std::copy_n(mesh->solid_low,3,a);std::copy_n(mesh->solid_high,3,b);
            }
        }
        for(unsigned c=0;c<3;++c){low[c]=std::min(low[c],a[c]);high[c]=std::max(high[c],b[c]);}
    }
    for(unsigned c=0;c<3;++c){low[c]-=.1F;high[c]+=.1F;}
    std::copy(low,low+3,shadow.low.begin());
    std::copy(high,high+3,shadow.high.begin());
    shadow.selected_bounds=true;
    return true;
}

CarShadowPlacement project_car_shadow(const CarShadowAsset& asset, const VehicleState& state,
    const VehicleSpec& spec, CarShadowRoadQuery query, const void* context) {
    CarShadowPlacement out;
    out.projected=true;
    const auto& h=spec.handling;
    auto q=state.orientation;
    const double norm=std::sqrt(q.x*q.x+q.y*q.y+q.z*q.z+q.w*q.w);
    if (!asset.selected_bounds || !query || !(norm>0) || !std::isfinite(norm)) return out;
    q.x/=norm;q.y/=norm;q.z/=norm;q.w/=norm;
    const auto origin=sub(state.position,rotate(q,{h.centre_of_mass[0],h.centre_of_mass[1],h.centre_of_mass[2]}));
    row(out.transform.rows,3,origin);
    TrackContact reference;
    if (!query(origin,reference,context)) return out;
    const float fade=std::clamp((static_cast<float>(origin.z)-reference.point[2])/3.0F,0.0F,1.0F);
    const float scale=1.05F*(1.0F+0.75F*fade);
    const auto forward=rotate(q,{1,0,0}), side=rotate(q,{0,1,0}), up=rotate(q,{0,0,1});
    const float lx=asset.low[0]*scale,hx=asset.high[0]*scale;
    const float ly=asset.low[1]*scale,hy=asset.high[1]*scale;
    row(out.transform.rows,0,mul(forward,(hx-lx)*0.5F));
    row(out.transform.rows,1,mul(side,(hy-ly)*0.5F));
    row(out.transform.rows,2,up);
    row(out.transform.rows,3,add(origin,add(mul(forward,(hx+lx)*0.5F),mul(side,(hy+ly)*0.5F))));
    // 0x56EFD9: quantize the vertex alpha before world.efx doubles it.
    //56F025 multiplies alignment by fade before114;674898 truncates.
    const float strength=std::abs(static_cast<float>(up.z))*(1.0F-fade);
    out.strength=strength;
    const float a=std::clamp(std::trunc(114.0F*strength),0.0F,254.0F);
    out.alpha=2.0F*a/255.0F;
    TrackContact wheels[4]{};bool wheel_ok[4]{};
    for(unsigned i=0;i<4;++i) {
        const auto& w=h.wheels[i];
        wheel_ok[i]=query(add(origin,rotate(q,{w.longitudinal,w.lateral,w.vertical})),wheels[i],context);
    }
    constexpr unsigned fallback[16]={2,2,1,1,2,2,1,1,3,3,0,0,3,3,0,0};
    unsigned invalid[4]{};float last=static_cast<float>(origin.z);
    for(unsigned i=0;i<16;++i) {
        const auto& v=asset.vertices[i];
        const auto& m=out.transform.rows;
        const VehicleVec3 point{v.px*m[0]+v.pz*m[4]+m[12],v.px*m[1]+v.pz*m[5]+m[13],v.px*m[2]+v.pz*m[6]+m[14]};
        //56ED31 restores the same render-origin Z for every grid tap,
        //after transforming XY. Pitch/roll must not change which stacked
        //road is admitted by440A40's origin.z+1 upper bound.
        auto query_point=point;query_point.z=origin.z;
        TrackContact tap;bool ok=query(query_point,tap,context);
        float z=ok?tap.point[2]:last;
        if (!ok) {
            const auto w=fallback[i];const auto& plane=wheels[w];
            if (wheel_ok[w] && std::abs(plane.normal[2])>1.0e-6F) {
                z=plane.point[2]-(plane.normal[0]*(static_cast<float>(point.x)-plane.point[0])+
                    plane.normal[1]*(static_cast<float>(point.y)-plane.point[1]))/plane.normal[2];ok=true;
            }
        }
        if ((((i>>1)^i)&5)==5) z+=0.08F;
        const float plane_delta=(static_cast<float>(point.x)-reference.point[0])*reference.normal[0]+
            (static_cast<float>(point.y)-reference.point[1])*reference.normal[1]+(z-reference.point[2])*reference.normal[2];
        ok=ok && std::isfinite(z) && std::abs(z-reference.point[2])<=4.0F && std::abs(plane_delta)<1.5F;
        if(ok)last=z;else ++invalid[i/4];
        out.road_heights[i]=last+0.04F;
    }
    for(unsigned r=0;r<3;++r)if(invalid[r]+invalid[r+1]<4)out.strip_mask|=1U<<r;
    return out;
}

CarShadowPlacement place_car_shadow(const CarShadowAsset& asset,const VehicleState& state,
                                    const VehicleSpec& spec,const TrackContactWorld& world) {
    struct Context {const TrackContactWorld& world;CarShadowGroundCache& cache;} context{world,asset.ground_cache};
    const auto query=[](const VehicleVec3& p,TrackContact& hit,const void* context) {
        const auto& c=*static_cast<const Context*>(context);
        return query_car_shadow_ground(c.world,p,c.cache,hit);
    };
    return project_car_shadow(asset,state,spec,query,&context);
}
CarShadowPlacement place_car_shadow(const CarShadowAsset& asset,const RaceSession& session) {
    return place_car_shadow(asset,session.car(),session.spec(),session.course().world);
}

void update_car_shadow_draw(const CarShadowPlacement& p, RecordedDraw& draw) {
    draw.placement=p.transform;
    draw.material.alpha[0]=p.alpha;
    draw.material.alpha[3]=4.0F;
    if(p.projected) {
        draw.material.alpha[1]=static_cast<float>(p.strip_mask);
        std::copy_n(p.road_heights.data(),4,draw.material.fallback);
        std::copy_n(p.road_heights.data()+4,4,draw.material.environment_min);
        std::copy_n(p.road_heights.data()+8,4,draw.material.environment_range);
        std::copy_n(p.road_heights.data()+12,4,draw.material.environment_color);
    }
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
    texture.name = asset.texture_name.c_str();
    texture.kind = UploadResourceKind::texture;
    texture.memory = UploadMemory::image;
    texture.data = asset.texture.rgba.data();
    texture.bytes = asset.texture.rgba.size();
    texture.alignment = kTextureStagingAlignment;
    texture.element_bytes = 4;
    texture.elements = std::uint64_t{asset.texture.width}*asset.texture.height;
    texture.texture_hash = asset.texture.hash;
    if (!check_texture_upload_format(TextureUploadFormat::rgba8,asset.texture.width,
                                     asset.texture.height,texture.bytes,texture.texture,error,asset.texture.mip_count)) return false;
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
    for (const auto& p : shader_manifest::kPrograms) if (std::strcmp(p.name,asset.selected_bounds?"car_shadow_textured":"car_textured") == 0) {
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
    update_car_shadow_draw(placement,draw);
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
    material.linear_filter = true;
    Rasterizer raster(frame,options,{},stats);
    for (const auto& triangle : asset.triangles) {
        if (placement.projected && !(placement.strip_mask & (1U << (triangle.a/4)))) continue;
        const std::uint16_t indices[3] = {triangle.a,triangle.b,triangle.c};
        ClipVertex clip[3];
        for (std::size_t k = 0; k < 3; ++k) {
            const auto& vertex = asset.vertices[indices[k]];
            auto world=placed_vertex(vertex,placement.transform.rows);
            if (placement.projected) world.y=placement.road_heights[indices[k]];
            clip[k] = project_point(camera,world);
            clip[k].u = vertex.u; clip[k].v = vertex.v;
        }
        if (!raster.draw_triangle(clip,material,
                static_cast<std::uint32_t>(stats.triangles_submitted++),error)) return false;
    }
    return true;
}
} // namespace nfsu
