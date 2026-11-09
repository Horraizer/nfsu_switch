#include "nfsu/race_hud.h"
#include "nfsu/player_powertrain.h"
#include "nfsu/texture.h"
#include "nfsu/part_placement.h"
#include "nfsu/race_events.h"
#include "nfsu/race_lap_hud_presentation.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <cstdio>
#include <fstream>
#include <stdexcept>

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
constexpr AtlasRect kLapBacking{0,448,256,64};
constexpr AtlasRect kLapWhite{444,256,16,16};
constexpr AtlasRect kDriftBack{840,40,128,64},kDriftDegrees{840,104,64,64},
                    kDriftMeter{904,104,64,64},kDriftNeedle{968,104,16,64};
constexpr AtlasRect kNosText{704,336,64,64};
constexpr AtlasRect kTurboFill{512,270,128,128}, kTurboLines{640,270,64,128};
constexpr AtlasRect kTurboNeedle{704,270,16,64}, kTurboRed1{720,270,32,32}, kTurboRed2{752,270,32,32};
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
                std::memcpy(atlas.rgba.data()+((kUnit.y+y)*kRaceHudAtlasWidth+x)*4,
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
                ((source.rect.y + y) * kRaceHudAtlasWidth + source.rect.x) * 4;
            std::memcpy(dst, src, static_cast<std::size_t>(source.rect.w) * 4);
        }
        return true;
    }
    error = std::string(source.path) + ": required HUD texture absent";
    return false;
}

bool load_countdown(const std::vector<unsigned char>& bytes,RaceHudAtlas& atlas,bool drift,std::string& error) {
    const std::size_t at=drift?0x17140c:0x16f40c;
    const auto u32=[](const unsigned char* p){return p[0]|std::uint32_t(p[1])<<8|std::uint32_t(p[2])<<16|std::uint32_t(p[3])<<24;};
    const auto real=[&](const unsigned char* p){const auto b=u32(p);float f;std::memcpy(&f,&b,4);return f;};
    if(bytes.size()<at+16 || std::memcmp(bytes.data()+at,"JDLZ",4) || u32(bytes.data()+at+12)>bytes.size()-at){error="countdown FNG absent";return false;}
    std::vector<unsigned char> fng;std::size_t used=0;
    if(!jdlz_decompress(bytes.data()+at,u32(bytes.data()+at+12),1024*1024,fng,used,error))return false;
    unsigned found=0,comments=0;
    for(std::size_t p=0;p+16<=fng.size();++p) {
        const auto* v=fng.data()+p;if(std::memcmp(v,"FOb\xEA",4))continue;
        const auto outer=u32(v+4),n=u32(v+12);
        if(outer>fng.size()-p-8 || outer<8 || n>outer-8 || std::memcmp(v+8,"ObjD",4)){error="countdown object bounds";return false;}
        bool selected=false,comment=false,geometry=false;RaceDragHudObject object;
        for(std::size_t a=16;a<16+n;) {
            if(16+n-a<4){error="countdown field header";return false;}
            const unsigned len=v[a+2]|unsigned(v[a+3])<<8;const auto* value=v+a+4;
            if(len>16+n-a-4){error="countdown field bounds";return false;}
            if(!std::memcmp(v+a,"On",2)){
                selected=len>=7 && !std::memcmp(value,"321_Go",7);
                comment=drift&&len>=13&&!std::memcmp(value,"Comment_DATA",13);
            }
            if(!std::memcmp(v+a,"SA",2) && len>=68) {
                object.position={real(value+28),real(value+32)};
                object.width=real(value+56);object.height=real(value+60);
                for(unsigned k=0;k<4;++k)object.color[k]=static_cast<unsigned char>(std::min(255U,u32(value+(k<3?2-k:3)*4)));
                geometry=true;
            }
            a+=4+len;
        }
        if(selected){if(!geometry || ++found!=1){error="countdown geometry absent or ambiguous";return false;}atlas.countdown=object;}
        if(comment){if(!geometry || ++comments!=1){error="drift comment geometry absent or ambiguous";return false;}atlas.drift_comment=object;}
        p+=7+outer;
    }
    if(drift&&comments!=1){error="Comment_DATA absent";return false;}
    atlas.countdown_ready=found==1;if(!atlas.countdown_ready)error="321_Go absent";
    return atlas.countdown_ready;
}
bool load_lap_fonts(const std::filesystem::path& root, RaceHudAtlas& atlas,
                    std::string& error,bool drag=false,bool knockout=false) {
    std::ifstream stream(root/"Languages/English.bin",std::ios::binary);
    if (!stream) { error="lap HUD English font file absent"; return false; }
    const std::vector<unsigned char> language{std::istreambuf_iterator<char>(stream),{}};
    const auto u32=[](const unsigned char* p) {
        return std::uint32_t(p[0]) | std::uint32_t(p[1])<<8 |
               std::uint32_t(p[2])<<16 | std::uint32_t(p[3])<<24;
    };
    std::vector<unsigned char> bytes;
    const auto packs=load_texture_pack_set(root/"Languages/LanguageTextures.bin",bytes);
    if (!packs.complete || !packs.ok()) { error="lap HUD language texture pack invalid"; return false; }
    constexpr const char* names[]={"conduitmditc_tt14i","font_conduitmditctt38bi","lcd_let48"};
    constexpr const char* needed[]={" 0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz:./-?+!",
                                    " 0123456789/Lapstndrh+-xRceOvMPHGO!", "0123456789NR"};
    // The full name alphabet uses the free bottom strip. This lies below
    // every turbo/NOS image and to the right of the lap backing.
    int pack_x=256,pack_y=400,row_height=0;
    for (unsigned f=0;f<3;++f) {
        RaceLapHudFont font;
        for (std::size_t at=0;at+8<=language.size();) {
            const auto size=u32(language.data()+at+4);
            if (size>language.size()-at-8) { error="lap HUD font chunk truncated"; return false; }
            const auto* p=language.data()+at+8;
            if (u32(language.data()+at)==0x30201 && size>=640 &&
                std::memcmp(p,names[f],std::strlen(names[f])+1)==0) {
                const auto* end=static_cast<const unsigned char*>(std::memchr(p+256,0,256));
                if (!end) { error="lap HUD font texture name invalid"; return false; }
                std::string name(p+256,end);
                for (auto& c:name) if (c>='a' && c<='z') c=char(c-'a'+'A');
                if (!decode_lap_hud_fntf(p+512,size-512,bin_hash(name),float(p[530]+p[531]),0,1,font)) {
                    error="lap HUD FNTF invalid"; return false;
                }
                break;
            }
            at+=8+size;
        }
        if (!font.known) { error="lap HUD required English font absent"; return false; }
        std::vector<unsigned char> rgba;
        unsigned tw=0,th=0;
        for (const auto& pack:packs.packs) for (const auto& t:pack.textures) {
            if (t.hash!=font.texture_hash || !t.valid) continue;
            std::vector<unsigned char> payload; std::size_t unused=0; std::uint64_t top=0;
            if (!texture_payload(bytes.data(),bytes.size(),t,payload,unused,error) ||
                !level_size(t.format,t.width,t.height,top) || top>payload.size() ||
                !decode_level(t.format,t.width,t.height,payload.data(),std::size_t(top),rgba,error)) return false;
            tw=t.width; th=t.height;
        }
        if (rgba.empty()) { error="lap HUD font texture absent"; return false; }
        atlas.lap_font_heights[f]=font.line_height;
        auto glyphs=std::string(needed[f])+(f==1 && atlas.drift_ready?"XTO SLOW!Wg.y":"")+
            (f==1 && drag?"Gof!SiTotaledBlownEngK":"")+(f==1 && knockout?"Youwk":"");
        // Comment_DATA uses the large font too; keep all earlier atlas UVs.
        if(f==1&&atlas.drift_ready)for(char c:std::string("GOOD DRIFTGREAT SUPERB!COLOSSAL!OUTRAGEOUS!INSANE DRIFT!!DRIFT KING!!"))
            if(glyphs.find(c)==std::string::npos)glyphs+=c;
        std::array<bool,128> packed{};
        for (const char* c=glyphs.c_str();*c;++c) {
            if(atlas.drift_ready){if(packed[unsigned(*c)])continue;packed[unsigned(*c)]=true;}
            const auto it=std::find_if(font.glyphs.begin(),font.glyphs.end(),
                [&](const RaceLapHudGlyph& g){return g.code==unsigned(*c);});
            if (it==font.glyphs.end()) { error="lap HUD glyph absent"; return false; }
            const auto& g=*it;
            auto& out=atlas.lap_fonts[f][unsigned(*c)];
            out={0,0,int(g.width),int(g.height),g.bearing_x,g.bearing_y,g.advance};
            if (!g.width || !g.height) continue;
            if (g.x+g.width>tw || g.y+g.height>th) { error="lap HUD glyph outside texture"; return false; }
            if (f==2) { out.x=kLcd.x+g.x-1; out.y=kLcd.y+g.y-1; continue; }
            const int w=g.width+2,h=g.height+2;
            if (pack_x+w>kRaceHudAtlasWidth) { pack_x=256;pack_y+=row_height;row_height=0; }
            if (pack_y+h>512) { error="lap HUD atlas capacity exceeded"; return false; }
            out.x=pack_x;out.y=pack_y;
            for (int y=0;y<h;++y) for (int x=0;x<w;++x) {
                const int sx=std::clamp(int(g.x)+x-1,0,int(tw)-1);
                const int sy=std::clamp(int(g.y)+y-1,0,int(th)-1);
                std::memcpy(atlas.rgba.data()+((pack_y+y)*kRaceHudAtlasWidth+pack_x+x)*4,
                            rgba.data()+(sy*tw+sx)*4,4);
            }
            pack_x+=w;row_height=std::max(row_height,h);
        }
    }
    atlas.lap_ready=true;
    return true;
}

