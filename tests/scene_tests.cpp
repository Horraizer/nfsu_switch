#include "nfsu/geometry.h"
#include "nfsu/image.h"
#include "nfsu/render.h"
#include "nfsu/scene.h"

#include <cstdint>
#include <cstring>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace {

int g_failures = 0;

void check(bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << "test failure: " << message << '\n';
        ++g_failures;
    }
}

constexpr std::uint32_t kHashA = 0xA000000AU;  // one usable texture
constexpr std::uint32_t kHashB = 0xB000000BU;  // in no texture
constexpr std::uint32_t kHashC = 0xC000000CU;  // two textures: unusable
constexpr std::uint32_t kHashE = 0xE000000EU;  // in no texture
constexpr std::uint32_t kMeshHashBody = 0x01020304U;
constexpr std::uint32_t kMeshHashBroken = 0x11121314U;
constexpr std::uint32_t kMeshHashWheel = 0x21222324U;
constexpr std::uint32_t kMeshHashDup = 0x31323334U;

nfsu::MeshVertex vertex(float px, float py, float pz, float u, float v) {
    nfsu::MeshVertex out;
    out.px = px;
    out.py = py;
    out.pz = pz;
    out.u = u;
    out.v = v;
    out.color[0] = out.color[1] = out.color[2] = out.color[3] = 255;
    return out;
}

void store_u32(unsigned char* out, std::uint32_t value) {
    out[0] = static_cast<unsigned char>(value);
    out[1] = static_cast<unsigned char>(value >> 8U);
    out[2] = static_cast<unsigned char>(value >> 16U);
    out[3] = static_cast<unsigned char>(value >> 24U);
}

void store_float(unsigned char* out, float value) {
    std::uint32_t bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    store_u32(out, bits);
}

nfsu::MeshMaterial material(std::uint32_t first, std::uint32_t count,
                            std::uint32_t slot, unsigned char tag) {
    nfsu::MeshMaterial out;
    out.first_index = first;
    out.index_count = count;
    out.texture_slot = slot;
    out.secondary_slot = 0x100U + tag;
    out.flags = 0x5000U + tag;
    out.offset = 0x1000U + tag;
    for (int k = 0; k < 3; ++k) {
        out.bounds_min[k] = -1.0F - static_cast<float>(k) - tag;
        out.bounds_max[k] = 1.0F + static_cast<float>(k) + tag;
    }
    for (std::size_t k = 0; k < nfsu::kMaterialRecordSize; ++k) {
        out.raw[k] = static_cast<unsigned char>(tag * 7U + k);
    }
    for (std::size_t k = 0; k < 3; ++k) {
        store_float(out.raw + k * 4U, out.bounds_min[k]);
        store_float(out.raw + 16U + k * 4U, out.bounds_max[k]);
    }
    store_u32(out.raw + 12, out.index_count);
    store_u32(out.raw + 28, out.texture_slot);
    store_u32(out.raw + 32, out.secondary_slot);
    store_u32(out.raw + 52, out.first_index);
    store_u32(out.raw + 56, out.flags);
    return out;
}

// A quad facing +X at depth x, spanning pz in [z0, z1].
nfsu::Mesh quad_mesh(std::size_t index, const std::string& name, float x, float z0,
                     float z1) {
    nfsu::Mesh mesh;
    mesh.index = index;
    mesh.name = name;
    mesh.valid = true;
    mesh.vertices = {vertex(x, 0, z0, 0, 0), vertex(x, 1, z0, 0, 1),
                     vertex(x, 1, z1, 1, 1), vertex(x, 0, z1, 1, 0)};
    mesh.triangles = {{0, 1, 2}, {0, 2, 3}};
    return mesh;
}

// Mesh 0 BODY: resolved A (triangle 0), unresolved B (triangle 1).
// Mesh 1 BROKEN: invalid.
// Mesh 2 WHEEL: unusable C, invalid slot, bad range with B, unresolved E.
// Meshes 3 and 4: both named DUP.
nfsu::GeometryReport scene_geometry() {
    nfsu::GeometryReport report;
    nfsu::Mesh body = quad_mesh(0, "BODY", 0.0F, 0.1F, 0.9F);
    body.texture_hashes = {kHashA, kHashB};
    body.materials = {material(0, 3, 0, 1), material(3, 3, 1, 2)};
    report.meshes.push_back(body);

    nfsu::Mesh broken;
    broken.index = 1;
    broken.name = "BROKEN";
    report.meshes.push_back(broken);

    nfsu::Mesh wheel = quad_mesh(2, "WHEEL", 0.0F, -0.9F, -0.1F);
    wheel.texture_hashes = {kHashC, kHashB, kHashE};
    wheel.materials = {material(0, 3, 0, 3), material(0, 3, 7, 4),
                       material(1, 3, 1, 5), material(3, 3, 2, 6)};
    report.meshes.push_back(wheel);

    nfsu::Mesh dup_a = quad_mesh(3, "DUP", -0.5F, 1.2F, 1.5F);
    dup_a.texture_hashes = {kHashA};
    dup_a.materials = {material(0, 6, 0, 7)};
    report.meshes.push_back(dup_a);
    nfsu::Mesh dup_b = quad_mesh(4, "DUP", -0.5F, -1.5F, -1.2F);
    dup_b.texture_hashes = {kHashB};
    dup_b.materials = {material(0, 6, 0, 8)};
    report.meshes.push_back(dup_b);
    const std::uint32_t hashes[] = {kMeshHashBody, kMeshHashBroken,
                                    kMeshHashWheel, kMeshHashDup, kMeshHashDup};
    for (std::size_t i = 0; i < report.meshes.size(); ++i) {
        nfsu::Mesh& mesh = report.meshes[i];
        mesh.directory_hash = hashes[i];
        mesh.directory_offset = 0x200U + i * 0x100U;
        mesh.directory_size = 0x80U;
        mesh.has_directory_entry = true;
    }
    report.mesh_directory_valid = true;
    return report;
}

