#include "nfsu/frontend_scene.h"
#include "nfsu/frontend_preset.h"
#include "nfsu/stock_frame.h"
#include "nfsu/race_events.h"
#include "nfsu/track.h"
#include <algorithm>
#include <stdexcept>
namespace nfsu {
bool load_frontend_scene(const std::filesystem::path& root,
    std::vector<unsigned char>& rgba,unsigned width,unsigned height,std::string& error) {
    std::vector<unsigned char> front,road;
    std::vector<RaceTableIssue> issues;
    RaceTableLimits limits; limits.max_file_size=32*1024*1024;limits.max_image_size=64*1024*1024;
    if(!load_race_image(root/"FrontEnd/FrontB.lzc",front,issues,limits) ||
       !load_race_image(root/"FrontEnd/Road_Segment.bin",road,issues,limits)) {
        error="original frontend tunnel assets unreadable";return false;
    }
    // Geometry alignment is relative to the decoded enclosing FrontB image.
    const auto region=parse_track_region(front.data(),381824,0);
    GeometryReport geometry;
    for(const auto& source_mesh:region.meshes) {
        Mesh mesh;mesh.index=geometry.meshes.size();mesh.name=source_mesh.name;
        mesh.valid=source_mesh.valid;mesh.offset=source_mesh.offset;
        mesh.texture_hashes=source_mesh.texture_hashes;mesh.material_key_hashes=source_mesh.material_key_hashes;
        mesh.materials=source_mesh.materials;mesh.triangles=source_mesh.triangles;
        for(const auto& v:source_mesh.vertices) {
            MeshVertex vertex;vertex.px=v.px;vertex.py=v.py;vertex.pz=v.pz;
            for(unsigned c=0;c<4;++c) vertex.color[c]=static_cast<unsigned char>(std::min(255U,unsigned(v.color[c])*2));
            vertex.u=v.u;vertex.v=v.v;mesh.vertices.push_back(vertex);
        }
        geometry.meshes.push_back(std::move(mesh));
    }
    MeshSelection selection;
    for(const auto& mesh:geometry.meshes) {
        if(mesh.name.rfind("TRFE_ROAD02",0)==0 && mesh.name.size()>2 && mesh.name.substr(mesh.name.size()-2)=="_A")
            selection.selectors.push_back({MeshSelectorKind::name,0,mesh.name,0});
    }
    if(selection.selectors.size()!=9) { error="original frontend tunnel meshes absent";return false; }
    std::vector<unsigned char> tunnel_globals;
    issues.clear();
    if(!load_race_image(root/"Global/GLOBALB.BUN",tunnel_globals,issues,limits)) {
        error="original tunnel shared textures unreadable";return false;
    }
    const auto global_packs=parse_texture_pack_set(tunnel_globals.data(),tunnel_globals.size());
    const auto packs=parse_texture_pack_set(road.data(),road.size());
    RenderTextureSource source;source.bytes=&road;
    for(const auto& pack:packs.packs) source.packs.push_back(&pack);
    RenderTextureSource global_source;global_source.bytes=&tunnel_globals;
    for(const auto& pack:global_packs.packs) global_source.packs.push_back(&pack);
    RenderTextureSet textures;
    if(!decode_render_texture_sources({source,global_source},RenderTextureLimits{},textures,error)) return false;
    PreparedCarScene tunnel;
    if(!prepare_car_scene(geometry,textures,selection,SceneLimits{},tunnel,error)) return false;
    CarAssets assets;
    if(!load_car_assets(root/"Global/GLOBALB.BUN","SKYLINE",assets,error)) return false;
    StockFrameInputs inputs;
    if(!read_car_frame_inputs(root,assets,inputs,true,error)) {
        error=car_frame_inputs_error(inputs,error,root);return false;
    }
    const auto& globals=inputs.files[2].bytes;
    const auto parts=parse_car_part_database(globals.data(),globals.size());
    InstalledPartSelection installed;
    if(!resolve_frontend_skyline_parts(front,parts,installed,error)) return false;
    auto request=stock_car_request("SKYLINE");
    request.installed_parts=installed;request.pass=CarDrawPass::front_end;
    auto skin=stock_car_skin("SKYLINE");
    if(!resolve_frontend_skyline_skin(globals,parts,installed,skin.selection,error)) return false;
    StockFrame car;
    if(!prepare_stock_frame(inputs,request,skin,car,error,true)) return false;
    RenderOptions options;options.width=width;options.height=height;
    // Native 0x486C80/0x487680 initial camera: raw-axis position from
    // rotating the original (0,0.75,6) offset, looking at (0,0,0.75).
    // Subsequent original road and orbit updates are recorded by the GPU path.
    options.camera.yaw_deg=155;options.camera.pitch_deg=3.01;options.camera.distance=5.961;
    options.camera.target={0,0.75,0};options.camera.fov_y_deg=45;
    options.camera.far_plane=300;options.material_state=MaterialStateMode::retail;
    options.texture_filter=TextureFilterMode::linear;
    options.vertex_color=true;options.shading=ShadingMode::none;options.car_shading=true;
    Frame room;RenderStats stats;
    auto room_options=options;room_options.car_shading=false;
    if(!render_scene(tunnel,room_options,RenderLimits{},room,stats,error) ||
       !render_stock_frame(car,options,error)) return false;
    rgba=room.rgba();
    for(std::size_t i=0;i<room.depth().size();++i)
        if(car.frame.depth()[i]<room.depth()[i])
            std::copy_n(car.frame.rgba().data()+i*4,4,rgba.data()+i*4);
    return true;
}
}