void add_quad(RaceHudFrame& frame, const AtlasRect& uv,
              const std::array<std::array<float, 2>, 4>& corners,
              std::array<unsigned char, 3> tint,
              unsigned char opacity = 255) noexcept {
    if (frame.quads == kRaceHudMaxQuads) return;
    const float u0 = float(uv.x) / kRaceHudAtlasWidth;
    const float v0 = float(uv.y) / frame.atlas_height;
    const float u1 = float(uv.x + uv.w) / kRaceHudAtlasWidth;
    const float v1 = float(uv.y + uv.h) / frame.atlas_height;
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

float race_hud_turbo_rotation(float pressure,bool drag) noexcept {
    const float first=drag?300.F:30.F,last=drag?60.F:219.F;
    const float arc=last<=first?360.F-first+last:last-first;
    long double angle=(static_cast<long double>(pressure)+1.F)*.5F*arc+first;
    if(angle>360.F)angle-=360.F;
    if(angle<0)angle=360.F-angle;
    return static_cast<float>(angle);
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
                         RaceHudAtlas& atlas, std::string& error, bool first_track_map, bool boost, bool drift, unsigned map_track, bool knockout) {
    atlas={};
    atlas.rgba.assign(kRaceHudAtlasWidth*kRaceHudAtlasSize*4, 0);
    for (const char* path : {"Global/INGAMEB.BUN", "Global/DYNTEX.BIN", "Global/GLOBALB.BUN",
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
        if (std::strcmp(path,"Global/INGAMEB.BUN")==0 && !load_countdown(bytes,atlas,drift,error))return false;
        if (std::strcmp(path,"Global/INGAMEB.BUN")==0 &&
            !copy_texture(bytes,set,{path,bin_hash("3RDPERSON_BOXCORNER"),kLapBacking},atlas,error)) return false;
        if (std::strcmp(path,"Global/GLOBALB.BUN")==0 &&
            !copy_texture(bytes,set,{path,bin_hash("WHITE16X16"),kLapWhite},atlas,error)) return false;
        if (std::strcmp(path,"Languages/LanguageTextures.bin")==0 &&
            !copy_units(bytes,set,atlas,error)) return false;
    }
    if(drift) {
        std::vector<unsigned char> bytes;
        const auto packs=load_texture_pack_set(root/"Global/INGAMEB.BUN",bytes);
        if(!packs.complete||!packs.ok()){error="drift HUD pack invalid";return false;}
        for(const auto& source:std::array<SourceSpec,4>{{
            {"Global/INGAMEB.BUN",bin_hash("3RDPERSON_DRIFTANGLE_ANGLE_BACKING"),kDriftBack},
            {"Global/INGAMEB.BUN",bin_hash("3RDPERSON_DRIFTANGLE_ANGLE_DEGREES"),kDriftDegrees},
            {"Global/INGAMEB.BUN",bin_hash("3RDPERSON_DRIFTANGLE_FILLER"),kDriftMeter},
            {"Global/INGAMEB.BUN",bin_hash("3RDPERSON_TURBODIAL"),kDriftNeedle}}})
            if(!copy_texture(bytes,packs,source,atlas,error))return false;
        if(!load_drift_hud_animations(bytes,atlas.drift_animations,error))return false;
        atlas.drift_ready=true;
    }
    if (first_track_map || drift) {
        const unsigned track=map_track?map_track:drift?1301:1003;
        if (!load_minimap_resource(root,atlas.map,error,track)) return false;
        std::vector<unsigned char> bytes;
        const auto map_name="Tracks/TEX"+std::to_string(track)+"TRACK.BIN";
        const char* map_file=map_name.c_str();
        const auto packs=load_texture_pack_set(root/map_file,bytes);
        if (!packs.complete || !packs.ok() ||
            !copy_texture(bytes,packs,{map_file,atlas.map.hash,{513,1,256,256}},atlas,error)) return false;
        // One texel of periodic padding isolates linear WRAP from other HUD
        // artwork. These are copies of the original map pixels, not a mask.
        for (int y=0;y<258;++y) for (int x=0;x<258;++x) {
            if (x>0 && x<257 && y>0 && y<257) continue;
            const int sx=513+(x+255)%256,sy=1+(y+255)%256;
            std::memcpy(atlas.rgba.data()+((y)*kRaceHudAtlasWidth+512+x)*4,
                        atlas.rgba.data()+(sy*kRaceHudAtlasWidth+sx)*4,4);
        }
        for (const auto& source : std::array<SourceSpec,2>{{
                {"Global/INGAMEB.BUN",0x7E22DED8,{780,1,32,32}},
                {"Global/INGAMEB.BUN",0x72974A14,{820,1,16,16}}}}) {
            const auto markers=load_texture_pack_set(root/source.path,bytes);
            if (!markers.complete || !markers.ok() || !copy_texture(bytes,markers,source,atlas,error)) return false;
        }
    }
    if (boost) {
        std::vector<unsigned char> bytes;
        const auto packs=load_texture_pack_set(root/"Global/INGAMEB.BUN",bytes);
        if (!packs.complete || !packs.ok()) { error="boost HUD pack invalid";return false; }
        for (const auto& source:std::array<SourceSpec,6>{{
            {"Global/INGAMEB.BUN",bin_hash("SR_NOS_TEXT"),kNosText},
            {"Global/INGAMEB.BUN",bin_hash("3RDPERSON_TURBOFILL"),kTurboFill},
            {"Global/INGAMEB.BUN",bin_hash("3RDPERSON_TURBOLINES"),kTurboLines},
            {"Global/INGAMEB.BUN",bin_hash("3RDPERSON_TURBODIAL"),kTurboNeedle},
            {"Global/INGAMEB.BUN",bin_hash("3RDPERSON_TURBORED_1"),kTurboRed1},
            {"Global/INGAMEB.BUN",bin_hash("3RDPERSON_TURBORED_2"),kTurboRed2}}})
            if (!copy_texture(bytes,packs,source,atlas,error)) return false;
        for (unsigned k=0;k<6;++k) {
            const auto name="SR_NOS_BAR6_"+std::to_string(k+1);
            if (!copy_texture(bytes,packs,{"Global/INGAMEB.BUN",bin_hash(name),{800+int(k)*32,270,32,32}},atlas,error)) return false;
        }
        for (unsigned k=0;k<2;++k) {
            const auto name="SR_NOS_BAR2_"+std::to_string(k+1);
            if (!copy_texture(bytes,packs,{"Global/INGAMEB.BUN",bin_hash(name),{800+int(k)*64,310,64,64}},atlas,error)) return false;
        }
        atlas.boost_ready=true;
    }
    return load_lap_fonts(root,atlas,error,false,knockout);
}

void append_race_over(const RaceHudAtlas& atlas,double seconds,unsigned width,unsigned height,RaceHudFrame& frame,std::string_view message) noexcept {
    if(!atlas.lap_ready || !width || !height || !std::isfinite(seconds) || seconds<0)return;
    // HUD_SingleRace RaceOverMessage/RaceOverBacking, original4A7E1F
    // Animate state. Keys are base + delta, at the FEng 1200 Hz clock.
    const float tick=float(std::min(seconds*1200,700.0));
    const auto sample=[&](const auto& keys) {
        if(tick<=keys.front()[0])return keys.front()[1];
        for(std::size_t i=1;i<keys.size();++i)if(tick<keys[i][0]) {
            const float t=(tick-keys[i-1][0])/(keys[i][0]-keys[i-1][0]);
            return keys[i-1][1]+t*(keys[i][1]-keys[i-1][1]);
        }
        return keys.back()[1];
    };
    constexpr std::array<std::array<float,2>,7> sizes{{{{240,0}},{{260,-.5F}},{{340,0}},{{420,-.3F}},{{460,.1F}},{{520,.1F}},{{560,0}}}};
    constexpr std::array<std::array<float,2>,3> alpha{{{{240,-255}},{{260,-255}},{{340,0}}}};
    constexpr std::array<std::array<float,2>,7> backing{{{{0,-631.707946777F}},{{60,-505.366333008F}},{{100,-546.939941406F}},{{140,-505.369995117F}},{{180,-589.322509766F}},{{220,0}},{{300,0}}}};
    const float scale=std::min(width/1280.F,height/720.F)*5.F/3.F;
    const float bw=650+sample(backing);
    if(frame.lap_first_quad==kRaceHudMaxQuads)frame.lap_first_quad=frame.quads;
    rect(frame,kLapWhite,float(width)*.5F+(1.24171447754F-bw*.5F)*scale,
        (244-65)*scale,bw*scale,130*scale,{{0,0,0}},250);
    const float size=(.75F+sample(sizes))*scale;
    float advance=0;
    for(const unsigned char c:message)if(c)advance+=atlas.lap_fonts[1][c].advance;
    float x=float(width)*.5F-advance*size*.5F;
    const auto opacity=static_cast<unsigned char>(std::clamp(int(255+sample(alpha)+.5F),0,255));
    // Resource0: Conduit38 Bold Italic, centered horizontally and vertically.
    const float y=243*scale-38*size*.5F;
    for(const unsigned char c:message)if(c) {
        const auto& g=atlas.lap_fonts[1][c];
        if(g.width && g.height) {
            const auto q=frame.quads;
            rect(frame,{g.x,g.y,g.width+2,g.height+2},x+g.bearing_x*size,y+g.bearing_y*size,
                std::max(4,g.width)*size,g.height*size,{{194,146,0}},opacity);
            if(frame.quads>q)for(unsigned v=0;v<4;++v) {
                frame.vertices[q*4+v].u+=(v==0||v==3?.5F:-.5F)/kRaceHudAtlasWidth;
                frame.vertices[q*4+v].v+=(v<2?.5F:-.5F)/kRaceHudAtlasSize;
            }
        }
        x+=g.advance*size;
    }
}

static void build_race_hud_geometry(const VehicleState&,double,double,std::uint32_t,std::uint32_t,
                                    RaceHudFrame&,const RaceHudAtlas*) noexcept;
static void build_race_hud_base(const RaceSession& session,
                                std::uint32_t width,std::uint32_t height,RaceHudFrame& frame,
                                const RaceHudAtlas* metric_atlas) noexcept {
    const auto& handling = session.spec().handling;
    build_race_hud_geometry(session.car(), handling.max_rpm, handling.redline_rpm,
                            width,height,frame,metric_atlas);
    const auto snapshot = race_lap_hud_snapshot(session.course().route, session.timing(), 0);
    build_race_lap_hud_panel(snapshot, RaceLapHudStyle::unknown, frame.lap);
}
void build_race_hud_frame(const RaceSession& session,
                          std::uint32_t width,std::uint32_t height,RaceHudFrame& frame) noexcept {
    build_race_hud_base(session,width,height,frame,nullptr);
}

namespace {
void append_drift_hud(const RaceSession& session,const RaceHudAtlas& atlas,
                      unsigned width,unsigned height,RaceHudFrame& frame) noexcept {
    if(!atlas.drift_ready)return;
    const auto& drift=session.drift();const auto& chain=session.style().item(0);
    auto& clock=frame.drift_clock;const auto now=session.timing().now_ticks;
    if(!clock.initialized || now<clock.now) {
        clock={};clock.initialized=true;clock.multiplier=drift.multiplier();clock.total=chain.total;
        clock.slow_at=now;clock.wrong_at=now-100000;
    }
    if(drift.multiplier()>clock.multiplier)clock.multiplier_at=now;
    if(chain.total>clock.total)clock.total_at=now;
    if(drift.warning()!=clock.warning || drift.recovering()!=clock.recovering)clock.slow_at=now;
    const bool wrong=drift.reset_countdown()>0;
    if(wrong!=clock.wrong_way)clock.wrong_at=now;
    clock.now=now;clock.multiplier=drift.multiplier();clock.total=chain.total;
    clock.warning=drift.warning();clock.recovering=drift.recovering();clock.wrong_way=wrong;
    const auto elapsed=[&](std::int32_t from){return unsigned(std::max<std::int64_t>(0,std::int64_t(now)-from)*1200/kRaceTicksPerSecond);};
    const auto total=sample_drift_hud_animation(atlas,"SP_DRIFT_TOTAL","Highlight",elapsed(clock.total_at));
    const auto total_label=sample_drift_hud_animation(atlas,"Style_TotalDrift_Title","Highlight",elapsed(clock.total_at));
    const float scale=std::min(width/1280.F,height/720.F)*5.F/3.F;
    const auto box=[&](AtlasRect uv,float x,float y,float w,float h,
                       std::array<unsigned char,3> color,unsigned char alpha) {
        rect(frame,uv,(320+x-w*.5F)*scale,(240+y-h*.5F)*scale,w*scale,h*scale,color,alpha);
    };
    const auto text=[&](const char* value,unsigned font,float x,float y,float size,
                        unsigned align,std::array<unsigned char,3> color,unsigned char alpha=254) {
        float advance=0;for(const unsigned char* c=reinterpret_cast<const unsigned char*>(value);*c;++c)
            advance+=atlas.lap_fonts[font][*c<128?*c:'?'].advance;
        const float k=size*scale;
        x=(320+x)*scale-advance*k*((align&2)?1.F:(align&1)?.5F:0.F);y=(240+y)*scale;
        // FEng Sj vertical center/bottom use the font line height (4F48B0).
        y-=atlas.lap_font_heights[font]*k*((align&4)?.5F:(align&8)?1.F:0.F);
        for(const unsigned char* c=reinterpret_cast<const unsigned char*>(value);*c;++c) {
            const auto& g=atlas.lap_fonts[font][*c<128?*c:'?'];
            if(g.width&&g.height)rect(frame,{g.x,g.y,g.width+2,g.height+2},
                x+g.bearing_x*k,y+g.bearing_y*k,std::max(4,g.width)*k,g.height*k,color,alpha);
            x+=g.advance*k;
        }
    };
    constexpr std::array<unsigned char,3> blue{{150,190,255}},cyan{{0,128,192}},
        yellow{{255,255,0}};
    frame.lap_first_quad=frame.quads;
    const auto snapshot=race_lap_hud_snapshot(session.course().route,session.timing(),0,true,session.time_limit_ticks());
    build_race_lap_hud_panel(snapshot,RaceLapHudLanguage{true,0},frame.lap);
    // HUD_Drift.fng: best drift score and banked drift total.
    box(kLapBacking,-178,-192,256,64,{{0,0,0}},214);
    box(kLapWhite,-221,-159,156,20,{{0,0,0}},254);
    box(kLapWhite,-221,-138,156,18,cyan,200);
    text(frame.lap.fields[5].text.data(),2,-237,-220,1,2,blue);
    text(frame.lap.fields[4].text.data(),1,-226,-218,.5F,0,cyan);
    text("Laps",1,-230,-197,.4F,0,cyan);
    text("Best:",0,-296,-169,.9F,0,blue);
    char best[24];std::snprintf(best,sizeof best,"%d",int(session.style().item(0).best));
    text(best,0,-151,-169,.9F,2,blue);
    text("Total:",0,-296.25F,-148,.9F,0,total_label.color,total_label.alpha);
    char value[48];std::snprintf(value,sizeof value,"%d",int(session.style().item(0).total));
    text(value,0,-151.25F,-148,.9F,2,total.color,total.alpha);
    // Current chain and original multiplier panel (FEng parent -198,-80).
    if(wrong || elapsed(clock.wrong_at)<200) {
        const auto age=elapsed(clock.wrong_at);
        const char* state=wrong?(age<600?"FadeIn":"Blink"):"FadeOut";
        const auto t=wrong && age>=600?(age-600)%800:age;
        const auto appearance=sample_drift_hud_animation(atlas,"WrongWay_CountDown",state,t);
        const auto label=sample_drift_hud_animation(atlas,"WrongWayText",state,t);
        text("Wrong Way",1,0,-107,label.scale,1,label.color,label.alpha);
        std::snprintf(value,sizeof value,"%1.1f",drift.reset_countdown());
        text(value,1,0,-70,appearance.scale,1,appearance.color,appearance.alpha);
    }
    const auto& publication=session.style().drift_publication();
    if(publication.sequence!=clock.score.sequence&&publication.failed) {
        clock.comment_hidden=true;clock.comment_hidden_at=publication.published;
    }
    clock.score.sample(publication,now,drift.zone());
    if(clock.score.visible) {
        const auto& color=clock.score.color;
        const std::array<unsigned char,3> tint{{color[0],color[1],color[2]}};
        text(session.style().rule(0).label.c_str(),0,51,-214,1,2,tint,color[3]);
        std::snprintf(value,sizeof value,"%d",int(clock.score.points));
        text(value,1,52,-202,.95F,2,tint,color[3]);
    }
    box(kLapWhite,-221.37773F,-107.12933F,155.61226F,39.95583F,{{0,0,0}},220);
    box(kLapWhite,-272.15277F,-78.15F,54.18888F,18,{{0,0,0}},220);
    std::snprintf(value,sizeof value,"X%d",drift.multiplier());
    const auto multiplier=sample_drift_hud_animation(atlas,"drift_multiplier_text","Highlight",elapsed(clock.multiplier_at));
    text(value,1,-265,-99,multiplier.scale,5,multiplier.color,multiplier.alpha);
    if(drift.warning() || drift.recovering()) {
        const auto slow=sample_drift_hud_animation(atlas,"SlowMessage",drift.warning()?"LosingMult":"TooSlowToGain",elapsed(clock.slow_at));
        text("TOO SLOW!",1,-301,-72,.6F,0,slow.color,slow.alpha);
    }
    if(drift.target()) {
        std::snprintf(value,sizeof value,"GAIN X%d WITH",drift.multiplier()+1);
        text(value,0,-147,-128,.9F,2,blue);
        std::snprintf(value,sizeof value,"%d",drift.target());
        text(value,2,-157,-111,.45F,2,yellow);
    }else text("MAXXXED",0,-147,-128,.9F,2,yellow);
    // Virtual score table, in score order (43EB00/43F130).
    const float right=float(width)/scale-640;
    box(kLapBacking,242+right,-192,156,64,{{0,0,0}},214);
    const char* suffix=session.player_position()==1?"st":session.player_position()==2?"nd":session.player_position()==3?"rd":"th";
    std::snprintf(value,sizeof value,"%u",session.player_position());
    text(value,2,247+right,-220,1,2,blue);
    text(suffix,1,249+right,-218,.5F,0,blue);
    text("/4",1,249+right,-191,.5F,0,cyan);
    unsigned rank=0;
    for(const auto& row:session.drift_competition().standings()) {
        const float y=-150+float(rank++)*24;
        box(kLapWhite,242+right,y,156,22,row.actor?std::array<unsigned char,3>{{0,0,0}}:cyan,row.actor?180:128);
        text(row.name.c_str(),0,170+right,y-10,.9F,0,blue);
        std::snprintf(value,sizeof value,"%d",int(row.score));
        text(value,0,314+right,y-10,.9F,2,blue);
    }
    box(kLapWhite,242+right,-36,156,20,cyan,128);
    box(kLapWhite,242+right,-12,156,20,cyan,128);
    text("Style Points:",0,314+right,-46,.9F,2,blue);
    std::snprintf(value,sizeof value,"%d",session.style().total());
    text(value,0,314+right,-22,.9F,2,blue);
    // The original gauge uses absolute slip capped at90, suppressed without
    // published support or positive forward speed (4A7B00). Bottom anchor
    // follows the same 720p translation as the ordinary tach cluster.
    float degrees=session.car().published_support && session.car().owner_speeds.forward>0?
        std::min(90.F,std::abs(drift.angle())*57.2957763671875F):0;
    const float cx=float(width)*.5F,by=float(height)-240*scale;
    const auto sprite=[&](AtlasRect uv,float x,float y,float w,float h,
                          std::array<unsigned char,3> color,unsigned char alpha) {
        rect(frame,uv,cx+(x-w*.5F)*scale,by+(y-h*.5F)*scale,w*scale,h*scale,color,alpha);
    };
    sprite(kDriftBack,47,198,128,64,{{0,0,0}},100);
    sprite(kDriftDegrees,67,194,64,64,{{255,255,255}},100);
    sprite(kDriftMeter,68,196,64,64,cyan,200);
    rotated(frame,kDriftNeedle,cx+88*scale,by+222*scale,16*scale,64*scale,8*scale,32*scale,
            (90+degrees)*3.14159265358979323846/180.,{{255,255,255}});
    const float dx=cx/scale-320,dy=by/scale-240;
    std::snprintf(value,sizeof value,"%d",int(degrees));
    text(value,2,23+dx,183+dy,.7F,1,cyan,200);
    text("o",0,43+dx,176+dy,.8F,0,cyan);
    //4A83C0 suppresses the general trick title in drift, but still shows
    //the signed style award at SP_Trick_Point under its authored parent.
    if(const auto* notice=session.style().notice())if(notice->kind>0){
        const bool loss=notice->failed||notice->points<0;
        const auto color=loss?std::array<unsigned char,3>{{128,0,0}}:std::array<unsigned char,3>{{194,146,0}};
        const int base=int(notice->points/notice->multiplier);
        if(notice->multiplier>1)std::snprintf(value,sizeof value,loss?"%dx%d":"%+dx%d",base,notice->multiplier);
        else std::snprintf(value,sizeof value,loss?"%d":"%+d",base);
        text(value,1,282+right,-28,.95F,2,color,loss?255:200);
    }
    if(const unsigned kind=drift.comment(now);kind&&(!clock.comment_hidden||drift.comment_started()>clock.comment_hidden_at)) {
        const auto& o=atlas.drift_comment;
        text(session.style().rule(kind).label.c_str(),1,o.position[0],o.position[1],o.width,1,
            {{o.color[0],o.color[1],o.color[2]}},o.color[3]);
    }
    for(std::size_t q=frame.lap_first_quad;q<frame.quads;++q)for(unsigned v=0;v<4;++v) {
        frame.vertices[q*4+v].u+=(v==0||v==3?.5F:-.5F)/kRaceHudAtlasWidth;
        frame.vertices[q*4+v].v+=(v<2?.5F:-.5F)/kRaceHudAtlasSize;
    }
    frame.lap.presentation_ready=true;
}
}

void append_countdown(const RaceSession& session,const RaceHudAtlas& atlas,unsigned width,unsigned height,RaceHudFrame& frame) noexcept {
    const auto& t=session.timing();const auto& config=session.timing_config();
    const int count=race_countdown_value(config,t);
    if(!count || !atlas.countdown_ready)return;
    // Original4A8000 ->434DD0: each digit grows1..3 and fades with the
    // quarter-turn sine. The authored321_Go position/font are shared by modes.
    const float elapsed=float(t.now_ticks-(t.go_valid?t.go_ticks:t.countdown_start_ticks))*.00025F;
    const float step=config.countdown_step_seconds;
    const float phase=t.go_valid?std::clamp(elapsed/step,0.F,1.F):std::clamp((elapsed-(3-count)*step)/step,0.F,1.F);
    const float scale=std::min(width/1280.F,height/720.F)*5.F/3.F;
    const float size=(1+phase*2)*scale;
    const auto angle=static_cast<std::uint16_t>(0x4000+static_cast<int>(double(phase)*-5898240.F)/360);
    const auto alpha=static_cast<unsigned char>(std::clamp(int(player_angle_sin(angle)*255),0,255));
    char text[8];if(count==4)std::snprintf(text,sizeof text,"GO!");else std::snprintf(text,sizeof text,"%d",count);
    float advance=0;for(const char* c=text;*c;++c)advance+=atlas.lap_fonts[1][unsigned(*c)].advance;
    const auto& o=atlas.countdown;float x=width*.5F+o.position[0]*scale-advance*size*.5F;
    const float y=(240+o.position[1])*scale-atlas.lap_font_heights[1]*size*.5F;
    if(frame.lap_first_quad==kRaceHudMaxQuads)frame.lap_first_quad=frame.quads;
    for(const char* c=text;*c;++c) {
        const auto& g=atlas.lap_fonts[1][unsigned(*c)];
        if(g.width && g.height)rect(frame,{g.x,g.y,g.width+2,g.height+2},x+g.bearing_x*size,y+g.bearing_y*size,
            std::max(4,g.width)*size,g.height*size,{{o.color[0],o.color[1],o.color[2]}},alpha);
        x+=g.advance*size;
    }
}
void build_race_hud_frame(const RaceSession& session, const RaceHudAtlas& atlas,
                          std::uint32_t width, std::uint32_t height,
                          RaceHudFrame& frame,bool metric_speed) noexcept {
    build_race_hud_base(session,width,height,frame,metric_speed?&atlas:nullptr);
    if(session.start_cinematic()){frame.quads=0;return;}
    if(session.is_drag()&&!atlas.drag_objects.empty()) {
        frame.quads=0;frame.lap_first_quad=0;frame.atlas_height=atlas.height;
        append_race_drag_hud(session,atlas,width,height,frame);return;
    }
    const bool turbo=*std::max_element(session.spec().induction.begin(),session.spec().induction.end())>0;
    append_race_boost_hud(atlas,session.nitrous(),turbo,session.turbo_pressure(),width,height,frame,
                          session.spec().nitrous.count);
    if (!atlas.lap_ready || !width || !height) return;
    append_countdown(session,atlas,width,height,frame);
    if(session.is_drift()) {
        // The reference keeps only the gauge during the start countdown.
        if(!session.timing().go_valid)return;
        append_drift_hud(session,atlas,width,height,frame);
        return;
    }
    const auto snapshot=race_lap_hud_snapshot(session.course().route,session.timing(),0,true,session.time_limit_ticks());
    build_race_lap_hud_panel(snapshot,RaceLapHudLanguage{true,0},frame.lap);
    if (!frame.lap.formatting_valid) return;
    if(frame.lap_first_quad==kRaceHudMaxQuads)frame.lap_first_quad=frame.quads;
    // Original 640x480 FEng root coordinates. Keep the reference's upper-left
    // anchor and uniform scale in the port's 720p canvas, as with the gauge.
    // This matches the supplied Deck stills; it does not emulate a widescreen mod.
    const float scale=std::min(width/1280.0F,height/720.0F)*5.0F/3.0F;
    const auto backing=[&](const AtlasRect& uv,float x,float y,float w,float h,unsigned char alpha) {
        rect(frame,uv,(320+x-w*.5F)*scale,(240+y-h*.5F)*scale,w*scale,h*scale,{{0,0,0}},alpha);
    };
    // Reverse equal-depth package order: 3,2,1, then the text bucket.
    backing(kLapWhite,-221,-137,156,20,254);
    backing(kLapWhite,-221,-159,156,20,254);
    backing(kLapBacking,-178,-192,256,64,214);
    const auto& bindings=race_lap_hud_bindings();
    for (int i=6;i>=0;--i) {
        const auto& b=bindings[std::size_t(i)];
        const unsigned font=i==5?2U:(i==4 || i==6)?1U:0U;
        const char* text=frame.lap.fields[std::size_t(i)].text.data();
        if (i==1) text="Current:";
        if (i==3) text="Best:";
        if (i==6) text="Laps";
        // English.bin localized 0x61C9B7A2 (different from zero-best literal).
        if (i==0 && !frame.lap.current_valid) text="--:--.--";
        float advance=0;
        for (const char* c=text;*c;++c) advance+=atlas.lap_fonts[font][unsigned(*c)].advance;
        const float s=b.scale*scale;
        float x=(320+b.x)*scale-((b.justification&2)?advance*s:0);
        float y=(240+b.y)*scale;
        // Native free-lap branch moves LapText beside the lap number.
        if (i==6 && snapshot.free_laps) x=(320+bindings[5].x+10)*scale;
        const std::array<unsigned char,3> tint{{
            static_cast<unsigned char>(((unsigned(b.rgba[0])*255+128)>>8)/2*2),
            static_cast<unsigned char>(((unsigned(b.rgba[1])*255+128)>>8)/2*2),
            static_cast<unsigned char>(((unsigned(b.rgba[2])*255+128)>>8)/2*2)}};
        for (const char* c=text;*c;++c) {
            const auto& g=atlas.lap_fonts[font][unsigned(*c)];
            if (g.width && g.height)
                rect(frame,{g.x,g.y,g.width+2,g.height+2},x+g.bearing_x*s,y+g.bearing_y*s,
                     std::max(4,g.width)*s,g.height*s,tint,254);
            x+=g.advance*s;
        }
    }
    // 0x40BEF0 applies a half-texel inset before world.efx linear sampling.
    if(session.rival_count()) {
        // HUD_SingleRace.fng: POSITION group, LBData/LeaderText and their
        // separate backings. Preserve original 640x480 offsets and fonts,
        // anchored to the right edge on the port's widescreen canvas.
        const float right=float(width)-640*scale;
        const auto box=[&](AtlasRect uv,float x,float y,float w,float h,
                           std::array<unsigned char,3> color,unsigned char alpha) {
            rect(frame,uv,right+(320+x-w*.5F)*scale,(240+y-h*.5F)*scale,
                 w*scale,h*scale,color,alpha);
        };
        const auto label=[&](const char* text,unsigned font,float x,float y,float size,
                             bool align_right,std::array<unsigned char,3> color,unsigned char alpha=254) {
            float advance=0;
            for(const unsigned char* c=reinterpret_cast<const unsigned char*>(text);*c;++c)
                advance+=atlas.lap_fonts[font][*c<128?*c:'?'].advance;
            const float s=size*scale;
            x=right+(320+x)*scale-(align_right?advance*s:0);y=(240+y)*scale;
            for(const unsigned char* c=reinterpret_cast<const unsigned char*>(text);*c;++c) {
                const auto& g=atlas.lap_fonts[font][*c<128?*c:'?'];
                if(g.width && g.height)rect(frame,{g.x,g.y,g.width+2,g.height+2},
                    x+g.bearing_x*s,y+g.bearing_y*s,std::max(4,g.width)*s,g.height*s,color,alpha);
                x+=g.advance*s;
            }
        };
        box(kLapBacking,165,-192,265,64,{{0,0,0}},214);
        // PositionBacking's authored SAT matrix rotates the corner by pi.
        auto* v=frame.vertices.data()+(frame.quads-1)*4;
        for(unsigned i=0;i<4;++i) {
            v[i].u=float(2*kLapBacking.x+kLapBacking.w)/kRaceHudAtlasWidth-v[i].u;
            v[i].v=float(2*kLapBacking.y+kLapBacking.h)/kRaceHudAtlasSize-v[i].v;
        }
        const auto& standings=session.standings();
        for(unsigned row=0;row<standings.count;++row) {
            // SetPlayerColor: BGRA base(-63,56,-24,255) + key(255,72,24,0).
            const std::array<unsigned char,3> color=standings.order[row]==0?
                std::array<unsigned char,3>{{0,126,190}}:std::array<unsigned char,3>{{0,0,0}};
            box(kLapWhite,220,-160+20*float(row),137.923553F,18,color,254);
            box(kLapWhite,139,-160+20*float(row),20.5F,18,{{0,0,0}},254);
        }
        constexpr std::array<unsigned char,3> blue{{148,188,254}},dark_blue{{0,126,190}};
        char position[8],total[8];
        std::snprintf(position,sizeof position,"%u",session.player_position());
        std::snprintf(total,sizeof total,"/%u",standings.count);
        const char* ordinal=session.player_position()==1?"st":session.player_position()==2?"nd":
                            session.player_position()==3?"rd":"th";
        label(position,2,204,-220,1,true,blue);
        label(ordinal,1,209,-221,.5F,false,blue);
        label(total,1,205,-202,.5F,false,dark_blue);
        for(unsigned row=0;row<standings.count;++row) {
            char rank[8];std::snprintf(rank,sizeof rank,"%u:",row+1);
            label(rank,0,134,-170+20*float(row)-(row?1:0),.9F,false,blue);
            const unsigned actor=standings.order[row];
            // The port has no player-profile name yet. YOU is an explicit
            // local identity, not a guessed name from the retail intro.
            const auto text=session.leaderboard_label(actor);
            label(text.c_str(),0,284,-170+20*float(row),.9F,true,blue);
        }
        if(session.style().ready()) {
            // HUD_SingleRace StylePointsGroup and its authored children.
            box(kLapWhite,209,-70,161.7972F,18,{{0,128,192}},255);
            box(kLapWhite,209,-50,161.7972F,18,{{0,128,192}},200);
            constexpr std::array<unsigned char,3> pale{{207,233,242}},gold{{194,146,0}};
            char total_style[24];std::snprintf(total_style,sizeof total_style,"%d",session.style().total());
            label("Style Points:",0,284,-80,.9F,true,pale,200);
            label(total_style,0,284,-60,.9F,true,pale,200);
            if(const auto* notice=session.style().notice()) {
                const bool loss=notice->failed||notice->points<0;
                const auto tint=loss?std::array<unsigned char,3>{{128,0,0}}:gold;
                const auto alpha=static_cast<unsigned char>(loss?255:200); //4A87B0 packed FF800000.
                label(session.style().rule(notice->kind).label.c_str(),0,282,-38,.9F,true,tint,alpha);
                char points[32];const int base=int(notice->points/notice->multiplier);
                if(notice->multiplier>1)std::snprintf(points,sizeof points,loss?"%dx%d":"%+dx%d",base,notice->multiplier);
                else std::snprintf(points,sizeof points,loss?"%d":"%+d",base);
                label(points,1,282,-28,.95F,true,tint,alpha);
            }
        }
    }
    constexpr float inset=.5F/kRaceHudAtlasSize;
    for (std::size_t q=frame.lap_first_quad;q<frame.quads;++q) {
        const float du=frame.vertices[q*4].u<frame.vertices[q*4+1].u?1.F:-1.F;
        const float dv=frame.vertices[q*4].v<frame.vertices[q*4+3].v?1.F:-1.F;
        for (unsigned v=0;v<4;++v) {
            frame.vertices[q*4+v].u+=du*((v==0 || v==3)?(.5F/kRaceHudAtlasWidth):(-.5F/kRaceHudAtlasWidth));
            frame.vertices[q*4+v].v+=dv*(v<2?inset:-inset);
        }
    }
    frame.lap.presentation_ready=true;
}

void append_race_boost_hud(const RaceHudAtlas& atlas, const NitrousSnapshot& nos,
        bool turbo, float pressure, unsigned width, unsigned height, RaceHudFrame& frame,
        unsigned nitrous_count) noexcept {
    if (!atlas.boost_ready || !width || !height) return;
    const float scale=std::min(width/1280.0F,height/720.0F), s=scale*5.0F/3.0F;
    const float px=width-147*scale,py=height-119*scale;
    const unsigned before=frame.quads;
    // NOS_2 or NOS_6 group plus child SAT centers, relative to Tach needle.
    // Full original artwork is retained; charge modulates segment alpha.
    constexpr float x[6]={18,5,-14,-23,-24,-17};
    constexpr float y[6]={-19,-27,-24,-13,4,16};
    constexpr float x2[2]={2.001083F,-19.498917F};
    constexpr float y2[2]={-22.999462F,1.499538F};
    const unsigned bars=nitrous_count==2?2U:6U;
    const float segments=nos.capacity?float(bars)*float(nos.remaining)/float(nos.capacity):0;
    if (nos.capacity) for (unsigned k=0;k<bars;++k) {
        const float filled=std::clamp(segments-float(k),0.0F,1.0F);
        const int size=bars==2?64:32;
        const float cx=bars==2?x2[k]:x[k],cy=bars==2?y2[k]:y[k];
        rect(frame,{800+int(k)*size,bars==2?310:270,size,size},
            px+(cx-size*.5F)*s,py+(cy-size*.5F)*s,size*s,size*s,
            {{0,255,0}},static_cast<unsigned char>(150*filled));
    }
    // Put NOS below scale/needle, preserving their existing painter order.
    if (frame.quads>before && before>1) {
        std::array<TrackVertex,kRaceHudMaxQuads*4> copy=frame.vertices;
        const unsigned added=frame.quads-before;
        std::copy_n(copy.begin()+before*4,added*4,frame.vertices.begin()+4);
        std::copy_n(copy.begin()+4,(before-1)*4,frame.vertices.begin()+(added+1)*4);
    }
    // SR_NOS_Text is authored artwork (including the diagonal N2O letters),
    // a 64x64 SAT sprite at (85.361099,-27.569458) in GaugeCluster.
    // Relative to its Needle SAT (82.36,-27.57), it stays visible when an
    // installed bottle is empty; only the charge bars fade with depletion.
    if (nos.capacity) rect(frame,kNosText,px+(85.361099F-82.36F-32)*s,
        py+(-27.569458F+27.57F-32)*s,64*s,64*s,{{255,255,255}},255);
    if (!turbo) return;
    // Original Turbo_Group and child SAT geometry, relative to Needle SAT.
    const float gx=-15.972229F-82.36F,gy=5.263870F+27.57F;
    const auto sprite=[&](AtlasRect uv,float cx,float cy,float w,float h,
                          std::array<unsigned char,3> tint,unsigned char alpha) {
        rect(frame,uv,px+(gx+cx-w*.5F)*s,py+(gy+cy-h*.5F)*s,w*s,h*s,tint,alpha);
    };
    sprite(kTurboFill,-5.666672F,15.166672F,128,128,{{255,255,255}},120);
    sprite(kTurboRed1,16.333328F,-37.833328F,32,32,{{150,0,0}},150);
    sprite(kTurboRed2,-16.666672F,12.166672F,32,32,{{150,0,0}},150);
    sprite(kTurboLines,-1.666672F,15.166672F,64,128,{{0,128,192}},255);
    // Ordinary 4A1460 maps -1..1 to30..219 degrees.
    const float rotation=race_hud_turbo_rotation(pressure,false);
    rotated(frame,kTurboNeedle,px+(gx+5)*s,py+(gy-8)*s,16*s,64*s,8*s,32*s,
        rotation*(3.14159265358979323846/180.0),{{207,233,242}});
    // TurboPSI has Sj=0 (left/top), unlike right-aligned speed and lap text.
    // Native 4F48B0 starts at the SAT anchor; subtracting the string advance
    // incorrectly moves this label left by its whole width.
    const char* label="Turbo";
    float tx=px+(gx+8.333328F)*s;
    const float ty=py+(gy+7.166672F)*s;
    for (const char* c=label;*c;++c) {
        const auto& g=atlas.lap_fonts[0][unsigned(*c)];
        if (g.width && g.height) rect(frame,{g.x,g.y,g.width+2,g.height+2},
            tx+g.bearing_x*.8F*s,ty+g.bearing_y*.8F*s,g.width*.8F*s,g.height*.8F*s,{{150,190,255}},255);
        tx+=g.advance*.8F*s;
    }
}

void build_race_hud_frame_for_car(const VehicleState& car,
                                  double max_rpm,double redline_rpm,
                                  std::uint32_t width,std::uint32_t height,RaceHudFrame& frame) noexcept {
    build_race_hud_geometry(car,max_rpm,redline_rpm,width,height,frame,nullptr);
}
static void build_race_hud_geometry(const VehicleState& car,
                                    double max_rpm,double redline_rpm,
                                    std::uint32_t width,std::uint32_t height,RaceHudFrame& frame,
                                    const RaceHudAtlas* metric_atlas) noexcept {
    frame.metric_speed=metric_atlas!=nullptr;
    frame.quads = 0;
    frame.atlas_height=kRaceHudAtlasSize;
    frame.lap_first_quad = kRaceHudMaxQuads;
    frame.lap = {};
    // 4A0FBC consumes actor+3D4 (biased state+394), published by
    // 478A47..478A6E. Preserve its binary32 unit conversion/store.
    const float magnitude=std::abs(car.owner_speeds.magnitude);
    const float displayed_mph=magnitude*2.236999988555908203125F;
    const float displayed_kph=magnitude*3.6F;
    frame.readout={car.step,car.engine_rpm,car.wheel_rpm,max_rpm,redline_rpm,
        race_hud_needle_rotation(car.engine_rpm,max_rpm),car.gear,
        static_cast<int>(std::min(999.0F,displayed_mph)),
        static_cast<int>(std::min(999.0F,displayed_kph))};
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
               gear_pos[1],gear_scale,car.transmission.mode==3 ?
                   std::array<unsigned char,3>{{0,128,192}} :
                   std::array<unsigned char,3>{{255,128,64}});
    //478F92..478FA3 exports clutch mode==3 to actor3E8;4A115A..4A1173
    // uses blue while engaged, orange during the other clutch phases.
    const int value=frame.metric_speed?frame.readout.kph:frame.readout.mph;
    char speed[4]{};
    speed[0] = static_cast<char>('0'+value/100);
    speed[1] = static_cast<char>('0'+(value/10)%10);
    speed[2] = static_cast<char>('0'+value%10);
    const int first = value >= 100 ? 0 : value >= 10 ? 1 : 2;
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
    if(metric_atlas) {
        constexpr std::string_view label="KM/H";
        const auto& font=metric_atlas->lap_fonts[0];
        float units_width=0;
        for(unsigned char c:label)units_width+=font[c].advance;
        x=units_pos[0]-units_width*units_scale;
        for(unsigned char c:label) {
            const auto& g=font[c];
            if(g.width&&g.height)glyph_quad(frame,{{g.x,g.y,g.width+2,g.height+2},
                g.width,g.height,g.bearing_x,g.bearing_y,g.advance},x,units_pos[1],units_scale,{{150,190,255}});
            x+=g.advance*units_scale;
        }
        return;
    }
    float units_width=0;
    for (const auto& letter : kUnits) units_width+=letter.advance;
    x=units_pos[0]-units_width*units_scale;
    for (const auto& letter : kUnits) {
        glyph_quad(frame,letter,x,units_pos[1],units_scale,{{150,190,255}});
        x+=letter.advance*units_scale;
    }
}

void RaceFpsHistory::add(double milliseconds) noexcept {
    if (!std::isfinite(milliseconds) || milliseconds <= 0) return;
    if (count == intervals.size()) {
        std::move(intervals.begin()+1, intervals.end(), intervals.begin());
        --count;
    }
    intervals[count++] = milliseconds;
}
double RaceFpsHistory::mean_ms() const noexcept {
    double sum=0;
    for (std::size_t i=0;i<count;++i) sum+=intervals[i];
    return count ? sum/count : 0;
}
void append_race_fps_overlay(const RaceHudAtlas& atlas, const RaceFpsHistory& history,
                             unsigned width, unsigned height, RaceHudFrame& frame) noexcept {
    if (!atlas.lap_ready || !width || !height || !history.count) return;
    const float scale=std::min(width/1280.0F,height/720.0F);
    const float x=24*scale, bottom=(height-28*scale), top=bottom-80*scale;
    if (frame.lap_first_quad==kRaceHudMaxQuads) frame.lap_first_quad=frame.quads;
    rect(frame,kLapWhite,x-6*scale,top-28*scale,230*scale,114*scale,{{0,0,0}},190);
    // 60 Hz / 30 Hz thresholds; bars clip at 66.7 ms, text retains full values.
    for (const double ms : {1000.0/60,1000.0/30})
        rect(frame,kLapWhite,x,bottom-float(ms/66.6666667)*80*scale,180*scale,scale,
             {{100,100,100}},255);
    for (std::size_t i=0;i<history.count;++i) {
        const double ms=history.intervals[i];
        const float h=float(std::min(ms/66.6666667,1.0))*80*scale;
        const std::array<unsigned char,3> color=ms>25 ? std::array<unsigned char,3>{{255,80,60}} :
                                                       std::array<unsigned char,3>{{80,230,120}};
        rect(frame,kLapWhite,x+float(i)*2*scale,bottom-h,1.5F*scale,h,color,255);
    }
    char text[80];
    std::snprintf(text,sizeof text,"%.1f FPS  %.1f ms",1000/history.mean_ms(),
                  history.intervals[history.count-1]);
    float cursor=x;
    for (const char* c=text;*c;++c) {
        const auto& g=atlas.lap_fonts[0][unsigned(*c)];
        if (g.width && g.height)
            rect(frame,{g.x,g.y,g.width+2,g.height+2},cursor+g.bearing_x*scale,
                 top-24*scale+g.bearing_y*scale,(g.width+2)*scale,(g.height+2)*scale);
        cursor+=g.advance*scale;
    }
}

void composite_race_hud(const RaceHudAtlas& atlas, const RaceHudFrame& frame,
                         unsigned char* rgba, std::uint32_t width,
                         std::uint32_t height, std::uint32_t pitch) noexcept {
    if (!rgba || atlas.rgba.size() != kRaceHudAtlasWidth*atlas.height*4) return;
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
                const float qa=a.pz>0?a.pz:1,qb=b.pz>0?b.pz:1,qc=c.pz>0?c.pz:1;
                const float total=w0*qa+w1*qb+w2*qc;
                const float wa=w0*qa/total,wb=w1*qb/total,wc=w2*qc/total;
                float u=wa*a.u+wb*b.u+wc*c.u;
                float vcoord=wa*a.v+wb*b.v+wc*c.v;
                if (a.pz>0) {
                    const float local_u=(u*kRaceHudAtlasWidth-513)/256;
                    const float local_v=(vcoord*kRaceHudAtlasSize-1)/256;
                    u=(513+(local_u-std::floor(local_u))*256)/kRaceHudAtlasWidth;
                    vcoord=(1+(local_v-std::floor(local_v))*256)/kRaceHudAtlasSize;
                }
                const int tx=std::max(0,std::min(int(kRaceHudAtlasWidth)-1,static_cast<int>(u*kRaceHudAtlasWidth)));
                const int ty=std::max(0,std::min(int(atlas.height)-1,static_cast<int>(vcoord*atlas.height)));
                const auto* source=atlas.rgba.data()+(ty*kRaceHudAtlasWidth+tx)*4;
                std::array<unsigned char,4> filtered{};
                if (q>=frame.lap_first_quad) {
                    const float fx=u*kRaceHudAtlasWidth-.5F,fy=vcoord*atlas.height-.5F;
                    const int ix=int(std::floor(fx)),iy=int(std::floor(fy));
                    const float ax=fx-std::floor(fx),ay=fy-std::floor(fy);
                    for (unsigned k=0;k<4;++k) {
                        const auto sample=[&](int dx,int dy) {
                            return float(atlas.rgba[(std::clamp(iy+dy,0,int(atlas.height)-1)*kRaceHudAtlasWidth+std::clamp(ix+dx,0,int(kRaceHudAtlasWidth)-1))*4+k]);
                        };
                        filtered[k]=static_cast<unsigned char>(std::clamp(
                            (sample(0,0)*(1-ax)+sample(1,0)*ax)*(1-ay)+
                            (sample(0,1)*(1-ax)+sample(1,1)*ax)*ay+.5F,0.0F,255.0F));
                    }
                    source=filtered.data();
                }
                auto* target=rgba+y*pitch+x*4;
                const auto channel=[&](int k) {return unsigned(std::clamp(
                    w0*a.color[k]+w1*b.color[k]+w2*c.color[k]+.5F,0.0F,255.0F));};
                const unsigned alpha=(unsigned(source[3])*channel(3)+127)/255;
                for (int k=0;k<3;++k) {
                    const unsigned tint=(channel(k)*unsigned(source[k])+127)/255;
                    target[k]=static_cast<unsigned char>((tint*alpha+
                        unsigned(target[k])*(255-alpha)+127)/255);
                }
                target[3]=255;
                break;
            }
        }
    }
}

#include "race_drag_hud.inc"

} // namespace nfsu