nfsu::RenderTexture texture(std::uint32_t hash, std::uint32_t size, unsigned char seed) {
    nfsu::RenderTexture out;
    out.hash = hash;
    out.width = size;
    out.height = size;
    out.valid = true;
    out.rgba.resize(std::size_t{size} * size * 4U);
    for (std::size_t i = 0; i < out.rgba.size(); ++i) {
        out.rgba[i] = static_cast<unsigned char>(seed + i * 13U);
    }
    return out;
}

nfsu::RenderTextureSet scene_textures() {
    nfsu::RenderTextureSet set;
    set.textures.push_back(texture(0x12345678U, 2, 1));  // unused
    nfsu::RenderTexture a = texture(kHashA, 4, 40);
    a.source = 1;
    a.pack = 2;
    a.record = 3;
    set.textures.push_back(a);
    set.textures.push_back(texture(kHashC, 2, 9));
    set.textures.push_back(texture(kHashC, 2, 11));
    return set;
}

nfsu::MeshSelector by_index(std::uint64_t index) {
    nfsu::MeshSelector selector;
    selector.kind = nfsu::MeshSelectorKind::index;
    selector.index = index;
    return selector;
}

nfsu::MeshSelector by_name(const std::string& name) {
    nfsu::MeshSelector selector;
    selector.kind = nfsu::MeshSelectorKind::name;
    selector.name = name;
    return selector;
}

nfsu::MeshSelector by_hash(std::uint32_t hash) {
    nfsu::MeshSelector selector;
    selector.kind = nfsu::MeshSelectorKind::hash;
    selector.hash = hash;
    return selector;
}

nfsu::MeshSelection selection_of(std::vector<nfsu::MeshSelector> selectors) {
    nfsu::MeshSelection selection;
    selection.selectors = std::move(selectors);
    return selection;
}

bool prepare(const nfsu::GeometryReport& geometry,
             const nfsu::RenderTextureSet& textures,
             const nfsu::MeshSelection& selection, nfsu::PreparedCarScene& scene,
             std::string& error,
             const nfsu::SceneLimits& limits = nfsu::SceneLimits{}) {
    return nfsu::prepare_car_scene(geometry, textures, selection, limits, scene, error);
}

std::vector<std::size_t> mesh_positions(const nfsu::PreparedCarScene& scene) {
    std::vector<std::size_t> out;
    for (const nfsu::PreparedMesh& mesh : scene.meshes) {
        out.push_back(mesh.source_index);
    }
    return out;
}

bool empty_scene(const nfsu::PreparedCarScene& scene) {
    return scene.profile == nfsu::SelectionProfile::all_meshes &&
           scene.selectors.empty() && scene.source_mesh_count == 0 &&
           scene.skipped_invalid_meshes.empty() && scene.meshes.empty() &&
           scene.draws.empty() && scene.textures.empty() &&
           scene.unresolved_hashes.empty();
}

// Field-by-field equality, floats compared as bits.
bool same_draw(const nfsu::PreparedDraw& a, const nfsu::PreparedDraw& b) {
    return a.mesh == b.mesh && a.material_index == b.material_index &&
           a.first_index == b.first_index && a.index_count == b.index_count &&
           a.texture_slot == b.texture_slot && a.texture_hash == b.texture_hash &&
           a.binding == b.binding && a.texture == b.texture &&
           a.texture_source == b.texture_source && a.texture_pack == b.texture_pack &&
           a.texture_record == b.texture_record &&
           a.secondary_slot == b.secondary_slot && a.flags == b.flags &&
           a.record_offset == b.record_offset &&
           std::memcmp(a.bounds_min, b.bounds_min, sizeof(a.bounds_min)) == 0 &&
           std::memcmp(a.bounds_max, b.bounds_max, sizeof(a.bounds_max)) == 0 &&
           a.raw == b.raw && a.drawable == b.drawable;
}

bool same_scene(const nfsu::PreparedCarScene& a, const nfsu::PreparedCarScene& b) {
    if (a.profile != b.profile || a.selectors.size() != b.selectors.size() ||
        a.source_mesh_count != b.source_mesh_count ||
        a.skipped_invalid_meshes != b.skipped_invalid_meshes ||
        a.meshes.size() != b.meshes.size() || a.draws.size() != b.draws.size() ||
        a.textures.size() != b.textures.size() ||
        a.unresolved_hashes != b.unresolved_hashes) {
        return false;
    }
    for (std::size_t i = 0; i < a.selectors.size(); ++i) {
        if (a.selectors[i].kind != b.selectors[i].kind ||
            a.selectors[i].index != b.selectors[i].index ||
            a.selectors[i].name != b.selectors[i].name ||
            a.selectors[i].hash != b.selectors[i].hash) {
            return false;
        }
    }
    for (std::size_t i = 0; i < a.meshes.size(); ++i) {
        const nfsu::PreparedMesh& x = a.meshes[i];
        const nfsu::PreparedMesh& y = b.meshes[i];
        if (x.source_index != y.source_index || x.name != y.name ||
            x.directory_hash != y.directory_hash ||
            x.directory_offset != y.directory_offset ||
            x.directory_size != y.directory_size ||
            x.has_directory_entry != y.has_directory_entry ||
            x.selector != y.selector || x.material_count != y.material_count ||
            x.vertices.size() != y.vertices.size() ||
            x.triangles.size() != y.triangles.size() ||
            (!x.vertices.empty() &&
             std::memcmp(x.vertices.data(), y.vertices.data(),
                         x.vertices.size() * sizeof(nfsu::MeshVertex)) != 0) ||
            (!x.triangles.empty() &&
             std::memcmp(x.triangles.data(), y.triangles.data(),
                         x.triangles.size() * sizeof(nfsu::MeshTriangle)) != 0)) {
            return false;
        }
    }
    for (std::size_t i = 0; i < a.draws.size(); ++i) {
        if (!same_draw(a.draws[i], b.draws[i])) {
            return false;
        }
    }
    for (std::size_t i = 0; i < a.textures.size(); ++i) {
        const nfsu::RenderTexture& x = a.textures[i];
        const nfsu::RenderTexture& y = b.textures[i];
        if (x.hash != y.hash || x.width != y.width || x.height != y.height ||
            x.rgba != y.rgba || x.source != y.source || x.pack != y.pack ||
            x.record != y.record) {
            return false;
        }
    }
    return true;
}

