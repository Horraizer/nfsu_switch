#include "nfsu/race_hud.h"
#include "nfsu/screen_text.h"
#include "nfsu/texture.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>

namespace nfsu {
namespace {

struct AtlasRect { int x, y, w, h; };
constexpr std::array<AtlasRect, 4> kLines{{
    {0, 0, 256, 128}, {256, 0, 256, 128},
    {0, 128, 256, 128}, {256, 128, 256, 128}
}};
constexpr std::array<AtlasRect, 8> kRed{{
    {0, 384, 64, 32}, {64, 384, 64, 32},
    {128, 384, 64, 32}, {192, 384, 64, 32},
    {0, 416, 64, 32}, {64, 416, 64, 32},
    {128, 416, 64, 32}, {192, 416, 64, 32}
}};
constexpr std::array<std::uint32_t, 4> kLinesHashes{{
    0x4F9F0245, 0xC41B7346, 0x3897E447, 0x40FE164F
}};
constexpr std::array<std::uint32_t, 8> kRedHashes{{
    0x88DF62DF, 0x25AEEEB6, 0x94892E04, 0xD5D99FA0,
    0xE1836AC5, 0x22D3DC61, 0x2E7DA786, 0x6FCE1922
}};
constexpr AtlasRect kNeedle{0, 256, 16, 128};
constexpr AtlasRect kHub{16, 256, 16, 16};
constexpr AtlasRect kSpeedBox{32, 256, 128, 32};
constexpr AtlasRect kLcd{160, 256, 128, 128};
constexpr AtlasRect kUnit{384, 384, 72, 18};
constexpr AtlasRect kDisc{256, 384, 128, 128};

struct SourceSpec {
    const char* path;
    std::uint32_t hash;
    AtlasRect rect;
};
constexpr std::array<SourceSpec, 16> kSources{{
    {"Global/INGAMEB.BUN", 0x40FAB387, kNeedle},
    {"Global/INGAMEB.BUN", 0x56447D19, kHub},
    {"Global/INGAMEB.BUN", 0x6D079188, kSpeedBox},
    {"Languages/LanguageTextures.bin", 0xBBBA71C2, kLcd},
    {"Global/DYNTEX.BIN", kLinesHashes[0], kLines[0]},
    {"Global/DYNTEX.BIN", kLinesHashes[1], kLines[1]},
    {"Global/DYNTEX.BIN", kLinesHashes[2], kLines[2]},
    {"Global/DYNTEX.BIN", kLinesHashes[3], kLines[3]},
    {"Global/DYNTEX.BIN", kRedHashes[0], kRed[0]},
    {"Global/DYNTEX.BIN", kRedHashes[1], kRed[1]},
    {"Global/DYNTEX.BIN", kRedHashes[2], kRed[2]},
    {"Global/DYNTEX.BIN", kRedHashes[3], kRed[3]},
    {"Global/DYNTEX.BIN", kRedHashes[4], kRed[4]},
    {"Global/DYNTEX.BIN", kRedHashes[5], kRed[5]},
    {"Global/DYNTEX.BIN", kRedHashes[6], kRed[6]},
    {"Global/DYNTEX.BIN", kRedHashes[7], kRed[7]},
}};

bool copy_texture(const std::vector<unsigned char>& bytes,
                  const TexturePackSet& set, const SourceSpec& source,
                  RaceHudAtlas& atlas, std::string& error) {
    for (const auto& pack : set.packs) for (const auto& record : pack.textures) {
        if (record.hash != source.hash) continue;
        if (!record.valid || record.width != source.rect.w ||
            record.height != source.rect.h) {
            error = std::string(source.path) + ": HUD texture shape changed";
            return false;
        }
        std::vector<unsigned char> payload, rgba;
        std::size_t unused = 0;
        std::uint64_t top = 0;
        if (!texture_payload(bytes.data(), bytes.size(), record, payload, unused, error) ||
            !level_size(record.format, record.width, record.height, top) ||
            top > payload.size() ||
            !decode_level(record.format, record.width, record.height,
                          payload.data(), static_cast<std::size_t>(top), rgba, error)) {
            if (error.empty()) error = "HUD texture decode failed";
            return false;
        }
        for (int y = 0; y < source.rect.h; ++y) {
            const auto src = rgba.data() + y * source.rect.w * 4;
            auto* dst = atlas.rgba.data() +
                ((source.rect.y + y) * kRaceHudAtlasSize + source.rect.x) * 4;
            std::memcpy(dst, src, static_cast<std::size_t>(source.rect.w) * 4);
        }
        return true;
    }
    error = std::string(source.path) + ": required HUD texture absent";
    return false;
}

void add_quad(RaceHudFrame& frame, const AtlasRect& uv,
              const std::array<std::array<float, 2>, 4>& corners,
              std::array<unsigned char, 3> tint,
              unsigned char opacity = 255) noexcept {
    if (frame.quads == kRaceHudMaxQuads) return;
    const float u0 = float(uv.x) / kRaceHudAtlasSize;
    const float v0 = float(uv.y) / kRaceHudAtlasSize;
    const float u1 = float(uv.x + uv.w) / kRaceHudAtlasSize;
    const float v1 = float(uv.y + uv.h) / kRaceHudAtlasSize;
    const std::array<std::array<float, 2>, 4> tex{{
        {{u0, v0}}, {{u1, v0}}, {{u1, v1}}, {{u0, v1}}
    }};
    for (int i = 0; i < 4; ++i) {
        auto& v = frame.vertices[frame.quads * 4 + i];
        v.px = corners[i][0]; v.py = corners[i][1]; v.pz = 0;
        v.u = tex[i][0]; v.v = tex[i][1];
        v.color[0] = tint[0]; v.color[1] = tint[1];
        v.color[2] = tint[2]; v.color[3] = opacity;
    }
    ++frame.quads;
}

void rect(RaceHudFrame& frame, const AtlasRect& uv,
          float x, float y, float w, float h,
          std::array<unsigned char, 3> tint = {{255, 255, 255}},
          unsigned char opacity = 255) noexcept {
    add_quad(frame, uv, {{{{x, y}}, {{x+w, y}}, {{x+w, y+h}}, {{x, y+h}}}}, tint,
             opacity);
}

void rotated(RaceHudFrame& frame, const AtlasRect& uv,
             float pivot_x, float pivot_y, float w, float h,
             float source_pivot_x, float source_pivot_y, double radians,
             std::array<unsigned char, 3> tint) noexcept {
    const float cs = static_cast<float>(std::cos(radians));
    const float sn = static_cast<float>(std::sin(radians));
    const std::array<std::array<float, 2>, 4> local{{
        {{-source_pivot_x, -source_pivot_y}},
        {{w-source_pivot_x, -source_pivot_y}},
        {{w-source_pivot_x, h-source_pivot_y}},
        {{-source_pivot_x, h-source_pivot_y}}
    }};
    std::array<std::array<float, 2>, 4> corners{};
    for (int i = 0; i < 4; ++i) {
        corners[i] = {{pivot_x + local[i][0]*cs - local[i][1]*sn,
                       pivot_y + local[i][0]*sn + local[i][1]*cs}};
    }
    add_quad(frame, uv, corners, tint);
}

AtlasRect glyph(char ch) noexcept {
    switch (ch) {
    case '0': return {kLcd.x+1,kLcd.y+1,27,34};
    case '9': return {kLcd.x+28,kLcd.y+1,26,34};
    case '2': return {kLcd.x+54,kLcd.y+1,27,34};
    case '3': return {kLcd.x+81,kLcd.y+1,27,34};
    case '8': return {kLcd.x+1,kLcd.y+35,27,35};
    case '5': return {kLcd.x+28,kLcd.y+35,26,35};
    case '6': return {kLcd.x+54,kLcd.y+35,27,35};
    case 'R': return {kLcd.x+81,kLcd.y+35,25,35};
    case '1': return {kLcd.x+106,kLcd.y+35,13,35};
    case '7': return {kLcd.x+1,kLcd.y+70,25,35};
    case 'N': return {kLcd.x+26,kLcd.y+70,26,35};
    case '4': return {kLcd.x+52,kLcd.y+70,27,35};
    default: return {kLcd.x+1,kLcd.y+1,27,34};
    }
}

void digit(RaceHudFrame& frame, char ch, float x, float y,
           float h, std::array<unsigned char, 3> tint) noexcept {
    const auto uv = glyph(ch);
    rect(frame, uv, x, y, h*uv.w/uv.h, h, tint);
}

} // namespace

RaceHudDial select_race_hud_dial(double max_rpm, double redline_rpm) noexcept {
    // 0x00432A90..0x00432C43: max RPM chooses LINES, then redline RED.
    if (max_rpm >= 9500.0)
        return {kLinesHashes[3], redline_rpm >= 9000.0 ? kRedHashes[7] : kRedHashes[6]};
    if (max_rpm >= 8500.0)
        return {kLinesHashes[2], redline_rpm >= 8000.0 ? kRedHashes[5] : kRedHashes[4]};
    if (max_rpm >= 7500.0)
        return {kLinesHashes[1], redline_rpm >= 7000.0 ? kRedHashes[3] :
                                  redline_rpm >= 6500.0 ? kRedHashes[2] : 0};
    return {kLinesHashes[0], redline_rpm >= 6500.0 ? kRedHashes[1] : kRedHashes[0]};
}

double race_hud_needle_rotation(double rpm, double max_rpm) noexcept {
    // 0x004A0EA0: clamp(rpm/max, 0, 1), then use the max-RPM angle band.
    if (!(max_rpm > 0.0)) return 44.0;
    const double t = std::max(0.0, std::min(1.0, rpm/max_rpm));
    if (max_rpm >= 10000.0) return 45.0 + 180.0*t;
    if (max_rpm >= 9000.0) return 50.0 + 160.0*t;
    return 44.0 + 171.0*t;
}

bool load_race_hud_atlas(const std::filesystem::path& root,
                         RaceHudAtlas& atlas, std::string& error) {
    atlas.rgba.assign(kRaceHudAtlasSize*kRaceHudAtlasSize*4, 0);
    for (const char* path : {"Global/INGAMEB.BUN", "Global/DYNTEX.BIN",
                             "Languages/LanguageTextures.bin"}) {
        std::vector<unsigned char> bytes;
        const auto set = load_texture_pack_set(root / path, bytes);
        if (!set.complete || !set.ok()) {
            error = std::string(path) + ": HUD pack parse failed";
            return false;
        }
        for (const auto& source : kSources)
            if (std::strcmp(path, source.path) == 0 &&
                !copy_texture(bytes, set, source, atlas, error)) return false;
    }
    // The stock game stores its normal label in font/layout data not decoded
    // here. This small fallback label is explicitly an approximation.
    ScreenImage image{atlas.rgba.data(),kRaceHudAtlasSize,kRaceHudAtlasSize,
                      kRaceHudAtlasSize*4};
    draw_screen_text(image,kUnit.x,kUnit.y,"MPH",2,{255,255,255,255},{0,0,0,0});
    // A soft, translucent disc shades the whole gauge as in retail. It is
    // generated in memory; no game texture or derived image is packaged.
    for (int y=0; y<kDisc.h; ++y) for (int x=0; x<kDisc.w; ++x) {
        const double dx=x+0.5-kDisc.w/2.0, dy=y+0.5-kDisc.h/2.0;
        const double radius=std::sqrt(dx*dx+dy*dy);
        auto* pixel=atlas.rgba.data()+((kDisc.y+y)*kRaceHudAtlasSize+kDisc.x+x)*4;
        pixel[0]=pixel[1]=pixel[2]=0;
        pixel[3]=static_cast<unsigned char>(std::max(0.0,std::min(1.0,64.0-radius))*100.0);
    }
    return true;
}

void build_race_hud_frame(const RaceSession& session,
                          std::uint32_t width, std::uint32_t height,
                          RaceHudFrame& frame) noexcept {
    const auto& handling = session.spec().handling;
    build_race_hud_frame_for_car(session.car(), handling.max_rpm,
                                 handling.redline_rpm, width, height, frame);
}

void build_race_hud_frame_for_car(const VehicleState& car,
                          double max_rpm, double redline_rpm,
                          std::uint32_t width, std::uint32_t height,
                          RaceHudFrame& frame) noexcept {
    frame.quads = 0;
    if (!width || !height) return;
    // 1280x720 is the logical canvas; scale within a 5% safe area in either
    // handheld or docked mode. These placement values are screenshot fits.
    const float scale = std::min(width/1280.0F,height/720.0F) * 0.95F;
    const float right = width - 72.0F*scale;
    const float bottom = height - 18.0F*scale;
    const auto pos = [&](float x, float y) {
        return std::array<float,2>{{right+(x-1280.0F)*scale,
                                     bottom+(y-720.0F)*scale}};
    };
    const auto place = [&](const AtlasRect& uv, float x, float y, float w, float h,
                           std::array<unsigned char,3> tint,
                           unsigned char opacity = 255) {
        const auto p=pos(x,y); rect(frame,uv,p[0],p[1],w*scale,h*scale,tint,opacity);
    };
    const auto dial=select_race_hud_dial(max_rpm,redline_rpm);
    const auto line=std::find(kLinesHashes.begin(),kLinesHashes.end(),dial.lines_hash);
    const auto red=std::find(kRedHashes.begin(),kRedHashes.end(),dial.red_hash);
    place(kDisc,1025,494,218,218,{{255,255,255}});
    // The full 256x128 LINES sprite is centered at the same FEng position
    // as 3rdPersonTach. Only its left 128x128 texels contain the face.
    // Register the decoded face's (69,70) hub with the needle pivot.
    constexpr float face_size=218.0F;
    constexpr float face_texels=128.0F;
    constexpr float texel=face_size/face_texels;
    const float face_x=1134.0F-69.0F*texel;
    const float face_y=603.0F-70.0F*texel;
    const auto& slot=kLines[static_cast<std::size_t>(line-kLinesHashes.begin())];
    place(slot,face_x,face_y,256.0F*texel,128.0F*texel,
          {{55,189,245}},220);
    // HUD_SingleRace.fng centers RED 17 texels left and 42 up from LINES.
    // The different sprite sizes make their top-left origins (79,6) apart.
    if (red!=kRedHashes.end())
        place(kRed[static_cast<std::size_t>(red-kRedHashes.begin())],
              face_x+79.0F*texel,face_y+6.0F*texel,
              64.0F*texel,32.0F*texel,
              {{228,55,43}},220);
    // A 265-degree sprite-frame offset places the retail endpoint rotations
    // through the face's 0 and max labels about its registered hub.
    const double rotation=race_hud_needle_rotation(car.engine_rpm,max_rpm);
    const double dial_angle=(265.0-rotation)*3.14159265358979323846/180.0;
    const auto center=pos(1134,603);
    rotated(frame,kNeedle,center[0],center[1],24*scale,192*scale,
            12*scale,177*scale,1.5707963267948966-dial_angle,{{90,218,253}});
    place(kHub,1122,591,24,24,{{100,220,255}});
    const int gear = car.gear;
    const char gear_label = gear == kVehicleGearReverse ? 'R' :
        gear == kVehicleGearNeutral ? 'N' :
        static_cast<char>('0'+std::max(1,std::min(6,gear-kVehicleGearFirst+1)));
    const auto gear_pos=pos(1214,581);
    digit(frame,gear_label,gear_pos[0],gear_pos[1],48*scale,{{48,188,245}});
    place(kSpeedBox,1125,650,164,41,{{255,255,255}},140);
    // The port's vehicle state has no retail +0x3D4 scalar. Horizontal
    // velocity magnitude is the closest available visual-only estimate.
    const double mps = vehicle_sqrt(car.velocity.x*car.velocity.x+
                                    car.velocity.y*car.velocity.y);
    const int mph = static_cast<int>(std::min(999.0,mps*2.237));
    char speed[4]{};
    speed[0] = static_cast<char>('0'+mph/100);
    speed[1] = static_cast<char>('0'+(mph/10)%10);
    speed[2] = static_cast<char>('0'+mph%10);
    const int first = mph >= 100 ? 0 : mph >= 10 ? 1 : 2;
    const auto speed_pos=pos(1160-(2-first)*22.0F,650);
    for (int i=first;i<3;++i)
        digit(frame,speed[i],speed_pos[0]+(i-first)*23*scale,
              speed_pos[1],35*scale,{{35,178,235}});
    place(kUnit,1203,632,55,14,{{190,223,242}});
}

void composite_race_hud(const RaceHudAtlas& atlas, const RaceHudFrame& frame,
                         unsigned char* rgba, std::uint32_t width,
                         std::uint32_t height, std::uint32_t pitch) noexcept {
    if (!rgba || atlas.rgba.size() != kRaceHudAtlasSize*kRaceHudAtlasSize*4) return;
    for (std::size_t q=0;q<frame.quads;++q) {
        const auto* v=&frame.vertices[q*4];
        const float min_x=std::min({v[0].px,v[1].px,v[2].px,v[3].px});
        const float max_x=std::max({v[0].px,v[1].px,v[2].px,v[3].px});
        const float min_y=std::min({v[0].py,v[1].py,v[2].py,v[3].py});
        const float max_y=std::max({v[0].py,v[1].py,v[2].py,v[3].py});
        const int left=std::max(0,static_cast<int>(std::floor(min_x)));
        const int right=std::min(static_cast<int>(width),static_cast<int>(std::ceil(max_x)));
        const int top=std::max(0,static_cast<int>(std::floor(min_y)));
        const int bottom=std::min(static_cast<int>(height),static_cast<int>(std::ceil(max_y)));
        for (int y=top;y<bottom;++y) for (int x=left;x<right;++x) {
            for (int tri=0;tri<2;++tri) {
                const auto& a=v[0];
                const auto& b=v[tri==0?1:2];
                const auto& c=v[tri==0?2:3];
                const float px=x+0.5F,py=y+0.5F;
                const float det=(b.py-c.py)*(a.px-c.px)+(c.px-b.px)*(a.py-c.py);
                if (std::abs(det)<0.0001F) continue;
                const float w0=((b.py-c.py)*(px-c.px)+(c.px-b.px)*(py-c.py))/det;
                const float w1=((c.py-a.py)*(px-c.px)+(a.px-c.px)*(py-c.py))/det;
                const float w2=1.0F-w0-w1;
                if (w0<0 || w1<0 || w2<0) continue;
                const float u=w0*a.u+w1*b.u+w2*c.u;
                const float vcoord=w0*a.v+w1*b.v+w2*c.v;
                const int tx=std::max(0,std::min(511,static_cast<int>(u*512)));
                const int ty=std::max(0,std::min(511,static_cast<int>(vcoord*512)));
                const auto* source=atlas.rgba.data()+(ty*512+tx)*4;
                auto* target=rgba+y*pitch+x*4;
                const unsigned alpha=(unsigned(source[3])*unsigned(v[0].color[3])+127)/255;
                for (int k=0;k<3;++k) {
                    const unsigned tint=(unsigned(v[0].color[k])*unsigned(source[k])+127)/255;
                    target[k]=static_cast<unsigned char>((tint*alpha+
                        unsigned(target[k])*(255-alpha)+127)/255);
                }
                target[3]=255;
                break;
            }
        }
    }
}

} // namespace nfsu
