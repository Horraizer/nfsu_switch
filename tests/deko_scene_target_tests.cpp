#include "nfsu/deko_scene_target.h"
#include <iostream>

int main() {
    using namespace nfsu;
    DekoSceneTarget target;
    const auto check = [](bool ok, const char* message) {
        if (!ok) std::cerr << "FAIL: " << message << '\n';
        return ok;
    };
    for (const auto height : {720U, 540U, 450U, 360U}) {
        if (!check(plan_deko_scene_target(1280,720,true,height,target), "valid target refused") ||
            !check(target.height == height && target.width * 9 == height * 16,
                   "target changes aspect ratio") ||
            !check(target.upscale == (height != 720), "native path gained a blit")) return 1;
    }
    // Diagnostic applications keep their original dimensions and capture
    // contract. The race's explicit 720 setting also works with --panel.
    if (!check(plan_deko_scene_target(640,480,false,0,target) &&
               target.width == 640 && target.height == 480 && !target.upscale, "offscreen default") ||
        !check(plan_deko_scene_target(1280,720,false,720,target) && !target.upscale,
               "native panel refused")) return 1;
    for (const auto height : {540U,450U,360U,1U,480U,721U,0xffffffffU}) {
        if (!check(!plan_deko_scene_target(1280,720,false,height,target), "scaled capture accepted") ||
            !check(target.width == 0 && target.height == 0 && !target.upscale,
                   "refusal retained stale target")) return 1;
    }
    if (!check(!plan_deko_scene_target(1024,768,true,540,target), "stretched camera accepted") ||
        !check(!plan_deko_scene_target(0,720,true,0,target), "empty target accepted") ||
        !check(!plan_deko_scene_target(16385,720,true,0,target), "oversized target accepted") ||
        !check(!plan_deko_scene_target(1280,720,true,480,target), "unknown scale accepted")) return 1;
    for (const char* text : {"720","540","450","360"}) {
        std::uint32_t height = 0;
        if (!check(deko_parse_scene_height(text,height) &&
                   plan_deko_scene_target(1280,720,true,height,target), "CLI target mismatch")) return 1;
    }
    for (const char* text : {"", "540x", "0540", "-360", "720 ", "0", "480"}) {
        std::uint32_t height = 720;
        if (!check(!deko_parse_scene_height(text,height) && height == 720, "invalid CLI changed mode")) return 1;
    }
    std::uint32_t height = 720;
    if (!check(!deko_parse_scene_height(nullptr,height) && height == 720, "missing CLI value accepted")) return 1;
    std::cout << "PASS: native/capture identity, scaled aspect, rejected targets and CLI\n";
    return 0;
}
