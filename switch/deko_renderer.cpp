#include "deko_renderer.h"
#include "nfsu/memory_budget.h"
#include "nfsu/draw_execution.h"
#include "nfsu/deko_gpfifo_budget.h"
#include "nfsu/deko_memory_usage.h"
#include "nfsu/deko_draw_order.h"

#include <deko3d.hpp>
#include <switch.h>
#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <new>
#include <utility>
#include <vector>

namespace nfsu {
namespace {
namespace sm = shader_manifest;
constexpr std::uint32_t kDataFlags = DkMemBlockFlags_CpuUncached | DkMemBlockFlags_GpuCached;
constexpr std::uint32_t kImageFlags = DkMemBlockFlags_GpuCached | DkMemBlockFlags_Image;
static_assert(DK_MEMBLOCK_ALIGNMENT == kDekoMemBlockAlignment);
static_assert(DK_IMAGE_LINEAR_STRIDE_ALIGNMENT == kDekoImageLinearStrideAlignment);
static_assert(DK_UNIFORM_BUF_ALIGNMENT == kDekoUniformBufferAlignment);
static_assert(sizeof(DkImageDescriptor) == kDrawDescriptorBytes);
static_assert(sizeof(DkSamplerDescriptor) == kDrawDescriptorBytes);

std::uint64_t align_up(std::uint64_t n, std::uint64_t a) { return (n + a - 1) & ~(a - 1); }
double elapsed_ms(std::uint64_t start) {
    return armTicksToNs(armGetSystemTick() - start) / 1000000.0;
}
constexpr DkCompareOp kCompare[] = {DkCompareOp_Never, DkCompareOp_Less,
    DkCompareOp_Equal, DkCompareOp_Lequal, DkCompareOp_Greater, DkCompareOp_NotEqual,
    DkCompareOp_Gequal, DkCompareOp_Always};
constexpr DkBlendFactor kFactor[] = {DkBlendFactor_Zero, DkBlendFactor_One,
    DkBlendFactor_SrcColor, DkBlendFactor_InvSrcColor, DkBlendFactor_SrcAlpha,
    DkBlendFactor_InvSrcAlpha, DkBlendFactor_DstAlpha, DkBlendFactor_InvDstAlpha,
    DkBlendFactor_DstColor, DkBlendFactor_InvDstColor};
constexpr DkBlendOp kBlend[] = {DkBlendOp_Add, DkBlendOp_Sub, DkBlendOp_RevSub,
    DkBlendOp_Min, DkBlendOp_Max};
} // namespace

struct DekoRenderer::Impl {
    // Destroy in reverse order: command buffer and swapchain, memory, queue,
    // then device. shutdown() first waits for every last use of that memory.
    dk::UniqueDevice device;
    dk::UniqueQueue queue;
    std::vector<dk::UniqueMemBlock> blocks;
    dk::UniqueSwapchain swapchain;
    dk::UniqueCmdBuf cmd;
    std::array<dk::UniqueCmdBuf, 2> static_cmd;
    dk::MemBlock buffers, images, staging, uniforms, static_uniforms, descriptors, readback;
    dk::MemBlock query_memory;
    std::array<dk::MemBlock, 2> static_command_memory;
    std::array<dk::MemBlock, 2> command_memory;
    MemoryArena buffer_arena, image_arena, staging_arena, uniform_arena;
    UploadLedger ledger;
    std::vector<UploadHandle> handles;
    std::vector<UploadRecord> records;
    std::vector<dk::Image> textures;
    std::vector<ArenaAllocation> padded_staging;
    ArenaAllocation uniform_allocation;
    bool uniform_live = false;
    std::array<dk::Fence, 2> frame_fences{};
    std::array<bool, 2> frame_in_flight{};
    std::array<bool, 2> query_pending{};
    std::array<DekoSceneFrame, 2> query_frames{};
    std::array<std::uint64_t, 2> query_generations{};
    DekoGpuProfile gpu_profile;
    std::uint64_t profile_generation = 1;
    bool profile_enabled = false;
    bool opaque_first = false;
    bool sky_with_opaque = false;
    std::array<dk::Image, 2> colors;
    std::array<dk::Image, 2> scene_colors;
    std::array<dk::Image, 2> depths;
    DekoSceneTarget scene_target;
    dk::Shader vertex, textured, textured_opaque, fallback, sky_vertex, sky_fragment;
    const RecordedCommands* commands = nullptr;
    const PreparedCommandSource* package = nullptr;
    std::vector<const RecordedDraw*> static_definitions;
    std::vector<const RecordedDraw*> dynamic_definitions;
    std::vector<std::size_t> static_group_slots;
    std::array<std::vector<DkCmdList>, 2> static_lists;
    std::array<std::vector<std::pair<std::size_t, std::size_t>>, 2> static_selection;
    std::array<std::uint32_t, 2> static_alpha_cutoff{};
    DekoGpfifoUsage gpfifo_usage;
    std::size_t static_instances = 0;
    std::size_t dynamic_capacity = 0;
    std::uint32_t package_width = 0, package_height = 0;
    std::array<unsigned char, 4> package_clear{};
    UploadTable uploaded;
    std::size_t draw_capacity = 0;
    std::uint64_t fence_number = 0;
    std::uint64_t gpu_bytes = 0;
    std::array<std::uint32_t, 2> command_bytes{};
    std::uint32_t uniform_stride = 0;
    std::uint32_t sampler_offset = 0;
    char diagnostic[768]{};
    bool shut_down = false;

    static void debug(void* owner, const char* context, DkResult result, const char* message) {
        if (result == DkResult_Success) return;
        auto& self = *static_cast<Impl*>(owner);
        // No allocation, exception or UI operation from a driver callback.
        std::snprintf(self.diagnostic, sizeof self.diagnostic, "deko3d %s (%u): %s",
            context ? context : "unknown", static_cast<unsigned>(result), message ? message : "failure");
    }

