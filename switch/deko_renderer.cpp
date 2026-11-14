#include "nfsu/deko_overlay_upload.h"
#include "nfsu/frontend_glow_fast.h"
#include "nfsu/race_effect_fast.h"
#include "deko_renderer.h"
#include "frontend_startup_trace.h"
#include "nfsu/frontend.h"
#include "nfsu/memory_budget.h"
#include "nfsu/draw_execution.h"
#include "nfsu/deko_gpfifo_budget.h"
#include "nfsu/deko_memory_usage.h"
#include "nfsu/deko_command_memory.h"
#include "nfsu/deko_draw_order.h"
#include "nfsu/deko_draw_bindings.h"
#include "nfsu/deko_texture_layout.h"
#if defined(NFSU_SPEED_BLUR_PROBE)
#include "nfsu/speed_blur_reference.h"
#endif

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
    dk::MemBlock streamed_memory;
    std::vector<std::pair<std::size_t,std::uint64_t>> streamed_resources;
    std::vector<std::uint64_t> streamed_offsets;
    std::uint64_t streamed_stride=0;
    std::size_t streamed_slot=0;
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
    dk::MemBlock reflected_color_memory,reflected_depth_memory,cube_color_memory,cube_depth_memory;
    dk::Image reflection_color, reflection_depth;
    bool reflection_ready = false;
    dk::MemBlock frontend_cube_uniforms;
    dk::MemBlock headlight_uniforms, movable_uniforms;
    dk::MemBlock animated_descriptors;
    std::size_t animation_descriptor_stride=0,animation_aliases=0;
    DkGpuAddr image_descriptors(std::size_t slot) {
        return animation_aliases?animated_descriptors.getGpuAddr()+slot*animation_descriptor_stride:descriptors.getGpuAddr();
    }

    std::vector<std::size_t> movable_instances,movable_slots;
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
    bool query_recording = false;
    bool opaque_first = false;
    bool sky_with_opaque = false;
    std::array<dk::Image, 2> colors;
    std::array<dk::Image, 2> scene_colors;
    std::array<dk::Image, 2> depths;
    DekoSceneTarget scene_target;
    bool fxaa_enabled=false;
    bool split_screen=false;
    bool speed_blur_enabled=false;
    SpeedBlurFrame speed_blur_frame;
#if defined(NFSU_SPEED_BLUR_PROBE)
    // One full active NOS frame. Compare actual GPU passes with the RGBA8
    // reference checked against the original ps_1_1 effect. Timing is perturbed.
    dk::MemBlock speed_blur_probe;
    bool speed_blur_probe_recorded=false, speed_blur_probe_enabled=false;
    int speed_blur_probe_slot=-1;
    SpeedBlurFrame speed_blur_probe_frame;
    std::array<std::size_t,5> blur_probe_offsets{},blur_probe_bytes{};
    std::size_t blur_probe_total=0;
    void sample_blur(unsigned stage,const dk::ImageView& view,unsigned width,unsigned height) {
        if(!speed_blur_probe_enabled||speed_blur_probe_recorded||speed_blur_frame.alpha<240)return;
        cmd.barrier(DkBarrier_Full,DkInvalidateFlags_Image|DkInvalidateFlags_L2Cache);
        cmd.copyImageToBuffer(view,{0,0,0,width,height,1},
            {speed_blur_probe.getGpuAddr()+blur_probe_offsets[stage],width*4,0});
        cmd.barrier(DkBarrier_Full,DkInvalidateFlags_L2Cache);
    }
    void consume_blur_probe(std::size_t slot) {
        if(speed_blur_probe_slot!=int(slot))return;
        const auto* data=static_cast<const unsigned char*>(speed_blur_probe.getCpuAddr());
        const auto& f=speed_blur_probe_frame;
        const auto* input=data+blur_probe_offsets[0];
        std::vector<unsigned char> source(input,input+blur_probe_bytes[0]);
        auto expected=diagnostic::speed_blur_reference(source,scene_target.width,scene_target.height,f);
        std::printf("BLUR_PROBE full_rgba=1 source=%ux%u alpha=%u offsets=%.9g,%.9g,%.9g,%.9g timing_perturbed=1\n",
            scene_target.width,scene_target.height,unsigned(f.alpha),f.offsets[0],f.offsets[1],f.offsets[2],f.offsets[3]);
        for(unsigned index=0;index<4;++index) {
            std::array<std::uint32_t,8> words{};
            std::memcpy(words.data(),static_cast<const unsigned char*>(speed_blur_uniforms.getCpuAddr())+(slot*4+index)*256,sizeof words);
            std::printf("BLUR_CONSTANTS stage=%u words=%08x,%08x,%08x,%08x,%08x,%08x,%08x,%08x\n",
                index+1,words[0],words[1],words[2],words[3],words[4],words[5],words[6],words[7]);
        }
        bool guards=true;
        for(unsigned stage=0;stage<5;++stage) {
            const auto start=blur_probe_offsets[stage]+blur_probe_bytes[stage];
            const auto end=stage==4?blur_probe_total:blur_probe_offsets[stage+1];
            for(auto i=start;i<end;++i)guards=guards&&data[i]==0xCD;
        }
        for(unsigned stage=1;stage<5;++stage) {
            const auto* actual=data+blur_probe_offsets[stage];const auto& reference=expected.image[stage];
            unsigned maximum[4]={},bad[4]={};unsigned long long sum[4]={};
            std::size_t first=reference.size();unsigned long long hash=14695981039346656037ULL;
            for(std::size_t i=0;i<reference.size();++i) {
                const unsigned c=unsigned(i%4),difference=unsigned(std::abs(int(actual[i])-int(reference[i])));
                maximum[c]=std::max(maximum[c],difference);sum[c]+=difference;
                if(difference>3){++bad[c];if(first==reference.size())first=i;}
                hash=(hash^actual[i])*1099511628211ULL;
            }
            const double count=double(reference.size()/4);
            std::printf("BLUR_PASS stage=%u pixels=%zu rgba_max=%u,%u,%u,%u rgba_mae=%.6f,%.6f,%.6f,%.6f over3=%u,%u,%u,%u hash=%016llx\n",
                stage,reference.size()/4,maximum[0],maximum[1],maximum[2],maximum[3],
                sum[0]/count,sum[1]/count,sum[2]/count,sum[3]/count,bad[0],bad[1],bad[2],bad[3],hash);
            if(stage==1 || stage==2) {
                // Preserve the actual radial input/output for offline analysis.
                // This one-shot probe is deliberately excluded from FPS data.
                constexpr char digits[]="0123456789abcdef";
                char line[1025];
                for(std::size_t offset=0;offset<reference.size();offset+=512) {
                    const auto bytes=std::min<std::size_t>(512,reference.size()-offset);
                    for(std::size_t i=0;i<bytes;++i) {
                        const auto value=actual[offset+i];line[i*2]=digits[value>>4];line[i*2+1]=digits[value&15];
                    }
                    line[bytes*2]=0;
                    std::printf("BLUR_DATA stage=%u offset=%zu hex=%s\n",stage,offset,line);
                }
                std::printf("BLUR_DATA_END stage=%u bytes=%zu hash=%016llx\n",stage,reference.size(),hash);
            }
            if(first<reference.size())std::printf("BLUR_DIFF stage=%u x=%zu y=%zu channel=%zu actual=%u expected=%u\n",
                stage,(first/4)%expected.width[stage],(first/4)/expected.width[stage],first%4,
                unsigned(actual[first]),unsigned(reference[first]));
        }
        std::printf("BLUR_PROBE_END guards=%s one_frame=1 reference=d3d9_ps_1_1_checked\n",guards?"PASS":"FAIL");
        std::fflush(stdout);speed_blur_probe_slot=-1;
    }
