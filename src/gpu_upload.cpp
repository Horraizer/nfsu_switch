#include "nfsu/gpu_upload.h"

#include <algorithm>
#include <new>
#include <utility>

namespace nfsu {

namespace {

static_assert(sizeof(MeshVertex) == 36,
              "the car vertex buffer uploads MeshVertex as it is laid out in memory");
static_assert(sizeof(TrackVertex) == kTrackVertexStride,
              "the track vertex buffer uploads TrackVertex, whose size TrackDrawCost counts");
static_assert(sizeof(MeshTriangle) == 3 * sizeof(std::uint16_t) &&
                  alignof(MeshTriangle) == alignof(std::uint16_t),
              "a triangle list is uploaded as the u16 index stream it already is");

constexpr std::uint64_t kRgba8TexelBytes = 4;

std::string hex32(std::uint32_t value) {
    static const char digits[] = "0123456789ABCDEF";
    std::string text = "0x00000000";
    for (std::size_t i = 0; i < 8; ++i) {
        text[9 - i] = digits[(value >> (4U * i)) & 0xFU];
    }
    return text;
}

// A stored mesh name as it appears in a resource name: printable ASCII kept,
// anything else replaced, so a name built from file bytes stays ASCII.
std::string printable(const std::string& text) {
    std::string out = text;
    for (char& ch : out) {
        const auto byte = static_cast<unsigned char>(ch);
        if (byte < 0x20U || byte > 0x7EU) {
            ch = '?';
        }
    }
    return out;
}

bool checked_multiply(std::uint64_t a, std::uint64_t b, std::uint64_t& out) noexcept {
    if (a != 0 && b > UINT64_MAX / a) {
        return false;
    }
    out = a * b;
    return true;
}

struct BufferSpec {
    UploadResourceKind kind = UploadResourceKind::vertex_buffer;
    std::string name;
    std::size_t source = 0;
    const unsigned char* data = nullptr;
    std::uint64_t elements = 0;
    std::uint64_t element_bytes = 0;
    std::uint64_t alignment = 0;
};

// Appends one buffer, or counts it as empty. Fails only on a buffer too big
// for one memory block.
bool add_buffer(const BufferSpec& spec, UploadTable& table, std::string& error) {
    if (spec.elements == 0) {
        ++table.totals.empty_buffers;
        return true;
    }
    std::uint64_t bytes = 0;
    if (!checked_multiply(spec.elements, spec.element_bytes, bytes) ||
        bytes > kMaxUploadResourceBytes) {
        error = spec.name + " holds " + std::to_string(spec.elements) + " elements of " +
                std::to_string(spec.element_bytes) + " bytes, more than the " +
                std::to_string(kMaxUploadResourceBytes) +
                " bytes one deko3d memory block can hold";
        return false;
    }
    UploadResource resource;
    resource.kind = spec.kind;
    resource.lifetime = UploadLifetime::scene;
    resource.memory = UploadMemory::buffer;
    resource.name = spec.name;
    resource.source = spec.source;
    resource.data = spec.data;
    resource.bytes = bytes;
    resource.alignment = spec.alignment;
    resource.element_bytes = spec.element_bytes;
    resource.elements = spec.elements;
    table.resources.push_back(std::move(resource));
    ++table.totals.resources;
    if (spec.kind == UploadResourceKind::vertex_buffer) {
        ++table.totals.vertex_buffers;
        table.totals.vertex_bytes += bytes;
    } else {
        ++table.totals.index_buffers;
        table.totals.index_bytes += bytes;
    }
    return true;
}

bool add_texture(const std::string& name, std::size_t source, const RenderTexture& texture,
                 UploadLifetime lifetime, UploadTable& table, std::string& error) {
    TextureUploadShape shape;
    std::string refusal;
    const auto format = texture.storage == RenderTextureStorage::dxt1 ? TextureUploadFormat::dxt1 :
                        texture.storage == RenderTextureStorage::dxt3 ? TextureUploadFormat::dxt3 :
                        TextureUploadFormat::rgba8;
    if (!texture.usable()) { error = name + ": invalid texture payload"; return false; }
    if (!check_texture_upload_format(format, texture.width,
                                     texture.height, texture.payload().size(), shape,
                                     refusal)) {
        error = name + ": " + refusal;
        return false;
    }
    UploadResource resource;
    resource.kind = UploadResourceKind::texture;
    resource.lifetime = lifetime;
    resource.memory = UploadMemory::image;
    resource.name = name;
    resource.source = source;
    resource.data = texture.payload().data();
    resource.bytes = shape.bytes;
    resource.alignment = kTextureStagingAlignment;
    resource.element_bytes = shape.texel_bytes;
    resource.elements = shape.bytes / shape.texel_bytes;
    resource.texture_hash = texture.hash;
    resource.texture = shape;
    table.resources.push_back(std::move(resource));
    ++table.totals.resources;
    ++table.totals.textures;
    table.totals.texture_bytes += shape.bytes;
    if (lifetime == UploadLifetime::generated) {
        ++table.totals.generated_textures;
        table.totals.generated_texture_bytes += shape.bytes;
    }
    if (shape.bytes > table.totals.largest_texture_bytes) {
        table.totals.largest_texture_bytes = shape.bytes;
    }
    return true;
}

std::string mesh_label(const char* scene, std::size_t index, const std::string& name) {
    std::string label = std::string(scene) + " mesh " + std::to_string(index);
    if (!name.empty()) {
        label += " " + printable(name);
    }
    return label;
}

} // namespace

// ---------------------------------------------------------------------------
// The format gate
// ---------------------------------------------------------------------------

const char* texture_upload_format_name(TextureUploadFormat format) noexcept {
    switch (format) {
    case TextureUploadFormat::rgba8:
        return "RGBA8";
    case TextureUploadFormat::dxt1:
        return "DXT1";
    case TextureUploadFormat::dxt3:
        return "DXT3";
    case TextureUploadFormat::a8r8g8b8:
        return "A8R8G8B8";
    case TextureUploadFormat::p8:
        return "P8";
    case TextureUploadFormat::unsupported:
        break;
    }
    return "unsupported";
}

bool check_texture_upload_format(
    TextureUploadFormat format,
    std::uint32_t width,
    std::uint32_t height,
    std::uint64_t byte_count,
    TextureUploadShape& shape,
    std::string& error) {
    shape = TextureUploadShape{};
    const std::string named = std::string("texture format ") +
                              texture_upload_format_name(format) + " is not uploadable: ";
    switch (format) {
    case TextureUploadFormat::rgba8:
    case TextureUploadFormat::dxt1:
    case TextureUploadFormat::dxt3:
        break;
    case TextureUploadFormat::a8r8g8b8:
        error = named + "the decode path turns a " + texture_upload_format_name(format) +
                " level into RGBA8 and no prepared scene holds the stored payload;"
                " upload the decoded RGBA8";
        return false;
    case TextureUploadFormat::p8:
        error = named + "decode_level refuses a P8 level because it does not carry its"
                        " 256-entry palette, and nothing in this project expands one"
                        " through its palette, so no P8 texture reaches a prepared scene";
        return false;
    case TextureUploadFormat::unsupported:
    default:
        error = named + "the pack parser rejects a record of this format before any"
                        " decode is tried";
        return false;
    }
    if (width == 0 || height == 0) {
        error = std::string(texture_upload_format_name(format)) + " texture " + std::to_string(width) + "x" + std::to_string(height) +
                " has a zero dimension";
        return false;
    }
    // At most (2^32 - 1)^2 * 4, which does not fit in 64 bits, so the
    // product is checked rather than trusted.
    std::uint64_t texels = 0;
    std::uint64_t want = 0;
    const bool compressed = format != TextureUploadFormat::rgba8;
    const std::uint64_t columns = compressed ? (std::uint64_t{width} + 3) / 4 : width;
    const std::uint64_t rows = compressed ? (std::uint64_t{height} + 3) / 4 : height;
    const std::uint64_t stride = !compressed ? kRgba8TexelBytes :
                               format == TextureUploadFormat::dxt1 ? 8 : 16;
    if (!checked_multiply(columns, rows, texels) ||
        !checked_multiply(texels, stride, want)) {
        error = std::string(texture_upload_format_name(format)) + " texture " + std::to_string(width) + "x" + std::to_string(height) +
                " is larger than 2^64 - 1 bytes";
        return false;
    }
    if (byte_count != want) {
        error = std::string(texture_upload_format_name(format)) + " texture " + std::to_string(width) + "x" + std::to_string(height) +
                " needs exactly " + std::to_string(want) + " bytes, got " +
                std::to_string(byte_count);
        return false;
    }
    if (want > kMaxUploadResourceBytes) {
        error = std::string(texture_upload_format_name(format)) + " texture " + std::to_string(width) + "x" + std::to_string(height) +
                " is " + std::to_string(want) + " bytes, more than the " +
                std::to_string(kMaxUploadResourceBytes) +
                " bytes one deko3d memory block can hold";
        return false;
    }
    shape.format = format;
    shape.width = width;
    shape.height = height;
    shape.texel_bytes = stride;
    shape.row_bytes = columns * stride;
    shape.bytes = want;
    return true;
}

// ---------------------------------------------------------------------------
// The resource table
// ---------------------------------------------------------------------------

const char* upload_resource_kind_name(UploadResourceKind kind) noexcept {
    switch (kind) {
    case UploadResourceKind::vertex_buffer:
        return "vertex buffer";
    case UploadResourceKind::index_buffer:
        return "index buffer";
    case UploadResourceKind::texture:
        return "texture";
    }
    return "unknown";
}

const char* upload_lifetime_name(UploadLifetime lifetime) noexcept {
    switch (lifetime) {
    case UploadLifetime::scene:
        return "scene";
    case UploadLifetime::generated:
        return "generated";
    }
    return "unknown";
}

const char* upload_memory_name(UploadMemory memory) noexcept {
    switch (memory) {
    case UploadMemory::buffer:
        return "buffer";
    case UploadMemory::image:
        return "image";
    }
    return "unknown";
}

bool build_car_upload_table(
    const PreparedCarScene& scene,
    std::size_t generated_texture_source,
    UploadTable& table,
    std::string& error) {
    table = UploadTable{};
    std::string invalid;
    if (!validate_prepared_scene(scene, invalid)) {
        error = "car scene refused: " + invalid;
        return false;
    }
    UploadTable result;
    try {
        result.resources.reserve(scene.meshes.size() * 2U + scene.textures.size());
        for (std::size_t m = 0; m < scene.meshes.size(); ++m) {
            const PreparedMesh& mesh = scene.meshes[m];
            const std::string label = mesh_label("car", m, mesh.name);
            BufferSpec vertices;
            vertices.kind = UploadResourceKind::vertex_buffer;
            vertices.name = label + " vertex buffer";
            vertices.source = m;
            vertices.data = reinterpret_cast<const unsigned char*>(mesh.vertices.data());
            vertices.elements = mesh.vertices.size();
            vertices.element_bytes = sizeof(MeshVertex);
            vertices.alignment = kCarVertexAlignment;
            BufferSpec indices;
            indices.kind = UploadResourceKind::index_buffer;
            indices.name = label + " index buffer";
            indices.source = m;
            indices.data = reinterpret_cast<const unsigned char*>(mesh.triangles.data());
            indices.elements = std::uint64_t{mesh.triangles.size()} * 3U;
            indices.element_bytes = sizeof(std::uint16_t);
            indices.alignment = kIndexAlignment;
            if (!add_buffer(vertices, result, error) || !add_buffer(indices, result, error)) {
                return false;
            }
        }
        for (std::size_t t = 0; t < scene.textures.size(); ++t) {
            const RenderTexture& texture = scene.textures[t];
            const bool generated = generated_texture_source != kNoHandle &&
                                   texture.source == generated_texture_source;
            const std::string name = std::string(generated ? "car generated texture "
                                                           : "car texture ") +
                                     std::to_string(t) + " hash " + hex32(texture.hash);
            if (!add_texture(name, t, texture,
                             generated ? UploadLifetime::generated : UploadLifetime::scene,
                             result, error)) {
                return false;
            }
        }
    } catch (const std::bad_alloc&) {
        error = "out of memory while building the car upload table";
        return false;
    }
    table = std::move(result);
    return true;
}

bool build_track_upload_table(
    const PreparedTrackScene& scene,
    UploadTable& table,
    std::string& error) {
    table = UploadTable{};
    std::string invalid;
    if (!validate_prepared_track_scene(scene, invalid)) {
        error = "track scene refused: " + invalid;
        return false;
    }
    const PreparedTrackRegion& region = scene.region;
    UploadTable result;
    try {
        result.resources.reserve(region.meshes.size() * 2U + region.textures.size());
        for (std::size_t m = 0; m < region.meshes.size(); ++m) {
            const PreparedTrackMesh& mesh = region.meshes[m];
            const std::string label = mesh_label("track", m, mesh.name);
            BufferSpec vertices;
            vertices.kind = UploadResourceKind::vertex_buffer;
            vertices.name = label + " vertex buffer";
            vertices.source = m;
            vertices.data = reinterpret_cast<const unsigned char*>(mesh.vertices.data());
            vertices.elements = mesh.vertices.size();
            vertices.element_bytes = sizeof(TrackVertex);
            vertices.alignment = kTrackVertexAlignment;
            BufferSpec indices;
            indices.kind = UploadResourceKind::index_buffer;
            indices.name = label + " index buffer";
            indices.source = m;
            indices.data = reinterpret_cast<const unsigned char*>(mesh.triangles.data());
            indices.elements = std::uint64_t{mesh.triangles.size()} * 3U;
            indices.element_bytes = sizeof(std::uint16_t);
            indices.alignment = kIndexAlignment;
            if (!add_buffer(vertices, result, error) || !add_buffer(indices, result, error)) {
                return false;
            }
        }
        for (std::size_t t = 0; t < region.textures.size(); ++t) {
            const RenderTexture& texture = region.textures[t];
            const std::string name =
                "track texture " + std::to_string(t) + " hash " + hex32(texture.hash);
            if (!add_texture(name, t, texture, UploadLifetime::scene, result, error)) {
                return false;
            }
        }
    } catch (const std::bad_alloc&) {
        error = "out of memory while building the track upload table";
        return false;
    }
    table = std::move(result);
    return true;
}

// ---------------------------------------------------------------------------
// The ledger
// ---------------------------------------------------------------------------

bool bounded_image_layout(const UploadResource& texture,
                          std::uint64_t& bytes, std::uint64_t& alignment,
                          std::string& error) {
    bytes = alignment = 0;
    TextureUploadShape shape;
    if (texture.kind != UploadResourceKind::texture ||
        !check_texture_upload_format(texture.texture.format, texture.texture.width,
            texture.texture.height, texture.bytes, shape, error)) {
        if (error.empty()) error = texture.name + ": expected a texture";
        return false;
    }
    if (shape.width > 16384 || shape.height > 16384) {
        error = texture.name + ": image exceeds the 2D dimension limit";
        return false;
    }
    bytes = ((shape.row_bytes + 63) & ~std::uint64_t{63}) *
            ((shape.rows() + 127) & ~std::uint64_t{127});
    bytes = (bytes + 4095) & ~std::uint64_t{4095};
    alignment = kDekoMemBlockAlignment;
    return true;
}

bool linear_image_layout(const UploadResource& texture,
                         std::uint64_t& bytes,
                         std::uint64_t& alignment,
                         std::string& error) {
    if (texture.kind != UploadResourceKind::texture) {
        error = texture.name + " is not a texture";
        return false;
    }
    bytes = texture.bytes;
    alignment = kTextureStagingAlignment;
    return true;
}

bool UploadLedger::create(const UploadArenas& arenas,
                          ImageLayoutQuery image_layout,
                          std::uint32_t max_resources,
                          std::string& error) {
    if (live_ != 0) {
        error = "upload ledger still tracks " + std::to_string(live_) +
                " resources; release and retire them first";
        return false;
    }
    if (arenas.buffers == nullptr || arenas.images == nullptr) {
        error = "upload ledger needs a buffer arena and an image arena";
        return false;
    }
    if (!image_layout) {
        error = "upload ledger needs an image layout query";
        return false;
    }
    if (max_resources == 0 || max_resources > kMaxArenaAllocations) {
        error = "upload ledger max resources " + std::to_string(max_resources) +
                " is outside 1.." + std::to_string(kMaxArenaAllocations);
        return false;
    }
    std::vector<Slot> slots;
    std::vector<std::uint32_t> free_slots;
    try {
        slots.reserve(max_resources);
        free_slots.reserve(max_resources);
    } catch (const std::bad_alloc&) {
        error = "out of memory reserving the upload ledger for " +
                std::to_string(max_resources) + " resources";
        return false;
    }
    std::uint64_t floor = generation_floor_;
    for (const Slot& slot : slots_) {
        floor = std::max(floor, slot.generation + 1U);
    }
    arenas_ = arenas;
    image_layout_ = std::move(image_layout);
    generation_floor_ = floor;
    max_resources_ = max_resources;
    slots_.swap(slots);
    free_slots_.swap(free_slots);
    live_ = 0;
    uploads_ = 0;
    releases_ = 0;
    reclaimed_ = 0;
    refused_uploads_ = 0;
    refused_releases_ = 0;
    lost_placements_ = 0;
    completed_ = 0;
    return true;
}

std::uint32_t UploadLedger::take_slot() noexcept {
    if (!free_slots_.empty()) {
        const std::uint32_t index = free_slots_.back();
        free_slots_.pop_back();
        return index;
    }
    // live_ < max_resources_ and every slot that is not live is on the free
    // list, so there is reserved room for one more.
    slots_.emplace_back();
    slots_.back().generation = generation_floor_;
    return static_cast<std::uint32_t>(slots_.size() - 1U);
}

bool UploadLedger::place(const UploadResource& resource, std::uint64_t fence, Slot& slot,
                         std::string& error) {
    const bool texture = resource.kind == UploadResourceKind::texture;
    if (texture != (resource.memory == UploadMemory::image)) {
        error = "cannot upload " + resource.name + ": a " +
                upload_resource_kind_name(resource.kind) + " does not belong in " +
                upload_memory_name(resource.memory) + " memory";
        return false;
    }
    if (resource.bytes == 0 || resource.bytes > kMaxUploadResourceBytes) {
        error = "cannot upload " + resource.name + ": " + std::to_string(resource.bytes) +
                " bytes is outside 1.." + std::to_string(kMaxUploadResourceBytes);
        return false;
    }
    std::uint64_t bytes = resource.bytes;
    std::uint64_t alignment = resource.alignment;
    MemoryArena* arena = arenas_.buffers;
    if (texture) {
        if (arenas_.staging == nullptr) {
            error = "cannot upload " + resource.name + ": this ledger has no staging arena";
            return false;
        }
        std::string layout_error;
        if (!image_layout_(resource, bytes, alignment, layout_error)) {
            error = "cannot upload " + resource.name + ": image layout refused: " +
                    layout_error;
            return false;
        }
        if (bytes < resource.bytes || bytes > kMaxUploadResourceBytes) {
            error = "cannot upload " + resource.name + ": the image layout needs " +
                    std::to_string(bytes) + " bytes for " + std::to_string(resource.bytes) +
                    " bytes of texels; a layout is at least the texels and at most " +
                    std::to_string(kMaxUploadResourceBytes);
            return false;
        }
        arena = arenas_.images;
    }

    ArenaAllocation placement;
    const ArenaRefusal placed = arena->allocate(bytes, alignment, placement);
    if (placed != ArenaRefusal::none) {
        error = "cannot upload " + resource.name + ": the " +
                memory_category_name(arena->config().category) + " arena for " +
                upload_memory_name(resource.memory) + " memory refused " +
                std::to_string(bytes) + " bytes at alignment " + std::to_string(alignment) +
                ": " + arena_refusal_name(placed);
        return false;
    }
    ArenaAllocation staging;
    if (texture) {
        const ArenaRefusal staged =
            arenas_.staging->allocate(resource.bytes, resource.alignment, staging);
        if (staged != ArenaRefusal::none) {
            arena->release(placement.handle);
            error = "cannot upload " + resource.name + ": the " +
                    memory_category_name(arenas_.staging->config().category) +
                    " arena for staging refused " + std::to_string(resource.bytes) +
                    " bytes at alignment " + std::to_string(resource.alignment) + ": " +
                    arena_refusal_name(staged);
            return false;
        }
    }

    UploadRecord& record = slot.record;
    record.kind = resource.kind;
    record.lifetime = resource.lifetime;
    record.memory = resource.memory;
    record.placement = placement;
    record.staged = texture;
    record.staging = texture ? staging : ArenaAllocation{};
    record.upload_fence = fence;
    record.released = false;
    record.release_fence = 0;
    return true;
}

bool UploadLedger::upload(const UploadResource& resource,
                          std::uint64_t fence,
                          UploadHandle& handle,
                          std::string& error) {
    if (max_resources_ == 0) {
        ++refused_uploads_;
        error = "cannot upload " + resource.name + ": the upload ledger was not created";
        return false;
    }
    if (live_ >= max_resources_) {
        ++refused_uploads_;
        error = "cannot upload " + resource.name + ": the ledger already tracks " +
                std::to_string(live_) + " resources, its limit";
        return false;
    }
    // The one step that allocates, taken before any arena is touched.
    std::string name;
    try {
        name = resource.name;
    } catch (const std::bad_alloc&) {
        ++refused_uploads_;
        error = "out of memory recording an upload";
        return false;
    }
    Slot scratch;
    if (!place(resource, fence, scratch, error)) {
        ++refused_uploads_;
        return false;
    }
    const std::uint32_t index = take_slot();
    Slot& slot = slots_[index];
    slot.record = std::move(scratch.record);
    slot.record.name = std::move(name);
    slot.live = true;
    ++live_;
    ++uploads_;
    handle = UploadHandle{index, slot.generation};
    return true;
}

bool UploadLedger::upload_table(const UploadTable& table,
                                std::uint64_t fence,
                                std::vector<UploadHandle>& handles,
                                std::string& error) {
    handles.clear();
    std::vector<UploadHandle> placed;
    try {
        placed.reserve(table.resources.size());
    } catch (const std::bad_alloc&) {
        ++refused_uploads_;
        error = "out of memory recording an upload table";
        return false;
    }
    for (const UploadResource& resource : table.resources) {
        UploadHandle handle;
        if (!upload(resource, fence, handle, error)) {
            // Nothing was submitted, so every placement goes back at once,
            // newest first, and the calls are taken back out of the counts.
            for (std::size_t k = placed.size(); k > 0; --k) {
                reclaim(placed[k - 1].slot);
                --uploads_;
                --reclaimed_;
            }
            return false;
        }
        placed.push_back(handle);
    }
    handles.swap(placed);
    return true;
}

bool UploadLedger::release(const UploadHandle& handle,
                           std::uint64_t last_use_fence,
                           std::string& error) {
    if (handle.slot >= slots_.size()) {
        ++refused_releases_;
        error = "release of an upload handle this ledger never issued";
        return false;
    }
    Slot& slot = slots_[handle.slot];
    if (handle.generation > slot.generation ||
        (handle.generation == slot.generation && !slot.live)) {
        ++refused_releases_;
        error = "release of an upload handle this ledger never issued";
        return false;
    }
    if (handle.generation < slot.generation) {
        ++refused_releases_;
        // The record outlives its reclaim until the slot is reused, so the
        // name is still the right one exactly when the slot moved on by one
        // generation and holds nothing yet.
        if (!slot.live && slot.generation == handle.generation + 1U) {
            error = "double release of " + slot.record.name +
                    ": it was released and reclaimed already";
        } else {
            error = "double release of an upload reclaimed earlier; its slot " +
                    std::to_string(handle.slot) + " has been reused";
        }
        return false;
    }
    UploadRecord& record = slot.record;
    if (record.released) {
        ++refused_releases_;
        error = "double release of " + record.name + ": already released at fence " +
                std::to_string(record.release_fence) + " and not yet reclaimed";
        return false;
    }
    record.released = true;
    record.release_fence = last_use_fence;
    ++releases_;
    return true;
}

bool UploadLedger::release_all(const std::vector<UploadHandle>& handles,
                               std::uint64_t last_use_fence,
                               std::string& error) {
    for (const UploadHandle& handle : handles) {
        if (!release(handle, last_use_fence, error)) {
            return false;
        }
    }
    return true;
}

void UploadLedger::reclaim(std::uint32_t index) noexcept {
    Slot& slot = slots_[index];
    UploadRecord& record = slot.record;
    MemoryArena* arena =
        record.memory == UploadMemory::image ? arenas_.images : arenas_.buffers;
    if (arena->release(record.placement.handle) != ArenaRefusal::none) {
        ++lost_placements_;
    }
    if (record.staged) {
        if (arenas_.staging->release(record.staging.handle) != ArenaRefusal::none) {
            ++lost_placements_;
        }
        record.staged = false;
    }
    slot.live = false;
    ++slot.generation;
    free_slots_.push_back(index);
    --live_;
    ++reclaimed_;
}

void UploadLedger::retire(std::uint64_t completed) noexcept {
    if (completed > completed_) {
        completed_ = completed;
    }
    for (std::size_t i = 0; i < slots_.size(); ++i) {
        Slot& slot = slots_[i];
        if (!slot.live) {
            continue;
        }
        UploadRecord& record = slot.record;
        if (record.staged && record.upload_fence <= completed_) {
            if (arenas_.staging->release(record.staging.handle) != ArenaRefusal::none) {
                ++lost_placements_;
            }
            record.staged = false;
        }
        if (record.released && record.release_fence <= completed_ &&
            record.upload_fence <= completed_) {
            reclaim(static_cast<std::uint32_t>(i));
        }
    }
}

std::vector<std::string> UploadLedger::unreleased() const {
    std::vector<std::string> names;
    for (const Slot& slot : slots_) {
        if (slot.live && !slot.record.released) {
            names.push_back(slot.record.name);
        }
    }
    return names;
}

std::vector<std::string> UploadLedger::releasing() const {
    std::vector<std::string> names;
    for (const Slot& slot : slots_) {
        if (slot.live && slot.record.released) {
            names.push_back(slot.record.name);
        }
    }
    return names;
}

bool UploadLedger::find(const UploadHandle& handle, UploadRecord& record) const {
    if (handle.slot >= slots_.size()) {
        return false;
    }
    const Slot& slot = slots_[handle.slot];
    if (!slot.live || slot.generation != handle.generation) {
        return false;
    }
    record = slot.record;
    return true;
}

UploadLedgerStats UploadLedger::stats() const noexcept {
    UploadLedgerStats stats;
    for (const Slot& slot : slots_) {
        if (!slot.live) {
            continue;
        }
        const UploadRecord& record = slot.record;
        if (record.released) {
            ++stats.releasing;
            stats.releasing_bytes += record.placement.size;
        } else {
            ++stats.resident;
            stats.resident_bytes += record.placement.size;
        }
        if (record.staged) {
            ++stats.staged;
            stats.staged_bytes += record.staging.size;
        }
    }
    stats.uploads = uploads_;
    stats.releases = releases_;
    stats.reclaimed = reclaimed_;
    stats.refused_uploads = refused_uploads_;
    stats.refused_releases = refused_releases_;
    stats.lost_placements = lost_placements_;
    stats.completed_fence = completed_;
    return stats;
}

} // namespace nfsu