    bool healthy(std::string& error) {
        if (diagnostic[0]) { error = diagnostic; return false; }
        if (queue && queue.isInErrorState()) { error = "deko3d queue entered error state"; return false; }
        return true;
    }

    bool block(std::uint64_t bytes, std::uint32_t flags, const char* name,
               dk::MemBlock& out, std::string& error) {
        bytes = align_up(bytes ? bytes : 1, DK_MEMBLOCK_ALIGNMENT);
        if (bytes > kMaxUploadResourceBytes || gpu_bytes + bytes > kGpuBudgetPlaceholder) {
            error = std::string(name) + ": GPU memory budget exceeded"; return false;
        }
        dk::UniqueMemBlock memory = dk::MemBlockMaker{device, static_cast<std::uint32_t>(bytes)}
            .setFlags(flags).create();
        if (!memory || !healthy(error)) {
            if (error.empty()) error = std::string(name) + ": deko3d memory block allocation failed";
            return false;
        }
        out = memory;
        blocks.push_back(std::move(memory));
        gpu_bytes += bytes;
        return true;
    }

    bool arena(MemoryArena& target, std::uint64_t bytes, std::string& error,
               std::uint32_t slots = kArenaAllocationsPlaceholder) {
        ArenaConfig config;
        config.category = MemoryCategory::gpu;
        config.capacity = align_up(bytes ? bytes : 1, DK_MEMBLOCK_ALIGNMENT);
        config.max_alignment = kMaxArenaAlignment;
        config.max_allocations = slots;
        return target.create(config, error);
    }

    dk::ImageLayout texture_layout(const UploadResource& resource) {
        dk::ImageLayout layout;
        const auto format = resource.texture.format == TextureUploadFormat::dxt1 ? DkImageFormat_RGBA_BC1 :
                            resource.texture.format == TextureUploadFormat::dxt3 ? DkImageFormat_RGBA_BC2 :
                            DkImageFormat_RGBA8_Unorm;
        dk::ImageLayoutMaker{device}.setFormat(format)
            .setDimensions(resource.texture.width, resource.texture.height).initialize(layout);
        return layout;
    }

    void begin(std::size_t slot = 0) {
        // Presentation waits for this slot's fence before overwriting it.
        cmd.clear();
        cmd.addMemory(command_memory[slot], 0, command_bytes[slot]);
    }

    bool submit(std::string& error) {
        if (!healthy(error)) return false;
        dk::Fence fence;
        cmd.signalFence(fence, true);
        const auto list = cmd.finishList();
        if (!healthy(error)) return false;
        queue.submitCommands(list);
        queue.flush();
        if (!healthy(error)) return false;
        const auto result = fence.wait();
        if (result != DkResult_Success) {
            error = "deko3d fence wait failed: " + std::to_string(static_cast<unsigned>(result));
            return false;
        }
        ++fence_number;
        ledger.retire(fence_number);
        return healthy(error);
    }

    bool shader(const char* name, DkStage stage, dk::Shader& out, std::string& error) {
        const std::string path = std::string("romfs:/shaders/") + name + ".dksh";
        std::ifstream stream(path, std::ios::binary | std::ios::ate);
        if (!stream) { error = path + ": failed to open offline shader"; return false; }
        const auto length = stream.tellg();
        if (length < 24 || length > 1024*1024) {
            error = path + ": invalid offline shader size"; return false;
        }
        std::vector<unsigned char> blob(static_cast<std::size_t>(length));
        stream.seekg(0);
        if (!stream.read(reinterpret_cast<char*>(blob.data()), length)) {
            error = path + ": failed to read offline shader"; return false;
        }
        std::uint32_t header[6];
        std::memcpy(header, blob.data(), sizeof header);
        if (header[0] != 0x48534B44 || header[1] != 24 || header[2] < 24 ||
            header[2] > blob.size() || header[3] == 0 ||
            std::uint64_t{header[2]} + header[3] != blob.size() || header[5] != 1 ||
            header[4] < 24 || header[4] >= header[2]) {
            error = path + ": invalid DKSH header"; return false;
        }
        dk::MemBlock code;
        if (!block(header[3], kDataFlags | DkMemBlockFlags_Code, name, code, error)) return false;
        std::memcpy(code.getCpuAddr(), blob.data() + header[2], header[3]);
        dk::ShaderMaker{code, 0}.setControl(blob.data()).initialize(out);
        if (!healthy(error) || !out.isValid() || out.getStage() != stage) {
            if (error.empty()) error = path + ": invalid shader or wrong stage";
            return false;
        }
        return true;
    }