nfsu::RenderOptions options() {
    nfsu::RenderOptions out;
    out.width = 48;
    out.height = 32;
    out.camera.target = {0.0, 0.5, 0.0};
    out.camera.yaw_deg = 0.0;
    out.camera.pitch_deg = 0.0;
    out.camera.distance = 5.0;
    out.camera.fov_y_deg = 45.0;
    return out;
}

std::string tga_bytes(const nfsu::Frame& frame) {
    std::ostringstream stream(std::ios::binary);
    std::string error;
    nfsu::write_tga(stream, frame.width(), frame.height(), frame.rgba(), error);
    return stream.str();
}

bool render(const nfsu::PreparedCarScene& scene, std::string& bytes,
            nfsu::RenderStats& stats, std::string& error,
            const nfsu::RenderLimits& limits = nfsu::RenderLimits{}) {
    nfsu::Frame frame;
    if (!nfsu::render_scene(scene, options(), limits, frame, stats, error)) {
        bytes.clear();
        return false;
    }
    bytes = tga_bytes(frame);
    return true;
}

void test_all_meshes() {
    const nfsu::GeometryReport geometry = scene_geometry();
    nfsu::PreparedCarScene scene;
    std::string error;
    check(prepare(geometry, scene_textures(), nfsu::MeshSelection{}, scene, error),
          "all meshes: " + error);
    check(scene.profile == nfsu::SelectionProfile::all_meshes &&
              std::string(nfsu::selection_profile_name(scene.profile)) == "all meshes",
          "all-meshes profile");
    check(scene.selectors.empty() && scene.source_mesh_count == 5, "all-meshes sources");
    check(mesh_positions(scene) == std::vector<std::size_t>({0, 2, 3, 4}),
          "every valid mesh in source order");
    check(scene.skipped_invalid_meshes == std::vector<std::size_t>({1}),
          "invalid mesh skipped and listed");
    check(scene.meshes[1].name == "WHEEL" && scene.meshes[0].selector == nfsu::kNoHandle,
          "mesh identity");
    check(scene.draws.size() == 8, "one draw per material group");
    std::vector<std::size_t> resolved;
    nfsu::resolve_mesh_selection(geometry, nfsu::MeshSelection{}, nfsu::SceneLimits{},
                                 resolved, nullptr, error);
    check(resolved == mesh_positions(scene), "resolve_mesh_selection matches");

    std::string bytes;
    nfsu::RenderStats stats;
    check(render(scene, bytes, stats, error), "render all: " + error);
    check(stats.meshes == 5 && stats.meshes_rendered == 4 &&
              stats.meshes_skipped_invalid == 1, "all-meshes mesh stats");
    check(stats.material_groups == 8 && stats.groups_resolved == 2 &&
              stats.groups_unresolved == 3 && stats.groups_unusable == 1 &&
              stats.groups_invalid == 2, "all-meshes group stats");
    // B from BODY, B (bad range) and E from WHEEL, B from DUP 4.
    check(stats.unresolved_hashes == 2, "unique unresolved hashes");
    check(stats.triangles_submitted == 2 + 2 + 2 + 2 && stats.triangles_resolved == 3,
          "all-meshes triangle stats");
}

