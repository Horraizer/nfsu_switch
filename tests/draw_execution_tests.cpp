#include "nfsu/draw_commands.h"
#include <cstdio>
#include <functional>

int main() {
    using namespace nfsu;
    namespace sm = shader_manifest;
    MeshVertex vertices[3]{};
    std::uint16_t indices[] = {0, 1, 2};
    RecordedCommands good;
    good.width = 1280; good.height = 720;
    UploadResource v;
    v.name = "fixture vertices"; v.kind = UploadResourceKind::vertex_buffer;
    v.data = reinterpret_cast<const unsigned char*>(vertices);
    v.bytes = sizeof vertices; v.element_bytes = sizeof(MeshVertex); v.elements = 3;
    UploadResource idx;
    idx.name = "fixture indices"; idx.kind = UploadResourceKind::index_buffer;
    idx.data = reinterpret_cast<const unsigned char*>(indices);
    idx.bytes = sizeof indices; idx.element_bytes = 2; idx.elements = 3;
    good.uploads.resources = {v, idx};
    RecordedDraw d;
    d.name = "fixture draw"; d.vertex_resource = 0; d.index_resource = 1;
    d.vertex_count = 3; d.index_count = 3;
    for (const auto& p : sm::kPrograms)
        if (std::string(p.name) == "car_fallback") { d.program = &p; d.pipeline = p.variants[0]; }
    good.draws.push_back(d);
    int failures = 0;
    std::string error;
    if (!validate_recorded_commands(good, error)) { std::printf("FAIL: valid fixture: %s\n", error.c_str()); ++failures; }
    const auto refuse = [&](const char* expected, const std::function<void(RecordedCommands&)>& mutate) {
        auto commands = good;
        mutate(commands);
        if (validate_recorded_commands(commands, error) || error.find(expected) == std::string::npos) {
            std::printf("FAIL: expected %s, got %s\n", expected, error.c_str()); ++failures;
        }
    };
    refuse("invalid viewport", [](auto& c) { c.width = 0; });
    refuse("unknown shader", [](auto& c) { c.draws[0].program = nullptr; });
    refuse("buffer resource", [](auto& c) { c.draws[0].index_resource = 20; });
    refuse("vertex or index range", [](auto& c) { c.draws[0].index_count = 6; });
    refuse("vertex or index range", [](auto& c) { c.draws[0].vertex_count = 2; });
    refuse("vertex or index range", [](auto& c) { c.draws[0].first_index = 1; });
    refuse("texture binding", [](auto& c) { c.draws[0].texture_resource = 0; });
    refuse("invalid pipeline", [](auto& c) { c.draws[0].pipeline.blend_dst = static_cast<sm::BlendFactor>(255); });
    refuse("invalid pipeline", [](auto& c) { c.draws[0].pipeline.alpha_ref = 256; });
    refuse("invalid upload", [](auto& c) { c.uploads.resources[0].data = nullptr; });
    refuse("invalid buffer", [](auto& c) { c.uploads.resources[0].bytes = 1; });
    indices[2] = 3;
    if (validate_recorded_commands(good, error) || error.find("missing vertex") == std::string::npos) {
        std::puts("FAIL: index outside vertex buffer accepted"); ++failures;
    }
    std::printf("draw execution validation: %d failures\n", failures);
    return failures ? 1 : 0;
}
