#include "nfsu/race_hud.h"
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
constexpr AtlasRect kFill{0, 256, 256, 128};
constexpr AtlasRect kNeedle{256, 256, 16, 128};
constexpr AtlasRect kLcd{272, 256, 128, 128};
constexpr AtlasRect kUnit{400, 256, 42, 14};
struct HudGlyph {
    AtlasRect uv;
    int width, height, bearing_x, bearing_y, advance;
};
// English.bin FNTF records, with the one-texel UV border used at 0x004F4790.
constexpr std::array<HudGlyph,3> kUnits{{
    {{400,256,15,14},13,12,0,5,13},
    {{415,256,13,14},11,12,0,5,11},
    {{428,256,14,14},12,12,0,5,11}
}};

struct SourceSpec {
    const char* path;
    std::uint32_t hash;
    AtlasRect rect;
};
constexpr std::array<SourceSpec, 15> kSources{{
    {"Global/INGAMEB.BUN", 0x40FAB387, kNeedle},
    {"Global/INGAMEB.BUN", 0x9107116C, kFill},
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

bool copy_units(const std::vector<unsigned char>& bytes,
                const TexturePackSet& set, RaceHudAtlas& atlas,
                std::string& error) {
    // Conduit 14 Italic matches the original stock MPH letterforms. These
    // padded rectangles come from English.bin's FNTF, not an image fit.
    constexpr std::array<AtlasRect,3> letters{{
        {112,32,15,14}, {22,32,13,14}, {75,45,14,14}
    }};
    for (const auto& pack : set.packs) for (const auto& record : pack.textures) {
        if (record.hash != 0x5B9D84DB) continue;
        if (!record.valid || record.width != 256 || record.height != 256) break;
        std::vector<unsigned char> payload, rgba;
        std::size_t unused=0; std::uint64_t top=0;
        if (!texture_payload(bytes.data(),bytes.size(),record,payload,unused,error) ||
            !level_size(record.format,256,256,top) || top>payload.size() ||
            !decode_level(record.format,256,256,payload.data(),
                          static_cast<std::size_t>(top),rgba,error)) return false;
        int x=kUnit.x;
        for (const auto& letter : letters) {
            for (int y=0; y<letter.h; ++y)
                std::memcpy(atlas.rgba.data()+((kUnit.y+y)*512+x)*4,
                            rgba.data()+((letter.y+y)*256+letter.x)*4,
                            static_cast<std::size_t>(letter.w)*4);
            x+=letter.w;
        }
        return true;
    }
    error="Languages/LanguageTextures.bin: required HUD units font absent or changed";
    return false;
}

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

HudGlyph glyph(char ch) noexcept {
    const auto lcd=[](int x,int y,int w,int h,int bx,int by,int advance) {
        return HudGlyph{{kLcd.x+x-1,kLcd.y+y-1,w+2,h+2},w,h,bx,by,advance};
    };
    switch (ch) {
    case '1': return lcd(104,36,13,35, 2,9,12);
    case '2': return lcd(53,1,26,34, -1,9,22);
    case '3': return lcd(80,1,24,34, -1,9,21);
    case '4': return lcd(49,72,23,36, 2,8,23);
    case '5': return lcd(28,36,23,34, 1,9,22);
    case '6': return lcd(52,36,24,34, 0,9,23);
    case '7': return lcd(1,72,20,35, 2,9,19);
    case '8': return lcd(1,36,26,34, 0,9,24);
    case '9': return lcd(28,1,24,34, 1,9,23);
    case 'N': return lcd(22,72,26,36, 0,8,24);
    case 'R': return lcd(77,36,26,35, 0,9,24);
    default: return lcd(1,1,26,34, 0,9,24);
    }
}

void glyph_quad(RaceHudFrame& frame, const HudGlyph& letter, float x, float y,
                float scale, std::array<unsigned char, 3> tint) noexcept {
    rect(frame, letter.uv,x+letter.bearing_x*scale,y+letter.bearing_y*scale,
         letter.width*scale,letter.height*scale,tint);
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
        if (std::strcmp(path,"Languages/LanguageTextures.bin")==0 &&
            !copy_units(bytes,set,atlas,error)) return false;
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
    const double mps=vehicle_sqrt(car.velocity.x*car.velocity.x+
                                  car.velocity.y*car.velocity.y);
    frame.readout={car.step,car.engine_rpm,car.wheel_rpm,max_rpm,redline_rpm,
        race_hud_needle_rotation(car.engine_rpm,max_rpm),car.gear,
        static_cast<int>(std::min(999.0,mps*2.237))};
    if (!width || !height) return;
    // Preserve the 720p presentation canvas. Reference 1280x800 captures are
    // compared at a common bottom edge (an 80px crop), not stretched. The
    // absolute anchor and texel scale are measured fits; local offsets are
    // the HUD_SingleRace.fng SAT/SAD values (docs/HUD_253.md).
    const float scale = std::min(width/1280.0F,height/720.0F);
    constexpr float texel=5.0F/3.0F;
    const float pivot_x=width-147.0F*scale;
    const float pivot_y=height-119.0F*scale;
    const auto pos = [&](float x, float y) {
        return std::array<float,2>{{pivot_x+x*texel*scale,
                                     pivot_y+y*texel*scale}};
    };
    const auto place = [&](const AtlasRect& uv, float x, float y, float w, float h,
                           std::array<unsigned char,3> tint,
                           unsigned char opacity = 255) {
        const auto p=pos(x,y); rect(frame,uv,p[0],p[1],w*texel*scale,h*texel*scale,tint,opacity);
    };
    const auto dial=select_race_hud_dial(max_rpm,redline_rpm);
    const auto line=std::find(kLinesHashes.begin(),kLinesHashes.end(),dial.lines_hash);
    const auto red=std::find(kRedHashes.begin(),kRedHashes.end(),dial.red_hash);
    // The full 256x128 LINES sprite is centered at the same FEng position
    // as 3rdPersonTach. Only its left 128x128 texels contain the face.
    // The decoded face's (69,70) hub is one native texel above the pivot.
    constexpr float face_x=-69.0F;
    constexpr float face_y=-71.0F;
    const auto& slot=kLines[static_cast<std::size_t>(line-kLinesHashes.begin())];
    // The screenshot-matched third-person fill replaces the invented disc,
    // supplemental hub and unrelated speed-counter box. rpm_fill SAT alpha
    // is 150 and its centre differs from Tach by less than 0.002 texels.
    place(kFill,face_x,face_y,256,128,{{255,255,255}},150);
    // Preserve the fill/RED registration. Stored SAT z values do not by
    // themselves establish the runtime painter order of GaugeCluster.
    if (red!=kRedHashes.end())
        place(kRed[static_cast<std::size_t>(red-kRedHashes.begin())],
              face_x+79.0F,face_y+6.0F,64,32,
              {{150,0,0}},150);
    // GaugeCluster stores Tach before Needle. Draw scale marks first so the
    // needle crosses above them, as in the original HUD.
    place(slot,face_x,face_y,256,128,{{0,128,192}},255);
    // The FEng needle is centered on the face hub. Its ring surrounds source
    // row 64 and its shaft tip is near row 125, in the +v direction.
    // 0x004F6A70 rotates the sprite's +v axis to 270 - rotation in y-down
    // screen space. At zero RPM the stock 44-degree input yields 226 degrees.
    const double rotation=frame.readout.rotation;
    const double dial_angle=(270.0-rotation)*3.14159265358979323846/180.0;
    const auto center=pos(0,0);
    rotated(frame,kNeedle,center[0],center[1],16.0F*texel*scale,
            128.0F*texel*scale,8.0F*texel*scale,64.0F*texel*scale,
            4.7123889803846899-dial_angle,{{207,233,242}});
    const int gear = car.gear;
    const char gear_label = gear == kVehicleGearReverse ? 'R' :
        gear == kVehicleGearNeutral ? 'N' :
        static_cast<char>('0'+std::max(1,std::min(6,gear-kVehicleGearFirst+1)));
    const float gear_scale=0.85F*texel*scale; // SAD 0x4680
    const auto gear_glyph=glyph(gear_label);
    const auto gear_pos=pos(59,-26);
    glyph_quad(frame,gear_glyph,gear_pos[0]-gear_glyph.advance*gear_scale,
               gear_pos[1],gear_scale,{{0,128,192}});
    // The port's vehicle state has no retail +0x3D4 scalar. Horizontal
    // velocity magnitude is the closest available visual-only estimate.
    const int mph=frame.readout.mph;
    char speed[4]{};
    speed[0] = static_cast<char>('0'+mph/100);
    speed[1] = static_cast<char>('0'+(mph/10)%10);
    speed[2] = static_cast<char>('0'+mph%10);
    const int first = mph >= 100 ? 0 : mph >= 10 ? 1 : 2;
    const float speed_scale=0.6F*texel*scale; // SAD 0x42F0
    float speed_width=0;
    for (int i=first;i<3;++i) speed_width+=glyph(speed[i]).advance;
    const auto speed_pos=pos(24.39F,25.82F);
    float x=speed_pos[0]-speed_width*speed_scale;
    for (int i=first;i<3;++i) {
        const auto letter=glyph(speed[i]);
        glyph_quad(frame,letter,x,speed_pos[1],speed_scale,{{0,128,192}});
        x+=letter.advance*speed_scale;
    }
    // Right alignment matches the original stock captures; native FNTF
    // bearings/advances and SAD 0x43EC scale replace the bold-font fit.
    const float units_scale=0.8F*texel*scale;
    const auto units_pos=pos(63.64F,16.57F);
    float units_width=0;
    for (const auto& letter : kUnits) units_width+=letter.advance;
    x=units_pos[0]-units_width*units_scale;
    for (const auto& letter : kUnits) {
        glyph_quad(frame,letter,x,units_pos[1],units_scale,{{150,190,255}});
        x+=letter.advance*units_scale;
    }
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