void test_selection() {
    const nfsu::GeometryReport geometry = scene_geometry();
    const nfsu::RenderTextureSet textures = scene_textures();
    std::string error;
    nfsu::PreparedCarScene scene;

    check(prepare(geometry, textures, selection_of({by_index(2)}), scene, error),
          "index selection: " + error);
    check(scene.profile == nfsu::SelectionProfile::explicit_subset &&
              mesh_positions(scene) == std::vector<std::size_t>({2}) &&
              scene.meshes[0].selector == 0 && scene.skipped_invalid_meshes.empty(),
          "index selection result");
    check(scene.draws.size() == 4 && scene.draws[0].mesh == 0, "subset draws");

    check(prepare(geometry, textures, selection_of({by_name("WHEEL")}), scene, error),
          "name selection: " + error);
    check(mesh_positions(scene) == std::vector<std::size_t>({2}) &&
              scene.meshes[0].name == "WHEEL", "name selection result");

    check(prepare(geometry, textures, selection_of({by_hash(kMeshHashWheel)}),
                  scene, error),
          "hash selection: " + error);
    check(mesh_positions(scene) == std::vector<std::size_t>({2}) &&
              scene.meshes[0].directory_hash == kMeshHashWheel &&
              scene.meshes[0].has_directory_entry,
          "hash selection result");

    // Mixed and out of order: source order, provenance kept per mesh.
    nfsu::PreparedCarScene mixed;
    check(prepare(geometry, textures,
                  selection_of({by_index(3), by_name("WHEEL"), by_index(0)}), mixed, error),
          "mixed selection: " + error);
    check(mesh_positions(mixed) == std::vector<std::size_t>({0, 2, 3}),
          "selector order normalized to source order");
    check(mixed.meshes[0].selector == 2 && mixed.meshes[1].selector == 1 &&
              mixed.meshes[2].selector == 0, "selection provenance");
    check(mixed.selectors.size() == 3 &&
              mixed.selectors[1].kind == nfsu::MeshSelectorKind::name &&
              mixed.selectors[1].name == "WHEEL", "selectors kept as given");

    nfsu::PreparedCarScene reordered;
    check(prepare(geometry, textures,
                  selection_of({by_name("BODY"), by_index(3), by_index(2)}), reordered, error),
          "reordered selection: " + error);
    check(reordered.draws.size() == mixed.draws.size(), "same draw count");
    bool draws_same = reordered.draws.size() == mixed.draws.size();
    for (std::size_t i = 0; draws_same && i < mixed.draws.size(); ++i) {
        draws_same = same_draw(mixed.draws[i], reordered.draws[i]);
    }
    check(draws_same, "selector order does not change draws");
    std::string a;
    std::string b;
    nfsu::RenderStats sa;
    nfsu::RenderStats sb;
    check(render(mixed, a, sa, error) && render(reordered, b, sb, error) && a == b,
          "selector order does not change the frame");
    check(sa.meshes == 3 && sa.meshes_skipped_invalid == 0, "subset mesh stats");

    // Every valid mesh selected explicitly draws the same frame as the
    // all-meshes profile.
    nfsu::PreparedCarScene all;
    nfsu::PreparedCarScene every;
    check(prepare(geometry, textures, nfsu::MeshSelection{}, all, error) &&
              prepare(geometry, textures,
                      selection_of({by_index(4), by_index(3), by_index(2), by_index(0)}),
                      every, error),
          "all vs every: " + error);
    check(render(all, a, sa, error) && render(every, b, sb, error) && a == b,
          "explicit full subset matches all meshes");

    std::vector<std::size_t> positions;
    std::vector<std::size_t> selected_by;
    check(nfsu::resolve_mesh_selection(geometry, selection_of({by_index(4), by_name("BODY")}),
                                       nfsu::SceneLimits{}, positions, &selected_by,
                                       error) &&
              positions == std::vector<std::size_t>({0, 4}) &&
              selected_by == std::vector<std::size_t>({1, 0}),
          "resolve_mesh_selection reports provenance");
}

void test_selection_errors() {
    const nfsu::GeometryReport geometry = scene_geometry();
    const nfsu::RenderTextureSet textures = scene_textures();
    nfsu::PreparedCarScene filled;
    std::string error;
    check(prepare(geometry, textures, nfsu::MeshSelection{}, filled, error), "fill");

    const auto rejects = [&](const nfsu::MeshSelection& selection,
                             const std::string& needle, const std::string& label,
                             const nfsu::SceneLimits& limits = nfsu::SceneLimits{}) {
        nfsu::PreparedCarScene scene = filled;
        std::string message;
        const bool ok = prepare(geometry, textures, selection, scene, message, limits);
        check(!ok, label + " rejected");
        check(empty_scene(scene), label + " leaves the scene empty");
        check(message.find(needle) != std::string::npos,
              label + " message: " + message);
        std::vector<std::size_t> positions = {9};
        std::string resolve_message;
        check(!nfsu::resolve_mesh_selection(geometry, selection, limits, positions,
                                            nullptr, resolve_message) &&
                  positions.empty() && resolve_message == message,
              label + " resolve_mesh_selection agrees");
    };
    rejects(selection_of({by_index(5)}), "out of range", "index past the end");
    rejects(selection_of({by_index(1ULL << 40)}), "out of range", "huge index");
    rejects(selection_of({by_name("NOPE")}), "no mesh has this name", "unknown name");
    rejects(selection_of({by_name("body")}), "no mesh has this name", "case-sensitive name");
    rejects(selection_of({by_name("")}), "empty", "empty name");
    rejects(selection_of({by_name("DUP")}), "ambiguous", "duplicate stored name");
    rejects(selection_of({by_hash(0xFFFFFFFFU)}), "no mesh has", "unknown hash");
    rejects(selection_of({by_hash(kMeshHashDup)}), "ambiguous", "duplicate hash");
    rejects(selection_of({by_name(std::string("BODY\0X", 6))}), "printable",
            "embedded NUL");
    rejects(selection_of({by_name(std::string(nfsu::kMeshNameSize + 1, 'A'))}), "printable",
            "name longer than any stored name");
    rejects(selection_of({by_index(1)}), "invalid", "invalid mesh by index");
    rejects(selection_of({by_name("BROKEN")}), "invalid", "invalid mesh by name");
    rejects(selection_of({by_hash(kMeshHashBroken)}), "invalid", "invalid mesh by hash");
    rejects(selection_of({by_index(0), by_index(0)}), "already selected", "index twice");
    rejects(selection_of({by_name("WHEEL"), by_name("WHEEL")}), "already selected",
            "name twice");
    rejects(selection_of({by_index(2), by_name("WHEEL")}), "already selected by selector 0",
            "index and name of one mesh");
    rejects(selection_of({by_hash(kMeshHashWheel), by_index(2)}),
            "already selected by selector 0", "hash and index of one mesh");
    nfsu::MeshSelector index_with_name = by_index(0);
    index_with_name.name = "BODY";
    rejects(selection_of({index_with_name}), "must not carry a name",
            "index selector with name");
    nfsu::MeshSelector name_with_index = by_name("BODY");
    name_with_index.index = 1;
    rejects(selection_of({name_with_index}), "must not carry an index",
            "name selector with index");
    nfsu::MeshSelector hash_with_name = by_hash(kMeshHashBody);
    hash_with_name.name = "BODY";
    rejects(selection_of({hash_with_name}), "must not carry", "hash selector with name");
    nfsu::MeshSelector index_with_hash = by_index(0);
    index_with_hash.hash = kMeshHashBody;
    rejects(selection_of({index_with_hash}), "must not carry", "index selector with hash");
    nfsu::GeometryReport no_directory = geometry;
    no_directory.mesh_directory_valid = false;
    std::vector<std::size_t> positions;
    std::string no_directory_error;
    check(!nfsu::resolve_mesh_selection(no_directory,
                                        selection_of({by_hash(kMeshHashBody)}),
                                        nfsu::SceneLimits{}, positions, nullptr,
                                        no_directory_error) &&
              no_directory_error.find("unavailable") != std::string::npos,
          "hash selector requires a verified directory");
    nfsu::MeshSelector bogus = by_index(0);
    bogus.kind = static_cast<nfsu::MeshSelectorKind>(9);
    rejects(selection_of({bogus}), "unknown selector kind", "unknown selector kind");

    nfsu::SceneLimits limits;
    limits.max_selectors = 2;
    rejects(selection_of({by_index(0), by_index(2), by_index(3)}), "mesh selectors",
            "selector count limit", limits);
    limits = nfsu::SceneLimits{};
    limits.max_selector_text_bytes = 8;
    rejects(selection_of({by_name("BODY"), by_name("WHEEL")}), "exceed", "selector text limit",
            limits);
    // The first problem in selector order is reported.
    rejects(selection_of({by_name("NOPE"), by_index(99)}), "selector 0", "first error wins");
}

