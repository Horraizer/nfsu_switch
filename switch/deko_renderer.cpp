#include "nfsu/frontend_glow_fast.h"
#include "nfsu/race_effect_fast.h"
#include "deko_renderer.h"
#include "frontend_startup_trace.h"
#include "nfsu/frontend.h"
#include "nfsu/memory_budget.h"
#include "nfsu/draw_execution.h"
#include "nfsu/deko_gpfifo_budget.h"
#include "nfsu/deko_memory_usage.h"
#include "nfsu/deko_draw_order.h"
#include "nfsu/deko_texture_layout.h"

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
#if defined(NFSU_DEKO_DEBUG_DIAGNOSTICS)
#include <unistd.h>
#endif

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
    dk::MemBlock query_memory,frontend_query_memory;
    struct AbGpuRow { DekoSceneFrame frame; double total; std::array<double,7> stages; bool valid; };
    std::vector<AbGpuRow> ab_rows;
    bool ab_passes=true;
    bool frontend_profile_passes=false;
    StartupJournal* startup_journal=nullptr;
    bool trace(const char* stage,std::string& error,const char* name="none",std::uint64_t bytes=0) {
        return trace_frontend_startup(startup_journal,stage,error,name,bytes,gpu_bytes);
    }
    std::array<bool,2> frontend_query_pending{};
    unsigned frontend_profile_samples=0;
    std::size_t frontend_car_begin=0,frontend_car_end=0;
    dk::MemBlock hud_image_memory, hud_staging, hud_vertices, hud_indices;
    dk::MemBlock hud_uniforms, hud_descriptors;
    dk::Image hud_image;
    bool hud_enabled = false;
    dk::MemBlock reflection_uniforms, frontend_light_uniforms;
    dk::Image reflection_color, reflection_depth;
    bool reflection_ready = false;
    dk::MemBlock frontend_cube_uniforms;
    dk::MemBlock headlight_uniforms;
    bool cube_initialized=false;
    dk::Image frontend_cube_color,frontend_cube_depth;
    bool frontend_cube_ready=false;
    bool frontend_first_frame=true;
    std::size_t frontend_cube_capacity=0;
    static constexpr std::size_t reflection_capacity = 4096;
    std::size_t reflection_limit=reflection_capacity;
    std::uint32_t reflection_width = 160, reflection_height = 90;
    std::array<dk::MemBlock, 2> static_command_memory;
    std::array<dk::MemBlock, 2> command_memory;
    MemoryArena buffer_arena, image_arena, staging_arena, uniform_arena;
    UploadLedger ledger;
    std::vector<UploadHandle> handles;
    std::vector<UploadRecord> records;
    std::vector<dk::Image> textures;
    std::size_t first_texture=kNoHandle;
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
    bool fxaa_enabled=false;
    bool streaks_enabled=false, streaks_initialized=false;
    dk::Shader streak_vertex,streak_fade_fragment,streak_copy_fragment;
    dk::Image streak_color;
    dk::MemBlock streak_descriptors;
    bool msaa_enabled=false;
#if defined(NFSU_RACE_MSAA8)
    static constexpr DkMsMode msaa_mode=DkMsMode_8x;
    static constexpr unsigned msaa_samples=8;
#else
    static constexpr DkMsMode msaa_mode=DkMsMode_2x;
    static constexpr unsigned msaa_samples=2;
#endif
    std::uint64_t msaa_budget_extra=0;
    std::uint64_t mip_budget_extra=0;
#if defined(NFSU_DEKO_DEBUG_DIAGNOSTICS)
    bool msaa_first_frame=true;
#else
    bool msaa_first_frame=false;
#endif
    dk::Image msaa_color;
    dk::Shader fxaa_vertex,fxaa_fragment;
    dk::MemBlock fxaa_descriptors;
    std::array<dk::Image,2> fxaa_colors;
    dk::Shader shadow_vertex, shadow_fragment;
    dk::Shader vertex, textured, textured_opaque, fallback, frontend_vertex, frontend_fragment;
    dk::Shader frontend_car_vertex, frontend_car_fragment;
#if defined(NFSU_FRONTEND_PREFLIGHT)
    dk::Shader probe_vertex,probe_constant,probe_textured;
#endif
    dk::Shader car_vertex, car_fragment, sky_vertex, sky_fragment;
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
        // Preserve the initiating fault instead of a later cascade.
        if(self.diagnostic[0])return;
        // No allocation, exception or UI operation from a driver callback.
        std::snprintf(self.diagnostic, sizeof self.diagnostic, "deko3d %s (%u): %s",
            context ? context : "unknown", static_cast<unsigned>(result), message ? message : "failure");
#if defined(NFSU_DEKO_DEBUG_DIAGNOSTICS)
        // The validating SDK can report before the outer healthy() check.
        // Write immediately without allocating or touching presentation UI.
        ::write(STDERR_FILENO,self.diagnostic,std::strlen(self.diagnostic));
        ::write(STDERR_FILENO,"\n",1);
