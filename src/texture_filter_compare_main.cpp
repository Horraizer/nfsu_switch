// #262: the same fixed replay pose on the CPU and deko3d, without HUD/effects.
#include "nfsu/race_gpu_scene.h"
#include "nfsu/posix_file_system.h"
#include "nfsu/track_render.h"
#include "nfsu/frame_compare.h"
#include "nfsu/arg_path.h"
#include "nfsu/texture.h"
#include "nfsu/texture_decode.h"
#include "nfsu/texture_filter_reference.h"
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <fstream>
#include <memory>
#include <stdexcept>
#include <string>
#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif
#if defined(__SWITCH__)
#include "deko_renderer.h"
#include "nfsu/screen_text.h"
#include <switch.h>
#endif

namespace {
using namespace nfsu;
void require(bool ok, const std::string& error) { if (!ok) throw std::runtime_error(error); }
std::filesystem::path comparison_path(const char* text) {
#if defined(__SWITCH__)
    return std::filesystem::path(text);
#else
    return argument_path(text);
#endif
}


#if !defined(__SWITCH__)
std::filesystem::path physical_existing_path(const std::filesystem::path& path) {
#if defined(_WIN32)
    // This Windows GCC canonical implementation does not resolve junctions.
    // Ask the opened existing object, without requesting write access.
    const HANDLE handle = CreateFileW(path.c_str(),0,
        FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,
        FILE_FLAG_BACKUP_SEMANTICS,nullptr);
    require(handle != INVALID_HANDLE_VALUE,"cannot resolve output ancestry");
    struct Guard { HANDLE handle; ~Guard(){CloseHandle(handle);} } guard{handle};
    const DWORD length = GetFinalPathNameByHandleW(handle,nullptr,0,FILE_NAME_NORMALIZED);
    require(length != 0,"cannot resolve output ancestry");
    std::wstring name(length,L'\0');
    const DWORD written = GetFinalPathNameByHandleW(handle,name.data(),length,FILE_NAME_NORMALIZED);
    require(written != 0 && written < length,"cannot resolve output ancestry");
    name.resize(written);
    // Keep the filesystem library's normal DOS/UNC root parsing for parents.
    if(name.compare(0,8,L"\\\\?\\UNC\\") == 0) name=L"\\\\"+name.substr(8);
    else if(name.compare(0,4,L"\\\\?\\") == 0) name.erase(0,4);
    return std::filesystem::path(name);
#else
    return std::filesystem::canonical(path);
#endif
}

void protect_retail_output(const std::filesystem::path& root,
                           const std::filesystem::path& output) {
    auto ancestor=std::filesystem::absolute(output);
    // A fresh leaf (and possibly its parents) need not exist. Resolve the
    // deepest existing ancestor before walking parents: lexical parents of
    // a sibling junction to root/nested would never reach root.
    for(;;) {
        std::error_code code;
        const bool exists=std::filesystem::exists(ancestor,code);
        require(!code,"cannot inspect output ancestry");
        if(exists) break;
        const auto parent=ancestor.parent_path();
        require(parent != ancestor,"cannot find existing output ancestor");
        ancestor=parent;
    }
    ancestor=physical_existing_path(ancestor);
    const auto protected_root=physical_existing_path(root);
    for(;;) {
        std::error_code code;
        const bool inside=std::filesystem::equivalent(ancestor,protected_root,code);
        require(!code,"cannot compare output ancestry");
        require(!inside,"output is inside retail installation");
        const auto parent=ancestor.parent_path();
        if(parent==ancestor) break;
        ancestor=parent;
    }
}
#endif

Frame reference_frame(const std::filesystem::path& root, const RaceSession& session,
                      const PreparedTrackScene& source, const TrackDrawList& list,
                      const RenderOptions& options) {
    std::string error;
    // Diagnostic CPU storage only. Decode the existing top levels, never mips.
    // The production race retains its native blocks and its existing budgets.
    auto track = source;
    track.region.cost.texture_bytes = 0;
    std::uint64_t decoded_bytes = 0;
    for (auto& texture : track.region.textures) {
        const auto bytes = std::uint64_t{texture.width}*texture.height*4;
        require(bytes <= RenderTextureLimits{}.max_total_bytes-decoded_bytes,
                "comparison decoded texture budget exceeded");
        decoded_bytes += bytes;
        if (texture.storage != RenderTextureStorage::rgba8) {
            require(decode_level(texture.storage == RenderTextureStorage::dxt1 ? PixelFormat::dxt1 : PixelFormat::dxt3,
                                texture.width,texture.height,texture.blocks.data(),texture.blocks.size(),texture.rgba,error),error);
            texture.blocks.clear(); texture.blocks.shrink_to_fit();
            texture.storage = RenderTextureStorage::rgba8;
        }
        track.region.cost.texture_bytes += texture.rgba.size();
    }
    Frame background; RenderStats stats; TrackRenderStats track_stats;
    require(render_track_draw_list(track,list,options,{},background,stats,track_stats,error),error);
    // Track decode and raster temporaries die before preparing the car.
    track = {};
    StockFrameInputs inputs; StockFrame car;
    require(read_stock_frame_inputs(root,inputs),"comparison car input missing");
    require(prepare_race_car_frame(inputs,root,stock_240sx_request(),stock_240sx_skin(),car,error,options.car_shading),error);
    CarShadowAsset shadow;
    require(prepare_car_shadow("240SX",inputs,car.car_pack,shadow,error),error);
    require(render_car_shadow(shadow,place_car_shadow(session),options,background,stats,error),error);
    const auto base = car.scene.instances;
    require(drive_place_instances(base,session.car(),session.spec(),car.scene.instances,&session.animation()),
            "comparison car placement failed");
    require(render_stock_frame(car,options,error),error);
    Frame result;
    require(compose_race_frame(background,car.frame,result),"comparison frame dimensions differ");
    return result;
}

#if defined(__SWITCH__)
void show_comparison(const Frame& cpu, const std::vector<unsigned char>& gpu,
                     const FrameComparison& metrics, const std::string& identity,
                     double cpu_ms, double gpu_ms, double readback_ms, const std::string& cpu_hash) {
    std::string error;
    std::vector<unsigned char> difference;
    const RgbaFrameView a{cpu.rgba().data(),cpu.rgba().size(),cpu.width(),cpu.height(),cpu.width()*4};
    const RgbaFrameView b{gpu.data(),gpu.size(),cpu.width(),cpu.height(),cpu.width()*4};
    require(amplify_frame_difference(a,b,difference,error),error);
    Framebuffer fb{};
    require(R_SUCCEEDED(framebufferCreate(&fb,nwindowGetDefault(),1280,720,PIXEL_FORMAT_RGBA_8888,2)),"comparison framebuffer failed");
    struct Guard { Framebuffer* fb; ~Guard(){framebufferClose(fb);} } guard{&fb};
    require(R_SUCCEEDED(framebufferMakeLinear(&fb)),"comparison linear framebuffer failed");
    PadState pad; padConfigureInput(1,HidNpadStyleSet_NpadStandard); padInitializeDefault(&pad);
    const auto* selected = &gpu;
    bool overlay = true;
    char report[1024];
    std::snprintf(report,sizeof report,"%s\n%s: within +/-2 %.6f%%, RGB PSNR %.6f dB\nExact %.6f%%, max channel %u\nCPU reference+prepare %.3f ms; GPU render+wait %.3f ms; readback %.3f ms\nCPU TGA SHA %s\nGPU RGBA SHA %s\nA GPU | B CPU | X DIFF x16 | Y report | + exit\nScene 1280x720; sky on; particles/glows/wet/HUD off\nThresholds: within +/-2 >=99%% AND RGB PSNR >=35 dB",
        identity.c_str(),metrics.passed?"PASS":"FAIL",100.0*metrics.within_two_pixels/metrics.pixels,
        metrics.psnr_db,100.0*metrics.exact_pixels/metrics.pixels,metrics.max_channel_difference,cpu_ms,gpu_ms,readback_ms,cpu_hash.c_str(),
        sha256_hex(sha256(gpu.data(),gpu.size())).c_str());
    std::printf("%s\n",report); std::fflush(stdout);
    while(appletMainLoop()) {
        padUpdate(&pad); const auto down=padGetButtonsDown(&pad);
        if(down&HidNpadButton_Plus) break;
        if(down&HidNpadButton_A) selected=&gpu;
        if(down&HidNpadButton_B) selected=&cpu.rgba();
        if(down&HidNpadButton_X) selected=&difference;
        if(down&HidNpadButton_Y) overlay=!overlay;
        u32 stride=0; auto* pixels=static_cast<unsigned char*>(framebufferBegin(&fb,&stride));
        require(pixels,"comparison framebuffer pixels unavailable");
        for(std::size_t y=0;y<720;++y) std::copy_n(selected->data()+y*1280*4,1280*4,pixels+y*stride);
        if(overlay) {
            ScreenImage image{pixels,1280,720,stride};
            const char* cursor=report;
            for(int row=0;*cursor && row<12;++row) {
                char text[106]; std::size_t count=0;
                while(*cursor && *cursor!='\n' && count<105) text[count++]=*cursor++;
                if(*cursor=='\n') ++cursor;
                text[count]=0;
                fill_screen_rect(image,0,row*20,1280,20,{20,24,30,255});
                draw_screen_text(image,6,row*20+2,text,2,{230,230,230,255},{20,24,30,255});
            }
        }
        framebufferEnd(&fb);
    }
}
#endif

int run(int argc,char** argv) {
    std::filesystem::path root,recording,output;
#if defined(__SWITCH__)
    root="sdmc:/nfsu"; recording="romfs:/texture_filter_v2.txt";
    require(R_SUCCEEDED(romfsInit()),"comparison RomFS unavailable");
    struct Guard { ~Guard(){romfsExit();} } guard;
#else
    recording="tests/recordings/texture_filter_v2.txt";
#endif
    TextureFilterMode filter=TextureFilterMode::legacy;
    bool shaded=false;
    std::uint64_t step=180;
    for(int i=1;i<argc;++i) {
        const std::string arg=argv[i];
        if(arg=="--help") {
            std::puts("texture_filter_compare --root ROOT [--input V2_REPLAY] [--step 180|600|6000] [--texture-filter legacy|linear] [--car-shading on|off] [--out NEW.tga (desktop)]");
            return 0;
        }
        require(i+1<argc,"missing comparison option value");
        const std::string value=argv[++i];
        if(arg=="--root") root=comparison_path(argv[i]);
        else if(arg=="--input") recording=comparison_path(argv[i]);
        else if(arg=="--out") output=comparison_path(argv[i]);
        else if(arg=="--texture-filter") require(parse_texture_filter(value,filter),"invalid texture filter");
        else if(arg=="--car-shading") { require(value=="on" || value=="off","invalid shading mode"); shaded=value=="on"; }
        else if(arg=="--step") {
            require(!value.empty() && value.find_first_not_of("0123456789")==std::string::npos,"invalid replay step");
            step=std::stoull(value); require(step==180 || step==600 || step==6000,"comparison step must be 180, 600 or 6000");
        } else require(false,"unknown comparison option: "+arg);
    }
    require(!root.empty(),"--root required");
    std::string error;
#if defined(__SWITCH__)
    require(output.empty(),"console comparison is read-only");
#else
    require(!output.empty(),"--out required on desktop");
    protect_retail_output(root,output);
#endif
    RaceRun race; RaceGpuScene scene;
    require(scene.set_texture_filter(filter),"comparison filter refused");
    require(race.prepare_metadata(root,error),error);
    require(scene.load(posix_file_system(),root,error,nullptr,nullptr,false,false,shaded),error);
    require(race.start(root,scene.track(),error),error);
    std::ifstream in(recording,std::ios::binary);
    const std::string text((std::istreambuf_iterator<char>(in)),{});
    require(bool(in),"comparison recording unavailable");
    require(sha256_hex(sha256(text.data(),text.size()))==kTextureFilterFixtureHash,
            "comparison recording differs from v2 fixture");
    std::vector<RaceInputRun> runs;
    require(parse_race_input(text,runs,error),"comparison recording failed: "+error);
    std::size_t index=0, used=0;
    for(std::uint64_t tick=0;tick<step;++tick) {
        GamepadFrame pad;
        require(index<runs.size(),"comparison fixture exhausted");
        pad=race_input_frame(runs[index],tick);
        if(++used==runs[index].ticks) { ++index; used=0; }
        require(race.simulate(pad,error) && race.commit(error),error);
    }
    // Input identity and simulation identity are separate gates. A gameplay
    // recording edit cannot silently change this diagnostic, and a physics
    // edit must be attributed before changing its pose/image references.
    bool pose_pinned=false;
    for(const auto& reference : kTextureFilterPoseReferences)
        if(reference.step==step)
            pose_pinned=race.sequence_hash()==reference.sequence && race.steps_hash()==reference.steps;
    require(pose_pinned,"comparison v2 simulation differs from pinned pose");
    std::printf("FIXTURE v2 input-sha256 %s sequence %s steps %s\n",
                kTextureFilterFixtureHash,race.sequence_hash().c_str(),race.steps_hash().c_str());
    RaceGpuFrame frame; require(scene.frame(race.session(),frame,error),error);
    RenderOptions options;
    options.sky=true; options.material_state=MaterialStateMode::retail;
    options.car_shading=shaded; options.texture_filter=filter;
    require(drive_render_camera(race.session().camera(),ChaseFovMode::vertical_4_3,1280,720,options.camera,error),error);
    options.camera.far_plane=frame.far_plane;
    // Re-run the original selector, then assert every selected identity/order
    // against the GPU frame before drawing. No comparison-specific culling.
    CameraFrame basis;
    require(make_camera_frame(options.camera,1280,720,basis,error),error);
    TrackCamera camera;
    camera.eye[0]=float(basis.eye.x); camera.eye[1]=float(basis.eye.y); camera.eye[2]=float(basis.eye.z);
    camera.target[0]=float(options.camera.target.x); camera.target[1]=float(options.camera.target.y);
    camera.target[2]=float(options.camera.target.z);
    camera.up[0]=0; camera.up[1]=1; camera.up[2]=0;
    camera.aspect=1280.0F/720;
    camera.vertical_fov=float(options.camera.fov_y_deg*(3.14159265358979323846/180));
    camera.near_plane=float(options.camera.near_plane); camera.far_plane=float(frame.far_plane);
    TrackFrustum frustum;
    require(track_frustum_look_at(camera,frustum),"comparison frustum failed");
    const auto visible=cull_track_region(scene.track().objects,frustum);
    require(visible.ok() && visible.policy_held,"comparison culling degraded");
    TrackDrawList list; TrackDrawListOptions order; order.order=TrackDrawOrder::retail_bucket;
    require(build_track_draw_list(scene.track().scene,visible,order,list,error),error);
    RecordedCommands checked;
    require(record_track_commands(scene.track().scene,list,options,{},checked,error),error);
    require(checked.draw_count()==frame.track_draws,"CPU/GPU selected track count differs");
    for(std::size_t i=0;i<frame.track_draws;++i) {
        const auto& a=checked.draw_view(i).definition;
        const auto b=frame.commands.draw_view(i);
        require(a.group==b.definition.group && a.instance==b.instance && a.program==b.definition.program,
                "CPU/GPU selected track order/filter differs");
    }
    std::vector<unsigned char> gpu;
#if defined(__SWITCH__)
    double gpu_ms=0, readback_ms=0;
    {
        DekoRenderer renderer;
        require(renderer.open(frame.commands,error,false,kTrackSliceDrawLimit) &&
                renderer.capture(gpu,gpu_ms,readback_ms,error) && renderer.close(error),error);
    }
#endif
    const auto cpu_start=std::chrono::steady_clock::now();
    const auto cpu=reference_frame(root,race.session(),scene.track().scene,list,options);
    const double cpu_ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-cpu_start).count();
    StockFrameImage encoded;
    require(encode_stock_frame(cpu,encoded,error),error);
    const auto hash=sha256_hex(encoded.sha256);
    bool pinned=false;
    for (const auto& reference : kTextureFilterReferences)
        if(reference.step==step && reference.filter==filter && reference.shaded==shaded)
            pinned=hash==reference.sha256;
    std::printf("REFERENCE step %llu filter %s shading %s tga-sha256 %s\n",
                static_cast<unsigned long long>(step),filter==TextureFilterMode::linear?"linear":"legacy",shaded?"on":"off",hash.c_str());
#if defined(__SWITCH__)
    FrameComparison metrics;
    require(compare_frames({cpu.rgba().data(),cpu.rgba().size(),1280,720,1280*4},
                           {gpu.data(),gpu.size(),1280,720,1280*4},metrics,error),error);
    metrics.passed=metrics.passed && pinned;
    const std::string identity=std::string(pinned?"CPU PIN PASS | ":"CPU PIN FAIL | ")+"NFSU #262 fixture v2 step "+std::to_string(step)+" "+(filter==TextureFilterMode::linear?"linear":"legacy")+
        " shading "+(shaded?"on":"off")+" build "+NFSU_RACE_BUILD_TAG;
    show_comparison(cpu,gpu,metrics,identity,cpu_ms,gpu_ms,readback_ms,hash);
    return metrics.passed?0:1;
#else
    (void)cpu_ms;
    require(pinned,"CPU reference image differs from desktop pin");
    FrameWriteResult written;
    require(write_frame_tga(cpu,output,{recording},written),written.error);
    return 0;
#endif
}
}