#endif
    dk::Shader speed_blur_vertex,speed_blur_fragment,speed_blur_reduce,speed_blur_copy;
    std::array<dk::Image,2> speed_blur_colors;
    dk::MemBlock speed_blur_descriptors,speed_blur_uniforms;
    std::array<dk::MemBlock,2> speed_blur_memory;
    bool streaks_enabled=false, streaks_initialized=false;
    dk::Shader streak_vertex,streak_fade_fragment,streak_copy_fragment;
    dk::Image streak_color;
    dk::MemBlock streak_descriptors,streak_memory;
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
    dk::Shader shadow_vertex, shadow_fragment, headlight_vertex;
    dk::Shader vertex, textured, textured_opaque, race_world, fallback, frontend_vertex, frontend_fragment;
    bool race_world_available = false;
    dk::Shader frontend_car_vertex, frontend_car_fragment;
#if defined(NFSU_FRONTEND_PREFLIGHT)
    dk::Shader probe_vertex,probe_constant,probe_textured;
#endif
    dk::Shader car_vertex, car_fragment, sky_vertex, sky_fragment;
    const RecordedCommands* commands = nullptr;
    const PreparedCommandSource* package = nullptr;
    std::vector<const RecordedDraw*> static_definitions;
    std::vector<const RecordedDraw*> dynamic_definitions;
    std::vector<std::size_t> dynamic_submission_order;
    std::vector<std::size_t> static_group_slots;
    DekoCubeRanges cube_ranges;
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
    std::uint64_t gpu_peak_bytes = 0;
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
            std::printf("GPU_BUDGET resource=%s requested=%llu resident=%llu limit=%llu\n",name,
                (unsigned long long)bytes,(unsigned long long)gpu_bytes,
                (unsigned long long)(kGpuBudgetPlaceholder+msaa_budget_extra+mip_budget_extra));
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
        gpu_peak_bytes = std::max(gpu_peak_bytes,gpu_bytes);
        if(!trace("allocation.after",error,name,bytes))return false;
        return true;
    }

    // Caller must drain submissions and discard recorded commands before
    // retiring an allocation. Used for upload scratch and overlay replacement.
    bool release_completed_block(dk::MemBlock& scratch,const char* name,std::string& error) {
        if(!scratch) {error="upload scratch was already released";return false;}
        for(auto it=blocks.begin();it!=blocks.end();++it) {
            if(static_cast<DkMemBlock>(*it)!=static_cast<DkMemBlock>(scratch))continue;
            const auto bytes=scratch.getSize();
            if(bytes>gpu_bytes) {error="upload scratch accounting underflow";return false;}
            scratch=nullptr;
            blocks.erase(it);
            gpu_bytes-=bytes;
            std::printf("GPU_UPLOAD_RETIRED resource=%s freed=%llu resident=%llu\n",name,
                (unsigned long long)bytes,(unsigned long long)gpu_bytes);
            return true;
        }
        error="upload scratch owner missing";return false;
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
        const auto upload=deko_overlay_stripe(width,height,0);
        const std::uint32_t vertex_slot_bytes=sizeof(TrackVertex)*capacity*4;
        if (pixels.size() != texel_bytes) {
            error = "HUD atlas has the wrong size"; return false;
        }
        dk::ImageLayout layout;
        dk::ImageLayoutMaker{device}.setFormat(DkImageFormat_RGBA8_Unorm)
            .setDimensions(width,height).initialize(layout);
        if (!block(layout.getSize(),kImageFlags,"HUD image",hud_image_memory,error) ||
            !block(upload.bytes,kDataFlags,"HUD staging",hud_staging,error) ||
            !block(vertex_slot_bytes*2,kDataFlags,"HUD vertices",hud_vertices,error) ||
            !block(capacity*6*sizeof(std::uint16_t),kDataFlags,
                   "HUD indices",hud_indices,error) ||
            !block(3*256,kDataFlags,"HUD uniforms",hud_uniforms,error) ||
            !block(3*kDrawDescriptorBytes,kDataFlags,"HUD descriptors",hud_descriptors,error))
            return false;
        hud_image.initialize(layout,hud_image_memory,0);

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
        frame.options[2]=1024; // Unit-W HUD with separate projective route UVs.
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
        sampler.setFilter(DkFilter_Linear,DkFilter_Linear);
        sampler_descriptor.initialize(sampler);
        std::memcpy(static_cast<unsigned char*>(hud_descriptors.getCpuAddr())+
                    2*kDrawDescriptorBytes,&sampler_descriptor,sizeof sampler_descriptor);
        // Each submit waits before the same staging memory is rewritten.
        // The destination remains one complete atlas, including odd tail rows.
        for(std::uint32_t y=0;y<height;) {
            const auto stripe=deko_overlay_stripe(width,height,y);
            std::memcpy(hud_staging.getCpuAddr(),pixels.data()+std::size_t(y)*stripe.row_pitch,stripe.bytes);
            begin();
            cmd.copyBufferToImage({hud_staging.getGpuAddr(),stripe.row_pitch,stripe.bytes},
                                  view,{0,y,0,width,stripe.rows,1});
            cmd.barrier(DkBarrier_Full,DkInvalidateFlags_Image|DkInvalidateFlags_Descriptors);
            if(!submit(error))return false;
            cmd.clear();y+=stripe.rows;
        }
        if(!release_completed_block(hud_staging,"HUD staging",error))return false;
        overlay_capacity=capacity;hud_enabled=true;
        return true;
    }

    bool init_hud(const RaceHudAtlas& atlas,std::string& error) {
        return init_overlay(atlas.rgba,kRaceHudAtlasWidth,atlas.height,kRaceHudMaxQuads,error);
    }
    float source_width() const { return static_cast<float>(commands->width); }
    float source_height() const { return static_cast<float>(commands->height); }

    bool open(const RecordedCommands& source, std::string& error, bool presentation,
              std::size_t capacity, bool profile_frame, bool reorder_opaque, bool early_sky,
              std::uint32_t scene_height, const RaceHudAtlas* hud_atlas,bool reuse_frontend=false) {
        if (commands) { error = "deko3d renderer open is one-shot"; return false; }
        if (!plan_deko_scene_target(source.width, source.height, presentation, scene_height, scene_target)) {
            error = "invalid scene resolution: scaling requires 1280x720 direct presentation"; return false;
        }
#if defined(NFSU_RACE_FXAA)
        fxaa_enabled=presentation && source.race_environment && !split_screen;
#endif
#if defined(NFSU_RACE_MSAA2) || defined(NFSU_RACE_MSAA8)
        msaa_enabled=presentation && source.race_environment && !split_screen;
#endif
        if(fxaa_enabled && msaa_enabled) { error="select one race AA mode";return false; }
        DrawExecutionPlan plan;
        if (!plan_draw_execution(source, plan, error)) return false;
        draw_capacity = capacity ? capacity : source.draw_count();
        if (draw_capacity < source.draw_count() || draw_capacity > (source.package_identity() ? kDekoPreparedDrawCapacity : 4096)) {
            error = "invalid reserved draw capacity"; return false;
        }
        package = source.package_identity();
        opaque_first = reorder_opaque;
        sky_with_opaque = early_sky;
        if (package) {
            package_width = source.width; package_height = source.height;
            std::memcpy(package_clear.data(), source.clear_color, 4);
            static_instances = source.static_track_instance_count();
            movable_slots.assign(static_instances,kNoHandle);
            for(std::size_t i=0;i<static_instances;++i)if(source.movable_track_instance(i)) {
                movable_slots[i]=movable_instances.size();movable_instances.push_back(i);
            }
            if (source.static_track_draw_count() > source.draw_count()) {
                error = "prepared track count exceeds selected commands"; return false;
            }
            dynamic_capacity = source.draw_count() - source.static_track_draw_count();
            if (!deko_static_package_fits(static_instances,source.static_track_definition_count(),
                                          dynamic_capacity,draw_capacity)) {
                error = "prepared static package exceeds capacity: instances="+std::to_string(static_instances)+
                    " definitions="+std::to_string(source.static_track_definition_count())+
                    " dynamic="+std::to_string(dynamic_capacity)+" reserved="+std::to_string(draw_capacity); return false;
            }
            const auto count = source.static_track_definition_count();
            static_definitions.reserve(count);
            for (std::size_t i = 0; i < count; ++i) {
                const auto& draw = source.static_track_definition(i);
                // Sparse source group IDs are bounded to a 512 KiB slot table.
                if (draw.group >= kDekoStaticGroupCapacity) { error = "prepared group ID exceeds capacity"; return false; }
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
            dynamic_submission_order.reserve(dynamic_capacity);
            for (std::size_t i = 0; i < dynamic_capacity; ++i)
                dynamic_submission_order.push_back(i);
            const auto& resources = source.resource_table().resources;
            // Draw all opaque actors before either layer of vehicle glass.
            // Keep the original slots for texture aliases and uniform offsets.
            std::stable_partition(dynamic_submission_order.begin(),dynamic_submission_order.end(),
                [&](std::size_t index) {
                    const auto& draw = *dynamic_definitions[index];
                    if (!draw.pipeline.blend || draw.texture_resource >= resources.size()) return true;
                    const auto hash = resources[draw.texture_resource].texture_hash;
                    return std::find(kIntactCrashWindowHashes.begin(),kIntactCrashWindowHashes.end(),hash)
                        == kIntactCrashWindowHashes.end();
                });
        } else dynamic_capacity = draw_capacity;
        // Prepared static draws are child lists, not repeated root commands.
        // Reserve root storage for the dynamic tail and child calls only.
        const auto root_draw_capacity = package ? dynamic_capacity +
            deko_track_list_calls(static_cast<std::uint32_t>(draw_capacity)) : draw_capacity;
        reflection_limit=(source.frame.options[2]&8U)?source.reflection_draw_count():reflection_capacity;
        plan.uniform_bytes = static_cast<std::uint32_t>((1 + 2 * dynamic_capacity) * 256);
        const auto root_commands=root_draw_capacity+
            (source.race_environment?kRaceCubeDrawLimit:6*source.frontend_cube_draws.size())+
            ((source.frame.options[2]&10U)?reflection_limit:0);
        plan.command_bytes = static_cast<std::uint32_t>(deko_command_memory(
            root_commands,source.resource_table().resources.size()));
        uniform_stride = plan.uniform_bytes;
        profile_enabled = profile_frame && presentation;
        const auto uniform_bytes = std::uint64_t{uniform_stride} * (presentation ? 2 : 1);
        uploaded = source.resource_table();
        commands = &source;
        if(!reuse_frontend) {
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
        }
        const ImageLayoutQuery layout_query = [this](const UploadResource& r, std::uint64_t& bytes,
                                                    std::uint64_t& alignment, std::string& e) {
            const auto layout = texture_layout(r);
            bytes = layout.getSize(); alignment = layout.getAlignment();
            std::uint64_t bound = 0, bound_alignment = 0;
            if (!deko_texture_image_bound(r, bound, bound_alignment, e)) return false;
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
        const auto resource_count=source.resource_table().resources.size();
        if(resource_count>kDrawResourceCapacity){error="upload resource namespace exceeds backing limit";return false;}
        const auto ledger_capacity=static_cast<std::uint32_t>(std::max<std::size_t>(8192,resource_count));
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
            !ledger.create({&buffer_arena, &image_arena, &staging_arena}, layout_query, ledger_capacity, error) ||
            !ledger.upload_table(source.resource_table(), 1, handles, error)) return false;
        command_bytes[0] = plan.command_bytes;
        command_bytes[1] = static_cast<std::uint32_t>(deko_command_memory(root_commands));
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
        streamed_resources.clear();streamed_offsets.assign(handles.size(),UINT64_MAX);streamed_stride=0;
        for(std::size_t i=0;i<uploaded.resources.size();++i)if(uploaded.resources[i].streamed_vertices) {
            const auto& r=uploaded.resources[i];
            if(r.kind!=UploadResourceKind::vertex_buffer || r.bytes>2*1024*1024 ||
                streamed_stride+r.bytes>2*1024*1024){error="streamed vertices exceed bounded buffer";return false;}
            streamed_offsets[i]=streamed_stride;streamed_resources.emplace_back(i,streamed_stride);
            streamed_stride=(streamed_stride+r.bytes+255)&~UINT64_C(255);
        }
        if(streamed_stride&&!block(streamed_stride*2,kDataFlags,"streamed vertices",streamed_memory,error))return false;
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
        // Original worldheadlight sampler: linear min/mag/mip and clamp.
        // One-level reflection targets still use level0.
        sampler.setFilter(DkFilter_Linear, DkFilter_Linear, DkMipFilter_Linear)
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
        if(ledger.stats().staged) {error="texture upload fence left staging live";return false;}
        cmd.clear();
        if(!release_completed_block(staging,"texture staging",error))return false;
        // Upload copies are complete and cmd was cleared above. Their command
        // reserve is never needed by presentation; keep only the frame span.
        if(command_bytes[0]!=command_bytes[1]) {
            if(!release_completed_block(command_memory[0],"upload command memory",error))return false;
            command_bytes[0]=command_bytes[1];
            if(!block(command_bytes[0],kDataFlags,"frame command memory",command_memory[0],error))return false;
        }
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
            if(!block(color_layout.getSize(),kImageFlags,"frontend cube faces",cube_color_memory,error) ||
               !block(z_layout.getSize(),kImageFlags,"frontend cube depth",cube_depth_memory,error))return false;
            frontend_cube_color.initialize(color_layout,cube_color_memory,0);
            frontend_cube_depth.initialize(z_layout,cube_depth_memory,0);
            dk::ImageDescriptor descriptor;descriptor.initialize(dk::ImageView{frontend_cube_color});
            std::memcpy(static_cast<unsigned char*>(descriptors.getCpuAddr())+plan.sampler_offset,&descriptor,sizeof descriptor);
            frontend_cube_ready=true;
        }
#if defined(NFSU_FRONTEND_PREFLIGHT)
        if(!reuse_frontend && frontend_cube_ready && (!shader("frontend_probe.vert",DkStage_Vertex,probe_vertex,error) ||
            !shader("frontend_probe.frag",DkStage_Fragment,probe_constant,error) ||
            !shader("frontend_probe_textured.frag",DkStage_Fragment,probe_textured,error)))return false;
#endif
        if (!block(2*256,kDataFlags,"frontend light uniforms",frontend_light_uniforms,error))return false;
        if (!block(2*256,kDataFlags,"headlight uniforms",headlight_uniforms,error))return false;
        if(!movable_instances.empty() && !block(2*movable_instances.size()*256,kDataFlags,"moving prop uniforms",movable_uniforms,error))return false;
        if(!write_uniforms(source, 0,error))return false;
        if(!reuse_frontend) {
        if (!shader("frontend_car.vert",DkStage_Vertex,frontend_car_vertex,error) ||
            !shader("frontend_car.frag",DkStage_Fragment,frontend_car_fragment,error))return false;
        if (!shader("car_shaded.vert", DkStage_Vertex, car_vertex, error) ||
            !shader("car_shaded.frag", DkStage_Fragment, car_fragment, error) ||
            !shader("sky.vert", DkStage_Vertex, sky_vertex, error) ||
            !shader("sky.frag", DkStage_Fragment, sky_fragment, error) ||
            !shader("placed.vert", DkStage_Vertex, vertex, error) ||
            !shader("placed_headlight.vert", DkStage_Vertex, headlight_vertex, error) ||
            !shader("car_shadow.vert", DkStage_Vertex, shadow_vertex, error) ||
            !shader("car_shadow.frag", DkStage_Fragment, shadow_fragment, error) ||
            !shader("textured.frag", DkStage_Fragment, textured, error) ||
            !shader("textured_opaque.frag", DkStage_Fragment, textured_opaque, error) ||
            !shader("frontend_ui.vert", DkStage_Vertex, frontend_vertex, error) ||
            !shader("frontend_ui.frag", DkStage_Fragment, frontend_fragment, error) ||
            !shader("fallback.frag", DkStage_Fragment, fallback, error)) return false;
#if defined(NFSU_RACE_WORLD_SHADER)
        if (package && !static_definitions.empty()) {
            if (!shader("race_world.frag", DkStage_Fragment, race_world, error)) return false;
            race_world_available = true;
        }
#endif
        }
        if(!check_draw_command_encoding(source,error))return false;
        if (package && !prepare_static_commands(source, presentation, error)) return false;
        // The comparison uses the existing single-sample scene/depth path.
        // MSAA remains independently selectable; its depth cannot be attached
        // to a single-sample light history target.
        streaks_enabled=presentation && source.race_environment && !msaa_enabled && !split_screen;
        if(streaks_enabled) {
            if(!shader("fxaa.vert",DkStage_Vertex,streak_vertex,error) ||
               !shader("streak_fade.frag",DkStage_Fragment,streak_fade_fragment,error) ||
               !shader("streak_copy.frag",DkStage_Fragment,streak_copy_fragment,error) ||
               !block(2*kDrawDescriptorBytes,kDataFlags,"light streak descriptors",streak_descriptors,error))return false;
            dk::ImageLayout layout;
            dk::ImageLayoutMaker{device}.setFlags(DkImageFlags_UsageRender)
                .setFormat(DkImageFormat_RGBA8_Unorm).setDimensions(scene_target.width,scene_target.height).initialize(layout);
            if(!block(layout.getSize(),kImageFlags,"light streak history",streak_memory,error))return false;
            streak_color.initialize(layout,streak_memory,0);
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
        if(!reuse_frontend) {
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
        // Two half-resolution images are shared by the single ordered queue.
        // Constants have four distinct pass slots per fenced presentation slot.
        speed_blur_enabled=presentation && source.race_environment && !msaa_enabled && !split_screen && source.borrowed() &&
            (scene_target.upscale || fxaa_enabled);
        if(speed_blur_enabled) {
            if(!shader("speed_blur.vert",DkStage_Vertex,speed_blur_vertex,error) ||
               !shader("speed_blur.frag",DkStage_Fragment,speed_blur_fragment,error) ||
               !shader("speed_blur_reduce.frag",DkStage_Fragment,speed_blur_reduce,error) ||
               !shader("speed_blur_copy.frag",DkStage_Fragment,speed_blur_copy,error) ||
               !block(5*kDrawDescriptorBytes,kDataFlags,"speed blur descriptors",speed_blur_descriptors,error) ||
               !block(2*4*256,kDataFlags,"speed blur constants",speed_blur_uniforms,error))return false;
#if defined(NFSU_SPEED_BLUR_PROBE)
            blur_probe_total=0;
            for(unsigned stage=0;stage<5;++stage) {
                const auto width=stage==0||stage==4?scene_target.width:scene_target.width/2;
                const auto height=stage==0||stage==4?scene_target.height:scene_target.height/2;
                blur_probe_offsets[stage]=blur_probe_total;
                blur_probe_bytes[stage]=std::size_t(width)*height*4;
                blur_probe_total+=((blur_probe_bytes[stage]+255)&~std::size_t(255))+256;
            }
            if(!block(blur_probe_total,DkMemBlockFlags_CpuUncached|DkMemBlockFlags_GpuUncached,
                      "speed blur full-frame probe",speed_blur_probe,error))return false;
            std::memset(speed_blur_probe.getCpuAddr(),0xCD,blur_probe_total);
#endif
            auto* data=static_cast<unsigned char*>(speed_blur_descriptors.getCpuAddr());
            dk::ImageLayout layout;
            dk::ImageLayoutMaker{device}.setFlags(DkImageFlags_UsageRender)
                .setFormat(DkImageFormat_RGBA8_Unorm)
                .setDimensions(scene_target.width/2,scene_target.height/2).initialize(layout);
            for(unsigned i=0;i<2;++i) {
                if(!block(layout.getSize(),kImageFlags,"speed blur half image",speed_blur_memory[i],error))return false;
                speed_blur_colors[i].initialize(layout,speed_blur_memory[i],0);
                dk::ImageDescriptor full,half;
                full.initialize(dk::ImageView{scene_colors[i]});
                half.initialize(dk::ImageView{speed_blur_colors[i]});
                std::memcpy(data+i*kDrawDescriptorBytes,&full,sizeof full);
                std::memcpy(data+(i+2)*kDrawDescriptorBytes,&half,sizeof half);
            }
            dk::Sampler sampler;
            sampler.setFilter(DkFilter_Linear,DkFilter_Linear,DkMipFilter_None)
                .setWrapMode(DkWrapMode_ClampToEdge,DkWrapMode_ClampToEdge,DkWrapMode_ClampToEdge);
            dk::SamplerDescriptor descriptor;descriptor.initialize(sampler);
            std::memcpy(data+4*kDrawDescriptorBytes,&descriptor,sizeof descriptor);
            std::printf("RACE SPEED_BLUR original=40CD40 half=%ux%u passes=4 HUD=sharp\n",
                scene_target.width/2,scene_target.height/2);
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
        }
        if (hud_atlas && !init_hud(*hud_atlas,error)) return false;
        animation_aliases=source.texture_alias_count();
        if(animation_aliases) {
            animation_descriptor_stride=sampler_offset;
            if(!block(2*animation_descriptor_stride,kDataFlags,"animated texture descriptors",animated_descriptors,error))return false;
            for(unsigned slot=0;slot<2;++slot)std::memcpy(static_cast<unsigned char*>(animated_descriptors.getCpuAddr())+slot*animation_descriptor_stride,
                descriptors.getCpuAddr(),animation_descriptor_stride);
            std::printf("WORLD ANIMATION texture_aliases=%zu immutable_frames=1 per_slot_descriptors=1\n",animation_aliases);
        }

        std::printf("deko3d: %zu draws, %zu uploads, %llu bytes of GPU blocks (placeholder budget)\n",
            source.draw_count(), handles.size(), static_cast<unsigned long long>(gpu_bytes));
        std::printf("GPU_RESIDENCY live=%llu peak=%llu limit=%llu\n",
            (unsigned long long)gpu_bytes,(unsigned long long)gpu_peak_bytes,
            (unsigned long long)(kGpuBudgetPlaceholder+msaa_budget_extra+mip_budget_extra));
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
                next.draw_count() - next.static_track_draw_count() > dynamic_capacity) {
                error = "prepared command package changed or exceeded capacity"; return false;
            }
            if(!validate_prepared_dynamic_prefix(dynamic_definitions,next,error))return false;
            const auto track_count = next.static_track_draw_count();
            for (std::size_t i = 0; i < track_count; ++i) {
                const auto view = next.draw_view(i);
                const auto group = view.definition.group;
                if (!view.track_instance || view.instance >= static_instances ||
                    group >= static_group_slots.size() ||
                    static_group_slots[group] == kNoHandle ||
                    &view.definition != static_definitions[static_group_slots[group]] ||
                    &view.placement != &next.static_track_instance(view.instance) ||
                    &view.material != &view.definition.material) {
                    error = "prepared track selection left its static package"; return false;
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
        streamed_slot=slot;
        // The caller has waited for this frame slot's fence before writing.
        for(const auto& item:streamed_resources) {
            const auto& r=source.resource_table().resources.at(item.first);
            std::memcpy(static_cast<unsigned char*>(streamed_memory.getCpuAddr())+slot*streamed_stride+item.second,
                r.data,static_cast<std::size_t>(r.bytes));
        }
        auto* data = static_cast<unsigned char*>(uniforms.getCpuAddr()) +
            uniform_allocation.offset + slot * uniform_stride;
        std::memcpy(data, &source.frame, sizeof source.frame);
        for(std::size_t i=0;i<movable_instances.size();++i)
            std::memcpy(static_cast<unsigned char*>(movable_uniforms.getCpuAddr())+(slot*movable_instances.size()+i)*256,
                &source.static_track_instance(movable_instances[i]),sizeof(DrawInstanceUniform));
        std::memcpy(static_cast<unsigned char*>(headlight_uniforms.getCpuAddr())+slot*256,&source.headlight_frame,sizeof(DrawHeadlightUniform));
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
            if(!view.submit)continue;
            const auto index = i - first_dynamic;
            // Both direct exhaust and history lights need current constants.
            // Submission selection belongs to the draw passes below.
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
                   const RecordedDraw* previous = nullptr,dk::Shader* fragment_override=nullptr) {
        const auto bindings = deko_draw_bindings(draw,previous);
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
        if (bindings.shaders)
            out.bindShaders(DkStageFlag_GraphicsMask,
                            {shadow ? &shadow_vertex : frontend ? &frontend_car_vertex : shaded ? &car_vertex : sky ? &sky_vertex : has_texture ? &headlight_vertex : &vertex, fragment});
        if (instance_addr)
            out.bindUniformBuffer(DkStage_Vertex, 1, instance_addr, sizeof(DrawInstanceUniform));
        out.bindUniformBuffer(DkStage_Fragment, 2, material_addr, sizeof(DrawMaterialUniform));
        if (shaded || shadow) out.bindUniformBuffer(DkStage_Vertex, 2, material_addr, sizeof(DrawMaterialUniform));
        // world.efx uses a linear sampler for the sky. CPU diagnostics
        // retain the legacy sampler specified by the manifest.
        if (bindings.texture && has_texture) out.bindTextures(DkStage_Fragment, 0,
            dkMakeTextureHandle(static_cast<std::uint32_t>(draw.texture_resource),
                sky || draw.program->textures[0].filter == sm::Filter::linear ? 1 : 0));
        if (bindings.pipeline) {
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
        }
        if (bindings.layout) {
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
        if (bindings.vertex) out.bindVtxBuffer(0,
            streamed_offsets[draw.vertex_resource]==UINT64_MAX?buffers.getGpuAddr()+v.offset:
                streamed_memory.getGpuAddr()+streamed_slot*streamed_stride+streamed_offsets[draw.vertex_resource],
            static_cast<std::uint32_t>(v.size));
        if (bindings.index) out.bindIdxBuffer(DkIdxFormat_Uint16, buffers.getGpuAddr()+records[draw.index_resource].placement.offset);
        out.drawIndexed(DkPrimitive_Triangles, draw.index_count, 1, draw.first_index,
                        static_cast<std::int32_t>(draw.first_vertex), 0);
    }

    bool check_draw_command_encoding(const RecordedCommands& source,std::string& error) {
        // SDK capture writes ordinary GPU words into CPU scratch only. All
        // upload submissions have completed; no captured list is submitted.
        // Force every binding on each draw to cover pass/list boundaries.
        std::array<std::uint32_t,4096> scratch{};
        std::uint64_t maximum=0;std::size_t checked=0;
        const auto check=[&](const RecordedDraw& draw) {
            cmd.beginCaptureCmds(scratch.data(),scratch.size());
            emit_draw(cmd,draw,uniforms.getGpuAddr(),uniforms.getGpuAddr());
            const auto bytes=std::uint64_t(cmd.endCaptureCmds())*4;
            maximum=std::max(maximum,bytes);++checked;
            return healthy(error) && bytes<=kDekoDrawCommandBytes;
        };
        for(std::size_t i=0;i<source.draw_count();++i)
            if(!check(source.draw_view(i).definition)) {
                if(error.empty())error="draw command encoding exceeds reservation";return false;
            }
        for(std::size_t i=0;i<source.reflection_draw_count();++i)
            if(!check(source.reflection_draw(i))) {
                if(error.empty())error="reflection command encoding exceeds reservation";return false;
            }
        for(const auto& draw:source.frontend_cube_draws)if(!check(draw)) {
            if(error.empty())error="cube command encoding exceeds reservation";return false;
        }
        std::printf("GPU_COMMAND_ENCODING draws=%zu max_draw_bytes=%llu reserved_draw_bytes=%llu root0=%u root1=%u static_slot=%llu\n",
            checked,(unsigned long long)maximum,(unsigned long long)kDekoDrawCommandBytes,
            command_bytes[0],command_bytes[1],(unsigned long long)(package?deko_command_memory(draw_capacity):0));
        return true;
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
        const auto bytes = deko_command_memory(draw_capacity);
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
        const bool world_available = race_world_available && commands->frame.options[3] == 0;
        const auto world_draw = [](const RecordedDraw& draw) {
            const auto marker = draw.material.fallback[3];
            const auto lamp = draw.material.alpha[3];
            return (marker == 6.0F || marker == 7.0F) &&
                lamp != 5.0F && lamp != 6.0F && lamp != 7.0F && lamp != 8.0F;
        };
        std::size_t emitted = 0;
        bool ok = true;
        auto visit =
            [&](std::size_t i) {
                if (!ok) return;
                if (emitted % kDekoTrackChunkDraws == 0) previous = nullptr;
                const auto view = commands->draw_view(i);
                const auto index = static_group_slots.at(view.definition.group);
                const auto* definition = static_definitions.at(index);
                const bool use_world = world_available && world_draw(*definition);
                if (world_available && previous && use_world != world_draw(*previous)) previous = nullptr;
                emit_draw(recorder, *definition,
                          movable_slots[view.instance]==kNoHandle ? static_uniforms.getGpuAddr()+view.instance*256 :
                              movable_uniforms.getGpuAddr()+(slot*movable_instances.size()+movable_slots[view.instance])*256,
                          static_uniforms.getGpuAddr() + (static_instances+index)*256,
                          previous, use_world ? &race_world : nullptr);
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
                      const unsigned char* additive=nullptr,std::size_t linear_from=kRaceHudMaxQuads,std::size_t quad_offset=0) {
        if (!hud_enabled || !quads || quad_offset>overlay_capacity || quads>overlay_capacity-quad_offset) return;
        const std::uint32_t slot_bytes=sizeof(TrackVertex)*overlay_capacity*4;
        auto* vertices=reinterpret_cast<TrackVertex*>(
            static_cast<unsigned char*>(hud_vertices.getCpuAddr())+slot*slot_bytes+quad_offset*4*sizeof(TrackVertex));
        for (std::size_t i=0;i<quads*4;++i) {
            vertices[i]=input[i];
            vertices[i].px=input[i].px-source_width()*0.5F;
            vertices[i].py=input[i].py*(split_screen?2.F:1.F)-source_height()*0.5F;
            vertices[i].pz=additive?input[i].pz:(input[i].pz>0?input[i].pz:-1.0F);
        }
        cmd.bindShaders(DkStageFlag_GraphicsMask,{additive?&frontend_vertex:&vertex,additive?&frontend_fragment:&textured_opaque});
        const auto base=hud_uniforms.getGpuAddr();
        cmd.bindUniformBuffer(DkStage_Vertex,0,base,sizeof(DrawFrameUniform));
        cmd.bindUniformBuffer(DkStage_Fragment,0,base,sizeof(DrawFrameUniform));
        cmd.bindUniformBuffer(DkStage_Vertex,1,base+256,sizeof(DrawInstanceUniform));
        cmd.bindUniformBuffer(DkStage_Fragment,2,base+512,sizeof(DrawMaterialUniform));
        cmd.bindImageDescriptorSet(hud_descriptors.getGpuAddr(),1);
        cmd.bindSamplerDescriptorSet(hud_descriptors.getGpuAddr()+kDrawDescriptorBytes,2);
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
        cmd.bindVtxBuffer(0,hud_vertices.getGpuAddr()+slot*slot_bytes+quad_offset*4*sizeof(TrackVertex),
                          quads*4*sizeof(TrackVertex));
        cmd.bindIdxBuffer(DkIdxFormat_Uint16,hud_indices.getGpuAddr());
        const auto linear_at=[&](std::size_t index) {
            return additive?input[4*index].pz<=kFrontendFontVertexZ:index>=linear_from;
        };
        for(std::size_t first=0;first<quads;) {
            const bool add=additive && additive[first];
            const bool linear=linear_at(first);
            std::size_t end=first+1;
            while(end<quads && bool(additive && additive[end])==add &&
                  linear_at(end)==linear)++end;
            cmd.bindTextures(DkStage_Fragment,0,dkMakeTextureHandle(0,linear?1:0));
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
        if(commands->texture_alias_count()!=animation_aliases){error="texture animation package changed";return false;}
        if(animation_aliases) {
            // Called after this slot's existing fence. Never modify descriptors
            // referenced by the other in-flight slot, or upload texture pixels.
            auto* out=static_cast<unsigned char*>(animated_descriptors.getCpuAddr())+slot*animation_descriptor_stride;
            const auto* original=static_cast<const unsigned char*>(descriptors.getCpuAddr());
            const auto& resources=commands->resource_table().resources;
            for(std::size_t i=0;i<animation_aliases;++i){
                const auto alias=commands->texture_alias(i);
                if(alias.resource>=handles.size()||alias.frame>=handles.size()||
                   resources[alias.resource].kind!=UploadResourceKind::texture||resources[alias.frame].kind!=UploadResourceKind::texture){error="texture animation left image resources";return false;}
                std::memcpy(out+alias.resource*kDrawDescriptorBytes,original+alias.frame*kDrawDescriptorBytes,kDrawDescriptorBytes);
            }
        }
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
        if (query_recording)
            cmd.reportCounter(DkCounter_TimestampPipelineTop, query_memory.getGpuAddr()+slot*160);
        const auto mark_ab=[&](unsigned point) {
            if(!query_recording || !query_frames[slot].ordinal || !ab_passes)return;
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
        cmd.bindImageDescriptorSet(image_descriptors(slot), static_cast<std::uint32_t>(handles.size()+(reflection_ready?1:0)+(frontend_cube_ready?1:0)));
        cmd.bindSamplerDescriptorSet(descriptors.getGpuAddr()+sampler_offset, (reflection_ready || frontend_cube_ready) ? 3 : 2);
        cmd.barrier(DkBarrier_None, DkInvalidateFlags_Shader | DkInvalidateFlags_Descriptors);
        cmd.bindUniformBuffer(DkStage_Vertex,4,headlight_uniforms.getGpuAddr()+slot*256,sizeof(DrawHeadlightUniform));
        cmd.bindUniformBuffer(DkStage_Fragment,4,headlight_uniforms.getGpuAddr()+slot*256,sizeof(DrawHeadlightUniform));
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
            deko_cube_ranges(commands->frontend_cube_draws.size(),
                [&](std::size_t i){return commands->frontend_cube_draws[i].frontend_sprite_view;},cube_ranges);
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
                for(const auto& range:cube_ranges[face]) for(std::size_t i=range.first;i<range.second;++i) {
                    const auto& draw=commands->frontend_cube_draws[i];
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
                        emit_draw(cmd,draw,instance,material,nullptr,&probe_constant);
                        if(!checkpoint("preflight actual mesh constant"))return false;
                        clear_probe();
                        emit_draw(cmd,draw,instance,material,nullptr,&probe_textured);
                        if(!checkpoint("preflight actual mesh texture0"))return false;
                        clear_probe();previous=nullptr;
                    }
#endif
                    emit_draw(cmd,draw,cube+(6+2*i)*256,cube+(7+2*i)*256,
                        previous);previous=&draw;
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
                          previous);
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
        const unsigned view_height=split_screen?scene_target.height/2:scene_target.height;
        const unsigned view_top=split_screen?unsigned(slot)*view_height:0;
        cmd.setViewports(0,{{0,float(view_top),float(scene_target.width),float(view_height),0,1}});
        cmd.setScissors(0,{{0,view_top,scene_target.width,view_height}});
        if(!split_screen||slot==0) {
            const auto* clear=commands->clear_color;
            cmd.setScissors(0,{{0,0,scene_target.width,scene_target.height}});
            cmd.clearColor(0,DkColorMask_RGBA,clear[0]/255.0F,clear[1]/255.0F,clear[2]/255.0F,clear[3]/255.0F);
        }
        cmd.clearDepthStencil(true,1,0,0);
        cmd.setScissors(0,{{0,view_top,scene_target.width,view_height}});
        if(!msaa_checkpoint("MSAA target clear"))return false;
        if(frontend&&frontend->background_quads){
            emit_overlay(slot,frontend->vertices.data(),frontend->background_quads,frontend->additive.data());
            cmd.bindImageDescriptorSet(image_descriptors(slot),static_cast<std::uint32_t>(handles.size()+(reflection_ready?1:0)+(frontend_cube_ready?1:0)));
            cmd.bindSamplerDescriptorSet(descriptors.getGpuAddr()+sampler_offset,(reflection_ready||frontend_cube_ready)?3:2);
            cmd.barrier(DkBarrier_None,DkInvalidateFlags_Shader|DkInvalidateFlags_Descriptors);
        }
        cmd.bindUniformBuffer(DkStage_Vertex, 0, base, sizeof(DrawFrameUniform));
        cmd.bindUniformBuffer(DkStage_Fragment, 0, base, sizeof(DrawFrameUniform));
        cmd.bindUniformBuffer(DkStage_Vertex,3,frontend_light_uniforms.getGpuAddr()+slot*256,sizeof(FrontendLighting));
        for (const auto list : static_lists[slot]) cmd.callList(list);
        // 40997E scenery/4099BA blur precede 4099C5 actors and
        // 4099D3 car nodes in the original. Keep cars, shadows and glows sharp.
        if(speed_blur_enabled && speed_blur_frame.active()) {
#if defined(NFSU_SPEED_BLUR_PROBE)
            sample_blur(0,color_view,scene_target.width,scene_target.height);
#endif
            const auto pass=[&](unsigned index,unsigned input,const dk::ImageView& output,
                                unsigned width,unsigned height,float mode,float multiplier,bool blend_on) {
                cmd.barrier(DkBarrier_Full,DkInvalidateFlags_Image|DkInvalidateFlags_L2Cache|DkInvalidateFlags_Descriptors);
                const std::size_t offset=(slot*4+index)*256;
                const auto& radial=speed_blur_frame.offsets;
                const float uniform[]={1.0F/float(width),1.0F/float(height),
                    float(speed_blur_frame.alpha)/255.0F,mode,
                    radial[0]*multiplier,radial[1]*multiplier,radial[2]*multiplier,radial[3]*multiplier};
                std::memcpy(static_cast<unsigned char*>(speed_blur_uniforms.getCpuAddr())+offset,uniform,sizeof uniform);
                cmd.bindUniformBuffer(DkStage_Fragment,0,speed_blur_uniforms.getGpuAddr()+offset,sizeof uniform);
                cmd.bindRenderTargets(&output,nullptr);
                cmd.setViewports(0,{{0,0,float(width),float(height),0,1}});
                cmd.setScissors(0,{{0,0,width,height}});
                const auto* fragment=index==0 ? &speed_blur_reduce :
                    (index==3 ? &speed_blur_copy : &speed_blur_fragment);
                cmd.bindShaders(DkStageFlag_GraphicsMask,{&speed_blur_vertex,fragment});
                cmd.bindImageDescriptorSet(speed_blur_descriptors.getGpuAddr(),4);
                cmd.bindSamplerDescriptorSet(speed_blur_descriptors.getGpuAddr()+4*kDrawDescriptorBytes,1);
                cmd.bindTextures(DkStage_Fragment,0,dkMakeTextureHandle(input,0));
                dk::RasterizerState raster;raster.setCullMode(DkFace_None);
                dk::ColorState color;color.setBlendEnable(0,blend_on).setAlphaCompareOp(DkCompareOp_Always);
                dk::ColorWriteState write;write.setMask(0,DkColorMask_RGBA);
                dk::DepthStencilState depth;depth.setDepthTestEnable(false).setDepthWriteEnable(false);
                // Original 40CD40 sets D3DRS_BLENDFACTOR; 40D6E0 uses
                // BLENDFACTOR/INVBLENDFACTOR for RGBA. Keep image alpha intact.
                const float factor=float(speed_blur_frame.alpha)/255.F;
                cmd.setBlendConst(factor,factor,factor,factor);
                dk::BlendState blend;blend.setFactors(DkBlendFactor_ConstColor,DkBlendFactor_InvConstColor,
                                                     DkBlendFactor_ConstColor,DkBlendFactor_InvConstColor);
                cmd.bindRasterizerState(raster);cmd.bindColorState(color);cmd.bindColorWriteState(write);
                cmd.bindDepthStencilState(depth);cmd.bindBlendStates(0,{blend});
                cmd.bindVtxAttribState(std::array<DkVtxAttribState,0>{});
                cmd.draw(DkPrimitive_Triangles,3,1,0,0);
#if defined(NFSU_SPEED_BLUR_PROBE)
                sample_blur(index+1,output,width,height);
#endif
            };
            dk::ImageView a{speed_blur_colors[0]},b{speed_blur_colors[1]};
            const unsigned w=scene_target.width/2,h=scene_target.height/2;
            pass(0,static_cast<unsigned>(slot),a,w,h,0,0,false);
            pass(1,2,b,w,h,1,1,false);
            pass(2,3,a,w,h,1,4,false);
            dk::ImageView composite{scene_colors[slot]};
            pass(3,2,composite,scene_target.width,scene_target.height,2,0,true);
#if defined(NFSU_SPEED_BLUR_PROBE)
            if(speed_blur_probe_enabled&&!speed_blur_probe_recorded&&speed_blur_frame.alpha>=240) {
                speed_blur_probe_recorded=true;speed_blur_probe_slot=int(slot);
                speed_blur_probe_frame=speed_blur_frame;
            }
#endif
            cmd.barrier(DkBarrier_Full,DkInvalidateFlags_Image|DkInvalidateFlags_L2Cache|DkInvalidateFlags_Descriptors);
            cmd.bindRenderTargets(&color_view,&depth_view);
            cmd.bindImageDescriptorSet(image_descriptors(slot),static_cast<std::uint32_t>(handles.size()+(reflection_ready?1:0)+(frontend_cube_ready?1:0)));
            cmd.bindSamplerDescriptorSet(descriptors.getGpuAddr()+sampler_offset,(reflection_ready||frontend_cube_ready)?3:2);
            cmd.bindUniformBuffer(DkStage_Vertex,0,base,sizeof(DrawFrameUniform));
            cmd.bindUniformBuffer(DkStage_Fragment,0,base,sizeof(DrawFrameUniform));
            if(reflection_ready)cmd.bindTextures(DkStage_Fragment,1,
                dkMakeTextureHandle(static_cast<std::uint32_t>(handles.size()+(frontend_cube_ready?1:0)),2));
        }
        const auto first_dynamic = package ? commands->static_track_draw_count() : 0;
        const RecordedDraw* previous_dynamic = nullptr;
        bool environment_is_car = false;
        const auto dynamic_count = commands->draw_count() - first_dynamic;
        const auto submission_count = package ? dynamic_submission_order.size() : dynamic_count;
        for (std::size_t order = 0; order < submission_count; ++order) {
            const auto submitted_index = package ? dynamic_submission_order[order] : order;
            if (submitted_index >= dynamic_count) continue;
            const auto i = first_dynamic + submitted_index;
            const auto index = i - first_dynamic;
            if(frontend && frontend_profile_passes) {
                if(i==frontend_car_begin)mark_frontend(10);
                if(i==frontend_car_end)mark_frontend(11);
            }
            const auto view = commands->draw_view(i);
            if(!view.submit)continue;
            if(commands->frontend_car_only && std::strcmp(view.definition.program->name,"frontend_car_textured"))continue;
            // Zero source alpha contributes no RGB, alpha or depth under
            // this validated additive state. Keep original uniform indices
            // and the last emitted draw for fixed-state reuse.
            // History lights are emitted once in the later streak pass.
            // Direct exhaust (marker 6) stays in this main scene pass.
            if(streaks_enabled && view.material.alpha[3]==5.0F)continue;
            if(frontend && frontend_glow_no_output(view,commands->frame))continue;
            if(!frontend && !commands->frontend_light_enabled && race_effect_no_output(view))continue;
            if(diagnose && previous_dynamic &&
                previous_dynamic->program!=view.definition.program) {
                if(!checkpoint(previous_dynamic->program->name))return false;
                previous_dynamic=nullptr;
                environment_is_car=false;
            }
            if(frontend_cube_ready && reflection_ready) {
                const bool car=std::strcmp(view.definition.program->name,"frontend_car_textured")==0 ||
                    std::strcmp(view.definition.program->name,"car_shaded_textured")==0 ||
                    std::strcmp(view.definition.program->name,"car_shaded_linear_textured")==0;
                if(!previous_dynamic || car != environment_is_car) {
                    cmd.bindTextures(DkStage_Fragment,1,dkMakeTextureHandle(
                        static_cast<std::uint32_t>(handles.size()+(car?0:1)),2));
                    environment_is_car=car;
                }
            }
            emit_draw(cmd, view.definition, base+(1+2*index)*256,
                      base+(2+2*index)*256,
                      previous_dynamic);
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
            cmd.bindImageDescriptorSet(image_descriptors(slot),static_cast<std::uint32_t>(handles.size()+(reflection_ready?1:0)+(frontend_cube_ready?1:0)));
            cmd.bindSamplerDescriptorSet(descriptors.getGpuAddr()+sampler_offset,(reflection_ready||frontend_cube_ready)?3:2);
            const RecordedDraw* previous_light=nullptr;
            for(std::size_t i=first_dynamic;i<commands->draw_count();++i) {
                const auto view=commands->draw_view(i);
                if(view.material.alpha[3]!=5.0F || view.material.alpha[0]<=0)continue;
                const auto index=i-first_dynamic;
                emit_draw(cmd,view.definition,base+(1+2*index)*256,base+(2+2*index)*256,
                    previous_light);
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
        if (hud) emit_overlay(slot,hud->vertices.data(),hud->quads,nullptr,hud->lap_first_quad);
        if(frontend){const auto first=frontend->background_quads;
            emit_overlay(slot,frontend->vertices.data()+first*4,frontend->additive.size()-first,
                         frontend->additive.data()+first,kRaceHudMaxQuads,first);
        }
        if(!checkpoint("UI overlay"))return false;
        if(!msaa_checkpoint("single-sample HUD"))return false;
        msaa_first_frame=false;
        mark_ab(7);
        mark_frontend(9);
        if(diagnose)frontend_first_frame=false;
        cmd.barrier(DkBarrier_Fragments, 0);
        if (query_recording)
            cmd.reportCounter(DkCounter_Timestamp, query_memory.getGpuAddr()+slot*160+16);
        return true;
    }

    bool present_split(const DekoRenderer::SplitViewSource& next,std::string& error) {
        if(!split_screen||!swapchain||!healthy(error)) {
            if(error.empty())error="split-screen renderer is not open";
            return false;
        }
        // A single fence covers both views. Each uses its own existing slot
        // for uniforms, descriptors, HUD vertices and GPU commands.
        for(unsigned i=0;i<frame_in_flight.size();++i)if(frame_in_flight[i]){
            if(frame_fences[i].wait()!=DkResult_Success){error="split-screen frame fence failed";return false;}
            frame_in_flight[i]=false;
        }
        const int image=queue.acquireImage(swapchain);
        if(!healthy(error)||image<0||image>=2) {
            if(error.empty())error="split-screen swapchain acquisition failed";
            return false;
        }
        for(unsigned player=0;player<2;++player) {
            const RecordedCommands* view=nullptr;
            const RaceHudFrame* hud=nullptr;
            if(!next(player,view,hud,error))return false;
            if(!view||!hud){error="split-screen frame missing a view or HUD";return false;}
            if(!update(*view,error,nullptr)||!write_uniforms(*view,player,error)||
               !record_frame(colors[image],player,hud,error))return false;
            const auto list=cmd.finishList();
            if(!healthy(error))return false;
            // finishList's control records belong to cmd until its next clear.
            queue.submitCommands(list);
        }
        queue.signalFence(frame_fences[image]);
        queue.presentImage(swapchain,image);
        queue.flush();
        frame_in_flight[image]=true;
        ++fence_number;
        return healthy(error);
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
#if defined(NFSU_SPEED_BLUR_PROBE)
            consume_blur_probe(slot);
#endif
            if (profile_enabled) consume_query(slot);
            consume_frontend_query(slot);
        }
        if (timing) timing->fence_wait_ms = elapsed_ms(stage);
        query_recording=profile_enabled && !frontend && (!scene_frame || scene_frame->sample);
        if (query_recording) {
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
        query_pending[slot] = query_recording;
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
        query_recording=profile_enabled;
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

    bool replace_frontend(const RecordedCommands& source,std::size_t capacity,std::string& error) {
        if(!commands || commands->race_environment || source.race_environment || package ||
           source.package_identity() || !swapchain || !hud_enabled ||
           source.width!=commands->width || source.height!=commands->height ||
           source.texture_alias_count()) {
            error="frontend replacement needs compatible resident menu targets";return false;
        }
        DrawExecutionPlan plan;
        if(!plan_draw_execution(source,plan,error))return false;
        queue.waitIdle();cmd.clear();
        // A drained queue cannot reference the command allocations below.
        cmd=nullptr;
        if(!ledger.release_all(handles,fence_number,error))return false;
        ledger.retire(fence_number<1?1:fence_number);
        if(uniform_live) {
            if(uniform_arena.release(uniform_allocation.handle)!=ArenaRefusal::none) {
                error="frontend replacement uniform retirement failed";return false;
            }
            uniform_live=false;
        }
        const auto stats=ledger.stats();
        if(stats.resident||stats.releasing||stats.staged||stats.lost_placements) {
            error="frontend replacement has outstanding uploads";return false;
        }
        auto retire=[&](dk::MemBlock& memory) {
            return !memory||release_completed_block(memory,"frontend scene",error);
        };
        for(auto* memory:{&buffers,&images,&staging,&uniforms,&descriptors,&streamed_memory,
             &command_memory[0],&command_memory[1],&reflection_uniforms,
             &reflected_color_memory,&reflected_depth_memory,&frontend_cube_uniforms,
             &cube_color_memory,&cube_depth_memory,&frontend_light_uniforms,&headlight_uniforms})
            if(!retire(*memory))return false;
        ledger=UploadLedger{};handles.clear();records.clear();textures.clear();
        uploaded={};first_texture=kNoHandle;commands=nullptr;
        frame_in_flight.fill(false);frontend_query_pending.fill(false);
        reflection_ready=false;frontend_cube_ready=false;cube_initialized=false;frontend_first_frame=true;
        frontend_profile_samples=0;
        if(!open(source,error,true,capacity,false,true,false,scene_target.height,nullptr,true))return false;
        frontend_car_begin=0;
        while(frontend_car_begin<source.draw_count() &&
              std::strcmp(source.draw_view(frontend_car_begin).definition.program->name,"frontend_car_textured"))++frontend_car_begin;
        frontend_car_end=frontend_car_begin;
        while(frontend_car_end<source.draw_count() &&
              !std::strcmp(source.draw_view(frontend_car_end).definition.program->name,"frontend_car_textured"))++frontend_car_end;
        std::puts("MENU SCENE replaced uploads; device, swapchain, display and HUD retained");
        return true;
    }

    bool shutdown(std::string& error) {
        if (shut_down) return true;
        shut_down = true;
        if (queue) queue.waitIdle();
#if defined(NFSU_SPEED_BLUR_PROBE)
        if(speed_blur_probe_slot>=0)consume_blur_probe(std::size_t(speed_blur_probe_slot));
#endif
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
bool DekoRenderer::open_split(const RecordedCommands& commands,const RaceHudAtlas& atlas,
                              std::size_t capacity,std::string& error) {
    impl_->split_screen=true;
    return open(commands,error,true,capacity,false,true,false,0,&atlas);
}
bool DekoRenderer::present_split(const SplitViewSource& next,std::string& error) {
    return impl_->present_split(next,error);
}
bool DekoRenderer::open_frontend(const RecordedCommands& commands,const FrontendUiAtlas& atlas,std::string& error,std::uint32_t scene_height,bool profile_passes,StartupJournal* startup_journal,std::size_t draw_capacity) {
    impl_->startup_journal=startup_journal;
    if(!open(commands,error,true,draw_capacity,false,true,false,scene_height))return false;
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
bool DekoRenderer::replace_frontend(const RecordedCommands& commands,std::size_t capacity,std::string& error) {
    error.clear();
    try {return impl_->replace_frontend(commands,capacity,error);}
    catch(const std::bad_alloc&) {error="not enough memory for frontend replacement";return false;}
}
bool DekoRenderer::set_frontend_overlay(const FrontendUiAtlas& atlas,std::string& error,bool final) {
    if(!impl_->queue || !impl_->commands) {error="overlay transition needs an open renderer";return false;}
    impl_->queue.waitIdle();
    impl_->cmd.clear();
    impl_->hud_enabled=false;
    // Only the HUD changes. World geometry, textures and the final frame stay
    // resident; never allocate a second renderer with a second copy of them.
    auto retire=[&](dk::MemBlock& block,const char* name) {
        return !block || impl_->release_completed_block(block,name,error);
    };
    // Results have no live NOS or light-history effect. Retire these before
    // uploading the larger combined frontend atlas; keep the race scene and
    // presentation targets so results still compose over the finish frame.
    if(final) {
    impl_->speed_blur_enabled=false;impl_->streaks_enabled=false;
    if(!retire(impl_->speed_blur_memory[0],"finished speed blur image0") ||
       !retire(impl_->speed_blur_memory[1],"finished speed blur image1") ||
       !retire(impl_->speed_blur_descriptors,"finished speed blur descriptors") ||
       !retire(impl_->speed_blur_uniforms,"finished speed blur constants") ||
       !retire(impl_->streak_memory,"finished light streak history") ||
       !retire(impl_->streak_descriptors,"finished light streak descriptors"))return false;
    }
    if(!retire(impl_->hud_image_memory,"race HUD image") ||
       !retire(impl_->hud_vertices,"race HUD vertices") ||
       !retire(impl_->hud_indices,"race HUD indices") ||
       !retire(impl_->hud_uniforms,"race HUD uniforms") ||
       !retire(impl_->hud_descriptors,"race HUD descriptors"))return false;
    return impl_->init_overlay(atlas.rgba,atlas.width,atlas.height,kFrontendUiMaxQuads,error);
}
bool DekoRenderer::restore_race_overlay(const RaceHudAtlas& atlas,std::string& error) {
    // Replace only the temporary UI allocation. Effect history/world resources
    // were retained by the non-final pause transition.
    impl_->queue.waitIdle();impl_->cmd.clear();impl_->hud_enabled=false;
    for(auto* block:{&impl_->hud_image_memory,&impl_->hud_vertices,&impl_->hud_indices,&impl_->hud_uniforms,&impl_->hud_descriptors})
        if(*block&&!impl_->release_completed_block(*block,"paused overlay",error))return false;
    if(atlas.rgba.empty())return true; // Explicit no-HUD launches have no atlas.
    return impl_->init_hud(atlas,error);
}
bool DekoRenderer::present_frontend(const FrontendUiFrame& frame,std::string& error,DekoPresentTiming* timing) {
    if(frame.background_quads>frame.additive.size() || frame.additive.size()>kFrontendUiMaxQuads || frame.vertices.size()!=frame.additive.size()*4 ||
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
        deko_allocated_block_size(impl_->static_uniforms)+deko_allocated_block_size(impl_->movable_uniforms),
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
void DekoRenderer::set_speed_blur(const SpeedBlurFrame& frame) {
    if(impl_)impl_->speed_blur_frame=frame;
}

void DekoRenderer::enable_speed_blur_probe(bool enabled) noexcept {
#ifdef NFSU_SPEED_BLUR_PROBE
    if(impl_)impl_->speed_blur_probe_enabled=enabled;
#else
    (void)enabled;
#endif
}
bool DekoRenderer::speed_blur_probe_complete() const noexcept {
#ifdef NFSU_SPEED_BLUR_PROBE
    return impl_&&impl_->speed_blur_probe_recorded&&impl_->speed_blur_probe_slot<0;
#else
    return false;
#endif
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