#endif
    }

    bool healthy(std::string& error) {
        if (diagnostic[0]) { error = diagnostic; return false; }
        if (queue && queue.isInErrorState()) { error = "deko3d queue entered error state"; return false; }
        return true;
    }

    bool block(std::uint64_t bytes, std::uint32_t flags, const char* name,
               dk::MemBlock& out, std::string& error) {
        bytes = align_up(bytes ? bytes : 1, DK_MEMBLOCK_ALIGNMENT);
        if(!trace("allocation.before",error,name,bytes))return false;
        if (bytes > kMaxUploadResourceBytes || gpu_bytes + bytes > kGpuBudgetPlaceholder + msaa_budget_extra + mip_budget_extra) {
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
        if(!trace("allocation.after",error,name,bytes))return false;
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
        auto maker = dk::ImageLayoutMaker{device};
        maker.setFormat(format).setDimensions(resource.texture.width, resource.texture.height)
            .setMipLevels(resource.texture.mip_count);
        if (deko_texture_needs_one_gob(resource.texture))
            maker.setFlags(DkImageFlags_CustomTileSize).setTileSize(DkTileSize_OneGob);
        maker.initialize(layout);
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
        // The SDK reserves the final 0x400 bytes of every code block. Page
        // alignment alone does not guarantee this tail for every shader size.
        if (!block(std::uint64_t{header[3]} + DK_SHADER_CODE_UNUSABLE_SIZE,
                   kDataFlags | DkMemBlockFlags_Code, name, code, error)) return false;
        std::memset(code.getCpuAddr(),0,code.getSize());
        std::memcpy(code.getCpuAddr(), blob.data() + header[2], header[3]);
        dk::ShaderMaker{code, 0}.setControl(blob.data()).initialize(out);
        if (!healthy(error) || !out.isValid() || out.getStage() != stage) {
            if (error.empty()) error = path + ": invalid shader or wrong stage";
            return false;
        }
        return true;
    }

    std::size_t overlay_capacity=0;
    bool init_overlay(const std::vector<unsigned char>& pixels,std::uint32_t width,std::uint32_t height,
                      std::size_t capacity,std::string& error) {
        if(!width || !height || width>4096 || height>4096 || !capacity || capacity>kFrontendUiMaxQuads) {
            error="overlay atlas dimensions or capacity invalid";return false;
        }
        const std::uint32_t texel_bytes=width*height*4;
        const std::uint32_t vertex_slot_bytes=sizeof(TrackVertex)*capacity*4;
        if (pixels.size() != texel_bytes) {
            error = "HUD atlas has the wrong size"; return false;
        }
        dk::ImageLayout layout;
        dk::ImageLayoutMaker{device}.setFormat(DkImageFormat_RGBA8_Unorm)
            .setDimensions(width,height).initialize(layout);
        if (!block(layout.getSize(),kImageFlags,"HUD image",hud_image_memory,error) ||
            !block(texel_bytes,kDataFlags,"HUD staging",hud_staging,error) ||
            !block(vertex_slot_bytes*2,kDataFlags,"HUD vertices",hud_vertices,error) ||
            !block(capacity*6*sizeof(std::uint16_t),kDataFlags,
                   "HUD indices",hud_indices,error) ||
            !block(3*256,kDataFlags,"HUD uniforms",hud_uniforms,error) ||
            !block(2*kDrawDescriptorBytes,kDataFlags,"HUD descriptors",hud_descriptors,error))
            return false;
        hud_image.initialize(layout,hud_image_memory,0);
        std::memcpy(hud_staging.getCpuAddr(),pixels.data(),texel_bytes);
        auto* indices=static_cast<std::uint16_t*>(hud_indices.getCpuAddr());
        for (std::uint16_t i=0;i<capacity;++i) {
            const std::uint16_t b=i*4;
            const std::uint16_t six[6]={b,static_cast<std::uint16_t>(b+1),
                static_cast<std::uint16_t>(b+2),b,
                static_cast<std::uint16_t>(b+2),static_cast<std::uint16_t>(b+3)};
            std::memcpy(indices+i*6,six,sizeof six);
        }
        DrawFrameUniform frame;
        frame.right[0]=1; frame.up[1]=-1; frame.forward[2]=1;
        frame.projection[0]=2.0F/source_width();
        frame.projection[1]=2.0F/source_height();
        frame.projection[2]=0.1F; frame.projection[3]=10;
        frame.options[1]=1;
        const DrawInstanceUniform instance;
        const DrawMaterialUniform material;
        auto* data=static_cast<unsigned char*>(hud_uniforms.getCpuAddr());
        std::memcpy(data,&frame,sizeof frame);
        std::memcpy(data+256,&instance,sizeof instance);
        std::memcpy(data+512,&material,sizeof material);
        dk::ImageView view{hud_image};
        dk::ImageDescriptor image_descriptor;
        image_descriptor.initialize(view);
        std::memcpy(hud_descriptors.getCpuAddr(),&image_descriptor,
                    sizeof image_descriptor);
        dk::Sampler sampler;
        sampler.setFilter(DkFilter_Nearest,DkFilter_Nearest)
            .setWrapMode(DkWrapMode_Repeat,DkWrapMode_Repeat,DkWrapMode_Repeat);
        dk::SamplerDescriptor sampler_descriptor;
        sampler_descriptor.initialize(sampler);
        std::memcpy(static_cast<unsigned char*>(hud_descriptors.getCpuAddr())+
                    kDrawDescriptorBytes,&sampler_descriptor,sizeof sampler_descriptor);
        begin();
        cmd.copyBufferToImage({hud_staging.getGpuAddr(),width*4,texel_bytes},
                              view,{0,0,0,width,height,1});
        cmd.barrier(DkBarrier_Full,DkInvalidateFlags_Image|
                    DkInvalidateFlags_Descriptors);
        if (!submit(error)) return false;
        overlay_capacity=capacity;hud_enabled=true;
        return true;
    }

    bool init_hud(const RaceHudAtlas& atlas,std::string& error) {
        return init_overlay(atlas.rgba,kRaceHudAtlasSize,kRaceHudAtlasSize,kRaceHudMaxQuads,error);
    }
    float source_width() const { return static_cast<float>(commands->width); }
    float source_height() const { return static_cast<float>(commands->height); }

    bool open(const RecordedCommands& source, std::string& error, bool presentation,
              std::size_t capacity, bool profile_frame, bool reorder_opaque, bool early_sky,
              std::uint32_t scene_height, const RaceHudAtlas* hud_atlas) {
        if (commands) { error = "deko3d renderer open is one-shot"; return false; }
        if (!plan_deko_scene_target(source.width, source.height, presentation, scene_height, scene_target)) {
            error = "invalid scene resolution: scaling requires 1280x720 direct presentation"; return false;
        }
#if defined(NFSU_RACE_FXAA)
        fxaa_enabled=presentation && source.race_environment;
#endif
#if defined(NFSU_RACE_MSAA2) || defined(NFSU_RACE_MSAA8)
        msaa_enabled=presentation && source.race_environment;
#endif
        if(fxaa_enabled && msaa_enabled) { error="select one race AA mode";return false; }
        DrawExecutionPlan plan;
        if (!plan_draw_execution(source, plan, error)) return false;
        draw_capacity = capacity ? capacity : source.draw_count();
        if (draw_capacity < source.draw_count() || draw_capacity > 4096) {
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
        reflection_limit=(source.frame.options[2]&8U)?source.reflection_draw_count():reflection_capacity;
        plan.uniform_bytes = static_cast<std::uint32_t>((1 + 2 * dynamic_capacity) * 256);
        plan.command_bytes = static_cast<std::uint32_t>(65536 +
            (draw_capacity + (source.race_environment ? kRaceCubeDrawLimit : 6*source.frontend_cube_draws.size()) + source.resource_table().resources.size() + ((source.frame.options[2] & 10U) ? reflection_limit : 0)) * 4096);
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
        if(source.race_environment) {
            auto top_table=source.resource_table();
            std::uint64_t top_staging=top_table.resources.size()*32;
            std::size_t chains=0,levels=0;
            for(auto& resource:top_table.resources)if(resource.kind==UploadResourceKind::texture) {
                levels+=resource.texture.mip_count;
                if(resource.texture.mip_count>1) {
                    ++chains;TextureUploadShape top;std::uint64_t offset=0;
                    if(!texture_upload_mip(resource.texture,0,top,offset,error))return false;
                    resource.texture=top;resource.bytes=top.bytes;resource.elements=top.bytes/top.texel_bytes;
                }
                TextureCopyPlan copy;if(!plan_texture_copy(resource,copy,error))return false;
                top_staging+=resource.bytes+(copy.padded?copy.staging_bytes+31:0);
            }
            if(chains) {
                SceneMemoryRequirement top_need;
                if(!upload_table_requirement(top_table,layout_query,top_need,error))return false;
                const auto actual=align_up(need.image_extent,DK_MEMBLOCK_ALIGNMENT)+align_up(plan.staging_bytes,DK_MEMBLOCK_ALIGNMENT);
                const auto baseline=align_up(top_need.image_extent,DK_MEMBLOCK_ALIGNMENT)+align_up(top_staging,DK_MEMBLOCK_ALIGNMENT);
                // Increase only by real image/staging costs of authored mips.
                mip_budget_extra=actual>baseline?actual-baseline:0;
                std::printf("WORLD MIPS chains=%zu levels=%zu extra_GPU_bytes=%llu staging_slots=%u\n",
                    chains,levels,(unsigned long long)mip_budget_extra,plan.staging_allocations);
            }
        }
        if (!block(need.buffer_extent, kDataFlags, "mesh buffers", buffers, error) ||
            !block(need.image_extent, kImageFlags, "texture images", images, error) ||
            !block(plan.staging_bytes,
                   kDataFlags, "texture staging", staging, error) ||
            !block(uniform_bytes, kDataFlags, "draw uniforms", uniforms, error) ||
            !block(plan.descriptor_bytes + (((source.frame.options[2] & 10U) ? 32U : 0U)+((source.race_environment || !source.frontend_cube_draws.empty())?32U:0U)+(((source.frame.options[2] & 10U) || (source.race_environment || !source.frontend_cube_draws.empty()))?32U:0U)), kDataFlags, "texture descriptors", descriptors, error)) return false;
        if (package && !block((static_instances + static_definitions.size()) * 256ULL,
                kDataFlags, "prepared track uniforms", static_uniforms, error)) return false;
        if (!arena(buffer_arena, need.buffer_extent, error,
                   plan.buffer_allocations) ||
            !arena(image_arena, need.image_extent, error) ||
            !arena(staging_arena, plan.staging_bytes, error, std::max(1U,plan.staging_allocations)) ||
            !arena(uniform_arena, uniform_bytes, error) ||
            !ledger.create({&buffer_arena, &image_arena, &staging_arena}, layout_query, 8192, error) ||
            !ledger.upload_table(source.resource_table(), 1, handles, error)) return false;
        command_bytes[0] = plan.command_bytes;
        command_bytes[1] = static_cast<std::uint32_t>(65536 + (draw_capacity + (source.race_environment ? kRaceCubeDrawLimit : 6*source.frontend_cube_draws.size()) + ((source.frame.options[2] & 10U) ? reflection_limit : 0)) * 4096);
        if (!block(command_bytes[0], kDataFlags, "command memory", command_memory[0], error) ||
            (presentation && !block(command_bytes[1], kDataFlags, "second frame command memory",
                                    command_memory[1], error))) return false;
        if (profile_enabled && !block(320,
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
            if(first_texture==kNoHandle)first_texture=i;
            textures[i].initialize(texture_layout(r), images, static_cast<std::uint32_t>(placement.placement.offset));
            for(std::uint32_t level=0;level<r.texture.mip_count;++level) {
                TextureCopyPlan copy;
                if (!plan_texture_copy(r, copy, error,level)) return false;
                auto offset=placement.staging.offset+copy.source_offset;
                if (copy.padded) {
                    ArenaAllocation padded;
                    const auto refusal = staging_arena.allocate(copy.staging_bytes, 32, padded);
                    if (refusal != ArenaRefusal::none) {
                        error = r.name + ": padded mip staging: " + arena_refusal_name(refusal); return false;
                    }
                    padded_staging.push_back(padded); offset = padded.offset;
                }
                auto* dst = static_cast<unsigned char*>(staging.getCpuAddr()) + offset;
                for (std::uint32_t y=0;y<copy.rows;++y)
                    std::memcpy(dst+y*copy.row_pitch_bytes,
                        r.data+copy.source_offset+y*copy.source_row_bytes,
                        static_cast<std::size_t>(copy.source_row_bytes));
                dk::ImageView level_view{textures[i]};level_view.setMipLevels(level,1);
                cmd.copyBufferToImage({staging.getGpuAddr()+offset,copy.row_pitch_bytes,copy.layer_stride_bytes},
                    level_view,{0,0,0,copy.width,copy.height,1});
            }
            dk::ImageView view{textures[i]};
            dk::ImageDescriptor descriptor;
            descriptor.initialize(view);
            std::memcpy(static_cast<unsigned char*>(descriptors.getCpuAddr()) + i*sizeof descriptor,
                        &descriptor, sizeof descriptor);
        }
        // Legacy nearest stays available. Authored world mip chains use linear min/mag/mip.
        dk::Sampler sampler;
        sampler.setFilter(DkFilter_Nearest, DkFilter_Nearest)
            .setWrapMode(DkWrapMode_Repeat, DkWrapMode_Repeat, DkWrapMode_Repeat);
        dk::SamplerDescriptor sampler_descriptor;
        sampler_descriptor.initialize(sampler);
        sampler_offset = plan.sampler_offset+((source.frame.options[2]&10U)?32U:0U)+((source.race_environment || !source.frontend_cube_draws.empty())?32U:0U);
        std::memcpy(static_cast<unsigned char*>(descriptors.getCpuAddr()) + sampler_offset,
                    &sampler_descriptor, sizeof sampler_descriptor);
        sampler.setFilter(DkFilter_Linear, DkFilter_Linear);
        if(source.race_environment) {
            sampler.setFilter(DkFilter_Linear,DkFilter_Linear,DkMipFilter_Linear)
                .setMaxAnisotropy(4.0f);
            std::puts("WORLD FILTER min=linear mag=linear mip=linear anisotropy=4");
        }
        sampler_descriptor.initialize(sampler);
        std::memcpy(static_cast<unsigned char*>(descriptors.getCpuAddr()) +
                        sampler_offset + kDrawDescriptorBytes,
                    &sampler_descriptor, sizeof sampler_descriptor);
        if ((source.frame.options[2] & 10U) != 0 || (source.race_environment || !source.frontend_cube_draws.empty())) {
        sampler.setFilter(DkFilter_Linear, DkFilter_Linear)
            .setMaxAnisotropy(1.0f)
            .setWrapMode(DkWrapMode_ClampToEdge, DkWrapMode_ClampToEdge, DkWrapMode_ClampToEdge);
        sampler_descriptor.initialize(sampler);
        std::memcpy(static_cast<unsigned char*>(descriptors.getCpuAddr()) +
                        sampler_offset + 2*kDrawDescriptorBytes,
                    &sampler_descriptor, sizeof sampler_descriptor);
        }
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
        if ((source.frame.options[2] & 10U) != 0) {
        if (!block(2*(1+2*reflection_limit)*256, kDataFlags, "reflection uniforms", reflection_uniforms, error)) return false;
        reflection_width = (source.frame.options[2]&8U)?320U:std::min(160U,source.width);
        reflection_height = (source.frame.options[2]&8U)?240U:std::max(1U,reflection_width*source.height/source.width);
        dk::ImageLayout reflected_color_layout, reflected_depth_layout;
        dk::ImageLayoutMaker{device}.setFlags(DkImageFlags_UsageRender)
            .setFormat(DkImageFormat_RGBA8_Unorm).setDimensions(reflection_width, reflection_height).initialize(reflected_color_layout);
        dk::ImageLayoutMaker{device}.setFlags(DkImageFlags_UsageRender)
            .setFormat(DkImageFormat_ZF32).setDimensions(reflection_width, reflection_height).initialize(reflected_depth_layout);
        dk::MemBlock reflected_color_memory, reflected_depth_memory;
        if (!block(reflected_color_layout.getSize(), kImageFlags, "reflection color", reflected_color_memory, error) ||
            !block(reflected_depth_layout.getSize(), kImageFlags, "reflection depth", reflected_depth_memory, error)) return false;
        reflection_color.initialize(reflected_color_layout, reflected_color_memory, 0);
        reflection_depth.initialize(reflected_depth_layout, reflected_depth_memory, 0);
        dk::ImageView reflected_view{reflection_color};
        dk::ImageDescriptor reflected_descriptor;
        reflected_descriptor.initialize(reflected_view);
        std::memcpy(static_cast<unsigned char*>(descriptors.getCpuAddr()) + plan.sampler_offset+((source.race_environment || !source.frontend_cube_draws.empty())?32U:0U),
                    &reflected_descriptor, sizeof reflected_descriptor);
        reflection_ready = true;
        }
        if((source.race_environment || !source.frontend_cube_draws.empty())) {
            if((source.frame.options[2]&2U)!=0 && !source.race_environment){error="frontend cube and race reflection targets cannot share a frame";return false;}
            frontend_cube_capacity=source.race_environment ? kRaceCubeDrawLimit : source.frontend_cube_draws.size();
            if(!block(2*(6+2*frontend_cube_capacity)*256,kDataFlags,"frontend cube uniforms",frontend_cube_uniforms,error))return false;
            dk::ImageLayout color_layout,z_layout;
            dk::ImageLayoutMaker{device}.setFlags(DkImageFlags_UsageRender)
                .setFormat(DkImageFormat_RGBA8_Unorm).setDimensions(384,256).initialize(color_layout);
            dk::ImageLayoutMaker{device}.setFlags(DkImageFlags_UsageRender)
                .setFormat(DkImageFormat_ZF32).setDimensions(384,256).initialize(z_layout);
            dk::MemBlock color_memory,z_memory;
            if(!block(color_layout.getSize(),kImageFlags,"frontend cube faces",color_memory,error) ||
               !block(z_layout.getSize(),kImageFlags,"frontend cube depth",z_memory,error))return false;
            frontend_cube_color.initialize(color_layout,color_memory,0);
            frontend_cube_depth.initialize(z_layout,z_memory,0);
            dk::ImageDescriptor descriptor;descriptor.initialize(dk::ImageView{frontend_cube_color});
            std::memcpy(static_cast<unsigned char*>(descriptors.getCpuAddr())+plan.sampler_offset,&descriptor,sizeof descriptor);
            frontend_cube_ready=true;
        }
#if defined(NFSU_FRONTEND_PREFLIGHT)
        if(frontend_cube_ready && (!shader("frontend_probe.vert",DkStage_Vertex,probe_vertex,error) ||
            !shader("frontend_probe.frag",DkStage_Fragment,probe_constant,error) ||
            !shader("frontend_probe_textured.frag",DkStage_Fragment,probe_textured,error)))return false;
#endif
        if (!block(2*256,kDataFlags,"frontend light uniforms",frontend_light_uniforms,error))return false;
        if (!block(2*256,kDataFlags,"headlight uniforms",headlight_uniforms,error))return false;
        if(!write_uniforms(source, 0,error))return false;
        if (!shader("frontend_car.vert",DkStage_Vertex,frontend_car_vertex,error) ||
            !shader("frontend_car.frag",DkStage_Fragment,frontend_car_fragment,error))return false;
        if (!shader("car_shaded.vert", DkStage_Vertex, car_vertex, error) ||
            !shader("car_shaded.frag", DkStage_Fragment, car_fragment, error) ||
            !shader("sky.vert", DkStage_Vertex, sky_vertex, error) ||
            !shader("sky.frag", DkStage_Fragment, sky_fragment, error) ||
            !shader("placed.vert", DkStage_Vertex, vertex, error) ||
            !shader("car_shadow.vert", DkStage_Vertex, shadow_vertex, error) ||
            !shader("car_shadow.frag", DkStage_Fragment, shadow_fragment, error) ||
            !shader("textured.frag", DkStage_Fragment, textured, error) ||
            !shader("textured_opaque.frag", DkStage_Fragment, textured_opaque, error) ||
            !shader("frontend_ui.vert", DkStage_Vertex, frontend_vertex, error) ||
            !shader("frontend_ui.frag", DkStage_Fragment, frontend_fragment, error) ||
            !shader("fallback.frag", DkStage_Fragment, fallback, error)) return false;
        if (package && !prepare_static_commands(source, presentation, error)) return false;
        // The comparison uses the existing single-sample scene/depth path.
        // MSAA remains independently selectable; its depth cannot be attached
        // to a single-sample light history target.
        streaks_enabled=presentation && source.race_environment && !msaa_enabled;
        if(streaks_enabled) {
            if(!shader("fxaa.vert",DkStage_Vertex,streak_vertex,error) ||
               !shader("streak_fade.frag",DkStage_Fragment,streak_fade_fragment,error) ||
               !shader("streak_copy.frag",DkStage_Fragment,streak_copy_fragment,error) ||
               !block(2*kDrawDescriptorBytes,kDataFlags,"light streak descriptors",streak_descriptors,error))return false;
            dk::ImageLayout layout;
            dk::ImageLayoutMaker{device}.setFlags(DkImageFlags_UsageRender)
                .setFormat(DkImageFormat_RGBA8_Unorm).setDimensions(scene_target.width,scene_target.height).initialize(layout);
            dk::MemBlock memory;
            if(!block(layout.getSize(),kImageFlags,"light streak history",memory,error))return false;
            streak_color.initialize(layout,memory,0);
            dk::ImageDescriptor image;image.initialize(dk::ImageView{streak_color});
            dk::Sampler sampler;sampler.setFilter(DkFilter_Linear,DkFilter_Linear)
                .setWrapMode(DkWrapMode_ClampToEdge,DkWrapMode_ClampToEdge,DkWrapMode_ClampToEdge);
            dk::SamplerDescriptor descriptor;descriptor.initialize(sampler);
            std::memcpy(streak_descriptors.getCpuAddr(),&image,sizeof image);
            std::memcpy(static_cast<unsigned char*>(streak_descriptors.getCpuAddr())+kDrawDescriptorBytes,&descriptor,sizeof descriptor);
            std::printf("LIGHT STREAKS history=%ux%u decay_RGB=130,150,180/255 original409B70 before_FXAA_HUD=1\n",scene_target.width,scene_target.height);
        }
        if(fxaa_enabled) {
            if(!shader("fxaa.vert",DkStage_Vertex,fxaa_vertex,error) ||
               !shader("fxaa.frag",DkStage_Fragment,fxaa_fragment,error) ||
               !block(3*kDrawDescriptorBytes,kDataFlags,"FXAA descriptors",fxaa_descriptors,error))return false;
            dk::Sampler sampler;
            sampler.setFilter(DkFilter_Linear,DkFilter_Linear)
                .setWrapMode(DkWrapMode_ClampToEdge,DkWrapMode_ClampToEdge,DkWrapMode_ClampToEdge);
            dk::SamplerDescriptor descriptor;descriptor.initialize(sampler);
            std::memcpy(static_cast<unsigned char*>(fxaa_descriptors.getCpuAddr())+2*kDrawDescriptorBytes,
                        &descriptor,sizeof descriptor);
        }
        dk::ImageLayout color_layout, depth_layout, scene_layout;
        const auto transfer_flags = (scene_target.upscale || msaa_enabled) ? DkImageFlags_Usage2DEngine : 0;
        dk::ImageLayoutMaker{device}.setFlags(DkImageFlags_UsageRender | transfer_flags |
                                            (presentation ? DkImageFlags_UsagePresent : 0))
            .setFormat(DkImageFormat_RGBA8_Unorm).setDimensions(source.width, source.height).initialize(color_layout);
        dk::ImageLayoutMaker{device}.setFlags(DkImageFlags_UsageRender)
            .setType(msaa_enabled?DkImageType_2DMS:DkImageType_2D)
            .setMsMode(msaa_enabled?msaa_mode:DkMsMode_1x)
            .setFormat(DkImageFormat_ZF32).setDimensions(scene_target.width, scene_target.height).initialize(depth_layout);
        if(msaa_enabled) {
            // One queue completes each resolve before the next clear. These
            // GPU-only color/depth targets can be shared across frame slots,
            // as in the SDK multisampling example; resolved images stay per slot.
            dk::ImageLayout msaa_layout;
            dk::ImageLayoutMaker{device}.setFlags(DkImageFlags_UsageRender|DkImageFlags_Usage2DEngine)
                .setType(DkImageType_2DMS).setMsMode(msaa_mode)
                .setFormat(DkImageFormat_RGBA8_Unorm)
                .setDimensions(scene_target.width,scene_target.height).initialize(msaa_layout);
            if(msaa_samples==8) {
                // The opt-in 8x comparison reserves only the measured extra
                // color/depth allocation over the tested 2x configuration.
                // Other resources retain the existing budget; no fallback.
                dk::ImageLayout base_color,base_depth;
                dk::ImageLayoutMaker{device}.setFlags(DkImageFlags_UsageRender|DkImageFlags_Usage2DEngine)
                    .setType(DkImageType_2DMS).setMsMode(DkMsMode_2x)
                    .setFormat(DkImageFormat_RGBA8_Unorm)
                    .setDimensions(scene_target.width,scene_target.height).initialize(base_color);
                dk::ImageLayoutMaker{device}.setFlags(DkImageFlags_UsageRender)
                    .setType(DkImageType_2DMS).setMsMode(DkMsMode_2x)
                    .setFormat(DkImageFormat_ZF32)
                    .setDimensions(scene_target.width,scene_target.height).initialize(base_depth);
                const auto actual=align_up(msaa_layout.getSize(),DK_MEMBLOCK_ALIGNMENT)+
                                  align_up(depth_layout.getSize(),DK_MEMBLOCK_ALIGNMENT);
                const auto baseline=align_up(base_color.getSize(),DK_MEMBLOCK_ALIGNMENT)+
                                    align_up(base_depth.getSize(),DK_MEMBLOCK_ALIGNMENT);
                if(actual<baseline){error="MSAA allocation size order invalid";return false;}
                msaa_budget_extra=actual-baseline;
                std::printf("RACE AA MSAA8 extra_target_bytes=%llu GPU_budget_bytes=%llu\n",
                    (unsigned long long)msaa_budget_extra,
                    (unsigned long long)(kGpuBudgetPlaceholder+msaa_budget_extra));
            }
            dk::MemBlock memory;
            if(!block(msaa_layout.getSize(),kImageFlags,"MSAA scene color",memory,error))return false;
            msaa_color.initialize(msaa_layout,memory,0);
        }
        if (scene_target.upscale || fxaa_enabled)
            dk::ImageLayoutMaker{device}.setFlags(DkImageFlags_UsageRender | DkImageFlags_Usage2DEngine)
                .setFormat(DkImageFormat_RGBA8_Unorm).setDimensions(scene_target.width, scene_target.height)
                .initialize(scene_layout);
        for (std::size_t i = 0; i < (presentation ? 2U : 1U); ++i) {
            auto& color = colors[i];
            dk::MemBlock memory;
            if (!block(color_layout.getSize(), kImageFlags, "present image", memory, error)) return false;
            if(!trace("image.initialize.before",error,"present image",color_layout.getSize()))return false;
            color.initialize(color_layout, memory, 0);
            if(!trace("image.initialize.after",error,"present image",color_layout.getSize()))return false;
            if (scene_target.upscale || fxaa_enabled) {
                dk::MemBlock scene_memory;
                if (!block(scene_layout.getSize(), kImageFlags, "scene image", scene_memory, error)) return false;
                if(!trace("image.initialize.before",error,"scene image",scene_layout.getSize()))return false;
                scene_colors[i].initialize(scene_layout, scene_memory, 0);
                if(!trace("image.initialize.after",error,"scene image",scene_layout.getSize()))return false;
                if(fxaa_enabled) {
                    dk::ImageDescriptor descriptor;descriptor.initialize(dk::ImageView{scene_colors[i]});
                    std::memcpy(static_cast<unsigned char*>(fxaa_descriptors.getCpuAddr())+i*kDrawDescriptorBytes,
                                &descriptor,sizeof descriptor);
                    if(scene_target.upscale) {
                        dk::MemBlock aa_memory;
                        if(!block(scene_layout.getSize(),kImageFlags,"FXAA scene output",aa_memory,error))return false;
                        fxaa_colors[i].initialize(scene_layout,aa_memory,0);
                    }
                }
            }
        }
        if(fxaa_enabled)std::printf("RACE AA FXAA source=%ux%u search=8 subpixel=0.5 before_upscale_and_HUD=1\n",scene_target.width,scene_target.height);
        if(msaa_enabled)std::printf("RACE AA MSAA%u source=%ux%u samples=%u FXAA=off before_upscale_and_HUD=1\n",msaa_samples,scene_target.width,scene_target.height,msaa_samples);
        for (std::size_t i = 0; i < (presentation && !msaa_enabled ? 2U : 1U); ++i) {
            dk::MemBlock depth_memory;
            if (!block(depth_layout.getSize(), kImageFlags, "depth image", depth_memory, error)) return false;
            if(!trace("image.initialize.before",error,"depth image",depth_layout.getSize()))return false;
            depths[i].initialize(depth_layout, depth_memory, 0);
            if(!trace("image.initialize.after",error,"depth image",depth_layout.getSize()))return false;
        }
        if (presentation) {
            const std::array<const DkImage*, 2> targets{&colors[0], &colors[1]};
            if(!trace("swapchain.before",error))return false;
            swapchain = dk::SwapchainMaker{device, nwindowGetDefault(), targets}.create();
            if(!trace("swapchain.after",error))return false;
            if (!swapchain || !healthy(error)) {
                if (error.empty()) error = "deko3d swapchain creation failed";
                return false;
            }
        }
        if (hud_atlas && !init_hud(*hud_atlas,error)) return false;
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
            if(!write_uniforms(next, 0,error))return false;
            if (timing) timing->uniform_write_ms = elapsed_ms(write_start);
        }
        commands = &next;
        return true;
    }

    void consume_query(std::size_t slot) {
        if (!query_pending[slot]) return;
        query_pending[slot] = false;
        if (!query_frames[slot].active || query_generations[slot] != profile_generation) return;
        const auto* words = static_cast<const std::uint64_t*>(query_memory.getCpuAddr()) + slot*20;
        const auto begin_timestamp = words[1], end_timestamp = words[3];
        if (!begin_timestamp || end_timestamp <= begin_timestamp) {
            ++gpu_profile.invalid; return;
        }
        const double value = dkTimestampToNs(end_timestamp-begin_timestamp)/1000000.0;
        gpu_profile.add_scene(value, query_frames[slot]);
        if(query_frames[slot].ordinal && ab_rows.size()<6000) {
            AbGpuRow row{query_frames[slot],value,{},true};
            if(ab_passes)for(unsigned i=0;i<7;++i) {
                const auto a=words[5+2*i],b=words[7+2*i];
                if(!a || b<a)row.valid=false;
                else row.stages[i]=dkTimestampToNs(b-a)/1000000.0;
            }
            ab_rows.push_back(row);
        }
    }

    bool write_uniforms(const RecordedCommands& source, std::size_t slot, std::string& error) {
        auto* data = static_cast<unsigned char*>(uniforms.getCpuAddr()) +
            uniform_allocation.offset + slot * uniform_stride;
        std::memcpy(data, &source.frame, sizeof source.frame);
        std::memcpy(static_cast<unsigned char*>(headlight_uniforms.getCpuAddr())+slot*256,&source.headlight_frame,sizeof(DrawFrameUniform));
        if(frontend_cube_ready) {
            if(source.frontend_cube_draws.size()>frontend_cube_capacity){error="cube draw capacity changed";return false;}
            auto* cube=static_cast<unsigned char*>(frontend_cube_uniforms.getCpuAddr())+slot*(6+2*frontend_cube_capacity)*256;
            for(unsigned face=0;face<6;++face)std::memcpy(cube+face*256,&source.frontend_cube_frames[face],sizeof(DrawFrameUniform));
            for(std::size_t i=0;i<source.frontend_cube_draws.size();++i) {
                std::memcpy(cube+(6+2*i)*256,&source.frontend_cube_draws[i].placement,sizeof(DrawInstanceUniform));
                std::memcpy(cube+(7+2*i)*256,&source.frontend_cube_draws[i].material,sizeof(DrawMaterialUniform));
            }
        }
        std::memcpy(static_cast<unsigned char*>(frontend_light_uniforms.getCpuAddr())+slot*256,
            &source.frontend_light,sizeof source.frontend_light);
        auto mirror = source.frame;
        mirror.options[2] = (mirror.options[2] & 1U) | 4U;
        if((source.frame.options[2]&8U)!=0)mirror=source.frontend_floor_frame;
        if (reflection_ready) {
            auto* reflected = static_cast<unsigned char*>(reflection_uniforms.getCpuAddr()) + slot*(1+2*reflection_limit)*256;
            std::memcpy(reflected, &mirror, sizeof mirror);
            for (std::size_t i = 0; i < source.reflection_draw_count() && i < reflection_limit; ++i) {
                const auto& draw = source.reflection_draw(i);
                std::memcpy(reflected+(1+2*i)*256, &draw.placement, sizeof draw.placement);
                auto material=source.frontend_light_enabled ? draw.material :
                    race_effect_gpu_material(draw,draw.material);
                std::memcpy(reflected+(2+2*i)*256, &material, sizeof material);
            }
        }
        const auto first_dynamic = package ? source.static_track_draw_count() : 0;
        for (std::size_t i = first_dynamic; i < source.draw_count(); ++i) {
            const auto view = source.draw_view(i);
            const auto index = i - first_dynamic;
            // Original LIGHTSTREAKS replaces direct glow submission: these
            // same sprites are emitted exactly once into the history below.
            if(streaks_enabled && view.material.alpha[3]==5.0F)continue;
            std::memcpy(data + (1+2*index)*256, &view.placement, sizeof(DrawInstanceUniform));
            // Copy a private GPU payload; the recorded retail material stays intact.
            auto material=view.material;
            if(source.frontend_light_enabled && frontend_glow_fast(view,source.frame))
                material.fallback[3]=3;
            else if(!source.frontend_light_enabled)
                material=race_effect_gpu_material(view.definition,view.material);
            std::memcpy(data + (2+2*index)*256, &material, sizeof(DrawMaterialUniform));
        }
        return true;
    }

    template<class Recorder>
    void emit_draw(Recorder& out, const RecordedDraw& draw,
                   std::uint64_t instance_addr, std::uint64_t material_addr,
                   bool bind_fixed_state = true,dk::Shader* fragment_override=nullptr) {
        const auto& state = draw.pipeline;
        const bool has_texture = draw.texture_resource != kNoHandle;
        const bool cutout = deko_textured_uses_cutout_shader(
            state.alpha_test != sm::CompareOp::always, commands->frame.options[3]);
        const bool shadow=std::strcmp(draw.program->name,"car_shadow_textured")==0;
        const bool frontend=std::strcmp(draw.program->name,"frontend_car_textured")==0;
        const bool shaded = frontend || std::strcmp(draw.program->name, "car_shaded_textured") == 0 ||
                            std::strcmp(draw.program->name, "car_shaded_linear_textured") == 0;
        const bool sky = std::strcmp(draw.program->name, "sky_textured") == 0;
        dk::Shader* fragment = shadow ? &shadow_fragment : frontend ? &frontend_car_fragment : shaded ? &car_fragment : sky ? &sky_fragment :
            !has_texture ? &fallback :
            cutout ? &textured : &textured_opaque;
        if(fragment_override)fragment=fragment_override;
        if (bind_fixed_state)
            out.bindShaders(DkStageFlag_GraphicsMask,
                            {shadow ? &shadow_vertex : frontend ? &frontend_car_vertex : shaded ? &car_vertex : sky ? &sky_vertex : &vertex, fragment});
        if (instance_addr)
            out.bindUniformBuffer(DkStage_Vertex, 1, instance_addr, sizeof(DrawInstanceUniform));
        out.bindUniformBuffer(DkStage_Fragment, 2, material_addr, sizeof(DrawMaterialUniform));
        if (shaded || shadow) out.bindUniformBuffer(DkStage_Vertex, 2, material_addr, sizeof(DrawMaterialUniform));
        if (bind_fixed_state && has_texture) out.bindTextures(DkStage_Fragment, 0,
            dkMakeTextureHandle(static_cast<std::uint32_t>(draw.texture_resource),
                draw.program->textures[0].filter == sm::Filter::linear ? 1 : 0));
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
            std::array<DkVtxAttribState, 4> attributes{};
            for(auto& attribute:attributes)attribute={0,1,0,DkVtxAttribSize_1x32,DkVtxAttribType_Float,0};
            for (std::size_t a = 0; a < draw.program->attribute_count; ++a) {
                const auto& input = draw.program->attributes[a];
                const bool byte = input.format == sm::VertexFormat::unorm8x4;
                attributes[input.location] = {input.buffer, 0, input.offset,
                    byte ? DkVtxAttribSize_4x8 : input.format == sm::VertexFormat::float32x3 ? DkVtxAttribSize_3x32 : DkVtxAttribSize_2x32,
                    byte ? DkVtxAttribType_Unorm : DkVtxAttribType_Float, 0};
            }
            // Attribute locations can be sparse (FE car: 0, 2, 3). The
            // API count covers locations, not the number of manifest records.
            // Match SDK trailing defaults: holes are fixed valid float slots,
            // not zero descriptors with an invalid size and active fetch.
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
        for (std::size_t i = 0; i < static_definitions.size(); ++i) {
            const auto& draw=*static_definitions[i];
            const auto material=source.frontend_light_enabled ? draw.material :
                race_effect_gpu_material(draw,draw.material);
            std::memcpy(data + (static_instances+i)*256, &material, sizeof material);
        }
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

    void emit_overlay(std::size_t slot,const TrackVertex* input,std::size_t quads,
                      const unsigned char* additive=nullptr) {
        if (!hud_enabled || !quads || quads>overlay_capacity) return;
        const std::uint32_t slot_bytes=sizeof(TrackVertex)*overlay_capacity*4;
        auto* vertices=reinterpret_cast<TrackVertex*>(
            static_cast<unsigned char*>(hud_vertices.getCpuAddr())+slot*slot_bytes);
        for (std::size_t i=0;i<quads*4;++i) {
            vertices[i]=input[i];
            vertices[i].px-=source_width()*0.5F;
            vertices[i].py-=source_height()*0.5F;
            vertices[i].pz=-1;
        }
        cmd.bindShaders(DkStageFlag_GraphicsMask,{additive?&frontend_vertex:&vertex,additive?&frontend_fragment:&textured_opaque});
        const auto base=hud_uniforms.getGpuAddr();
        cmd.bindUniformBuffer(DkStage_Vertex,0,base,sizeof(DrawFrameUniform));
        cmd.bindUniformBuffer(DkStage_Fragment,0,base,sizeof(DrawFrameUniform));
        cmd.bindUniformBuffer(DkStage_Vertex,1,base+256,sizeof(DrawInstanceUniform));
        cmd.bindUniformBuffer(DkStage_Fragment,2,base+512,sizeof(DrawMaterialUniform));
        cmd.bindImageDescriptorSet(hud_descriptors.getGpuAddr(),1);
        cmd.bindSamplerDescriptorSet(hud_descriptors.getGpuAddr()+kDrawDescriptorBytes,1);
        cmd.barrier(DkBarrier_None,DkInvalidateFlags_Shader|DkInvalidateFlags_Descriptors);
        cmd.bindTextures(DkStage_Fragment,0,dkMakeTextureHandle(0,0));
        dk::RasterizerState raster;
        raster.setCullMode(DkFace_None).setFrontFace(DkFrontFace_CCW);
        dk::ColorState color;
        color.setBlendEnable(0,true).setAlphaCompareOp(DkCompareOp_Always);
        dk::ColorWriteState write;
        write.setMask(0,DkColorMask_RGB);
        dk::BlendState blend;
        blend.setOps(DkBlendOp_Add,DkBlendOp_Add)
            .setFactors(DkBlendFactor_SrcAlpha,DkBlendFactor_InvSrcAlpha,
                        DkBlendFactor_One,DkBlendFactor_Zero);
        dk::DepthStencilState depth;
        depth.setDepthTestEnable(false).setDepthWriteEnable(false);
        cmd.bindRasterizerState(raster); cmd.bindColorState(color);
        cmd.bindColorWriteState(write); cmd.bindBlendStates(0,blend);
        cmd.bindDepthStencilState(depth); cmd.setAlphaRef(0);
        std::array<DkVtxAttribState,3> attributes{};
        attributes[0]={0,0,0,DkVtxAttribSize_3x32,DkVtxAttribType_Float,0};
        attributes[1]={0,0,12,DkVtxAttribSize_4x8,DkVtxAttribType_Unorm,0};
        attributes[2]={0,0,16,DkVtxAttribSize_2x32,DkVtxAttribType_Float,0};
        cmd.bindVtxAttribState(attributes);
        cmd.bindVtxBufferState(DkVtxBufferState{sizeof(TrackVertex),0});
        cmd.bindVtxBuffer(0,hud_vertices.getGpuAddr()+slot*slot_bytes,
                          quads*4*sizeof(TrackVertex));
        cmd.bindIdxBuffer(DkIdxFormat_Uint16,hud_indices.getGpuAddr());
        for(std::size_t first=0;first<quads;) {
            const bool add=additive && additive[first];
            std::size_t end=first+1;
            while(end<quads && bool(additive && additive[end])==add)++end;
            blend.setFactors(DkBlendFactor_SrcAlpha,add?DkBlendFactor_One:DkBlendFactor_InvSrcAlpha,
                             DkBlendFactor_One,DkBlendFactor_Zero);
            cmd.bindBlendStates(0,blend);
            cmd.drawIndexed(DkPrimitive_Triangles,(end-first)*6,1,first*6,0,0);first=end;
        }
    }

    void consume_frontend_query(std::size_t slot) {
        if(!frontend_query_pending[slot])return;
        frontend_query_pending[slot]=false;
        const auto* words=static_cast<const std::uint64_t*>(frontend_query_memory.getCpuAddr())+slot*26;
        double elapsed[9]{};
        for(unsigned i=0;i<9;++i) {
            const auto a=words[i*2+1],b=words[(i+1)*2+1];
            if(!a || b<a){std::printf("MENU GPU invalid timestamp sample\n");return;}
            elapsed[i]=dkTimestampToNs(b-a)/1000000.0;
        }
        const auto road_end=words[21],car_end=words[23],sprites_end=words[25];
        if(road_end<words[15] || car_end<road_end || sprites_end<car_end || words[17]<sprites_end) {
            std::printf("MENU GPU invalid main span timestamp sample\n");return;
        }
        if(++frontend_profile_samples<=3 || frontend_profile_samples%60==0)
            std::printf("MENU GPU timestamp_ms cube0=%.3f cube1=%.3f cube2=%.3f cube3=%.3f cube4=%.3f cube5=%.3f floor=%.3f main_upscale=%.3f ui=%.3f main_road=%.3f main_car=%.3f main_sprites_upscale=%.3f main_sprites=%.3f upscale=%.3f (profile barriers enabled)\n",
                elapsed[0],elapsed[1],elapsed[2],elapsed[3],elapsed[4],elapsed[5],elapsed[6],elapsed[7],elapsed[8],
                dkTimestampToNs(road_end-words[15])/1000000.0,dkTimestampToNs(car_end-road_end)/1000000.0,
                dkTimestampToNs(words[17]-car_end)/1000000.0,
                dkTimestampToNs(sprites_end-car_end)/1000000.0,dkTimestampToNs(words[17]-sprites_end)/1000000.0);
    }

    bool record_frame(const dk::Image& target, std::size_t slot,
                      const RaceHudFrame* hud, std::string& error,const FrontendUiFrame* frontend=nullptr) {
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
        if(msaa_enabled) {
            // Cube and road-reflection targets are single-sampled.
            cmd.bindMultisampleState(dk::MultisampleState{});
            cmd.setSampleMask(0xFFFFU);
        }
        // Diagnose the first live FE frame without dropping any pass. Waiting
        // completes each command-memory use before begin() reuses that slot.
        // Later frames keep the ordinary asynchronous presentation path.
        #if defined(NFSU_FRONTEND_PREFLIGHT)
        const bool diagnose=frontend && frontend_first_frame;
#else
        constexpr bool diagnose=false;
#endif
        const auto checkpoint=[&](const char* pass) {
            if(!diagnose)return true;
            std::printf("frontend GPU first frame: submit %s\n",pass);
            cmd.barrier(DkBarrier_Full,DkInvalidateFlags_Image|DkInvalidateFlags_L2Cache);
            if(!submit(error)) {error=std::string("first-frame ")+pass+": "+error;return false;}
            std::printf("frontend GPU first frame: complete %s\n",pass);
            begin(slot);return true;
        };
        const auto msaa_checkpoint=[&](const char* pass) {
            if(!msaa_enabled || !msaa_first_frame)return true;
            std::printf("MSAA PREFLIGHT submit %s\n",pass);
            cmd.barrier(DkBarrier_Full,DkInvalidateFlags_Image|DkInvalidateFlags_L2Cache);
            if(!submit(error)) {error=std::string("MSAA first-frame ")+pass+": "+error;return false;}
            std::printf("MSAA PREFLIGHT complete %s\n",pass);
            begin(slot);return true;
        };
        if (profile_enabled)
            cmd.reportCounter(DkCounter_TimestampPipelineTop, query_memory.getGpuAddr()+slot*160);
        const auto mark_ab=[&](unsigned point) {
            if(!profile_enabled || !query_frames[slot].ordinal || !ab_passes)return;
            if(point)cmd.barrier(DkBarrier_Fragments,0);
            cmd.reportCounter(point?DkCounter_Timestamp:DkCounter_TimestampPipelineTop,
                query_memory.getGpuAddr()+slot*160+32+point*16);
        };
        mark_ab(0);
        const auto mark_frontend=[&](unsigned point) {
            if(!frontend || !frontend_profile_passes)return;
            if(point)cmd.barrier(DkBarrier_Fragments,0);
            cmd.reportCounter(point?DkCounter_Timestamp:DkCounter_TimestampPipelineTop,
                frontend_query_memory.getGpuAddr()+slot*208+point*16);
        };
        mark_frontend(0);
        const auto base = uniforms.getGpuAddr() + uniform_allocation.offset + slot * uniform_stride;
        cmd.bindImageDescriptorSet(descriptors.getGpuAddr(), static_cast<std::uint32_t>(handles.size()+(reflection_ready?1:0)+(frontend_cube_ready?1:0)));
        cmd.bindSamplerDescriptorSet(descriptors.getGpuAddr()+sampler_offset, (reflection_ready || frontend_cube_ready) ? 3 : 2);
        cmd.barrier(DkBarrier_None, DkInvalidateFlags_Shader | DkInvalidateFlags_Descriptors);
        cmd.bindUniformBuffer(DkStage_Fragment,4,headlight_uniforms.getGpuAddr()+slot*256,sizeof(DrawFrameUniform));
        if(first_texture!=kNoHandle)cmd.bindTextures(DkStage_Fragment,2,dkMakeTextureHandle(
            static_cast<std::uint32_t>(commands->headlight_texture==kNoHandle ? first_texture : commands->headlight_texture),
            (reflection_ready || frontend_cube_ready)?2:1));
        if(!frontend_cube_ready && !reflection_ready && first_texture!=kNoHandle)
            cmd.bindTextures(DkStage_Fragment,1,dkMakeTextureHandle(static_cast<std::uint32_t>(first_texture),1));
        if(frontend_cube_ready) {
            // Both basic fragment stages declare sampler1. Define its handle
            // even for capture frames whose reflection branch is inactive.
            // This table entry is an ordinary uploaded asset, never the
            // capture target being written. Authored active bindings override
            // it after capture exactly as before.
            if(first_texture==kNoHandle){error="frontend capture has no safe uploaded texture";return false;}
            cmd.bindTextures(DkStage_Fragment,1,dkMakeTextureHandle(static_cast<std::uint32_t>(first_texture),2));
            if(commands->frontend_cube_draws.size()>frontend_cube_capacity || (!commands->race_environment && commands->frontend_cube_draws.size()!=frontend_cube_capacity)){error="frontend cube draw capacity changed";return false;}
            dk::ImageView color{frontend_cube_color},z{frontend_cube_depth};
            cmd.bindRenderTargets(&color,&z);
            cmd.setViewports(0,{{0,0,384,256,0,1}});cmd.setScissors(0,{{0,0,384,256}});
            if(!commands->race_environment || !cube_initialized) {
                cmd.clearColor(0,DkColorMask_RGBA,0,0,0,1);cmd.clearDepthStencil(true,1,0,0);
                cube_initialized=true;
            }
            if(!checkpoint("cube target bind/clear"))return false;
            const auto cube=frontend_cube_uniforms.getGpuAddr()+slot*(6+2*frontend_cube_capacity)*256;
            // Native quality3 face mask updates all six; each logical face is
            // 128-square. Atlas packing changes neither orientation nor view.
            for(unsigned face=0;face<6;++face) {
                if(!(commands->cube_face_mask & (1U<<face)))continue;
                cmd.setViewports(0,{{float((face%3)*128),float((face/3)*128),128,128,0,1}});
                cmd.setScissors(0,{{(face%3)*128,(face/3)*128,128,128}});
                if(commands->race_environment){cmd.clearColor(0,DkColorMask_RGBA,0,0,0,1);cmd.clearDepthStencil(true,1,0,0);}
                cmd.bindUniformBuffer(DkStage_Vertex,0,cube+face*256,sizeof(DrawFrameUniform));
                cmd.bindUniformBuffer(DkStage_Fragment,0,cube+face*256,sizeof(DrawFrameUniform));
                const RecordedDraw* previous=nullptr;
                std::size_t emitted=0;
                for(std::size_t i=0;i<commands->frontend_cube_draws.size();++i) {
                    const auto& draw=commands->frontend_cube_draws[i];
                    if(draw.frontend_sprite_view && draw.frontend_sprite_view!=face+10)continue;
                    if(diagnose && previous && (previous->program!=draw.program ||
                        bool(previous->frontend_sprite_view)!=bool(draw.frontend_sprite_view))) {
                        char pass[96];std::snprintf(pass,sizeof pass,"cube face%u %s %s",face,
                            previous->program->name,previous->frontend_sprite_view?"sprites":"solids");
                        if(!checkpoint(pass))return false;
                        previous=nullptr;
                    }
                    if(diagnose && !emitted)std::printf(
                        "frontend GPU first cube draw: face%u %s %s vertex%zu index%zu texture%zu first%u count%u\n",
                        face,draw.name.c_str(),draw.program->name,draw.vertex_resource,draw.index_resource,
                        draw.texture_resource,draw.first_index,draw.index_count);
#if defined(NFSU_FRONTEND_PREFLIGHT)
                    if(diagnose && face==0 && !emitted) {
                        const auto& vb=records.at(draw.vertex_resource).placement;
                        const auto& ib=records.at(draw.index_resource).placement;
                        const auto& resources=commands->resource_table().resources;
                        const auto& vr=resources.at(draw.vertex_resource);
                        const auto& ir=resources.at(draw.index_resource);
                        const auto instance=cube+(6+2*i)*256,material=cube+(7+2*i)*256;
                        if(vb.offset+vb.size>buffers.getSize() || ib.offset+ib.size>buffers.getSize() ||
                            vb.offset%4 || ib.offset%2 || instance%256 || material%256 ||
                            material+sizeof(DrawMaterialUniform)>frontend_cube_uniforms.getGpuAddr()+frontend_cube_uniforms.getSize() ||
                            vr.bytes>vb.size || ir.bytes>ib.size ||
                            std::memcmp(static_cast<unsigned char*>(buffers.getCpuAddr())+vb.offset,vr.data,vr.bytes) ||
                            std::memcmp(static_cast<unsigned char*>(buffers.getCpuAddr())+ib.offset,ir.data,ir.bytes)) {
                            error="first mesh GPU buffer address/copy contract failed";return false;
                        }
                        std::printf("frontend GPU bindings: vertex%llX+%llu index%llX+%llu frame%llX instance%llX material%llX\n",
                            (unsigned long long)(buffers.getGpuAddr()+vb.offset),(unsigned long long)vb.size,
                            (unsigned long long)(buffers.getGpuAddr()+ib.offset),(unsigned long long)ib.size,
                            (unsigned long long)(cube+face*256),(unsigned long long)instance,(unsigned long long)material);
                        const auto clear_probe=[&] {
                            cmd.clearColor(0,DkColorMask_RGBA,0,0,0,1);cmd.clearDepthStencil(true,1,0,0);
                        };
                        cmd.bindShaders(DkStageFlag_GraphicsMask,{&probe_vertex,&probe_constant});
                        dk::RasterizerState raster;raster.setCullMode(DkFace_None);
                        dk::ColorState color;color.setBlendEnable(0,false).setAlphaCompareOp(DkCompareOp_Always);
                        dk::ColorWriteState write;write.setMask(0,DkColorMask_RGBA);
                        dk::DepthStencilState depth;depth.setDepthTestEnable(false).setDepthWriteEnable(false);
                        cmd.bindRasterizerState(raster);cmd.bindColorState(color);cmd.bindColorWriteState(write);
                        cmd.bindDepthStencilState(depth);cmd.bindVtxAttribState(std::array<DkVtxAttribState,0>{});
                        cmd.bindVtxBufferState(std::array<DkVtxBufferState,0>{});
                        cmd.draw(DkPrimitive_Triangles,3,1,0,0);
                        if(!checkpoint("preflight procedural constant"))return false;
                        clear_probe();
                        emit_draw(cmd,draw,instance,material,true,&probe_constant);
                        if(!checkpoint("preflight actual mesh constant"))return false;
                        clear_probe();
                        emit_draw(cmd,draw,instance,material,true,&probe_textured);
                        if(!checkpoint("preflight actual mesh texture0"))return false;
                        clear_probe();previous=nullptr;
                    }
#endif
                    emit_draw(cmd,draw,cube+(6+2*i)*256,cube+(7+2*i)*256,
                        !previous || !same_fixed_state(*previous,draw));previous=&draw;
                    if(!emitted++) {
                        char pass[40];std::snprintf(pass,sizeof pass,"cube face%u first solid",face);
                        if(!checkpoint(pass))return false;
                        if(diagnose)previous=nullptr;
                    }
                }
                char pass[32];std::snprintf(pass,sizeof pass,"cube face%u",face);
                if(!checkpoint(pass))return false;
                mark_frontend(face+1);
            }
            cmd.barrier(DkBarrier_Fragments,DkInvalidateFlags_Image);
            cmd.bindTextures(DkStage_Fragment,1,dkMakeTextureHandle(static_cast<std::uint32_t>(handles.size()),2));
        }
        mark_ab(1);
        if ((commands->frame.options[2] & 10U) != 0) {
            if (!reflection_ready || commands->reflection_draw_count() > reflection_limit) {
                error = "reflection resources unavailable or capacity exceeded"; return false;
            }
            dk::ImageView reflection_view{reflection_color}, reflected_z{reflection_depth};
            cmd.bindRenderTargets(&reflection_view, &reflected_z);
            cmd.setViewports(0, {{0, 0, float(reflection_width), float(reflection_height), 0, 1}});
            cmd.setScissors(0, {{0, 0, reflection_width, reflection_height}});
            cmd.clearColor(0, DkColorMask_RGBA, 0, 0, 0, 1);
            cmd.clearDepthStencil(true, 1, 0, 0);
            const auto reflected_frame = reflection_uniforms.getGpuAddr() + slot*(1+2*reflection_limit)*256;
            cmd.bindUniformBuffer(DkStage_Vertex, 0, reflected_frame, sizeof(DrawFrameUniform));
            cmd.bindUniformBuffer(DkStage_Fragment, 0, reflected_frame, sizeof(DrawFrameUniform));
            const RecordedDraw* previous = nullptr;
            for (std::size_t i=0; i<commands->reflection_draw_count(); ++i) {
                const auto& draw = commands->reflection_draw(i);
                emit_draw(cmd, draw, reflected_frame+(1+2*i)*256, reflected_frame+(2+2*i)*256,
                          !previous || !same_fixed_state(*previous, draw));
                previous = &draw;
            }
            cmd.barrier(DkBarrier_Fragments, DkInvalidateFlags_Image);
            if(!checkpoint("floor reflection"))return false;
        }
        mark_ab(2);
        mark_frontend(7);
        if(!msaa_checkpoint("single-sample reflections"))return false;
        if (reflection_ready) cmd.bindTextures(DkStage_Fragment, 1,
            dkMakeTextureHandle(static_cast<std::uint32_t>(handles.size()+(frontend_cube_ready?1:0)), 2));
        dk::ImageView color_view{msaa_enabled?msaa_color:((scene_target.upscale || fxaa_enabled)?scene_colors[slot]:target)};
        dk::ImageView depth_view{depths[msaa_enabled?0:slot]};
        cmd.bindRenderTargets(&color_view, &depth_view);
        if(msaa_enabled) {
            dk::MultisampleState samples;
            samples.setMode(msaa_mode).setLocations();
            cmd.bindMultisampleState(samples);
            cmd.setSampleMask(0xFFFFU);
        }
        cmd.setViewports(0, {{0, 0, static_cast<float>(scene_target.width), static_cast<float>(scene_target.height), 0, 1}});
        cmd.setScissors(0, {{0, 0, scene_target.width, scene_target.height}});
        const auto* clear = commands->clear_color;
        cmd.clearColor(0, DkColorMask_RGBA, clear[0]/255.0F, clear[1]/255.0F, clear[2]/255.0F, clear[3]/255.0F);
        cmd.clearDepthStencil(true, 1, 0, 0);
        if(!msaa_checkpoint("MSAA target clear"))return false;
        cmd.bindUniformBuffer(DkStage_Vertex, 0, base, sizeof(DrawFrameUniform));
        cmd.bindUniformBuffer(DkStage_Fragment, 0, base, sizeof(DrawFrameUniform));
        cmd.bindUniformBuffer(DkStage_Vertex,3,frontend_light_uniforms.getGpuAddr()+slot*256,sizeof(FrontendLighting));
        for (const auto list : static_lists[slot]) cmd.callList(list);
        const auto first_dynamic = package ? commands->static_track_draw_count() : 0;
        const RecordedDraw* previous_dynamic = nullptr;
        for (std::size_t i = first_dynamic; i < commands->draw_count(); ++i) {
            if(frontend && frontend_profile_passes) {
                if(i==frontend_car_begin)mark_frontend(10);
                if(i==frontend_car_end)mark_frontend(11);
            }
            const auto view = commands->draw_view(i);
            const auto index = i - first_dynamic;
            // Zero source alpha contributes no RGB, alpha or depth under
            // this validated additive state. Keep original uniform indices
            // and the last emitted draw for fixed-state reuse.
            if(frontend && frontend_glow_no_output(view,commands->frame))continue;
            if(!frontend && !commands->frontend_light_enabled && race_effect_no_output(view))continue;
            if(diagnose && previous_dynamic &&
                previous_dynamic->program!=view.definition.program) {
                if(!checkpoint(previous_dynamic->program->name))return false;
                previous_dynamic=nullptr;
            }
            if(frontend_cube_ready && reflection_ready) {
                const bool car=std::strcmp(view.definition.program->name,"frontend_car_textured")==0 ||
                    std::strcmp(view.definition.program->name,"car_shaded_textured")==0 ||
                    std::strcmp(view.definition.program->name,"car_shaded_linear_textured")==0;
                cmd.bindTextures(DkStage_Fragment,1,dkMakeTextureHandle(
                    static_cast<std::uint32_t>(handles.size()+(car?0:1)),2));
            }
            emit_draw(cmd, view.definition, base+(1+2*index)*256,
                      base+(2+2*index)*256,
                      !previous_dynamic || !same_fixed_state(*previous_dynamic, view.definition));
            previous_dynamic = &view.definition;
        }
        cmd.barrier(DkBarrier_Fragments, 0);
        if(frontend && frontend_profile_passes && frontend_car_end==commands->draw_count())mark_frontend(11);
        // The preceding fragment barrier already completes the sprite pass.
        // This opt-in report reuses it; the normal path adds no GPU command.
        if(frontend && frontend_profile_passes)
            cmd.reportCounter(DkCounter_Timestamp,frontend_query_memory.getGpuAddr()+slot*208+12*16);
        if(!checkpoint(previous_dynamic?previous_dynamic->program->name:"main scene"))return false;
        mark_ab(3);
        if(!msaa_checkpoint("MSAA scene draw"))return false;
        if(msaa_enabled) {
            // Resolve at the original scene resolution, then use the same
            // linear upscale as the baseline. HUD is never multisampled.
            cmd.barrier(DkBarrier_Full,DkInvalidateFlags_L2Cache);
            dk::ImageView resolved_view{scene_target.upscale?scene_colors[slot]:target};
            cmd.resolveImage(color_view,resolved_view);
            if(!msaa_checkpoint("MSAA resolve"))return false;
            cmd.barrier(DkBarrier_Full,DkInvalidateFlags_Image|DkInvalidateFlags_L2Cache);
            cmd.bindMultisampleState(dk::MultisampleState{});
            if(!scene_target.upscale)cmd.bindRenderTargets(&resolved_view,nullptr);
        }
        if(streaks_enabled) {
            // One ordered GPU queue owns this persistent image across both
            // swap slots. Barriers separate sample, render and next-frame use.
            cmd.barrier(DkBarrier_Full,DkInvalidateFlags_Image|DkInvalidateFlags_L2Cache);
            dk::ImageView history{streak_color};
            cmd.bindRenderTargets(&history,nullptr);
            cmd.setViewports(0,{{0,0,float(scene_target.width),float(scene_target.height),0,1}});
            cmd.setScissors(0,{{0,0,scene_target.width,scene_target.height}});
            if(!streaks_initialized) {
                cmd.clearColor(0,DkColorMask_RGBA,0,0,0,0);
                streaks_initialized=true;
            }
            const auto fullscreen=[&](dk::Shader& fragment,bool fading) {
                cmd.bindShaders(DkStageFlag_GraphicsMask,{&streak_vertex,&fragment});
                dk::RasterizerState raster;raster.setCullMode(DkFace_None);
                dk::ColorState color;color.setBlendEnable(0,true).setAlphaCompareOp(DkCompareOp_Always);
                dk::ColorWriteState write;write.setMask(0,DkColorMask_RGB);
                dk::BlendState blend;blend.setFactors(fading?DkBlendFactor_Zero:DkBlendFactor_One,
                    fading?DkBlendFactor_SrcColor:DkBlendFactor_One,DkBlendFactor_Zero,DkBlendFactor_One);
                dk::DepthStencilState depth;depth.setDepthTestEnable(false).setDepthWriteEnable(false);
                cmd.bindRasterizerState(raster);cmd.bindColorState(color);cmd.bindColorWriteState(write);
                cmd.bindBlendStates(0,blend);cmd.bindDepthStencilState(depth);
                cmd.bindVtxAttribState(std::array<DkVtxAttribState,0>{});
                cmd.draw(DkPrimitive_Triangles,3,1,0,0);
            };
            fullscreen(streak_fade_fragment,true);
            cmd.barrier(DkBarrier_Fragments,0);
            // Test new lights against this frame's scene depth. Old history
            // remains in screen space, as in the original persistent target.
            cmd.bindRenderTargets(&history,&depth_view);
            cmd.bindImageDescriptorSet(descriptors.getGpuAddr(),static_cast<std::uint32_t>(handles.size()+(reflection_ready?1:0)+(frontend_cube_ready?1:0)));
            cmd.bindSamplerDescriptorSet(descriptors.getGpuAddr()+sampler_offset,(reflection_ready||frontend_cube_ready)?3:2);
            const RecordedDraw* previous_light=nullptr;
            for(std::size_t i=first_dynamic;i<commands->draw_count();++i) {
                const auto view=commands->draw_view(i);
                if(view.material.alpha[3]!=5.0F || view.material.alpha[0]<=0)continue;
                const auto index=i-first_dynamic;
                emit_draw(cmd,view.definition,base+(1+2*index)*256,base+(2+2*index)*256,
                    !previous_light || !same_fixed_state(*previous_light,view.definition));
                previous_light=&view.definition;
            }
            cmd.barrier(DkBarrier_Full,DkInvalidateFlags_Image|DkInvalidateFlags_L2Cache|DkInvalidateFlags_Descriptors);
            cmd.bindRenderTargets(&color_view,nullptr);
            cmd.bindImageDescriptorSet(streak_descriptors.getGpuAddr(),1);
            cmd.bindSamplerDescriptorSet(streak_descriptors.getGpuAddr()+kDrawDescriptorBytes,1);
            cmd.bindTextures(DkStage_Fragment,0,dkMakeTextureHandle(0,0));
            fullscreen(streak_copy_fragment,false);
        }
        mark_ab(4);
        if(fxaa_enabled) {
            // Complete scene writes before sampling. Every slot has distinct
            // input/output images; its fence protects descriptors and contents.
            cmd.barrier(DkBarrier_Full,DkInvalidateFlags_Image|DkInvalidateFlags_L2Cache|DkInvalidateFlags_Descriptors);
            dk::ImageView aa_view{scene_target.upscale?fxaa_colors[slot]:target};
            cmd.bindRenderTargets(&aa_view,nullptr);
            cmd.setViewports(0,{{0,0,float(scene_target.width),float(scene_target.height),0,1}});
            cmd.setScissors(0,{{0,0,scene_target.width,scene_target.height}});
            cmd.bindShaders(DkStageFlag_GraphicsMask,{&fxaa_vertex,&fxaa_fragment});
            cmd.bindImageDescriptorSet(fxaa_descriptors.getGpuAddr(),2);
            cmd.bindSamplerDescriptorSet(fxaa_descriptors.getGpuAddr()+2*kDrawDescriptorBytes,1);
            cmd.bindTextures(DkStage_Fragment,0,dkMakeTextureHandle(static_cast<std::uint32_t>(slot),0));
            dk::RasterizerState raster;raster.setCullMode(DkFace_None);
            dk::ColorState color;color.setBlendEnable(0,false).setAlphaCompareOp(DkCompareOp_Always);
            dk::ColorWriteState write;write.setMask(0,DkColorMask_RGBA);
            dk::DepthStencilState depth;depth.setDepthTestEnable(false).setDepthWriteEnable(false);
            cmd.bindRasterizerState(raster);cmd.bindColorState(color);cmd.bindColorWriteState(write);
            cmd.bindDepthStencilState(depth);cmd.bindVtxAttribState(std::array<DkVtxAttribState,0>{});
            cmd.draw(DkPrimitive_Triangles,3,1,0,0);
        }
        mark_ab(5);
        if (scene_target.upscale) {
            // Complete rendering before the 2D engine reads this slot. The
            // second full barrier includes its work in the timestamp span
            // and makes the native-size image ready for presentation/HUD.
            cmd.barrier(DkBarrier_Full, DkInvalidateFlags_L2Cache);
            dk::ImageView present_view{target};
            dk::ImageView filtered_view{fxaa_enabled?fxaa_colors[slot]:scene_colors[slot]};
            cmd.blitImage(filtered_view, {0, 0, 0, scene_target.width, scene_target.height, 1},
                          present_view, {0, 0, 0, commands->width, commands->height, 1},
                          DkBlitFlag_FilterLinear);
            cmd.barrier(DkBarrier_Full, DkInvalidateFlags_L2Cache);
            // The HUD is native 1280x720 artwork. Draw it on the presentation
            // image after the linear scene upscale, with no scene depth.
            cmd.bindRenderTargets(&present_view, nullptr);
            cmd.setViewports(0, {{0, 0, source_width(), source_height(), 0, 1}});
            cmd.setScissors(0, {{0, 0, commands->width, commands->height}});
        }
        mark_ab(6);
        mark_frontend(8);
        if(!msaa_checkpoint("single-sample upscale"))return false;
        if (hud) emit_overlay(slot,hud->vertices.data(),hud->quads);
        if (frontend) emit_overlay(slot,frontend->vertices.data(),frontend->additive.size(),frontend->additive.data());
        if(!checkpoint("UI overlay"))return false;
        if(!msaa_checkpoint("single-sample HUD"))return false;
        msaa_first_frame=false;
        mark_ab(7);
        mark_frontend(9);
        if(diagnose)frontend_first_frame=false;
        cmd.barrier(DkBarrier_Fragments, 0);
        if (profile_enabled)
            cmd.reportCounter(DkCounter_Timestamp, query_memory.getGpuAddr()+slot*160+16);
        return true;
    }

    bool present(std::string& error, DekoPresentTiming* timing,
                 const DekoSceneFrame* scene_frame, const RaceHudFrame* hud_frame,const FrontendUiFrame* frontend=nullptr) {
        if (timing) *timing = {};
        if (!swapchain || !healthy(error)) {
            if (error.empty()) error = "deko3d renderer is not open";
            return false;
        }
        auto stage = timing ? armGetSystemTick() : 0;
        if(!trace("present.acquire.before",error))return false;
        const int slot = queue.acquireImage(swapchain);
        if(!trace("present.acquire.after",error))return false;
        if (!healthy(error) || slot < 0 || slot >= 2) {
            if (error.empty()) error = "deko3d swapchain image acquisition failed";
            return false;
        }
        if (timing) timing->acquire_ms = elapsed_ms(stage);
        stage = timing ? armGetSystemTick() : 0;
        if (frame_in_flight[slot]) {
            if(!trace("present.fence.before",error))return false;
            const auto result = frame_fences[slot].wait();
            if(!trace("present.fence.after",error))return false;
            if (result != DkResult_Success) {
                error = "deko3d frame fence wait failed: " + std::to_string(static_cast<unsigned>(result));
                return false;
            }
            frame_in_flight[slot] = false;
            if (profile_enabled) consume_query(slot);
            consume_frontend_query(slot);
        }
        if (timing) timing->fence_wait_ms = elapsed_ms(stage);
        if (profile_enabled) {
            auto* report = static_cast<unsigned char*>(query_memory.getCpuAddr()) + slot*160;
            std::memset(report, 0, 160);
            query_frames[slot] = scene_frame ? *scene_frame : DekoSceneFrame{};
            query_generations[slot] = profile_generation;
        }
        if(frontend && frontend_profile_passes)
            std::memset(static_cast<unsigned char*>(frontend_query_memory.getCpuAddr())+slot*208,0,208);
        stage = timing ? armGetSystemTick() : 0;
        if(!write_uniforms(*commands, slot,error))return false;
        if (timing) timing->uniform_write_ms = elapsed_ms(stage);
        stage = timing ? armGetSystemTick() : 0;
        if(!trace("present.record.before",error))return false;
        if (!record_frame(colors[slot],slot,hud_frame,error,frontend)) return false;
        if(!trace("present.record.after",error))return false;
        if (timing) timing->record_ms = elapsed_ms(stage);
        stage = timing ? armGetSystemTick() : 0;
        const auto list = cmd.finishList();
        if (!healthy(error)) return false;
        if (timing) timing->finish_ms = elapsed_ms(stage);
        stage = timing ? armGetSystemTick() : 0;
        if(!trace("present.submit.before",error))return false;
        queue.submitCommands(list);
        if(!trace("present.submit.after",error))return false;
        queue.signalFence(frame_fences[slot]);
        if(!trace("present.queue.before",error))return false;
        queue.presentImage(swapchain, slot);
        if(!trace("present.queue.after",error))return false;
        if(!trace("present.flush.before",error))return false;
        queue.flush();
        if(!trace("present.flush.after",error))return false;
        frame_in_flight[slot] = true;
        query_pending[slot] = profile_enabled;
        frontend_query_pending[slot]=frontend && frontend_profile_passes;
        ++fence_number;
        if (timing) timing->submit_ms = elapsed_ms(stage);
        startup_journal=nullptr;
        return healthy(error);
    }

    bool capture(std::vector<unsigned char>& rgba, double& render_ms,
                 double& readback_ms, std::string& error,
                 const RaceHudFrame* hud_frame) {
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
        if (!record_frame(colors[0],0,hud_frame,error)) return false;
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
                        bool sky_with_opaque, std::uint32_t scene_height,
                        const RaceHudAtlas* hud_atlas) {
    error.clear();
    try { return impl_->open(commands, error, presentation, draw_capacity, profile_frame,
                             opaque_first, sky_with_opaque, scene_height, hud_atlas); }
    catch (const std::bad_alloc&) { error = "not enough memory for deko3d renderer"; return false; }
}
bool DekoRenderer::open_frontend(const RecordedCommands& commands,const FrontendUiAtlas& atlas,std::string& error,std::uint32_t scene_height,bool profile_passes,StartupJournal* startup_journal) {
    impl_->startup_journal=startup_journal;
    if(!open(commands,error,true,0,false,true,false,scene_height))return false;
    impl_->frontend_profile_passes=profile_passes;
    if(profile_passes) {
        auto& begin=impl_->frontend_car_begin;auto& end=impl_->frontend_car_end;
        begin=0;
        while(begin<commands.draw_count() && std::strcmp(commands.draw_view(begin).definition.program->name,"frontend_car_textured"))++begin;
        end=begin;
        while(end<commands.draw_count() && !std::strcmp(commands.draw_view(end).definition.program->name,"frontend_car_textured"))++end;
        if(begin==end) {error="frontend profile car span absent";return false;}
        for(std::size_t i=end;i<commands.draw_count();++i)
            if(!std::strcmp(commands.draw_view(i).definition.program->name,"frontend_car_textured")) {error="frontend profile car span not contiguous";return false;}
        for(std::size_t first=0;first<commands.draw_count();) {
            const auto& draw=commands.draw_view(first).definition;
            std::size_t last=first,triangles=0,blended=0,depth_write=0;
            while(last<commands.draw_count() && commands.draw_view(last).definition.program==draw.program) {
                const auto& current=commands.draw_view(last++).definition;
                triangles+=current.index_count/3;blended+=current.pipeline.blend;depth_write+=current.pipeline.depth_write;
            }
            std::printf("MENU GPU main span first=%zu end=%zu program=%s draws=%zu triangles=%zu blended=%zu depth_write=%zu\n",
                first,last,draw.program->name,last-first,triangles,blended,depth_write);first=last;
        }
        std::fflush(stdout);
    }
    if(profile_passes && !impl_->block(416,DkMemBlockFlags_CpuUncached|DkMemBlockFlags_GpuUncached,
        "frontend pass timestamp reports",impl_->frontend_query_memory,error)) {
        const auto failure=error;std::string cleanup;close(cleanup);error=failure;return false;
    }
    if(impl_->init_overlay(atlas.rgba,atlas.width,atlas.height,kFrontendUiMaxQuads,error))return true;
    const auto failure=std::string("frontend atlas upload: ")+error;
    std::string cleanup;close(cleanup);error=failure;return false;
}
bool DekoRenderer::present_frontend(const FrontendUiFrame& frame,std::string& error,DekoPresentTiming* timing) {
    if(frame.additive.size()>kFrontendUiMaxQuads || frame.vertices.size()!=frame.additive.size()*4 ||
       std::any_of(frame.additive.begin(),frame.additive.end(),[](auto mode){return mode>1;})) {
        error="frontend overlay frame invalid";return false;
    }
    if(impl_->present(error,timing,nullptr,nullptr,&frame))return true;
    error="frontend composed frame: "+error;return false;
}
bool DekoRenderer::reconfigure_target(const RecordedCommands& commands, std::string& error,
                                      std::size_t draw_capacity, bool profile_frame,
                                      bool sky_with_opaque, std::uint32_t scene_height,
                                      const RaceHudAtlas* hud_atlas) {
    if (!close(error)) return false;
    impl_ = std::make_unique<Impl>();
    return open(commands,error,true,draw_capacity,profile_frame,true,
                sky_with_opaque,scene_height,hud_atlas);
}
bool DekoRenderer::update(const RecordedCommands& commands, std::string& error,
                          DekoUpdateTiming* timing) {
    error.clear();
    try { return impl_->update(commands, error, timing); }
    catch (const std::bad_alloc&) { error = "not enough memory for draw update validation"; return false; }
}
std::uint64_t DekoRenderer::gpu_bytes() const noexcept { return impl_->gpu_bytes; }
DekoMemoryUsage DekoRenderer::memory_usage() const noexcept {
    return {deko_allocated_block_size(impl_->uniforms) +
            deko_allocated_block_size(impl_->reflection_uniforms),
        deko_allocated_block_size(impl_->static_uniforms),
        deko_allocated_block_size(impl_->static_command_memory[0]) +
            deko_allocated_block_size(impl_->static_command_memory[1])};
}
DekoGpfifoUsage DekoRenderer::gpfifo_usage() const noexcept { return impl_->gpfifo_usage; }
bool DekoRenderer::capture(std::vector<unsigned char>& rgba, double& render_ms,
                           double& readback_ms, std::string& error,
                           const RaceHudFrame* hud_frame) {
    error.clear();
    try { return impl_->capture(rgba, render_ms, readback_ms, error, hud_frame); }
    catch (const std::bad_alloc&) { rgba.clear(); error = "not enough memory for GPU readback"; return false; }
}
bool DekoRenderer::present(std::string& error, DekoPresentTiming* timing,
                           const DekoSceneFrame* scene_frame,
                           const RaceHudFrame* hud_frame) {
    error.clear();
    return impl_->present(error, timing, scene_frame, hud_frame);
}
void DekoRenderer::prepare_ab_trace(bool passes) { impl_->ab_passes=passes;impl_->ab_rows.reserve(6000); }
void DekoRenderer::dump_ab_trace() const {
    std::puts(impl_->ab_passes?"AB_GPU_COLUMNS frame,tick,valid,total_ms,cube_ms,road_ms,main_ms,streak_resolve_ms,fxaa_ms,upscale_ms,hud_ms":"AB_GPU_COLUMNS frame,tick,valid,total_ms");
    for(const auto& r:impl_->ab_rows) {
        std::printf("AB_GPU %llu,%llu,%u,%.6f",(unsigned long long)r.frame.ordinal,
            (unsigned long long)r.frame.tick,unsigned(r.valid),r.total);
        if(impl_->ab_passes)for(auto t:r.stages)std::printf(",%.6f",t);
        std::putchar('\n');
    }
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