void test_limits() {
    const nfsu::GeometryReport geometry = scene_geometry();
    const nfsu::RenderTextureSet textures = scene_textures();
    nfsu::PreparedCarScene filled;
    std::string error;
    check(prepare(geometry, textures, nfsu::MeshSelection{}, filled, error), "fill");
    const auto limited = [&](const nfsu::SceneLimits& limits, const std::string& needle,
                             const std::string& label) {
        nfsu::PreparedCarScene scene = filled;
        std::string message;
        check(!prepare(geometry, textures, nfsu::MeshSelection{}, scene, message, limits),
              label + " rejected");
        check(empty_scene(scene), label + " leaves the scene empty");
        check(message.find(needle) != std::string::npos, label + ": " + message);
    };
    nfsu::SceneLimits limits;
    limits.max_draws = 7;
    limited(limits, "material groups", "draw limit");
    limits = nfsu::SceneLimits{};
    limits.max_vertices = 15;
    limited(limits, "vertices", "vertex limit");
    limits = nfsu::SceneLimits{};
    limits.max_triangles = 7;
    limited(limits, "triangles", "triangle limit");
    limits = nfsu::SceneLimits{};
    limits.max_draw_triangles = 7;
    limited(limits, "draw more than", "drawn triangle limit");
    limits = nfsu::SceneLimits{};
    limits.max_texture_bytes = 63;
    limited(limits, "prepared textures", "texture byte limit");

    // At the limits everything passes.
    limits = nfsu::SceneLimits{};
    limits.max_draws = 8;
    limits.max_vertices = 16;
    limits.max_triangles = 8;
    limits.max_draw_triangles = 8;
    limits.max_texture_bytes = 64;
    nfsu::PreparedCarScene scene;
    check(prepare(geometry, textures, nfsu::MeshSelection{}, scene, error, limits),
          "exactly at the limits: " + error);

    // Overlapping ranges: many draws of the whole mesh.
    nfsu::GeometryReport overlap;
    overlap.meshes.push_back(quad_mesh(0, "OVER", 0.0F, 0.1F, 0.9F));
    overlap.meshes[0].texture_hashes = {kHashB};
    for (int i = 0; i < 10; ++i) {
        overlap.meshes[0].materials.push_back(material(0, 6, 0, 1));
    }
    limits = nfsu::SceneLimits{};
    limits.max_draw_triangles = 19;
    check(!prepare(overlap, textures, nfsu::MeshSelection{}, scene, error, limits) &&
              empty_scene(scene), "overlapping ranges counted per draw");
    limits.max_draw_triangles = 20;
    check(prepare(overlap, textures, nfsu::MeshSelection{}, scene, error, limits),
          "overlapping ranges at the limit");
    nfsu::RenderLimits render_limits;
    render_limits.max_submitted_triangles = 19;
    std::string bytes;
    nfsu::RenderStats stats;
    check(!render(scene, bytes, stats, error, render_limits) &&
              error.find("draws 20 triangles") != std::string::npos,
          "render bounds submitted triangles: " + error);
}

