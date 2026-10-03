#include "nfsu/race_lap_hud_presentation.h"
#include <cmath>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <limits>
#include <string>

namespace {
using namespace nfsu;
int failures = 0;
void check(bool ok, const char* label) {
    if (!ok) { std::cerr << "FAIL: " << label << '\n'; ++failures; }
}
RaceLapHudGeometryInput explicit_input() {
    RaceLapHudGeometryInput in;
    in.sa = {true,0,0,100,1,1,{{255,190,150,255}},{{0,0,0,1}}};
    in.parent.known = true;
    in.parent.matrix = {{1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1}};
    in.parent.bgra = {{256,256,256,256}};
    in.texture = {true,42,256,256};
    in.visibility_known = in.visible = in.layout_known = true;
    in.uv = {{0,0,1,1}};
    return in;
}
void tests() {
    for (int i = 0; i <= 6; ++i) {
        const auto s = race_lap_hud_style(true,i);
        check(s == (i ? static_cast<RaceLapHudStyle>(i) : RaceLapHudStyle::native_fallback),
              "recovered language IDs map to formatter");
    }
    check(race_lap_hud_style(false,0) == RaceLapHudStyle::unknown &&
          race_lap_hud_style(true,-1) == RaceLapHudStyle::unknown &&
          race_lap_hud_style(true,7) == RaceLapHudStyle::unknown,
          "unknown/unproved language has no default");
    RaceLapHudSnapshot snapshot; snapshot.valid = true; snapshot.laps = 2;
    snapshot.start_valid = true; snapshot.start_ticks = 4000; snapshot.world_ticks = 244000;
    RaceLapHudPanel panel;
    build_race_lap_hud_panel(snapshot,RaceLapHudLanguage{true,1},panel);
    check(panel.formatting_valid && !panel.presentation_ready &&
          std::strcmp(panel.fields[0].text.data()," 1'00\"00") == 0,
          "known language formats existing authoritative snapshot without drawing");
    build_race_lap_hud_panel(snapshot,RaceLapHudLanguage{},panel);
    check(!panel.formatting_valid && !panel.presentation_ready,"unknown language clears prior formatting");
    RaceLapHudPermissions p{true,0xA55}, view;
    check(!copy_lap_hud_permission(p,false,1).known,"missing profile remains unknown");
    for (unsigned char option : {0,1,2}) {
        const auto copied = copy_lap_hud_permission(p,true,option);
        check(copied.flags == ((p.flags & ~0x100U) | (option == 1 ? 0x100U : 0)),
              "profile byte is exact-one; unrelated permission bits preserved");
    }
    view = p; unsigned char option = 1;
    check(toggle_lap_hud_permission(p,view,option) && option == 1 && p.flags == view.flags,
          "toggle updates race/view/profile");
    const auto once = p.flags;
    check(toggle_lap_hud_permission(p,view,option) && p.flags == (once ^ 0x100U),
          "toggle round trip");
    check(!lap_hud_visibility(p,false,true).known &&
          !lap_hud_visibility(p,true,false).enabled,"lifecycle predicates are explicit");
    auto in = explicit_input(); RaceLapHudGeometry out;
    check(build_lap_hud_backing_geometry(in,out) && out.known && out.quads.size() == 1 &&
          !out.presentation_ready,"logical backing does not open production gate");
    check(out.quads[0].vertices[0].position[0] == 319.5F &&
          out.quads[0].vertices[0].color == 0x7F4B5F7FU,"centered sprite and channel packing");
    in.visible = false;
    check(build_lap_hud_backing_geometry(in,out) && out.known && out.quads.empty(),
          "known hidden object is distinct from unknown visibility");
    in.visibility_known = false;
    check(!build_lap_hud_backing_geometry(in,out) && !out.known && out.quads.empty(),
          "unknown visibility clears prior geometry");
    in = explicit_input(); in.texture.width = 0;
    check(!build_lap_hud_backing_geometry(in,out),"zero texture dimensions rejected");
    in = explicit_input(); in.parent.known = false;
    check(!build_lap_hud_backing_geometry(in,out),"identity parent is never implicit");
    in = explicit_input(); in.parent.matrix[3] = .1F;
    check(!build_lap_hud_backing_geometry(in,out),"perspective outside bounded contract");
    in = explicit_input(); in.sa.rotation[2] = .1F;
    check(!build_lap_hud_backing_geometry(in,out),"unproved local rotation is rejected explicitly");
    in = explicit_input(); in.sa.x = std::numeric_limits<float>::infinity();
    check(!build_lap_hud_backing_geometry(in,out),"nonfinite geometry refused");
    unsigned char fntf[144]{};
    std::memcpy(fntf,"FNTF",4); fntf[10] = 1; fntf[14] = 4;
    fntf[128] = '1'; fntf[130] = 5; fntf[131] = 8;
    fntf[132] = 10; fntf[134] = 20; fntf[137] = 255; fntf[138] = 254; fntf[142] = 6;
    RaceLapHudFont font;
    check(decode_lap_hud_fntf(fntf,sizeof(fntf),42,10,0,1,font) &&
          font.glyphs[0].bearing_x == -1 && font.glyphs[0].bearing_y == -2,
          "FNTF signed bearings decoded");
    in = explicit_input(); in.sj = 3 | 12;
    check(build_lap_hud_text_geometry(in,font,"1",out) &&
          out.quads[0].vertices[0].position[0] == 316 &&
          out.quads[0].vertices[0].position[1] == 233,"Sj center takes precedence on both axes");
    check(build_lap_hud_text_geometry(in,font," 1",out) && out.quads.size() == 1 &&
          out.quads[0].vertices[0].position[0] == 316,"absent padding space has no advance or quad");
    check(!build_lap_hud_text_geometry(in,font,"2",out) && !out.known,
          "missing glyph clears output instead of font substitution");
    in.sw = 1;
    check(!build_lap_hud_text_geometry(in,font,"1",out),"unproved wrapping rejected");
    check(!decode_lap_hud_fntf(fntf,143,42,10,0,1,font) && !font.known,
          "truncated FNTF clears previous font");
    check(!decode_lap_hud_fntf(fntf,144,42,10,1,1,font),"unproved runtime baseline rejected");
    in = explicit_input(); std::vector<RaceLapHudGeometry> objects;
    for (float depth : {-5.F,0.F,300.F,255.F}) {
        in.sa.depth = depth; check(build_lap_hud_backing_geometry(in,out),"queue fixture builds");
        objects.push_back(out);
    }
    std::vector<std::size_t> order;
    check(order_lap_hud_geometry(objects,order) && order == std::vector<std::size_t>({2,3,0,1}),
          "descending clamped buckets keep stable ties");
    objects[0].known = false;
    check(!order_lap_hud_geometry(objects,order) && order.empty(),"unknown queue input fails atomically");
}
std::vector<unsigned char> unhex(const std::string& text) {
    std::vector<unsigned char> bytes;
    if (text.size()%2) return bytes;
    for (std::size_t i = 0; i < text.size(); i += 2) {
        const auto digit = [](char c) { return c >= '0' && c <= '9' ? c-'0' :
            c >= 'a' && c <= 'f' ? c-'a'+10 : -1; };
        const int a = digit(text[i]), b = digit(text[i+1]);
        if (a < 0 || b < 0) return {};
        bytes.push_back(static_cast<unsigned char>(a*16+b));
    }
    return bytes;
}
int stream() {
    std::cout << std::setprecision(9);
    char command;
    while (std::cin >> command) {
        if (command == 'l') {
            int known, language; std::cin >> known >> language;
            RaceLapHudText text;
            const bool ok = format_lap_hud_ticks(240000,race_lap_hud_style(known != 0,language),text);
            std::cout << ok << '\t' << text.data() << '\n';
        } else if (command == 'p') {
            unsigned initial, option; std::cin >> initial >> option;
            auto p = copy_lap_hud_permission({true,initial},true,static_cast<unsigned char>(option));
            auto view = p; unsigned char profile = static_cast<unsigned char>(option);
            std::cout << p.flags << '\t' << lap_hud_visibility(view,true,true).enabled;
            for (int i = 0; i < 2; ++i) {
                if (!toggle_lap_hud_permission(p,view,profile)) return 2;
                std::cout << '\t' << view.flags << '\t' << p.flags << '\t' << unsigned(profile);
            }
            std::cout << '\n';
        } else if (command == 'q') {
            unsigned n; std::cin >> n; if (n > 128) return 2;
            std::vector<RaceLapHudGeometry> objects;
            auto in = explicit_input();
            for (unsigned i = 0; i < n; ++i) {
                std::cin >> in.sa.depth; RaceLapHudGeometry out;
                if (!build_lap_hud_backing_geometry(in,out)) return 2;
                objects.push_back(out);
            }
            std::vector<std::size_t> order;
            if (!order_lap_hud_geometry(objects,order)) return 2;
            for (auto i : order) std::cout << i << ' ';
            std::cout << '\n';
        } else if (command == 'g' || command == 'b') {
            auto in = explicit_input();
            std::cin >> in.texture.hash >> in.texture.width >> in.texture.height
                >> in.sa.x >> in.sa.y >> in.sa.depth >> in.sa.sx >> in.sa.sy;
            for (auto& c : in.sa.bgra) std::cin >> c;
            for (auto& v : in.sa.rotation) std::cin >> v;
            for (auto& v : in.parent.matrix) std::cin >> v;
            for (auto& c : in.parent.bgra) std::cin >> c;
            std::cin >> in.sj >> in.sl >> in.sw;
            for (auto& v : in.uv) std::cin >> v;
            RaceLapHudGeometry out;
            if (command == 'g') {
                float height, baseline, spacing; std::string hex, text;
                std::cin >> height >> baseline >> spacing >> hex >> text;
                auto bytes = unhex(hex), chars = unhex(text); chars.push_back(0);
                RaceLapHudFont font;
                if (!decode_lap_hud_fntf(bytes.data(),bytes.size(),in.texture.hash,height,baseline,spacing,font) ||
                    !build_lap_hud_text_geometry(in,font,reinterpret_cast<const char*>(chars.data()),out)) return 2;
            } else if (!build_lap_hud_backing_geometry(in,out)) return 2;
            std::cout << out.known << ' ' << out.presentation_ready << ' ' << out.bucket << ' ' << out.quads.size();
            for (const auto& q : out.quads) for (const auto& v : q.vertices) {
                for (float p : v.position) std::cout << ' ' << p;
                for (float uv : v.uv) std::cout << ' ' << uv;
                for (float uv : v.inset_uv) std::cout << ' ' << uv;
                std::cout << ' ' << v.color;
            }
            std::cout << '\n';
        } else return 2;
        if (!std::cin) return 2;
    }
    return std::cin.eof() ? 0 : 2;
}
} // namespace
int main(int argc, char** argv) {
    if (argc == 2 && std::strcmp(argv[1],"--oracle-stream") == 0) return stream();
    if (argc != 1) return 2;
    tests();
    std::cout << "lap HUD presentation: " << (failures ? "FAIL" : "PASS") << '\n';
    return failures ? 1 : 0;
}
