#include "nfsu/track_reflections.h"
#include "nfsu/track_zones.h"
#include "nfsu/texture_decode.h"
#include <algorithm>
#include <cmath>
#include <cstring>

namespace nfsu {
namespace {
std::uint32_t word(const unsigned char* p) {
    return std::uint32_t(p[0]) | (std::uint32_t(p[1])<<8) | (std::uint32_t(p[2])<<16) | (std::uint32_t(p[3])<<24);
}
float scalar(const unsigned char* p) { auto bits = word(p); float f; std::memcpy(&f, &bits, 4); return f; }
}
bool wet_road_lamp_ground_height(WetRoadLamp& lamp, float ground_height) {
    // 0x544899..0x5448BC: a hit within 0.05 of the light is marked with
    // the 9999 sentinel, which mode 2 rejects at 0x544952.
    if (std::abs(double(ground_height) - lamp.position[2]) < double(0.05F)) return false;
    lamp.reflected_height = 2*ground_height-lamp.position[2];
    return true;
}
bool load_wet_road_lamps(const std::filesystem::path& root, const TrackContactWorld& world,
                         WetRoadLamps& lamps, std::string& error) {
    lamps = {};
    std::vector<unsigned char> bytes;
    const auto zones = load_track_zones(root/"Tracks/TRACKB1003.lzc", bytes);
    if (!zones.ok() || !world.valid) { error = "lamp source or collision invalid"; return false; }
    // 0x540F6A..0x54101D: version 3 header and aligned 80-byte records.
    for (std::size_t top = 0; top+8 <= bytes.size();) {
        const auto size = word(bytes.data()+top+4);
        if (size > bytes.size()-top-8) { error = "lamp chunk outside image"; return false; }
        const auto end = top+8+size;
        if (word(bytes.data()+top) == 0x80135100U) {
            std::uint32_t count = 0; bool header = false;
            for (std::size_t p = top+8; p+8 <= end;) {
                const auto id = word(bytes.data()+p), length = word(bytes.data()+p+4);
                if (length > end-p-8) { error = "lamp child outside root"; return false; }
                const auto stop = p+8+length, first = (p+23)&~std::size_t(15);
                if (id == 0x135101U) {
                    if (first+64 > stop || word(bytes.data()+first+8) != 3) { error = "lamp header invalid"; return false; }
                    count = word(bytes.data()+first+48); header = true;
                } else if (id == 0x135102U) {
                    if (!header || count > 4096 || first > stop || count*80ULL != stop-first) { error = "lamp record range invalid"; return false; }
                    for (std::size_t i = 0; i < count; ++i) {
                        const auto* record = bytes.data()+first+i*80;
                        // Type 9 is the authored street light, table 0x6F4198.
                        if (!record[12] || record[13] != 9) continue;
                        WetRoadLamp lamp;
                        for (int k = 0; k < 3; ++k) {
                            lamp.position[k] = scalar(record+16+4*k);
                            lamp.direction[k] = scalar(record+32+4*k);
                            if (!std::isfinite(lamp.position[k]) || !std::isfinite(lamp.direction[k])) { error = "nonfinite lamp"; return false; }
                        }
                        TrackContact contact;
                        const auto status = query_track_contact_point(world, lamp.position, 0, 200, contact);
                        // The retail producer also omits a light when the ground query fails.
                        if (status != TrackContactStatus::hit) continue;
                        if (!wet_road_lamp_ground_height(lamp, contact.point[2])) continue;
                        lamps.lights.push_back(lamp);
                    }
                }
                p = stop;
            }
        }
        top = end;
    }
    if (lamps.lights.empty()) { error = "no street lamps with ground contacts"; return false; }
    std::vector<unsigned char> texture_bytes;
    const auto packs = load_texture_pack_set(root/"Global/GLOBALB.BUN", texture_bytes);
    const std::vector<std::uint32_t> wanted{0x3394FE62U}; // 0x6F3EF0, flare type-9 texture.
    RenderTextureSource source; source.bytes = &texture_bytes; source.wanted = &wanted; source.complete = packs.complete;
    for (const auto& pack : packs.packs) source.packs.push_back(&pack);
    RenderTextureSet decoded;
    if (!decode_render_texture_sources({source}, {}, decoded, error)) return false;
    for (auto& texture : decoded.textures) if (texture.claims_hash(wanted[0]) && texture.usable()) lamps.texture = std::move(texture);
    if (!lamps.texture.usable()) { error = "street lamp flare texture missing"; return false; }
    // Billboard geometry is shared by all lights, never rebuilt or uploaded per frame.
    for (std::size_t i = 0; i < 4; ++i) {
        auto& v = lamps.vertices[i];
        v.px = (i == 1 || i == 2) ? 1.0F : -1.0F;
        v.py = i < 2 ? 1.0F : -1.0F;
        v.u = (i == 1 || i == 2) ? 1.0F : 0.0F; v.v = i < 2 ? 0.0F : 1.0F;
        v.color[0] = 110; v.color[1] = 100; v.color[2] = 75; v.color[3] = 255;
    }
    return true;
}
bool wet_road_lamp_placement(const WetRoadLamp& lamp, const CameraFrame& camera, TrackTransform& out) {
    const Vec3 center{lamp.position[0], lamp.reflected_height, -lamp.position[1]};
    const auto projected = project_point(camera, center);
    if (projected.w <= camera.near_plane || projected.w > camera.far_plane) return false;
    const double dx = camera.eye.x-lamp.position[0], dy = -camera.eye.z-lamp.position[1], dz = camera.eye.y-lamp.position[2];
    const double distance = std::sqrt(dx*dx+dy*dy+dz*dz);
    const double facing = distance > 0 ? std::clamp((dx*lamp.direction[0]+dy*lamp.direction[1]+dz*lamp.direction[2])/distance, 0.0, 1.0) : 0;
    // Type-9 size endpoints from 0x6F40D8. The constant 0.02 is the port's
    // bounded billboard-size approximation, not the undecoded retail size/FOV curve.
    const double size = (30+70*facing)*0.02;
    const Vec3 right{camera.right.x*size, camera.right.y*size*10, camera.right.z*size};
    const Vec3 up{camera.up.x*size, camera.up.y*size*10, camera.up.z*size};
    out = {};
    out.linear[0] = float(right.x); out.linear[1] = float(-right.z); out.linear[2] = float(right.y);
    out.linear[3] = out.linear[4] = out.linear[5] = 0;
    out.linear[6] = float(up.x); out.linear[7] = float(-up.z); out.linear[8] = float(up.y);
    out.translation[0] = lamp.position[0]; out.translation[1] = lamp.position[1]; out.translation[2] = lamp.reflected_height;
    // Conservative sprite screen bounds, including the retail vertical stretch.
    const double margin = size*11*std::max(camera.focal_x,camera.focal_y);
    return std::abs(projected.x) <= projected.w+margin && std::abs(projected.y) <= projected.w+margin;
}
bool append_wet_lamp_uploads(const WetRoadLamps& lamps, UploadTable& uploads, std::string& error) {
    UploadResource v; v.name="wet lamp vertices"; v.data=reinterpret_cast<const unsigned char*>(lamps.vertices.data());
    v.bytes=sizeof(lamps.vertices); v.element_bytes=sizeof(TrackVertex); v.elements=4; v.alignment=alignof(TrackVertex); v.source=kNoHandle;
    UploadResource i; i.name="wet lamp indices"; i.kind=UploadResourceKind::index_buffer;
    i.data=reinterpret_cast<const unsigned char*>(lamps.indices.data()); i.bytes=sizeof(lamps.indices);
    i.element_bytes=2; i.elements=6; i.alignment=2; i.source=kNoHandle;
    UploadResource t; t.name="wet lamp texture"; t.kind=UploadResourceKind::texture; t.memory=UploadMemory::image;
    t.data=lamps.texture.rgba.data(); t.bytes=lamps.texture.rgba.size(); t.element_bytes=4;
    t.elements=std::uint64_t(lamps.texture.width)*lamps.texture.height; t.alignment=kTextureStagingAlignment;
    t.texture_hash=lamps.texture.hash; t.source=kNoHandle;
    if (!check_texture_upload_format(TextureUploadFormat::rgba8, lamps.texture.width, lamps.texture.height,t.bytes,t.texture,error)) return false;
    uploads.resources.push_back(v); uploads.resources.push_back(i); uploads.resources.push_back(t);
    auto& total=uploads.totals; total.resources+=3; ++total.vertex_buffers; ++total.index_buffers; ++total.textures;
    total.vertex_bytes+=v.bytes; total.index_bytes+=i.bytes; total.texture_bytes+=t.bytes;
    total.largest_texture_bytes=std::max(total.largest_texture_bytes,t.bytes);
    return true;
}
bool render_wet_road_lamps(const WetRoadLamps& lamps, const RenderOptions& options,
                           const RenderLimits& limits, Frame& frame, RenderStats& stats, std::string& error) {
    CameraFrame camera;
    if (!make_camera_frame(options.camera,options.width,options.height,camera,error)) return false;
    auto unlit=options; unlit.shading=ShadingMode::none; unlit.vertex_color=true; unlit.cull=CullMode::none;
    Rasterizer rasterizer(frame,unlit,limits,stats);
    RasterMaterial material; material.texture=&lamps.texture; material.blend=RenderBlendMode::additive;
    material.depth_write=false; material.alpha_scale=150.0/255;
    for (const auto& lamp : lamps.lights) {
        TrackTransform matrix;
        if (!wet_road_lamp_placement(lamp,camera,matrix)) continue;
        ClipVertex quad[4];
        for (std::size_t v=0; v<4; ++v) {
            const auto& p=lamps.vertices[v];
            const Vec3 world{matrix.translation[0]+p.px*matrix.linear[0]+p.py*matrix.linear[6],
                matrix.translation[2]+p.px*matrix.linear[2]+p.py*matrix.linear[8],
                -(matrix.translation[1]+p.px*matrix.linear[1]+p.py*matrix.linear[7])};
            quad[v]=project_point(camera,world); quad[v].u=p.u; quad[v].v=p.v;
            quad[v].r=p.color[0]; quad[v].g=p.color[1]; quad[v].b=p.color[2];
        }
        for (std::size_t t=0;t<6;t+=3) {
            ClipVertex triangle[3]={quad[lamps.indices[t]],quad[lamps.indices[t+1]],quad[lamps.indices[t+2]]};
            ++stats.triangles_submitted;
            if (!rasterizer.draw_triangle(triangle,material,0,error)) return false;
        }
    }
    return true;
}
}