    bool open(const RecordedCommands& source, std::string& error, bool presentation,
              std::size_t capacity, bool profile_frame, bool reorder_opaque, bool early_sky,
              std::uint32_t scene_height) {
        if (commands) { error = "deko3d renderer open is one-shot"; return false; }
        if (!plan_deko_scene_target(source.width, source.height, presentation, scene_height, scene_target)) {
            error = "invalid scene resolution: scaling requires 1280x720 direct presentation"; return false;
        }
        DrawExecutionPlan plan;
        if (!plan_draw_execution(source, plan, error)) return false;
        draw_capacity = capacity ? capacity : source.draw_count();
        if (draw_capacity < source.draw_count() || draw_capacity > 2047) {
            error = "invalid reserved draw capacity"; return false;
        }
        package = source.package_identity();
        opaque_first = reorder_opaque;
        sky_with_opaque = early_sky;
        if (package) {
            package_width = source.width; package_height = source.height;
            std::memcpy(package_clear.data(), source.clear_color, 4);
            static_instances = source.static_track_instance_count();
            if (source.static_track_draw_count() > source.draw_count()) {
                error = "prepared track count exceeds selected commands"; return false;
            }
            dynamic_capacity = source.draw_count() - source.static_track_draw_count();
            if (static_instances > 65536 || source.static_track_definition_count() > 4096 ||
                dynamic_capacity > draw_capacity) {
                error = "prepared static package exceeds capacity"; return false;
            }
            const auto count = source.static_track_definition_count();
            static_definitions.reserve(count);
            for (std::size_t i = 0; i < count; ++i) {
                const auto& draw = source.static_track_definition(i);
                // Sparse source group IDs are bounded to a 512 KiB slot table.
                if (draw.group >= 65536) { error = "prepared group ID exceeds capacity"; return false; }
                if (draw.group >= static_group_slots.size())
                    static_group_slots.resize(draw.group + 1, kNoHandle);
                if (static_group_slots[draw.group] != kNoHandle) {
                    error = "duplicate prepared group ID"; return false;
                }
                static_group_slots[draw.group] = i;
                static_definitions.push_back(&draw);
            }
            for (std::size_t i = source.static_track_draw_count(); i < source.draw_count(); ++i)
                dynamic_definitions.push_back(&source.draw_view(i).definition);
        } else dynamic_capacity = draw_capacity;
        plan.uniform_bytes = static_cast<std::uint32_t>((1 + 2 * dynamic_capacity) * 256);
        plan.command_bytes = static_cast<std::uint32_t>(65536 +
            (draw_capacity + source.resource_table().resources.size()) * 4096);
        uniform_stride = plan.uniform_bytes;
        profile_enabled = profile_frame && presentation;
        const auto uniform_bytes = std::uint64_t{uniform_stride} * (presentation ? 2 : 1);
        uploaded = source.resource_table();
        commands = &source;
        device = dk::DeviceMaker{}.setUserData(this).setCbDebug(debug)
            .setFlags(DkDeviceFlags_DepthZeroToOne | DkDeviceFlags_OriginUpperLeft).create();
        if (!device || !healthy(error)) {
            if (error.empty()) error = "deko3d device creation failed";
            return false;
        }
        queue = dk::QueueMaker{device}.setFlags(DkQueueFlags_Graphics).create();
        if (!queue || !healthy(error)) {
            if (error.empty()) error = "deko3d graphics queue creation failed";
            return false;
        }
        const ImageLayoutQuery layout_query = [this](const UploadResource& r, std::uint64_t& bytes,
                                                    std::uint64_t& alignment, std::string& e) {
            const auto layout = texture_layout(r);
            bytes = layout.getSize(); alignment = layout.getAlignment();
            std::uint64_t bound = 0, bound_alignment = 0;
            if (!bounded_image_layout(r, bound, bound_alignment, e)) return false;
            if (bytes > bound || alignment > bound_alignment) {
                e = r.name + ": deko3d layout exceeds the desktop image bound";
                return false;
            }
            return healthy(e) && bytes && alignment;
        };
        SceneMemoryRequirement need;
        if (!upload_table_requirement(source.resource_table(), layout_query, need, error)) return false;
        if (!block(need.buffer_extent, kDataFlags, "mesh buffers", buffers, error) ||
            !block(need.image_extent, kImageFlags, "texture images", images, error) ||
            !block(plan.staging_bytes,
                   kDataFlags, "texture staging", staging, error) ||
            !block(uniform_bytes, kDataFlags, "draw uniforms", uniforms, error) ||
            !block(plan.descriptor_bytes, kDataFlags, "texture descriptors", descriptors, error)) return false;
        if (package && !block((static_instances + static_definitions.size()) * 256ULL,
                kDataFlags, "prepared track uniforms", static_uniforms, error)) return false;
        if (!arena(buffer_arena, need.buffer_extent, error,
                   plan.buffer_allocations) ||
            !arena(image_arena, need.image_extent, error) ||
            !arena(staging_arena, plan.staging_bytes, error) ||
            !arena(uniform_arena, uniform_bytes, error) ||
            !ledger.create({&buffer_arena, &image_arena, &staging_arena}, layout_query, 8192, error) ||
            !ledger.upload_table(source.resource_table(), 1, handles, error)) return false;
        command_bytes[0] = plan.command_bytes;
        command_bytes[1] = static_cast<std::uint32_t>(65536 + draw_capacity * 4096);
        if (!block(command_bytes[0], kDataFlags, "command memory", command_memory[0], error) ||
            (presentation && !block(command_bytes[1], kDataFlags, "second frame command memory",
                                    command_memory[1], error))) return false;
        if (profile_enabled && !block(64,
            DkMemBlockFlags_CpuUncached | DkMemBlockFlags_GpuUncached,
            "GPU timestamp reports", query_memory, error))
            return false;
        cmd = dk::CmdBufMaker{device}.create();
        if (!cmd || !healthy(error)) {
            if (error.empty()) error = "deko3d command buffer creation failed";
            return false;
        }
        textures.resize(handles.size()); records.resize(handles.size());
        begin();
        for (std::size_t i = 0; i < handles.size(); ++i) {
            const auto& r = source.resource_table().resources[i];
            if (!ledger.find(handles[i], records[i])) { error = r.name + ": lost upload handle"; return false; }
            const auto& placement = records[i];
            if (r.kind != UploadResourceKind::texture) {
                std::memcpy(static_cast<unsigned char*>(buffers.getCpuAddr()) + placement.placement.offset,
                            r.data, static_cast<std::size_t>(r.bytes));
                continue;
            }
            auto offset = placement.staging.offset;
            TextureCopyPlan copy;
            if (!plan_texture_copy(r, copy, error)) return false;
            const auto pitch = copy.row_pitch_bytes;
            if (copy.padded) {
                ArenaAllocation padded;
                const auto refusal = staging_arena.allocate(copy.staging_bytes, 32, padded);
                if (refusal != ArenaRefusal::none) {
                    error = r.name + ": padded staging: " + arena_refusal_name(refusal); return false;
                }
                padded_staging.push_back(padded); offset = padded.offset;
            }
            auto* dst = static_cast<unsigned char*>(staging.getCpuAddr()) + offset;
            for (std::uint32_t y = 0; y < r.texture.rows(); ++y)
                std::memcpy(dst + y*pitch, r.data + y*r.texture.row_bytes,
                            static_cast<std::size_t>(r.texture.row_bytes));
            textures[i].initialize(texture_layout(r), images, static_cast<std::uint32_t>(placement.placement.offset));
            dk::ImageView view{textures[i]};
            cmd.copyBufferToImage({staging.getGpuAddr() + offset, copy.row_pitch_bytes,
                                   copy.layer_stride_bytes}, view, {0, 0, 0, r.texture.width, r.texture.height, 1});
            dk::ImageDescriptor descriptor;
            descriptor.initialize(view);
            std::memcpy(static_cast<unsigned char*>(descriptors.getCpuAddr()) + i*sizeof descriptor,
                        &descriptor, sizeof descriptor);
        }
        // One nearest/repeat sampler, just as all four manifests specify.
        dk::Sampler sampler;
        sampler.setFilter(DkFilter_Nearest, DkFilter_Nearest)
            .setWrapMode(DkWrapMode_Repeat, DkWrapMode_Repeat, DkWrapMode_Repeat);
        dk::SamplerDescriptor sampler_descriptor;
        sampler_descriptor.initialize(sampler);
        sampler_offset = plan.sampler_offset;
        std::memcpy(static_cast<unsigned char*>(descriptors.getCpuAddr()) + sampler_offset,
                    &sampler_descriptor, sizeof sampler_descriptor);
        cmd.barrier(DkBarrier_Full, DkInvalidateFlags_Image | DkInvalidateFlags_Descriptors);
        if (!submit(error)) return false;
        for (const auto& a : padded_staging) {
            if (staging_arena.release(a.handle) != ArenaRefusal::none) {
                error = "padded texture staging release failed"; return false;
            }
        }
        padded_staging.clear();
        if (uniform_arena.allocate(uniform_bytes, 256, uniform_allocation) != ArenaRefusal::none) {
            error = "draw uniform reservation failed"; return false;
        }
        uniform_live = true;
        write_uniforms(source, 0);
        if (!shader("sky.vert", DkStage_Vertex, sky_vertex, error) ||
            !shader("sky.frag", DkStage_Fragment, sky_fragment, error) ||
            !shader("placed.vert", DkStage_Vertex, vertex, error) ||
            !shader("textured.frag", DkStage_Fragment, textured, error) ||
            !shader("textured_opaque.frag", DkStage_Fragment, textured_opaque, error) ||
            !shader("fallback.frag", DkStage_Fragment, fallback, error)) return false;
        if (package && !prepare_static_commands(source, presentation, error)) return false;
        dk::ImageLayout color_layout, depth_layout, scene_layout;
        const auto transfer_flags = scene_target.upscale ? DkImageFlags_Usage2DEngine : 0;
        dk::ImageLayoutMaker{device}.setFlags(DkImageFlags_UsageRender | transfer_flags |
                                            (presentation ? DkImageFlags_UsagePresent : 0))
            .setFormat(DkImageFormat_RGBA8_Unorm).setDimensions(source.width, source.height).initialize(color_layout);
        dk::ImageLayoutMaker{device}.setFlags(DkImageFlags_UsageRender)
            .setFormat(DkImageFormat_ZF32).setDimensions(scene_target.width, scene_target.height).initialize(depth_layout);
        if (scene_target.upscale)
            dk::ImageLayoutMaker{device}.setFlags(DkImageFlags_UsageRender | DkImageFlags_Usage2DEngine)
                .setFormat(DkImageFormat_RGBA8_Unorm).setDimensions(scene_target.width, scene_target.height)
                .initialize(scene_layout);
        for (std::size_t i = 0; i < (presentation ? 2U : 1U); ++i) {
            auto& color = colors[i];
            dk::MemBlock memory;
            if (!block(color_layout.getSize(), kImageFlags, "present image", memory, error)) return false;
            color.initialize(color_layout, memory, 0);
            if (scene_target.upscale) {
                dk::MemBlock scene_memory;
                if (!block(scene_layout.getSize(), kImageFlags, "scene image", scene_memory, error)) return false;
                scene_colors[i].initialize(scene_layout, scene_memory, 0);
            }
        }
        for (std::size_t i = 0; i < (presentation ? 2U : 1U); ++i) {
            dk::MemBlock depth_memory;
            if (!block(depth_layout.getSize(), kImageFlags, "depth image", depth_memory, error)) return false;
            depths[i].initialize(depth_layout, depth_memory, 0);
        }
        if (presentation) {
            const std::array<const DkImage*, 2> targets{&colors[0], &colors[1]};
            swapchain = dk::SwapchainMaker{device, nwindowGetDefault(), targets}.create();
            if (!swapchain || !healthy(error)) {
                if (error.empty()) error = "deko3d swapchain creation failed";
                return false;
            }
        }
        std::printf("deko3d: %zu draws, %zu uploads, %llu bytes of GPU blocks (placeholder budget)\n",
            source.draw_count(), handles.size(), static_cast<unsigned long long>(gpu_bytes));
        return true;
    }

