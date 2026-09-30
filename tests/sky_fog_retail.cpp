#include "draw_command_checks.h"
#include "nfsu/track_slice.h"
#include "nfsu/track_render.h"
#include "nfsu/posix_file_system.h"
#include "nfsu/stock_frame.h"
#include "nfsu/texture.h"
#include <algorithm>
#include <filesystem>
#include <iostream>
#include <stdexcept>

// Explicit retail gate, invoked inside a before/after manifest wrapper.
// No game data is bundled. Pins are of complete encoded TGA files.
namespace {
void require(bool value, const std::string& error) {
    if (!value) throw std::runtime_error(error);
}
}
int main(int argc, char** argv) {
    using namespace nfsu;
    namespace fs = std::filesystem;
    try {
        require(argc == 3, "usage: nfsu_sky_fog_retail ROOT out/NEW_DIRECTORY");
        const fs::path output(argv[2]);
        require(!output.empty() && !output.is_absolute() && *output.begin() == "out", "output must be under workspace out");
        for (const auto& component : output) require(component != "..", "output cannot traverse parents");
        require(!fs::exists(output) && fs::create_directory(output), "output must be new");
        TrackSlice slice; std::string error;
        require(load_track_slice(posix_file_system(),argv[1],slice,error),error);
        auto& scene = slice.scene;
        require(scene.sky_mesh != kNoHandle,"event-1 sky missing");
        const auto& mesh = scene.region.meshes[scene.sky_mesh];
        require(mesh.vertices.size()==421 && mesh.triangles.size()==720,"sky mesh inventory changed");
        std::vector<std::uint32_t> hashes;
        for (const auto& draw : scene.region.draws) if (draw.mesh==scene.sky_mesh) hashes.push_back(draw.texture_hash);
        require(hashes==std::vector<std::uint32_t>{0x8922567D,0x67688AB0},"sky texture selection changed");
        scene.region.cost.texture_bytes=0;
        for (auto& texture : scene.region.textures) {
            if (texture.storage != RenderTextureStorage::rgba8) {
                const auto format = texture.storage == RenderTextureStorage::dxt1 ? PixelFormat::dxt1 : PixelFormat::dxt3;
                require(decode_level(format,texture.width,texture.height,texture.blocks.data(),
                                    texture.blocks.size(),texture.rgba,error),error);
                texture.blocks.clear(); texture.storage=RenderTextureStorage::rgba8;
            }
            scene.region.cost.texture_bytes += texture.rgba.size();
        }
        std::vector<TrackVisibleInstance> visible;
        for (const auto& section : scene.sections) for (std::size_t i=0;i<section.source_instances;++i) {
            TrackVisibleInstance item; item.section_index=section.section_index; item.instance_index=i;
            visible.push_back(item);
        }
        TrackDrawList list; TrackDrawListOptions order; order.order=TrackDrawOrder::retail_bucket;
        require(build_track_draw_list(scene,visible,order,list,error),error);
        const char* names[] = {"overview","street"};
        // #184 new pins: sky background, with existing retail track alpha/blend.
        const char* pins[] = {"C98DF00ED9C13218E2F6FBEB4CF4289E74CF5A533F26CF1F32017F36B5FE03DF",
                              "79831E2C9A6315793191E7E755BDF20C051D2258A0246C57D291C86796F4E5A6"};
        bool matched=true;
        for (int i=0;i<2;++i) {
            RenderOptions options;
            options.width=640; options.height=360; options.sky=true;
            options.material_state=MaterialStateMode::retail;
            // Street eye stays above the road while looking toward the skyline.
            options.camera.target={919,i ? 70.0 : 62.0,216}; options.camera.distance=i ? 30 : 500;
            options.camera.yaw_deg=0; options.camera.pitch_deg=i ? -15 : 20;
            options.camera.fov_y_deg=60; options.camera.near_plane=0.5; options.camera.far_plane=3000;
            Frame frame,without; RenderStats stats; TrackRenderStats track;
            std::vector<ReferenceDraw> trace;
            require(render_track_draw_list(scene,list,options,{},frame,stats,track,error,
                [&](const ReferenceDraw& d){trace.push_back(d);}),error);
            RecordedCommands commands;
            require(record_track_commands(scene,list,options,{},commands,error),error);
            const auto mismatch=compare_draws(commands,trace,true);
            require(mismatch.empty(),mismatch);
            options.sky=false;
            require(render_track_draw_list(scene,list,options,{},without,stats,track,error),error);
            require(frame.depth()==without.depth(),"sky changed world depth");
            std::size_t changed=0;
            for(std::size_t n=0;n<frame.ids().size();++n)
                if (!std::equal(frame.rgba().begin()+n*4,frame.rgba().begin()+n*4+4,without.rgba().begin()+n*4)) ++changed;
            require(changed>0,"sky had no visible contribution");
            StockFrameImage image;
            require(encode_stock_frame(frame,image,error),error);
            const auto hash=sha256_hex(image.sha256);
            FrameWriteResult written;
            require(write_frame_tga(frame,output/(std::string(names[i])+".tga"),{},written),written.error);
            std::cout<<names[i]<<" sha256 "<<hash<<" sky-pixels "<<changed<<" commands "<<trace.size()<<std::endl;
            matched &= hash==pins[i];
        }
        require(matched,"sky frame hash differs from pin");
        std::cout<<"PASS: sky frames, unchanged world depth, complete CPU/GPU draw trace\n";
    } catch (const std::exception& e) { std::cerr<<"FAIL: "<<e.what()<<'\n'; return 1; }
}
