#include "nfsu/frontend_scene.h"
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
            for(unsigned c=0;c<4;++c) vertex.color[c]=static_cast<unsigned char>(std::min(255U,unsigned(v.color[c])*2));vertex.u=v.u;vertex.v=v.v;mesh.vertices.push_back(vertex);
        }
        geometry.meshes.push_back(std::move(mesh));
    }
    MeshSelection selection;
    for(const auto& mesh:geometry.meshes) {
        if(mesh.name.rfind("TRFE_ROAD02",0)==0 && mesh.name.size()>2 && mesh.name.substr(mesh.name.size()-2)=="_A")
            selection.selectors.push_back({MeshSelectorKind::name,0,mesh.name,0});
    }
    if(selection.selectors.size()!=9) { error="original frontend tunnel meshes absent";return false; }
    const auto packs=parse_texture_pack_set(road.data(),road.size());
    RenderTextureSource source;source.bytes=&road;
    for(const auto& pack:packs.packs) source.packs.push_back(&pack);
    RenderTextureSet textures;
    if(!decode_render_texture_sources({source},RenderTextureLimits{},textures,error)) return false;
    PreparedCarScene tunnel;
    if(!prepare_car_scene(geometry,textures,selection,SceneLimits{},tunnel,error)) return false;
    StockFrameInputs inputs;
    if(!read_stock_frame_inputs(root,inputs)) { error=inputs.read_error(root);return false; }
    for(unsigned i=0;i<2;++i) {
        auto& file=inputs.files[i];file.path=i==0?"Cars/RSX/GEOMETRY.BIN":"Cars/RSX/TEXTURES.BIN";
        issues.clear();file.read=load_race_image(root/file.path,file.bytes,issues,limits);
        if(!file.read) {error="original RSX frontend model unreadable";return false;}
    }
    auto request=stock_240sx_request();request.car_type_name="RSX";request.pass=CarDrawPass::front_end;
    StockFrame car;
    auto skin=stock_240sx_skin();skin.car="RSX";skin.selection.car_name="RSX";
    const auto& globals=inputs.files[2].bytes;
    const auto types=parse_car_type_table(globals.data(),globals.size());
    const auto parts=parse_car_part_database(globals.data(),globals.size());
    const auto type=std::find_if(types.records.begin(),types.records.end(),
        [](const CarTypeRecord& t){return t.name=="RSX";});
    if(!types.ok() || !parts.ok() || type==types.records.end()) {
        error="original RSX default customization missing";return false;
    }
    const auto paint=std::find_if(parts.records.begin(),parts.records.end(),
        [&](const CarPartRecord& p){return p.slot==30 && p.part_name_hash==type->base_paint_part_hash;});
    if(paint==parts.records.end()) {error="original RSX default paint missing";return false;}
    unsigned channels[4]={0,0,0,0};bool found[4]={false,false,false,false};
    const char* names[4]={"RED","GREEN","BLUE","GLOSS"};
    for(int i=0;i<paint->attribute_count;++i) {
        const auto& a=parts.attributes.at(paint->attribute_index+i);
        for(unsigned c=0;c<4;++c) if(a.key==bin_hash(names[c])) {
            channels[c]=a.value;found[c]=true;
        }
    }
    if(!std::all_of(found,found+4,[](bool f){return f;})) {
        error="original RSX paint attributes absent";return false;
    }
    skin.selection.base_color=skin_pack_paint(channels[0],channels[1],channels[2],channels[3]);
    if(!prepare_stock_frame(inputs,request,skin,car,error,true)) return false;
    RenderOptions options;options.width=width;options.height=height;
    options.camera.yaw_deg=-35;options.camera.pitch_deg=8;options.camera.distance=6.5;
    options.camera.target={0,0.45,0};options.camera.fov_y_deg=35;
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
