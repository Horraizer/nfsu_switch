#include "deko_renderer.h"
#include "nfsu/memory_budget.h"
#include "nfsu/draw_execution.h"

#include <deko3d.hpp>
#include <switch.h>
#include <array>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <new>
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
    dk::MemBlock buffers, images, staging, uniforms, descriptors, readback;
    dk::MemBlock query_memory;
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
    std::array<dk::Image, 2> colors;
    std::array<dk::Image, 2> depths;
    dk::Shader vertex, textured, fallback;
    const RecordedCommands* commands = nullptr;
    UploadTable uploaded;
    std::size_t draw_capacity = 0;
    std::uint64_t fence_number = 0;
    std::uint64_t gpu_bytes = 0;
    std::array<std::uint32_t, 2> command_bytes{};
    std::uint32_t uniform_stride = 0;
    std::uint32_t sampler_offset = 0;
    char diagnostic[768]{};

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
              std::size_t capacity, bool profile_frame) {
        if (commands) { error = "deko3d renderer open is one-shot"; return false; }
        DrawExecutionPlan plan;
        if (!plan_draw_execution(source, plan, error)) return false;
        draw_capacity = capacity ? capacity : source.draws.size();
        if (draw_capacity < source.draws.size() || draw_capacity > 2047) {
            error = "invalid reserved draw capacity"; return false;
        }
        plan.uniform_bytes = static_cast<std::uint32_t>((1 + 2 * draw_capacity) * 256);
        plan.command_bytes = static_cast<std::uint32_t>(65536 +
            (draw_capacity + source.uploads.resources.size()) * 4096);
        uniform_stride = plan.uniform_bytes;
        profile_enabled = profile_frame && presentation;
        const auto uniform_bytes = std::uint64_t{uniform_stride} * (presentation ? 2 : 1);
        uploaded = source.uploads;
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
        if (!upload_table_requirement(source.uploads, layout_query, need, error)) return false;
        if (!block(need.buffer_extent, kDataFlags, "mesh buffers", buffers, error) ||
            !block(need.image_extent, kImageFlags, "texture images", images, error) ||
            !block(plan.staging_bytes,
                   kDataFlags, "texture staging", staging, error) ||
            !block(uniform_bytes, kDataFlags, "draw uniforms", uniforms, error) ||
            !block(plan.descriptor_bytes, kDataFlags, "texture descriptors", descriptors, error)) return false;
        if (!arena(buffer_arena, need.buffer_extent, error,
                   plan.buffer_allocations) ||
            !arena(image_arena, need.image_extent, error) ||
            !arena(staging_arena, plan.staging_bytes, error) ||
            !arena(uniform_arena, uniform_bytes, error) ||
            !ledger.create({&buffer_arena, &image_arena, &staging_arena}, layout_query, 8192, error) ||
            !ledger.upload_table(source.uploads, 1, handles, error)) return false;
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
            const auto& r = source.uploads.resources[i];
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
        if (!shader("placed.vert", DkStage_Vertex, vertex, error) ||
            !shader("textured.frag", DkStage_Fragment, textured, error) ||
            !shader("fallback.frag", DkStage_Fragment, fallback, error)) return false;
        dk::ImageLayout color_layout, depth_layout;
        dk::ImageLayoutMaker{device}.setFlags(DkImageFlags_UsageRender | (presentation ? DkImageFlags_UsagePresent : 0))
            .setFormat(DkImageFormat_RGBA8_Unorm).setDimensions(source.width, source.height).initialize(color_layout);
        dk::ImageLayoutMaker{device}.setFlags(DkImageFlags_UsageRender)
            .setFormat(DkImageFormat_ZF32).setDimensions(source.width, source.height).initialize(depth_layout);
        for (std::size_t i = 0; i < (presentation ? 2U : 1U); ++i) {
            auto& color = colors[i];
            dk::MemBlock memory;
            if (!block(color_layout.getSize(), kImageFlags, "present image", memory, error)) return false;
            color.initialize(color_layout, memory, 0);
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
            source.draws.size(), handles.size(), static_cast<unsigned long long>(gpu_bytes));
        return true;
    }

    bool update(const RecordedCommands& next, std::string& error, DekoUpdateTiming* timing) {
        if (timing) *timing = {};
        if (!commands || !uniform_live) {
            if (error.empty()) error = "renderer is not ready for a frame update";
            return false;
        }
        const auto validate_start = armGetSystemTick();
        if (!validate_draw_update(uploaded, next, draw_capacity, error)) return false;
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
        if (value > gpu_profile.scene.max()) {
            gpu_profile.worst_tick = query_frames[slot].tick;
            gpu_profile.worst_track_draws = query_frames[slot].track_draws;
            gpu_profile.worst_car_draws = query_frames[slot].car_draws;
        }
        gpu_profile.scene.add(value);
    }

    void write_uniforms(const RecordedCommands& source, std::size_t slot) {
        auto* data = static_cast<unsigned char*>(uniforms.getCpuAddr()) +
            uniform_allocation.offset + slot * uniform_stride;
        std::memcpy(data, &source.frame, sizeof source.frame);
        for (std::size_t i = 0; i < source.draws.size(); ++i) {
            std::memcpy(data + (1+2*i)*256, &source.draws[i].placement, sizeof(DrawInstanceUniform));
            std::memcpy(data + (2+2*i)*256, &source.draws[i].material, sizeof(DrawMaterialUniform));
        }
    }

    void record_frame(const dk::Image& target, std::size_t slot) {
        begin(slot);
        if (profile_enabled)
            cmd.reportCounter(DkCounter_TimestampPipelineTop, query_memory.getGpuAddr()+slot*32);
        dk::ImageView color_view{target}, depth_view{depths[slot]};
        cmd.bindRenderTargets(&color_view, &depth_view);
        cmd.setViewports(0, {{0, 0, static_cast<float>(commands->width), static_cast<float>(commands->height), 0, 1}});
        cmd.setScissors(0, {{0, 0, commands->width, commands->height}});
        const auto* clear = commands->clear_color;
        cmd.clearColor(0, DkColorMask_RGBA, clear[0]/255.0F, clear[1]/255.0F, clear[2]/255.0F, clear[3]/255.0F);
        cmd.clearDepthStencil(true, 1, 0, 0);
        cmd.bindImageDescriptorSet(descriptors.getGpuAddr(), static_cast<std::uint32_t>(handles.size()));
        cmd.bindSamplerDescriptorSet(descriptors.getGpuAddr()+sampler_offset, 1);
        cmd.barrier(DkBarrier_None, DkInvalidateFlags_Shader | DkInvalidateFlags_Descriptors);
        const auto base = uniforms.getGpuAddr() + uniform_allocation.offset + slot * uniform_stride;
        cmd.bindUniformBuffer(DkStage_Vertex, 0, base, sizeof(DrawFrameUniform));
        cmd.bindUniformBuffer(DkStage_Fragment, 0, base, sizeof(DrawFrameUniform));
        for (std::size_t i = 0; i < commands->draws.size(); ++i) {
            const auto& draw = commands->draws[i];
            const auto& state = draw.pipeline;
            const bool has_texture = draw.texture_resource != kNoHandle;
            cmd.bindShaders(DkStageFlag_GraphicsMask, {&vertex, has_texture ? &textured : &fallback});
            cmd.bindUniformBuffer(DkStage_Vertex, 1, base+(1+2*i)*256, sizeof(DrawInstanceUniform));
            cmd.bindUniformBuffer(DkStage_Fragment, 2, base+(2+2*i)*256, sizeof(DrawMaterialUniform));
            if (has_texture) cmd.bindTextures(DkStage_Fragment, 0,
                dkMakeTextureHandle(static_cast<std::uint32_t>(draw.texture_resource), 0));
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
            cmd.bindRasterizerState(raster); cmd.bindColorState(color);
            cmd.bindColorWriteState(write); cmd.bindBlendStates(0, blend); cmd.bindDepthStencilState(z);
            cmd.setAlphaRef(state.alpha_ref/255.0F);
            std::array<DkVtxAttribState, 3> attributes{};
            for (std::size_t a = 0; a < draw.program->attribute_count; ++a) {
                const auto& input = draw.program->attributes[a];
                const bool byte = input.format == sm::VertexFormat::unorm8x4;
                attributes[input.location] = {input.buffer, 0, input.offset,
                    byte ? DkVtxAttribSize_4x8 : input.format == sm::VertexFormat::float32x3 ? DkVtxAttribSize_3x32 : DkVtxAttribSize_2x32,
                    byte ? DkVtxAttribType_Unorm : DkVtxAttribType_Float, 0};
            }
            cmd.bindVtxAttribState(attributes);
            cmd.bindVtxBufferState(DkVtxBufferState{draw.program->buffers[0].stride, 0});
            const auto& v = records[draw.vertex_resource].placement;
            cmd.bindVtxBuffer(0, buffers.getGpuAddr()+v.offset, static_cast<std::uint32_t>(v.size));
            cmd.bindIdxBuffer(DkIdxFormat_Uint16, buffers.getGpuAddr()+records[draw.index_resource].placement.offset);
            cmd.drawIndexed(DkPrimitive_Triangles, draw.index_count, 1, draw.first_index,
                            static_cast<std::int32_t>(draw.first_vertex), 0);
        }
        cmd.barrier(DkBarrier_Fragments, 0);
        if (profile_enabled)
            cmd.reportCounter(DkCounter_Timestamp, query_memory.getGpuAddr()+slot*32+16);
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
        record_frame(colors[slot], slot);
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
        record_frame(colors[0], 0);
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
                        std::size_t draw_capacity, bool profile_frame) {
    error.clear();
    try { return impl_->open(commands, error, presentation, draw_capacity, profile_frame); }
    catch (const std::bad_alloc&) { error = "not enough memory for deko3d renderer"; return false; }
}
bool DekoRenderer::update(const RecordedCommands& commands, std::string& error,
                          DekoUpdateTiming* timing) {
    error.clear();
    try { return impl_->update(commands, error, timing); }
    catch (const std::bad_alloc&) { error = "not enough memory for draw update validation"; return false; }
}
std::uint64_t DekoRenderer::gpu_bytes() const noexcept { return impl_->gpu_bytes; }
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