    bool update(const RecordedCommands& next, std::string& error, DekoUpdateTiming* timing) {
        if (timing) *timing = {};
        if (!commands || !uniform_live) {
            if (error.empty()) error = "renderer is not ready for a frame update";
            return false;
        }
        const auto validate_start = armGetSystemTick();
        if (package) {
            if (!next.generation_valid() || next.package_identity() != package ||
                next.width != package_width || next.height != package_height ||
                std::memcmp(next.clear_color, package_clear.data(), 4) != 0 ||
                next.draw_count() > draw_capacity ||
                next.static_track_draw_count() > next.draw_count() ||
                next.draw_count() - next.static_track_draw_count() != dynamic_capacity) {
                error = "prepared command package changed or exceeded capacity"; return false;
            }
            const auto track_count = next.static_track_draw_count();
            for (std::size_t i = 0; i < next.draw_count(); ++i) {
                const auto view = next.draw_view(i);
                if (i < track_count) {
                    const auto group = view.definition.group;
                    if (!view.track_instance || view.instance >= static_instances ||
                        group >= static_group_slots.size() ||
                        static_group_slots[group] == kNoHandle ||
                        &view.definition != static_definitions[static_group_slots[group]] ||
                        &view.placement != &next.static_track_instance(view.instance) ||
                        &view.material != &view.definition.material) {
                        error = "prepared track selection left its static package"; return false;
                    }
                } else if (view.track_instance ||
                           &view.definition != dynamic_definitions[i-track_count]) {
                    error = "prepared dynamic definition changed"; return false;
                }
            }
        } else if (!validate_draw_update(uploaded, next, draw_capacity, error)) return false;
        if (timing) timing->validation_ms = elapsed_ms(validate_start);
        // The presentation path writes constants only after waiting for the
        // selected slot. The offscreen path is synchronous and keeps slot 0.
        if (!swapchain) {
            const auto write_start = armGetSystemTick();
            write_uniforms(next, 0);
            if (timing) timing->uniform_write_ms = elapsed_ms(write_start);
        }
        commands = &next;
        return true;
    }