int main(int argc,char** argv) {
    try { return run(argc,argv); }
    catch(const std::exception& e) {
        std::printf("FAIL: %s\n",e.what()); std::fflush(stdout);
#if defined(__SWITCH__)
        Framebuffer fb{};
        if(R_SUCCEEDED(framebufferCreate(&fb,nwindowGetDefault(),1280,720,PIXEL_FORMAT_RGBA_8888,2))) {
            if(R_SUCCEEDED(framebufferMakeLinear(&fb))) {
                PadState pad; padConfigureInput(1,HidNpadStyleSet_NpadStandard); padInitializeDefault(&pad);
                const auto lines=wrap_screen_text(std::string("COMPARISON FAILED: ")+e.what(),100);
                while(appletMainLoop()) {
                    padUpdate(&pad); if(padGetButtonsDown(&pad)&HidNpadButton_Plus) break;
                    u32 stride=0; auto* pixels=static_cast<unsigned char*>(framebufferBegin(&fb,&stride));
                    if(!pixels) break;
                    ScreenImage image{pixels,1280,720,stride};
                    fill_screen_rect(image,0,0,1280,720,{20,24,30,255});
                    for(std::size_t row=0;row<lines.size() && row<30;++row)
                        draw_screen_text(image,6,6+20*row,lines[row],2,{240,240,240,255},{20,24,30,255});
                    draw_screen_text(image,6,680,"Photograph this error. + exits.",2,{240,240,240,255},{20,24,30,255});
                    framebufferEnd(&fb);
                }
            }
            framebufferClose(&fb);
        }
#endif
        return 1;
    }
}
