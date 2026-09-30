#include "nfsu/light_glows.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iterator>

namespace nfsu {
namespace {
constexpr unsigned kGlowTextureSize = 64;
constexpr std::array<std::uint32_t, 4> kMarkerHashes = {
    0x9DB90133U, 0xD09091C6U, 0x31A66786U, 0xBF700A79U};

double dot3(const Vec3& a, const Vec3& b) noexcept {
    return a.x*b.x + a.y*b.y + a.z*b.z;
}
Vec3 difference(const Vec3& a, const Vec3& b) noexcept {
    return {a.x-b.x, a.y-b.y, a.z-b.z};
}
Vec3 placed_slot(const Vec3& slot, const float* m) noexcept {
    const double x = slot.x*m[0] + slot.y*m[4] + slot.z*m[8] + m[12];
    const double y = slot.x*m[1] + slot.y*m[5] + slot.z*m[9] + m[13];
    const double z = slot.x*m[2] + slot.y*m[6] + slot.z*m[10] + m[14];
    return {x, z, -y};
}
const PreparedInstance* lamp_instance(const PreparedCarScene& car, int slot) noexcept {
    for (const auto& instance : car.instances)
        if (instance.part_slot == static_cast<std::uint32_t>(slot)) return &instance;
    return nullptr;
}
ClipVertex glow_vertex(const CameraFrame& camera, const CarGlowSprite& sprite,
                       double x, double y, double u, double v) noexcept {
    const Vec3 world{sprite.world.x + sprite.radius*(x*camera.right.x + y*camera.up.x),
                     sprite.world.y + sprite.radius*(x*camera.right.y + y*camera.up.y),
                     sprite.world.z + sprite.radius*(x*camera.right.z + y*camera.up.z)};
    ClipVertex point = project_point(camera, world);
    point.u = u; point.v = v;
    return point;
}
} // namespace

CarGlowAnchors collect_car_glow_anchors(const StockFrame& stock) noexcept {
    CarGlowAnchors result{};
    for (const auto& marker : stock.report.markers) {
        for (std::size_t i = 0; i < kMarkerHashes.size(); ++i) {
            if (result[i].present || marker.name_hash != kMarkerHashes[i]) continue;
            const GeometryReport* source = marker.geometry_source == 0 ? &stock.car_geometry :
                marker.geometry_source - 1 < stock.stock_geometry.size() ?
                &stock.stock_geometry[marker.geometry_source - 1] : nullptr;
            if (!source || marker.source_mesh >= source->meshes.size()) continue;
            const auto& mesh = source->meshes[marker.source_mesh];
            if (marker.index >= mesh.markers.size()) continue;
            const auto& record = mesh.markers[marker.index];
            const Vec3 local{record.translation(0), record.translation(1), record.translation(2)};
            if (!std::isfinite(local.x) || !std::isfinite(local.y) || !std::isfinite(local.z))
                continue;
            result[i] = {local, true};
        }
    }
    return result;
}

CarGlowSprites place_car_glows(const CarGlowAnchors& anchors,
                               const PreparedCarScene& car,
                               const CameraFrame& camera,
                               bool braking) noexcept {
    CarGlowSprites result{};
    for (std::size_t i = 0; i < result.size(); ++i) {
        const bool head = i < 2;
        result[i].head = head;
        if (!anchors[i].present) continue;
        const auto* instance = lamp_instance(car, head ? kSlotHeadlight : kSlotBrakelight);
        if (!instance) continue;
        auto& sprite = result[i];
        sprite.world = placed_slot(anchors[i].slot, instance->transform);
        const Vec3 to_eye = difference(camera.eye, sprite.world);
        const double distance_sq = dot3(to_eye, to_eye);
        if (!(distance_sq > 0.0) || distance_sq > 1600.0) continue;
        const double inverse_distance = 1.0/std::sqrt(distance_sq);
        const Vec3 forward{instance->transform[0], instance->transform[2],
                           -instance->transform[1]};
        const double facing = dot3(forward, to_eye)*inverse_distance*(head ? 1.0 : -1.0);
        const double angle = std::clamp((facing + 0.1)*1.2, 0.0, 1.0);
        const double fade = std::clamp((1600.0-distance_sq)/400.0, 0.0, 1.0);
        sprite.opacity = angle*fade*(head ? 0.75 : braking ? 0.8 : 0.45);
        sprite.radius = head ? 0.36 : braking ? 0.30 : 0.22;
    }
    return result;
}

RenderTexture make_car_glow_texture(bool head) {
    RenderTexture texture;
    texture.hash = head ? 0x18500001U : 0x18500002U;
    texture.width = kGlowTextureSize; texture.height = kGlowTextureSize;
    texture.valid = true;
    texture.rgba.resize(kGlowTextureSize*kGlowTextureSize*4U);
    const double center = static_cast<double>(kGlowTextureSize)/2.0;
    for (unsigned y = 0; y < kGlowTextureSize; ++y)
        for (unsigned x = 0; x < kGlowTextureSize; ++x) {
        const double dx = (static_cast<double>(x)+0.5-center)/center;
        const double dy = (static_cast<double>(y)+0.5-center)/center;
        const double falloff = std::max(0.0, 1.0-dx*dx-dy*dy);
        const auto alpha = static_cast<unsigned char>(falloff*falloff*255.0+0.5);
        const std::size_t p = 4U*(y*kGlowTextureSize+x);
        texture.rgba[p+0] = 255;
        texture.rgba[p+1] = head ? 240 : 48;
        texture.rgba[p+2] = head ? 205 : 32;
        texture.rgba[p+3] = alpha;
    }
    return texture;
}

bool draw_car_glows(Frame& track, const RenderOptions& options,
                    const CarGlowSprites& sprites, RenderStats& stats,
                    std::string& error) {
    CameraFrame camera;
    if (!make_camera_frame(options.camera, options.width, options.height, camera, error))
        return false;
    static const RenderTexture head = make_car_glow_texture(true);
    static const RenderTexture tail = make_car_glow_texture(false);
    const RenderLimits limits;
    Rasterizer raster(track, options, limits, stats);
    for (std::size_t i = 0; i < sprites.size(); ++i) {
        const auto& sprite = sprites[i];
        if (sprite.opacity <= 0.0 || sprite.radius <= 0.0) continue;
        RasterMaterial material;
        material.texture = sprite.head ? &head : &tail;
        material.blend = RenderBlendMode::additive;
        material.depth_write = false;
        material.alpha_scale = sprite.opacity;
        const auto a = glow_vertex(camera, sprite, -1, -1, 0, 0);
        const auto b = glow_vertex(camera, sprite,  1, -1, 1, 0);
        const auto c = glow_vertex(camera, sprite,  1,  1, 1, 1);
        const auto d = glow_vertex(camera, sprite, -1,  1, 0, 1);
        const ClipVertex first[3] = {a,b,c};
        const ClipVertex second[3] = {a,c,d};
        if (!raster.draw_triangle(first, material, 0x7FFF0000U+static_cast<std::uint32_t>(2*i), error) ||
            !raster.draw_triangle(second, material, 0x7FFF0001U+static_cast<std::uint32_t>(2*i), error))
            return false;
    }
    return true;
}

void car_glow_transform(const CarGlowSprite& sprite,
                        const CameraFrame& camera, float (&rows)[16]) noexcept {
    std::fill(std::begin(rows), std::end(rows), 0.0F);
    const double radius = sprite.opacity > 0.0 ? sprite.radius : 0.0;
    rows[0] = static_cast<float>(camera.right.x*radius);
    rows[1] = static_cast<float>(-camera.right.z*radius);
    rows[2] = static_cast<float>(camera.right.y*radius);
    rows[8] = static_cast<float>(camera.up.x*radius);
    rows[9] = static_cast<float>(-camera.up.z*radius);
    rows[10] = static_cast<float>(camera.up.y*radius);
    rows[12] = static_cast<float>(sprite.world.x);
    rows[13] = static_cast<float>(-sprite.world.z);
    rows[14] = static_cast<float>(sprite.world.y);
    rows[15] = 1.0F;
}

} // namespace nfsu