    void consume_query(std::size_t slot) {
        if (!query_pending[slot]) return;
        query_pending[slot] = false;
        if (!query_frames[slot].active || query_generations[slot] != profile_generation) return;
        const auto* words = static_cast<const std::uint64_t*>(query_memory.getCpuAddr()) + slot*4;
        const auto begin_timestamp = words[1], end_timestamp = words[3];
        if (!begin_timestamp || end_timestamp <= begin_timestamp) {
            ++gpu_profile.invalid; return;
        }
        const double value = dkTimestampToNs(end_timestamp-begin_timestamp)/1000000.0;
        gpu_profile.add_scene(value, query_frames[slot]);
    }

    void write_uniforms(const RecordedCommands& source, std::size_t slot) {
        auto* data = static_cast<unsigned char*>(uniforms.getCpuAddr()) +
            uniform_allocation.offset + slot * uniform_stride;
        std::memcpy(data, &source.frame, sizeof source.frame);
        const auto first_dynamic = package ? source.static_track_draw_count() : 0;
        for (std::size_t i = first_dynamic; i < source.draw_count(); ++i) {
            const auto view = source.draw_view(i);
            const auto index = i - first_dynamic;
            std::memcpy(data + (1+2*index)*256, &view.placement, sizeof(DrawInstanceUniform));
            std::memcpy(data + (2+2*index)*256, &view.material, sizeof(DrawMaterialUniform));
        }
    }

    template<class Recorder>
    void emit_draw(Recorder& out, const RecordedDraw& draw,
                   std::uint64_t instance_addr, std::uint64_t material_addr,
                   bool bind_fixed_state = true) {
        const auto& state = draw.pipeline;
        const bool has_texture = draw.texture_resource != kNoHandle;
        const bool cutout = deko_textured_uses_cutout_shader(
            state.alpha_test != sm::CompareOp::always, commands->frame.options[3]);
        const bool sky = std::strcmp(draw.program->name, "sky_textured") == 0;
        dk::Shader* fragment = sky ? &sky_fragment : !has_texture ? &fallback :
            cutout ? &textured : &textured_opaque;
        if (bind_fixed_state)
            out.bindShaders(DkStageFlag_GraphicsMask, {sky ? &sky_vertex : &vertex, fragment});
        if (instance_addr)
            out.bindUniformBuffer(DkStage_Vertex, 1, instance_addr, sizeof(DrawInstanceUniform));
        out.bindUniformBuffer(DkStage_Fragment, 2, material_addr, sizeof(DrawMaterialUniform));
        if (bind_fixed_state && has_texture) out.bindTextures(DkStage_Fragment, 0,
            dkMakeTextureHandle(static_cast<std::uint32_t>(draw.texture_resource), 0));
        if (bind_fixed_state) {
            dk::RasterizerState raster;
            raster.setCullMode(state.cull == sm::Cull::back ? DkFace_Back :
                state.cull == sm::Cull::front ? DkFace_Front : DkFace_None).setFrontFace(DkFrontFace_CCW);
            dk::ColorState color;
            color.setBlendEnable(0, state.blend).setAlphaCompareOp(kCompare[static_cast<unsigned>(state.alpha_test)]);
            dk::ColorWriteState write;
            write.setMask(0, state.color_write == sm::ColorWrite::rgb ? DkColorMask_RGB : DkColorMask_RGBA);
            dk::BlendState blend;
            blend.setOps(kBlend[static_cast<unsigned>(state.blend_op)], DkBlendOp_Add)
                .setFactors(kFactor[static_cast<unsigned>(state.blend_src)],
                            kFactor[static_cast<unsigned>(state.blend_dst)], DkBlendFactor_One, DkBlendFactor_Zero);
            dk::DepthStencilState z;
            z.setDepthTestEnable(true).setDepthWriteEnable(state.depth_write)
                .setDepthCompareOp(kCompare[static_cast<unsigned>(state.depth_test)]);
            out.bindRasterizerState(raster); out.bindColorState(color);
            out.bindColorWriteState(write); out.bindBlendStates(0, blend); out.bindDepthStencilState(z);
            out.setAlphaRef(state.alpha_ref/255.0F);
            std::array<DkVtxAttribState, 3> attributes{};
            for (std::size_t a = 0; a < draw.program->attribute_count; ++a) {
                const auto& input = draw.program->attributes[a];
                const bool byte = input.format == sm::VertexFormat::unorm8x4;
                attributes[input.location] = {input.buffer, 0, input.offset,
                    byte ? DkVtxAttribSize_4x8 : input.format == sm::VertexFormat::float32x3 ? DkVtxAttribSize_3x32 : DkVtxAttribSize_2x32,
                    byte ? DkVtxAttribType_Unorm : DkVtxAttribType_Float, 0};
            }
            out.bindVtxAttribState(attributes);
            out.bindVtxBufferState(DkVtxBufferState{draw.program->buffers[0].stride, 0});
        }
        const auto& v = records[draw.vertex_resource].placement;
        out.bindVtxBuffer(0, buffers.getGpuAddr()+v.offset, static_cast<std::uint32_t>(v.size));
        out.bindIdxBuffer(DkIdxFormat_Uint16, buffers.getGpuAddr()+records[draw.index_resource].placement.offset);
        out.drawIndexed(DkPrimitive_Triangles, draw.index_count, 1, draw.first_index,
                        static_cast<std::int32_t>(draw.first_vertex), 0);
    }

