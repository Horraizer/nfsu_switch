#include "nfsu/light_glows.h"
#include "nfsu/frontend_motion.h"
#include "nfsu/texture_decode.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iterator>

namespace nfsu {
// Shared original 0x5443F0 primitive, used by frontend and race lights.
namespace {
struct GlowDescriptor {float low_size,high_size;unsigned char low[4],high[4];float exponent,offset,near_depth,far_depth;};
constexpr GlowDescriptor glow_descriptors[]={
    {0,64,{0,0,0,0},{100,120,170,80},1.4500000476837158F,.800000011920929F,.009999999776482582F,.014999999664723873F},
    {28,28,{0,0,0,0},{100,120,140,20},3,.800000011920929F,.009999999776482582F,.014999999664723873F},
    {20,30,{0,0,0,0},{128,0,0,80},1,.20000000298023224F,.009999999776482582F,.019999999552965164F},
    {30,100,{110,100,75,110},{110,100,75,150},1,4,.0010000000474974513F,.10000000149011612F}
};
}
bool frontend_car_glow(const float (&point)[3],const float (&toward_eye)[3],
    const float (&right)[3],const float (&up)[3],float facing,float depth_scale,
    unsigned mode,unsigned descriptor,float opacity,FrontendRoadGlow& out) noexcept {
    out={};
    if(mode>2 || descriptor>=4 || !std::isfinite(facing) || !std::isfinite(depth_scale) ||
       !std::isfinite(opacity) || opacity<0)return false;
    for(unsigned i=0;i<3;++i)if(!std::isfinite(point[i]) || !std::isfinite(toward_eye[i]) ||
        !std::isfinite(right[i]) || !std::isfinite(up[i]))return false;
    const auto& d=glow_descriptors[descriptor];
    const double gain=std::min(1.0,std::pow(double(std::max(0.0F,facing)),double(d.exponent))*double(opacity));
    const double size=(double(d.low_size)+(double(d.high_size)-d.low_size)*gain)*
        std::clamp(double(depth_scale),double(d.near_depth),double(d.far_depth))*(mode==2?1:1.5);
    if(size<=0)return false;
    const int signs[4][2]={{-1,1},{1,1},{1,-1},{-1,-1}};
    for(unsigned corner=0;corner<4;++corner)for(unsigned axis=0;axis<3;++axis) {
        const double extent=size*(signs[corner][0]*double(right[axis])+
            signs[corner][1]*double(up[axis]))*(mode==2 && axis==2?10:1);
        out.corners[corner][axis]=float(double(point[axis])+double(d.offset)*toward_eye[axis]+extent);
    }
    for(unsigned channel=0;channel<4;++channel)
        out.rgba[channel]=static_cast<unsigned char>(double(d.low[channel])+(int(d.high[channel])-d.low[channel])*gain);
    return true;
}
bool frontend_road_glow(const float (&point)[3],const float (&toward_eye)[3],
    const float (&right)[3],const float (&up)[3],float facing,float depth_scale,
    unsigned mode,FrontendRoadGlow& out) noexcept {
    return frontend_car_glow(point,toward_eye,right,up,facing,depth_scale,mode,3,1,out);
}

namespace {
constexpr unsigned kGlowTextureSize = 64;
constexpr std::array<std::uint32_t, 5> kMarkerHashes = {
    0x9DB90133U, 0xD09091C6U, 0x31A66786U, 0xBF700A79U, 0xA2A2FC7CU};

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
            result[i].slot=local; result[i].present=true;
            result[i].normal={record.matrix[8],record.matrix[9],record.matrix[10]};
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

bool load_original_car_glow_textures(const std::filesystem::path& root,
    std::array<RenderTexture,3>& textures,std::string& error) {
    textures={};std::vector<unsigned char> bytes;
    const auto packs=load_texture_pack_set(root/"Global/GLOBALB.BUN",bytes);
    const std::vector<std::uint32_t> wanted{0x8868656FU,0x3394FE62U,0x88D8E8A2U};
    auto filter=wanted;std::sort(filter.begin(),filter.end());
    RenderTextureSource source;source.bytes=&bytes;source.wanted=&filter;source.complete=packs.complete;
    for(const auto& pack:packs.packs)source.packs.push_back(&pack);
    RenderTextureSet decoded;
    if(!decode_render_texture_sources({source},{},decoded,error))return false;
    for(unsigned i=0;i<3;++i) {
        for(auto& texture:decoded.textures)if(texture.claims_hash(wanted[i]) && texture.usable())textures[i]=std::move(texture);
        if(!textures[i].usable()){error="original car glow texture missing: "+std::to_string(wanted[i])+" decoded="+std::to_string(decoded.textures.size());textures={};return false;}
    }
    return true;
}
OriginalCarGlows place_original_car_glows(const CarGlowAnchors& anchors,
    const PreparedCarScene& car,const CameraFrame& camera,unsigned display_width,
    double service_brake) noexcept {
    OriginalCarGlows result{};unsigned output=0;
    for(unsigned i=0;i<anchors.size();++i) {
        const bool head=i<2;const unsigned layers=head?2:1;
        for(unsigned layer=0;layer<layers;++layer) {
            auto& sprite=result[output++];sprite.texture=head?(layer==0?0:2):1;
            if(!anchors[i].present || display_width==0 || !std::isfinite(service_brake))continue;
            const auto* instance=lamp_instance(car,head?kSlotHeadlight:kSlotBrakelight);
            if(!instance)continue;
            const auto world=placed_slot(anchors[i].slot,instance->transform);
            const Vec3 d=difference(world,camera.eye);const double distance2=dot3(d,d);
            const double depth=dot3(d,camera.forward);
            if(!(distance2>0) || distance2>40000 || depth<=camera.near_plane || depth>=camera.far_plane ||
               std::abs(dot3(d,camera.right))*camera.focal_x>=depth ||
               std::abs(dot3(d,camera.up))*camera.focal_y>=depth)continue;
            const double distance=std::sqrt(distance2);
            const float point[3]={float(world.x),float(-world.z),float(world.y)};
            const float toward[3]={float(-d.x/distance),float(d.z/distance),float(-d.y/distance)};
            const float right[3]={float(camera.right.x),float(-camera.right.z),float(camera.right.y)};
            const float up[3]={float(-camera.up.x),float(camera.up.z),float(-camera.up.y)};
            const auto& n=anchors[i].normal;const auto* m=instance->transform;
            const double normal[3]={n.x*m[0]+n.y*m[4]+n.z*m[8],n.x*m[1]+n.y*m[5]+n.z*m[9],n.x*m[2]+n.y*m[6]+n.z*m[10]};
            const double facing=toward[0]*normal[0]+toward[1]*normal[1]+toward[2]*normal[2];
            // Original ordinary-owner base gains and service-axis response.
            const float gain=head?1.0F:float(std::min(1.0,double(.699999988079071F)+
                (service_brake>double(.05000000074505806F)?16.0*service_brake:0.0)));
            FrontendRoadGlow quad;
            if(!frontend_car_glow(point,toward,right,up,float(std::max(0.,facing)),
                float(depth/(double(display_width)*.5*camera.focal_x)),0,head?layer:2,gain,quad))continue;
            // Native consumer distance fade applies to final alpha.
            const double fade=distance2<=32000?1:(40000-distance2)/8000;
            for(unsigned c=0;c<4;++c)sprite.rgba[c]=quad.rgba[c];
            sprite.rgba[3]=static_cast<unsigned char>(double(sprite.rgba[3])*fade);
            // Race quad uses negative Y at v=0; preserve the original corner UV order.
            for(unsigned axis=0;axis<3;++axis) {
                sprite.transform[axis]=(quad.corners[1][axis]-quad.corners[0][axis])*.5F;
                sprite.transform[8+axis]=(quad.corners[3][axis]-quad.corners[0][axis])*.5F;
                sprite.transform[12+axis]=(quad.corners[0][axis]+quad.corners[2][axis])*.5F;
            }
            sprite.transform[15]=1;
        }
    }
    return result;
}

OriginalCarGlow place_original_road_glow(const float* point,const float* normal,
    const CameraFrame& camera,unsigned width) noexcept {
    OriginalCarGlow out;out.texture=1;
    const Vec3 world{point[0],point[2],-point[1]};
    const Vec3 d=difference(world,camera.eye);const double distance2=dot3(d,d);
    const double depth=dot3(d,camera.forward);
    if(!width || !(distance2>0) || distance2>=40000 || depth<=camera.near_plane ||
       depth>=camera.far_plane || std::abs(dot3(d,camera.right))*camera.focal_x>=depth ||
       std::abs(dot3(d,camera.up))*camera.focal_y>=depth)return out;
    const double distance=std::sqrt(distance2);
    const float toward[3]={float(-d.x/distance),float(d.z/distance),float(-d.y/distance)};
    const float right[3]={float(camera.right.x),float(-camera.right.z),float(camera.right.y)};
    const float up[3]={float(-camera.up.x),float(camera.up.z),float(-camera.up.y)};
    const float position[3]={point[0],point[1],point[2]};
    const float facing=std::max(0.0F,toward[0]*normal[0]+toward[1]*normal[1]+toward[2]*normal[2]);
    FrontendRoadGlow quad;
    if(!frontend_road_glow(position,toward,right,up,facing,
        float(depth/(double(width)*.5*camera.focal_x)),0,quad))return out;
    std::copy(std::begin(quad.rgba),std::end(quad.rgba),out.rgba);
    out.rgba[3]=static_cast<unsigned char>(double(out.rgba[3])*
        (distance2<=32000?1:(40000-distance2)/8000));
    for(unsigned axis=0;axis<3;++axis) {
        out.transform[axis]=(quad.corners[1][axis]-quad.corners[0][axis])*.5F;
        out.transform[8+axis]=(quad.corners[3][axis]-quad.corners[0][axis])*.5F;
        out.transform[12+axis]=(quad.corners[0][axis]+quad.corners[2][axis])*.5F;
    }
    out.transform[15]=1;return out;
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