void test_bindings_and_records() {
    const nfsu::GeometryReport geometry = scene_geometry();
    nfsu::PreparedCarScene scene;
    std::string error;
    check(prepare(geometry, scene_textures(), selection_of({by_index(0), by_index(2)}), scene,
                  error), "bindings: " + error);
    check(scene.draws.size() == 6, "binding draws");
    if (scene.draws.size() != 6) {
        return;
    }
    const nfsu::PreparedDraw& resolved = scene.draws[0];
    check(resolved.binding == nfsu::BindingKind::resolved && resolved.drawable &&
              resolved.texture == 0 && resolved.texture_hash == kHashA,
          "resolved draw");
    check(resolved.texture_source == 1 && resolved.texture_pack == 2 &&
              resolved.texture_record == 3, "resolved provenance");
    check(scene.textures.size() == 1 && scene.textures[0].hash == kHashA &&
              scene.textures[0].rgba == scene_textures().textures[1].rgba,
          "resolved texture copied once");
    const nfsu::PreparedDraw& unresolved = scene.draws[1];
    check(unresolved.binding == nfsu::BindingKind::unresolved && unresolved.drawable &&
              unresolved.texture == nfsu::kNoHandle && unresolved.texture_hash == kHashB &&
              unresolved.texture_source == 0, "unresolved draw");
    const nfsu::PreparedDraw& unusable = scene.draws[2];
    check(unusable.binding == nfsu::BindingKind::unusable && unusable.drawable &&
              unusable.texture == nfsu::kNoHandle && unusable.texture_hash == kHashC,
          "unusable draw");
    const nfsu::PreparedDraw& invalid_slot = scene.draws[3];
    check(invalid_slot.binding == nfsu::BindingKind::invalid_slot &&
              !invalid_slot.drawable && invalid_slot.texture_hash == 0 &&
              invalid_slot.texture_slot == 7, "invalid slot draw");
    const nfsu::PreparedDraw& bad_range = scene.draws[4];
    check(bad_range.binding == nfsu::BindingKind::unresolved && !bad_range.drawable &&
              bad_range.first_index == 1 && bad_range.index_count == 3,
          "bad range kept, not drawable");
    check(scene.draws[5].binding == nfsu::BindingKind::unresolved &&
              scene.draws[5].texture_hash == kHashE && scene.draws[5].mesh == 1 &&
              scene.draws[5].material_index == 3, "second mesh draw handle");
    check(scene.unresolved_hashes == std::vector<std::uint32_t>({kHashB, kHashE}),
          "unresolved hashes unique, ascending, including the bad range");
    for (const char* name : {"resolved", "unresolved", "unusable", "invalid_slot"}) {
        bool found = false;
        for (const nfsu::PreparedDraw& draw : scene.draws) {
            found = found || std::string(nfsu::binding_kind_name(draw.binding)) == name;
        }
        check(found, std::string("binding kind present: ") + name);
    }

    // Record fields and the raw 60 bytes are copied untouched.
    const nfsu::Mesh& wheel = geometry.meshes[2];
    for (std::size_t m = 0; m < wheel.materials.size(); ++m) {
        const nfsu::MeshMaterial& source = wheel.materials[m];
        const nfsu::PreparedDraw& draw = scene.draws[2 + m];
        check(draw.material_index == m && draw.first_index == source.first_index &&
                  draw.index_count == source.index_count &&
                  draw.texture_slot == source.texture_slot &&
                  draw.secondary_slot == source.secondary_slot &&
                  draw.flags == source.flags && draw.record_offset == source.offset,
              "record fields copied");
        check(std::memcmp(draw.raw.data(), source.raw, nfsu::kMaterialRecordSize) == 0 &&
                  std::memcmp(draw.bounds_min, source.bounds_min,
                              sizeof(draw.bounds_min)) == 0 &&
                  std::memcmp(draw.bounds_max, source.bounds_max,
                              sizeof(draw.bounds_max)) == 0,
              "raw record and bounds preserved");
    }

    // A resolved texture used twice is copied once and shared by handle.
    nfsu::PreparedCarScene shared;
    check(prepare(geometry, scene_textures(), selection_of({by_index(0), by_index(3)}), shared,
                  error) && shared.textures.size() == 1 &&
              shared.draws.size() == 3 && shared.draws[2].texture == 0,
          "one texture handle for two draws");
}

void test_determinism() {
    const nfsu::GeometryReport geometry = scene_geometry();
    const nfsu::RenderTextureSet textures = scene_textures();
    nfsu::PreparedCarScene first;
    nfsu::PreparedCarScene second;
    std::string error;
    const nfsu::MeshSelection selection = selection_of({by_name("DUP") /* ambiguous */});
    check(!prepare(geometry, textures, selection, first, error), "ambiguous fails");
    const nfsu::MeshSelection mixed = selection_of({by_index(4), by_name("BODY"), by_index(2)});
    check(prepare(geometry, textures, mixed, first, error) &&
              prepare(geometry, textures, mixed, second, error),
          "repeat preparation: " + error);
    check(same_scene(first, second), "repeat preparation is identical");
    std::string a;
    std::string b;
    nfsu::RenderStats sa;
    nfsu::RenderStats sb;
    check(render(first, a, sa, error) && render(first, b, sb, error) && a == b,
          "repeat render is byte-identical");
}

// The scene owns its data: changing or destroying the sources after
// preparation, including the hash lookup, changes nothing.
void test_lifetime() {
    std::string error;
    std::string before;
    nfsu::RenderStats stats_before;
    nfsu::PreparedCarScene scene;
    nfsu::PreparedCarScene snapshot;
    {
        nfsu::GeometryReport geometry = scene_geometry();
        nfsu::RenderTextureSet textures = scene_textures();
        check(prepare(geometry, textures, nfsu::MeshSelection{}, scene, error),
              "lifetime prepare: " + error);
        snapshot = scene;
        check(render(scene, before, stats_before, error), "lifetime render: " + error);

        // Point BODY's slot 0 at the unresolved hash: a fresh preparation
        // sees it, the prepared draw keeps its binding.
        geometry.meshes[0].texture_hashes[0] = kHashB;
        nfsu::PreparedCarScene fresh;
        check(prepare(geometry, textures, nfsu::MeshSelection{}, fresh, error) &&
                  fresh.draws[0].binding == nfsu::BindingKind::unresolved,
              "mutated lookup changes a new preparation");
        check(scene.draws[0].binding == nfsu::BindingKind::resolved &&
                  scene.draws[0].texture_hash == kHashA, "prepared binding kept");

        // Now wreck every source buffer the scene was prepared from.
        textures.textures[1].rgba.assign(textures.textures[1].rgba.size(), 0);
        textures.textures.clear();
        textures.textures.shrink_to_fit();
        for (nfsu::Mesh& mesh : geometry.meshes) {
            for (nfsu::MeshVertex& v : mesh.vertices) {
                v.px += 100.0F;
            }
            mesh.triangles.clear();
            mesh.triangles.shrink_to_fit();
            mesh.materials.clear();
        }
        geometry.meshes.clear();
        geometry.meshes.shrink_to_fit();
        std::string after;
        nfsu::RenderStats stats_after;
        check(render(scene, after, stats_after, error) && after == before,
              "render unchanged after the sources changed");
    }
    // Both sources are destroyed here.
    std::string after;
    nfsu::RenderStats stats_after;
    check(render(scene, after, stats_after, error) && after == before &&
              stats_after.groups_resolved == stats_before.groups_resolved &&
              stats_after.pixels_fallback == stats_before.pixels_fallback,
          "render unchanged after the sources are destroyed");
    check(same_scene(scene, snapshot), "scene unchanged");
}