    static bool same_fixed_state(const RecordedDraw& a, const RecordedDraw& b) {
        // Byte equality is conservative with padding: it may rebind an equal
        // state but cannot skip a bind when a state field differs.
        return a.program == b.program && a.texture_resource == b.texture_resource &&
            std::memcmp(&a.pipeline, &b.pipeline, sizeof a.pipeline) == 0;
    }

    bool prepare_static_commands(const RecordedCommands& source, bool presentation, std::string& error) {
        auto* data = static_cast<unsigned char*>(static_uniforms.getCpuAddr());
        for (std::size_t i = 0; i < static_instances; ++i)
            std::memcpy(data + i*256, &source.static_track_instance(i), sizeof(DrawInstanceUniform));
        for (std::size_t i = 0; i < static_definitions.size(); ++i)
            std::memcpy(data + (static_instances+i)*256, &static_definitions[i]->material,
                        sizeof(DrawMaterialUniform));
        if (static_definitions.empty()) return true;
        const auto bytes = 65536ULL + draw_capacity*4096ULL;
        for (std::size_t slot = 0; slot < (presentation ? 2U : 1U); ++slot) {
            if (!block(bytes, kDataFlags, "visible track commands", static_command_memory[slot], error))
                return false;
            static_cmd[slot] = dk::CmdBufMaker{device}.create();
            if (!static_cmd[slot] || !healthy(error)) {
                if (error.empty()) error = "visible command buffer creation failed";
                return false;
            }
            static_lists[slot].reserve(deko_track_list_calls(static_cast<std::uint32_t>(draw_capacity)));
            static_selection[slot].reserve(draw_capacity);
        }
        return true;
    }

    bool prepare_visible_commands(std::size_t slot, std::string& error) {
        const auto count = commands->static_track_draw_count();
        auto& selection = static_selection[slot];
        bool unchanged = selection.size() == count &&
            static_alpha_cutoff[slot] == commands->frame.options[3];
        if (unchanged) for (std::size_t i = 0; i < count; ++i) {
            const auto view = commands->draw_view(i);
            if (selection[i].first != view.definition.group ||
                selection[i].second != view.instance) { unchanged = false; break; }
        }
        if (unchanged) return true;
        auto& recorder = static_cmd[slot];
        recorder.clear();
        recorder.addMemory(static_command_memory[slot], 0,
                           static_cast<std::uint32_t>(static_command_memory[slot].getSize()));
        auto& lists = static_lists[slot];
        lists.clear();
        selection.clear();
        static_alpha_cutoff[slot] = commands->frame.options[3];
        for (std::size_t i = 0; i < count; ++i) {
            const auto view = commands->draw_view(i);
            selection.emplace_back(view.definition.group, view.instance);
        }
        const RecordedDraw* previous = nullptr;
        std::size_t emitted = 0;
        bool ok = true;
        auto visit =
            [&](std::size_t i) {
                if (!ok) return;
                if (emitted % kDekoTrackChunkDraws == 0) previous = nullptr;
                const auto view = commands->draw_view(i);
                const auto index = static_group_slots.at(view.definition.group);
                const auto* definition = static_definitions.at(index);
                emit_draw(recorder, *definition,
                          static_uniforms.getGpuAddr() + view.instance*256,
                          static_uniforms.getGpuAddr() + (static_instances+index)*256,
                          !previous || !same_fixed_state(*previous, *definition));
                previous = definition;
                ++emitted;
                if (emitted % kDekoTrackChunkDraws == 0 || emitted == count) {
                    lists.push_back(recorder.finishList());
                    ok = healthy(error);
                }
            };
        if (opaque_first) deko_visit_opaque_first(count,
            [&](std::size_t i) {
                const auto& definition = commands->draw_view(i).definition;
                const auto& state = definition.pipeline;
                return deko_draw_in_opaque_pass(
                    std::strcmp(definition.program->name, "sky_textured") == 0, state.blend,
                    state.alpha_test != sm::CompareOp::always, state.depth_write, sky_with_opaque);
            }, visit);
        else for (std::size_t i = 0; i < count; ++i) visit(i);
        if (!ok || emitted != count) return false;
        return true;
    }

