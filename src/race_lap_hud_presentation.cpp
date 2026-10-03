#include "nfsu/race_lap_hud_presentation.h"
#include <algorithm>
#include <cmath>
#include <cstring>

namespace nfsu {
namespace {
constexpr std::uint32_t lap_bit = 0x100;
unsigned word(const unsigned char* p) { return unsigned(p[0]) | unsigned(p[1]) << 8; }
unsigned dword(const unsigned char* p) { return word(p) | word(p+2) << 16; }
int signed_word(const unsigned char* p) { const int v = int(word(p)); return v >= 32768 ? v-65536 : v; }
int signed_byte(unsigned char v) { return v >= 128 ? int(v)-256 : int(v); }
bool valid(const RaceLapHudGeometryInput& in) {
    if (!in.sa.known || !in.parent.known || !in.texture.known ||
        !in.visibility_known || !in.texture.width || !in.texture.height ||
        in.texture.width > 32767 || in.texture.height > 32767) return false;
    for (float v : {in.sa.x,in.sa.y,in.sa.depth,in.sa.sx,in.sa.sy})
        if (!std::isfinite(v)) return false;
    if (in.sa.rotation != std::array<float,4>{{0,0,0,1}}) return false;
    for (float v : in.parent.matrix) if (!std::isfinite(v)) return false;
    const auto& m = in.parent.matrix;
    // Only the recovered affine 2D contract; reject perspective/3D inputs.
    if (m[2] != 0 || m[3] != 0 || m[6] != 0 || m[7] != 0 ||
        m[8] != 0 || m[9] != 0 || m[11] != 0 || m[15] != 1) return false;
    for (unsigned v : in.sa.bgra) if (v > 255) return false;
    for (unsigned v : in.parent.bgra) if (v > 256) return false;
    return std::isfinite(in.sa.depth + m[14]);
}
unsigned bucket(const RaceLapHudGeometryInput& in) {
    const float d = in.sa.depth + in.parent.matrix[14];
    return d <= 0 ? 0 : d >= 255 ? 255 : unsigned(d);
}
std::uint32_t color(const RaceLapHudGeometryInput& in) {
    unsigned c[4];
    for (unsigned i = 0; i < 4; ++i)
        c[i] = ((in.sa.bgra[i]*in.parent.bgra[i]+128) >> 8) / 2;
    return c[0] | c[1]<<8 | c[2]<<16 | c[3]<<24;
}
bool quad(const RaceLapHudGeometryInput& in, float x, float y, float w, float h,
          const std::array<float,4>& uv, RaceLapHudQuad& q) {
    const auto& m = in.parent.matrix;
    const float xs[4] = {x,x+w,x+w,x}, ys[4] = {y,y,y+h,y+h};
    const float us[4] = {uv[0],uv[2],uv[2],uv[0]}, vs[4] = {uv[1],uv[1],uv[3],uv[3]};
    for (unsigned i = 0; i < 4; ++i) {
        auto& v = q.vertices[i];
        const float lx = xs[i]*in.sa.sx + in.sa.x, ly = ys[i]*in.sa.sy + in.sa.y;
        v.position = {{lx*m[0]+ly*m[4]+m[12]+320, lx*m[1]+ly*m[5]+m[13]+240, 1}};
        v.uv = {{us[i],vs[i]}};
        v.inset_uv = {{us[i]+(i == 0 || i == 3 ? .5F : -.5F)/float(in.texture.width),
                      vs[i]+(i < 2 ? .5F : -.5F)/float(in.texture.height)}};
        v.color = color(in);
        for (float p : v.position) if (!std::isfinite(p)) return false;
        for (float p : v.inset_uv) if (!std::isfinite(p)) return false;
    }
    return true;
}
} // namespace

RaceLapHudPermissions copy_lap_hud_permission(RaceLapHudPermissions settings,
    bool profile_known, unsigned char profile_option) noexcept {
    if (!settings.known || !profile_known) return {};
    settings.flags = (settings.flags & ~lap_bit) | (profile_option == 1 ? lap_bit : 0);
    return settings;
}
bool toggle_lap_hud_permission(RaceLapHudPermissions& settings,
    RaceLapHudPermissions& view, unsigned char& profile_option) noexcept {
    if (!settings.known || !view.known || ((settings.flags ^ view.flags) & lap_bit)) return false;
    view.flags ^= lap_bit; settings.flags ^= lap_bit;
    profile_option = (view.flags & lap_bit) ? 1 : 0;
    return true;
}
RaceLapHudVisibility lap_hud_visibility(RaceLapHudPermissions view,
    bool predicates_known, bool lap_path_allowed) noexcept {
    if (!view.known || !predicates_known) return {};
    const bool enabled = lap_path_allowed && (view.flags & lap_bit);
    return {true,enabled,enabled ? 0x400U : 0U};
}

bool decode_lap_hud_fntf(const unsigned char* bytes, std::size_t size,
    std::uint32_t texture_hash, float line_height, float baseline,
    float line_spacing_scale, RaceLapHudFont& font) {
    font = {};
    if (!bytes || size < 128 || std::memcmp(bytes,"FNTF",4) ||
        !(dword(bytes+12) & 0x40000) || !std::isfinite(line_height) || line_height <= 0 ||
        baseline != 0 || line_spacing_scale != 1) return false;
    const unsigned count = word(bytes+10);
    if (!count || count > (size-128)/16) return false;
    RaceLapHudFont result;
    for (unsigned i = 0; i < count; ++i) {
        const auto* p = bytes+128+16*i;
        RaceLapHudGlyph g;
        g.code = std::uint16_t(word(p)); g.width = p[2]; g.height = p[3];
        g.x = std::uint16_t(word(p+4)); g.y = std::uint16_t(word(p+6));
        g.bearing_x = signed_byte(p[9]); g.bearing_y = signed_byte(p[10]);
        g.advance = signed_word(p+14);
        if (p[11]) {
            // Native 0x6387F0: per-glyph pair count/index into the FNTF
            // table at header+0x18, past its four-byte prefix.
            const std::size_t table = dword(bytes+24);
            const std::size_t index = word(p+12);
            if (table < 128+std::size_t(count)*16 || table > size || size-table < 4 ||
                index+std::size_t(p[11]) > (size-table-4)/4) return false;
            for (unsigned j = 0; j < p[11]; ++j) {
                const auto* pair = bytes+table+4+(index+j)*4;
                g.kerning.push_back({std::uint16_t(word(pair)),signed_byte(pair[2])});
            }
        }
        if (std::any_of(result.glyphs.begin(),result.glyphs.end(),
            [&](const RaceLapHudGlyph& old) { return old.code == g.code; })) return false;
        result.glyphs.push_back(g);
    }
    result.known = true; result.texture_hash = texture_hash;
    result.line_height = line_height; result.baseline = baseline;
    result.line_spacing_scale = line_spacing_scale;
    font = std::move(result);
    return true;
}

bool build_lap_hud_text_geometry(const RaceLapHudGeometryInput& in,
    const RaceLapHudFont& font, const char* text, RaceLapHudGeometry& out) {
    out = {};
    if (!valid(in) || !in.layout_known || in.sl || in.sw || (in.sj & ~0xFU) || !font.known || !text ||
        font.texture_hash != in.texture.hash || !std::isfinite(font.line_height) ||
        font.line_height <= 0 || font.baseline != 0 || font.line_spacing_scale != 1) return false;
    std::vector<const RaceLapHudGlyph*> selected;
    float width = 0;
    for (std::size_t i = 0; text[i]; ++i) {
        const auto code = static_cast<unsigned char>(text[i]);
        if (i >= 128 || code < 32 || code > 126 || code == '^' || code == '$') return false;
        const auto it = std::find_if(font.glyphs.begin(),font.glyphs.end(),
            [&](const RaceLapHudGlyph& g) { return g.code == code; });
        // LCD_LET48 has no space record. The original numeric fixture skips
        // that leading padding character without geometry or advance.
        if (it == font.glyphs.end()) {
            if (code == ' ') continue;
            return false;
        }
        if (i) {
            const auto pair = std::find_if(it->kerning.begin(),it->kerning.end(),
                [&](const RaceLapHudKerning& k) { return k.previous == static_cast<unsigned char>(text[i-1]); });
            if (pair != it->kerning.end() && pair->adjustment) return false;
        }
        selected.push_back(&*it); width += float(it->advance);
    }
    float x = in.sj & 1 ? -width/2 : in.sj & 2 ? -width : 0;
    const float y = in.sj & 4 ? -font.line_height/2 : in.sj & 8 ? -font.line_height : 0;
    RaceLapHudGeometry result; result.bucket = bucket(in);
    if (in.visible) for (const auto* g : selected) {
        const float tw = float(in.texture.width), th = float(in.texture.height);
        const std::array<float,4> uv{{(float(g->x)-1)/tw,(float(g->y)-1)/th,
            (float(g->x)+g->width+1)/tw,(float(g->y)+g->height+1)/th}};
        RaceLapHudQuad q;
        if (!quad(in,x+float(g->bearing_x),y+float(g->bearing_y),
            float(std::max(4U,unsigned(g->width))),float(g->height),uv,q)) return false;
        result.quads.push_back(q); x += float(g->advance);
    }
    result.known = true; out = std::move(result); return true;
}
bool build_lap_hud_backing_geometry(const RaceLapHudGeometryInput& in,
    RaceLapHudGeometry& out) {
    out = {};
    if (!valid(in)) return false;
    for (float v : in.uv) if (!std::isfinite(v)) return false;
    RaceLapHudGeometry result; result.bucket = bucket(in);
    if (in.visible) {
        const auto power = [](unsigned n) { unsigned p = 2; while (p < n) p *= 2; return p; };
        auto uv = in.uv;
        const float u_scale = float(in.texture.width)/float(power(in.texture.width));
        const float v_scale = float(in.texture.height)/float(power(in.texture.height));
        uv[0] *= u_scale; uv[2] *= u_scale;
        uv[1] *= v_scale; uv[3] *= v_scale;
        RaceLapHudQuad q;
        if (!quad(in,-.5F,-.5F,1,1,uv,q)) return false;
        result.quads.push_back(q);
    }
    result.known = true; out = std::move(result); return true;
}
bool order_lap_hud_geometry(const std::vector<RaceLapHudGeometry>& objects,
    std::vector<std::size_t>& order) {
    order.clear();
    for (std::size_t i = 0; i < objects.size(); ++i) {
        if (!objects[i].known || objects[i].bucket > 255) { order.clear(); return false; }
        order.push_back(i);
    }
    std::stable_sort(order.begin(),order.end(),[&](std::size_t a, std::size_t b) {
        return objects[a].bucket > objects[b].bucket;
    });
    return true;
}
} // namespace nfsu