// A scene changed after preparation is refused before the frame is
// allocated, never read out of bounds.
void test_hostile_scene() {
    const nfsu::GeometryReport geometry = scene_geometry();
    nfsu::PreparedCarScene good;
    std::string error;
    check(prepare(geometry, scene_textures(), selection_of({by_index(0), by_index(2)}), good,
                  error), "hostile base: " + error);
    check(nfsu::validate_prepared_scene(good, error), "base scene valid: " + error);
    const auto refused = [&](const nfsu::PreparedCarScene& scene,
                             const std::string& label) {
        nfsu::Frame frame;
        nfsu::RenderStats stats;
        std::string message;
        check(!nfsu::render_scene(scene, options(), nfsu::RenderLimits{}, frame, stats,
                                  message),
              label + " refused");
        check(frame.width() == 0 && frame.rgba().empty(), label + " allocates no frame");
        check(message.find("invalid prepared scene") != std::string::npos,
              label + " message: " + message);
    };
    nfsu::PreparedCarScene scene = good;
    scene.draws[0].first_index = 3;
    scene.draws[0].index_count = 6;
    refused(scene, "range past the end");
    scene = good;
    scene.draws[1].first_index = 0xFFFFFFFFU;
    scene.draws[1].index_count = 0xFFFFFFFFU;
    refused(scene, "wrapping range");
    scene = good;
    scene.draws[0].first_index = 1;
    refused(scene, "range not a multiple of 3");
    scene = good;
    scene.draws[4].drawable = true;
    refused(scene, "bad range made drawable");
    scene = good;
    scene.draws[0].drawable = false;
    refused(scene, "valid range made non-drawable");
    scene = good;
    scene.draws[3].drawable = true;
    refused(scene, "invalid slot made drawable");
    scene = good;
    scene.draws[0].mesh = 7;
    refused(scene, "mesh handle out of range");
    scene = good;
    scene.draws[5].mesh = 0;
    refused(scene, "mesh handle out of order");
    scene = good;
    scene.meshes[0].triangles.pop_back();
    refused(scene, "mesh shrunk under a draw");
    scene = good;
    scene.draws[0].texture = 1;
    refused(scene, "texture handle out of range");
    scene = good;
    scene.draws[0].texture = nfsu::kNoHandle;
    refused(scene, "resolved draw without texture");
    scene = good;
    scene.draws[1].texture = 0;
    refused(scene, "unresolved draw with a texture handle");
    scene = good;
    scene.draws[1].texture_source = 9;
    refused(scene, "unresolved draw with provenance");
    scene = good;
    scene.draws[3].texture_hash = kHashB;
    refused(scene, "invalid slot with a hash");
    scene = good;
    scene.draws[1].binding = static_cast<nfsu::BindingKind>(42);
    refused(scene, "unknown binding kind");
    scene = good;
    scene.textures[0].rgba.resize(8);
    refused(scene, "texture pixels shrunk");
    scene = good;
    scene.textures[0].width = 64;
    refused(scene, "texture width changed");
    scene = good;
    scene.textures[0].width = 0xFFFFFFFFU;
    scene.textures[0].height = 0xFFFFFFFFU;
    refused(scene, "texture size overflow");
    scene = good;
    scene.draws[0].texture_hash = kHashB;
    refused(scene, "texture hash changed");
    scene = good;
    scene.draws[0].texture_record = 9;
    refused(scene, "provenance changed");
    scene = good;
    scene.draws[0].index_count = 6;
    refused(scene, "decoded material changed away from raw record");
    scene = good;
    scene.draws[0].raw[12] ^= 1U;
    refused(scene, "raw material changed away from decoded fields");
    scene = good;
    scene.draws[1].material_index = 0;
    refused(scene, "draws duplicated in record order");
    scene = good;
    std::swap(scene.meshes[0], scene.meshes[1]);
    refused(scene, "meshes out of source order");
    scene = good;
    scene.meshes[1].source_index = 99;
    refused(scene, "mesh outside the source");
    scene = good;
    scene.meshes[1].selector = 0;
    refused(scene, "selector used twice");
    scene = good;
    scene.selectors[0].kind = static_cast<nfsu::MeshSelectorKind>(9);
    refused(scene, "unknown selector kind");
    scene = good;
    scene.selectors[0].index = 2;
    refused(scene, "index selector points at another mesh");
    scene = good;
    scene.selectors[0].name = "BODY";
    refused(scene, "index selector carries a name");
    scene = good;
    scene.selectors.clear();
    refused(scene, "profile without selectors");
    scene = good;
    scene.profile = static_cast<nfsu::SelectionProfile>(5);
    refused(scene, "unknown profile");
    scene = good;
    scene.unresolved_hashes = {kHashE, kHashB};
    refused(scene, "unsorted unresolved hashes");
    scene = good;
    scene.unresolved_hashes = {kHashB};
    refused(scene, "missing unresolved hash");
    scene = good;
    scene.unresolved_hashes = {kHashB, kHashC, kHashE};
    refused(scene, "extra unresolved hash");
    scene = good;
    scene.draws.pop_back();
    refused(scene, "last material draw removed");
    scene = good;
    scene.meshes[0].material_count += 1;
    refused(scene, "material count changed");
    scene = good;
    scene.textures.push_back(scene.textures[0]);
    refused(scene, "unused texture added");

    nfsu::PreparedCarScene all;
    check(prepare(geometry, scene_textures(), nfsu::MeshSelection{}, all, error),
          "all-profile hostile base: " + error);
    all.skipped_invalid_meshes.clear();
    refused(all, "all profile omits a source mesh");

    nfsu::PreparedCarScene named;
    check(prepare(geometry, scene_textures(),
                  selection_of({by_name("BODY"), by_name("WHEEL")}), named, error),
          "name-selector hostile base: " + error);
    scene = named;
    scene.selectors[0].name = "WHEEL";
    refused(scene, "name selector points at another mesh");
    scene = named;
    scene.selectors[0].index = 1;
    refused(scene, "name selector carries an index");

    nfsu::PreparedCarScene hashed;
    check(prepare(geometry, scene_textures(),
                  selection_of({by_hash(kMeshHashBody)}), hashed, error),
          "hash-selector hostile base: " + error);
    scene = hashed;
    scene.selectors[0].hash = kMeshHashWheel;
    refused(scene, "hash selector points at another mesh");
    scene = hashed;
    scene.meshes[0].directory_hash = kMeshHashWheel;
    refused(scene, "prepared mesh hash changed");
    scene = hashed;
    scene.selectors[0].name = "BODY";
    refused(scene, "hash selector carries a name");

    nfsu::RenderTextureSet two_textures = scene_textures();
    two_textures.textures.push_back(texture(kHashE, 2, 77));
    nfsu::PreparedCarScene two_handles;
    check(prepare(geometry, two_textures,
                  selection_of({by_index(0), by_index(2)}), two_handles, error) &&
              two_handles.textures.size() == 2,
          "two-handle hostile base: " + error);
    if (two_handles.textures.size() == 2) {
        std::swap(two_handles.textures[0], two_handles.textures[1]);
        for (nfsu::PreparedDraw& draw : two_handles.draws) {
            if (draw.binding == nfsu::BindingKind::resolved) {
                draw.texture = 1U - draw.texture;
            }
        }
        refused(two_handles, "texture handles reordered away from first use");
    }

    nfsu::PreparedCarScene same_hash;
    check(prepare(geometry, scene_textures(),
                  selection_of({by_index(0), by_index(3)}), same_hash, error),
          "same-hash hostile base: " + error);
    if (same_hash.draws.size() == 3) {
        nfsu::PreparedDraw& changed = same_hash.draws[2];
        changed.binding = nfsu::BindingKind::unusable;
        changed.texture = nfsu::kNoHandle;
        changed.texture_source = 0;
        changed.texture_pack = 0;
        changed.texture_record = 0;
        refused(same_hash, "same hash given inconsistent bindings");
    }
    scene = good;
    scene.textures[0].claims[0] = kHashB;
    scene.textures[0].claim_count = 1;
    refused(scene, "unresolved hash claimed by a prepared texture");

    // Vertex indices are checked per triangle, as for source data.
    scene = good;
    scene.meshes[0].triangles[0].b = 60000;
    std::string bytes;
    nfsu::RenderStats stats;
    check(render(scene, bytes, stats, error) && stats.triangles_bad_index == 1,
          "corrupted vertex index counted, not read: " + error);

    // An untouched copy still renders.
    check(render(good, bytes, stats, error), "unchanged scene renders: " + error);
}