    bool record_frame(const dk::Image& target, std::size_t slot, std::string& error) {
        if (package && !prepare_visible_commands(slot, error)) return false;
        const auto track_draws = package ? commands->static_track_draw_count() : 0;
        const auto calls = static_cast<std::uint32_t>(static_lists[slot].size());
        const auto entries = deko_frame_gpfifo_bound(calls);
        if (calls != deko_track_list_calls(static_cast<std::uint32_t>(track_draws)) ||
            entries >= kDekoGpfifoRingEntries) {
            error = "visible command lists exceed GPFIFO ring budget";
            return false;
        }
        gpfifo_usage.last_lists = calls;
        gpfifo_usage.last_entries = entries;
        gpfifo_usage.max_entries = std::max(gpfifo_usage.max_entries, entries);
        begin(slot);
        if (profile_enabled)
            cmd.reportCounter(DkCounter_TimestampPipelineTop, query_memory.getGpuAddr()+slot*32);
        dk::ImageView color_view{scene_target.upscale ? scene_colors[slot] : target}, depth_view{depths[slot]};
        cmd.bindRenderTargets(&color_view, &depth_view);
        cmd.setViewports(0, {{0, 0, static_cast<float>(scene_target.width), static_cast<float>(scene_target.height), 0, 1}});
        cmd.setScissors(0, {{0, 0, scene_target.width, scene_target.height}});
        const auto* clear = commands->clear_color;
        cmd.clearColor(0, DkColorMask_RGBA, clear[0]/255.0F, clear[1]/255.0F, clear[2]/255.0F, clear[3]/255.0F);
        cmd.clearDepthStencil(true, 1, 0, 0);
        cmd.bindImageDescriptorSet(descriptors.getGpuAddr(), static_cast<std::uint32_t>(handles.size()));
        cmd.bindSamplerDescriptorSet(descriptors.getGpuAddr()+sampler_offset, 1);
        cmd.barrier(DkBarrier_None, DkInvalidateFlags_Shader | DkInvalidateFlags_Descriptors);
        const auto base = uniforms.getGpuAddr() + uniform_allocation.offset + slot * uniform_stride;
        cmd.bindUniformBuffer(DkStage_Vertex, 0, base, sizeof(DrawFrameUniform));
        cmd.bindUniformBuffer(DkStage_Fragment, 0, base, sizeof(DrawFrameUniform));
        for (const auto list : static_lists[slot]) cmd.callList(list);
        const auto first_dynamic = package ? commands->static_track_draw_count() : 0;
        const RecordedDraw* previous_dynamic = nullptr;
        for (std::size_t i = first_dynamic; i < commands->draw_count(); ++i) {
            const auto view = commands->draw_view(i);
            const auto index = i - first_dynamic;
            emit_draw(cmd, view.definition, base+(1+2*index)*256,
                      base+(2+2*index)*256,
                      !previous_dynamic || !same_fixed_state(*previous_dynamic, view.definition));
            previous_dynamic = &view.definition;
        }
        cmd.barrier(DkBarrier_Fragments, 0);
        if (scene_target.upscale) {
            // Complete rendering before the 2D engine reads this slot. The
            // second full barrier includes its work in the timestamp span
            // and makes the native-size image ready for presentation/HUD.
            cmd.barrier(DkBarrier_Full, DkInvalidateFlags_L2Cache);
            dk::ImageView present_view{target};
            cmd.blitImage(color_view, {0, 0, 0, scene_target.width, scene_target.height, 1},
                          present_view, {0, 0, 0, commands->width, commands->height, 1},
                          DkBlitFlag_FilterLinear);
            cmd.barrier(DkBarrier_Full, DkInvalidateFlags_L2Cache);
        }
        if (profile_enabled)
            cmd.reportCounter(DkCounter_Timestamp, query_memory.getGpuAddr()+slot*32+16);
        return true;
    }

    bool present(std::string& error, DekoPresentTiming* timing, const DekoSceneFrame* scene_frame) {
        if (timing) *timing = {};
        if (!swapchain || !healthy(error)) {
            if (error.empty()) error = "deko3d renderer is not open";
            return false;
        }
        auto stage = armGetSystemTick();
        const int slot = queue.acquireImage(swapchain);
        if (!healthy(error) || slot < 0 || slot >= 2) {
            if (error.empty()) error = "deko3d swapchain image acquisition failed";
            return false;
        }
        if (timing) timing->acquire_ms = elapsed_ms(stage);
        stage = armGetSystemTick();
        if (frame_in_flight[slot]) {
            const auto result = frame_fences[slot].wait();
            if (result != DkResult_Success) {
                error = "deko3d frame fence wait failed: " + std::to_string(static_cast<unsigned>(result));
                return false;
            }
            frame_in_flight[slot] = false;
            if (profile_enabled) consume_query(slot);
        }
        if (timing) timing->fence_wait_ms = elapsed_ms(stage);
        if (profile_enabled) {
            auto* report = static_cast<unsigned char*>(query_memory.getCpuAddr()) + slot*32;
            std::memset(report, 0, 32);
            query_frames[slot] = scene_frame ? *scene_frame : DekoSceneFrame{};
            query_generations[slot] = profile_generation;
        }
        stage = armGetSystemTick();
        write_uniforms(*commands, slot);
        if (timing) timing->uniform_write_ms = elapsed_ms(stage);
        stage = armGetSystemTick();
        if (!record_frame(colors[slot], slot, error)) return false;
        if (timing) timing->record_ms = elapsed_ms(stage);
        stage = armGetSystemTick();
        const auto list = cmd.finishList();
        if (!healthy(error)) return false;
        if (timing) timing->finish_ms = elapsed_ms(stage);
        stage = armGetSystemTick();
        queue.submitCommands(list);
        queue.signalFence(frame_fences[slot]);
        queue.presentImage(swapchain, slot);
        queue.flush();
        frame_in_flight[slot] = true;
        query_pending[slot] = profile_enabled;
        ++fence_number;
        if (timing) timing->submit_ms = elapsed_ms(stage);
        return healthy(error);
    }

    bool capture(std::vector<unsigned char>& rgba, double& render_ms,
                 double& readback_ms, std::string& error) {
        rgba.clear(); render_ms = readback_ms = 0;
        if (!commands || !queue || !cmd || swapchain || !healthy(error)) {
            if (error.empty()) error = "capture requires an open offscreen renderer";
            return false;
        }
        const std::uint32_t pitch = commands->width * 4;
        const std::size_t bytes = std::size_t{pitch} * commands->height;
        constexpr std::size_t guard = 32;
        if (!readback && !block(bytes + 2*guard,
                DkMemBlockFlags_CpuUncached | DkMemBlockFlags_GpuUncached,
                "frame readback", readback, error)) return false;
        auto* data = static_cast<unsigned char*>(readback.getCpuAddr());
        std::memset(data, 0xCD, bytes + 2*guard);
        const auto render_start = armGetSystemTick();
        if (!record_frame(colors[0], 0, error)) return false;
        if (!submit(error)) return false;
        render_ms = armTicksToNs(armGetSystemTick() - render_start) / 1000000.0;
        const auto copy_start = armGetSystemTick();
        begin();
        cmd.barrier(DkBarrier_Full, DkInvalidateFlags_L2Cache);
        dk::ImageView view{colors[0]};
        // deko3d uses rowLength as a BYTE pitch. Depth 1 needs no layer
        // stride. No flipping or swizzling is done by the comparison.
        cmd.copyImageToBuffer(view, {0, 0, 0, commands->width, commands->height, 1},
                              {readback.getGpuAddr() + guard, pitch, 0});
        cmd.barrier(DkBarrier_Full, DkInvalidateFlags_L2Cache);
        if (!submit(error)) return false;
        readback_ms = armTicksToNs(armGetSystemTick() - copy_start) / 1000000.0;
        for (std::size_t i = 0; i < guard; ++i) {
            if (data[i] != 0xCD || data[guard+bytes+i] != 0xCD) {
                error = "GPU readback overwrote a guard region"; return false;
            }
        }
        rgba.assign(data + guard, data + guard + bytes);
        return healthy(error);
    }

    bool shutdown(std::string& error) {
        if (shut_down) return true;
        shut_down = true;
        if (queue) queue.waitIdle();
        if (profile_enabled) for (std::size_t slot = 0; slot < query_pending.size(); ++slot) {
            if (!query_pending[slot]) continue;
            const auto result = frame_fences[slot].wait();
            if (result != DkResult_Success) {
                ++gpu_profile.dropped;
                query_pending[slot] = false;
            } else consume_query(slot);
        }
        // waitIdle completes every submitted use, including presentation.
        bool ok = ledger.release_all(handles, fence_number, error);
        ledger.retire(fence_number < 1 ? 1 : fence_number);
        for (const auto& a : padded_staging) {
            if (staging_arena.release(a.handle) != ArenaRefusal::none) {
                error = "padded staging release failed"; ok = false;
            }
        }
        padded_staging.clear();
        if (uniform_live) {
            const auto result = uniform_arena.release(uniform_allocation.handle);
            if (result != ArenaRefusal::none) { error = "draw uniform release failed"; ok = false; }
            uniform_live = false;
        }
        const auto stats = ledger.stats();
        if (stats.resident || stats.releasing || stats.staged || stats.lost_placements) {
            error = "deko3d upload ledger has unreclaimed or lost placements"; ok = false;
        }
        handles.clear();
        return ok;
    }
};

DekoRenderer::DekoRenderer() : impl_(std::make_unique<Impl>()) {}
DekoRenderer::~DekoRenderer() {
    try { std::string ignored; impl_->shutdown(ignored); }
    catch (...) { if (impl_->queue) impl_->queue.waitIdle(); }
}
bool DekoRenderer::open(const RecordedCommands& commands, std::string& error, bool presentation,
                        std::size_t draw_capacity, bool profile_frame, bool opaque_first,
                        bool sky_with_opaque, std::uint32_t scene_height) {
    error.clear();
    try { return impl_->open(commands, error, presentation, draw_capacity, profile_frame,
                             opaque_first, sky_with_opaque, scene_height); }
    catch (const std::bad_alloc&) { error = "not enough memory for deko3d renderer"; return false; }
}
bool DekoRenderer::reconfigure_target(const RecordedCommands& commands, std::string& error,
                                      std::size_t draw_capacity, bool profile_frame,
                                      bool sky_with_opaque, std::uint32_t scene_height) {
    if (!close(error)) return false;
    impl_ = std::make_unique<Impl>();
    return open(commands,error,true,draw_capacity,profile_frame,true,
                sky_with_opaque,scene_height);
}
bool DekoRenderer::update(const RecordedCommands& commands, std::string& error,
                          DekoUpdateTiming* timing) {
    error.clear();
    try { return impl_->update(commands, error, timing); }
    catch (const std::bad_alloc&) { error = "not enough memory for draw update validation"; return false; }
}
std::uint64_t DekoRenderer::gpu_bytes() const noexcept { return impl_->gpu_bytes; }
DekoMemoryUsage DekoRenderer::memory_usage() const noexcept {
    return {deko_allocated_block_size(impl_->uniforms),
        deko_allocated_block_size(impl_->static_uniforms),
        deko_allocated_block_size(impl_->static_command_memory[0]) +
            deko_allocated_block_size(impl_->static_command_memory[1])};
}
DekoGpfifoUsage DekoRenderer::gpfifo_usage() const noexcept { return impl_->gpfifo_usage; }
bool DekoRenderer::capture(std::vector<unsigned char>& rgba, double& render_ms,
                           double& readback_ms, std::string& error) {
    error.clear();
    try { return impl_->capture(rgba, render_ms, readback_ms, error); }
    catch (const std::bad_alloc&) { rgba.clear(); error = "not enough memory for GPU readback"; return false; }
}
bool DekoRenderer::present(std::string& error, DekoPresentTiming* timing,
                           const DekoSceneFrame* scene_frame) {
    error.clear();
    return impl_->present(error, timing, scene_frame);
}
void DekoRenderer::reset_gpu_profile() {
    ++impl_->profile_generation;
    impl_->gpu_profile = {};
}
const DekoGpuProfile& DekoRenderer::gpu_profile() const noexcept { return impl_->gpu_profile; }
std::uint64_t DekoRenderer::profile_storage_bytes() const noexcept {
    return impl_->profile_enabled ?
        impl_->query_memory.getSize() + sizeof(impl_->gpu_profile) +
        sizeof(impl_->query_frames) + sizeof(impl_->query_generations) : 0;
}
bool DekoRenderer::close(std::string& error) {
    error.clear();
    const bool ok = impl_->shutdown(error);
    // Return the window before a caller opens the diagnostic framebuffer.
    impl_->cmd = nullptr; impl_->swapchain = nullptr;
    return ok && impl_->healthy(error);
}
} // namespace nfsu