void test_failure_leaves_outputs_empty() {
    const nfsu::GeometryReport geometry = scene_geometry();
    const nfsu::RenderTextureSet textures = scene_textures();
    nfsu::PreparedCarScene scene;
    std::string error;
    check(prepare(geometry, textures, nfsu::MeshSelection{}, scene, error), "fill");
    check(!prepare(geometry, textures, selection_of({by_index(1)}), scene, error) &&
              empty_scene(scene), "failed preparation empties the scene");

    check(prepare(geometry, textures, nfsu::MeshSelection{}, scene, error), "refill");
    nfsu::Frame frame;
    nfsu::RenderStats stats;
    nfsu::RenderLimits limits;
    limits.max_source_triangles = 7;
    check(!nfsu::render_scene(scene, options(), limits, frame, stats, error) &&
              frame.width() == 0, "triangle limit before the frame");
    nfsu::RenderOptions bad = options();
    bad.camera.pitch_deg = 95.0;
    check(!nfsu::render_scene(scene, bad, nfsu::RenderLimits{}, frame, stats, error) &&
              frame.width() == 0, "bad camera before the frame");

    // An empty scene (no valid meshes) renders the background only.
    nfsu::GeometryReport none;
    none.meshes.resize(2);
    check(prepare(none, textures, nfsu::MeshSelection{}, scene, error) &&
              scene.meshes.empty() && scene.skipped_invalid_meshes.size() == 2,
          "no valid meshes prepares an empty draw list");
    check(nfsu::render_scene(scene, options(), nfsu::RenderLimits{}, frame, stats,
                             error) &&
              stats.pixels_drawn == 0 && stats.meshes_skipped_invalid == 2,
          "empty scene renders background");
}

} // namespace

int main() {
    test_all_meshes();
    test_selection();
    test_selection_errors();
    test_limits();
    test_bindings_and_records();
    test_determinism();
    test_lifetime();
    test_hostile_scene();
    test_failure_leaves_outputs_empty();

    if (g_failures != 0) {
        std::cerr << g_failures << " scene test(s) failed\n";
        return 1;
    }
    std::cout << "scene tests passed\n";
    return 0;
}
