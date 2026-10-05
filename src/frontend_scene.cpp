#include "nfsu/frontend_scene.h"
#include <cmath>
#include "nfsu/frontend_preset.h"
#include "nfsu/frontend_motion.h"
#include "nfsu/race_gpu_scene.h"
#include <sstream>
#include <map>
#include <set>
#include "nfsu/stock_frame.h"
#include "nfsu/race_events.h"
#include "nfsu/track.h"
#include <algorithm>
#include <stdexcept>
namespace nfsu {
bool bind_frontend_skyline_runtime_skin(StockFrame& car,const StockSceneRequest& request,std::string& error) {
    if(!car.skinned || request.car_type_name!="SKYLINE") {
        error="original frontend runtime skin unavailable";return false;
    }
    // 0x580C37..0x580C73 gives body and global source slots the same
    // installed target. 0x56C375..0x56C389 binds GLOBAL_SKIN1 to that target.
    // Reuse the already-composed surface; never compose a second skin.
    const std::vector<std::uint32_t> bound={car.skin.target_hash,bin_hash("SKYLINE_SKIN1"),bin_hash("GLOBAL_SKIN1")};
    if(!bind_runtime_skin(car.skin,bound,car.skin_binding.source,car.textures,car.skin_binding,error))return false;
    std::vector<const GeometryReport*> sources={&car.car_geometry};
    for(const auto& geometry:car.stock_geometry)sources.push_back(&geometry);
    if(!prepare_stock_car_scene(sources,car.textures,car.parts,car.car_types,request,
        SceneLimits{},car.scene,car.report,error))return false;
    car.skin_bound_names.push_back("GLOBAL_SKIN1");return true;
}
bool bind_frontend_sponsors(const StockFrameInputs& inputs,const InstalledPartSelection& installed,
    StockFrame& car,std::string& error) {
    StockParts parts;
    FrontendSponsorHashes hashes;
    if(!select_installed_parts(car.parts,"SKYLINE",installed,parts,error) ||
       !resolve_frontend_sponsors(car.parts,parts,hashes,error))return false;
    // Genuine constructor 0x56AA9A..0x56AAC9 binds eight replacement entries
    // to slots19..29: panel=slot-19, subtract4 for the wide slots26..29.
    std::map<std::size_t,unsigned> panels;
    for(const auto& instance:car.scene.instances)if(instance.part_slot>=19 && instance.part_slot<=29) {
        unsigned panel=instance.part_slot-19;
        if(instance.part_slot>=26)panel-=4;
        auto found=panels.emplace(instance.mesh,panel);
        if(!found.second && found.first->second!=panel) {
            error="original sponsor mesh shared by distinct panels";return false;
        }
    }
    std::vector<std::uint32_t> wanted;
    for(const auto& panel:hashes)for(auto hash:panel)if(hash!=0x66994F4AU)wanted.push_back(hash);
    std::sort(wanted.begin(),wanted.end());wanted.erase(std::unique(wanted.begin(),wanted.end()),wanted.end());
    std::vector<RenderTextureSource> sources(1+car.shared_sets.size());
    sources[0].bytes=&inputs.files[1].bytes;sources[0].packs.push_back(&car.car_pack);sources[0].wanted=&wanted;
    const std::vector<const std::vector<unsigned char>*> bytes={&inputs.files[2].bytes,&inputs.files[3].bytes,&inputs.wheel_textures.bytes};
    for(std::size_t n=0;n<car.shared_sets.size();++n) {
        sources[n+1].bytes=bytes.at(n);sources[n+1].wanted=&wanted;
        sources[n+1].complete=car.shared_sets[n].complete;
        for(const auto& pack:car.shared_sets[n].packs)sources[n+1].packs.push_back(&pack);
    }
    RenderTextureSet textures;
    if(!decode_render_texture_sources(sources,RenderTextureLimits{},textures,error))return false;
    std::map<std::uint32_t,std::size_t> handles;
    for(auto& draw:car.scene.draws) {
        if(draw.texture_hash<0x910E6654U || draw.texture_hash>0x910E665BU)continue;
        auto panel=panels.find(draw.mesh);
        if(panel==panels.end()){error="original sponsor draw has no panel binding";return false;}
        const auto hash=hashes[panel->second][draw.texture_hash-0x910E6654U];
        // Original DEFAULTALPHA replacements are empty decal entries.
        if(hash==0x66994F4AU){draw.drawable=false;continue;}
        auto handle=handles.find(hash);
        if(handle==handles.end()) {
            auto texture=std::find_if(textures.textures.begin(),textures.textures.end(),[hash](const auto& t){return t.claims_hash(hash);});
            if(texture==textures.textures.end() || !texture->usable()) {
                std::ostringstream out;out<<"original sponsor texture unavailable: 0x"<<std::hex<<hash;error=out.str();return false;
            }
            const auto index=car.scene.textures.size();car.scene.textures.push_back(*texture);
            handle=handles.emplace(hash,index).first;
        }
        const auto& texture=car.scene.textures[handle->second];
        draw.texture_hash=hash;draw.texture=handle->second;draw.binding=BindingKind::resolved;draw.drawable=true;
        draw.texture_source=texture.source;draw.texture_pack=texture.pack;draw.texture_record=texture.record;
    }
    // Prepared scenes own resolved textures in first-use order.
    std::vector<RenderTexture> ordered;
    std::map<std::size_t,std::size_t> remap;
    for(auto& draw:car.scene.draws)if(draw.binding==BindingKind::resolved) {
        auto mapping=remap.emplace(draw.texture,ordered.size());
        if(mapping.second)ordered.push_back(car.scene.textures.at(draw.texture));
        draw.texture=mapping.first->second;
    }
    for(auto& instance:car.scene.instances)if(instance.brake_left_texture!=kNoHandle) {
        auto mapping=remap.emplace(instance.brake_left_texture,ordered.size());
        if(mapping.second)ordered.push_back(car.scene.textures.at(instance.brake_left_texture));
        instance.brake_left_texture=mapping.first->second;
    }
    car.scene.textures.swap(ordered);
    std::set<std::uint32_t> unresolved;
    for(const auto& draw:car.scene.draws)if(draw.binding==BindingKind::unresolved)unresolved.insert(draw.texture_hash);
    car.scene.unresolved_hashes.assign(unresolved.begin(),unresolved.end());
    return validate_prepared_scene(car.scene,error);
}
namespace {
bool load_scene_assets(const std::filesystem::path& root,unsigned width,unsigned height,
    PreparedCarScene& tunnel,StockFrame& car,RenderOptions& options,std::string& error) {
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
    RenderTextureSource source;source.bytes=&road;source.complete=packs.complete;
    for(const auto& pack:packs.packs) source.packs.push_back(&pack);
    RenderTextureSource global_source;global_source.bytes=&tunnel_globals;global_source.complete=global_packs.complete;
    for(const auto& pack:global_packs.packs) global_source.packs.push_back(&pack);
    RenderTextureSet textures;
    if(!decode_render_texture_sources({source,global_source},RenderTextureLimits{},textures,error)) return false;
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
    if(!prepare_stock_frame(inputs,request,skin,car,error,true) ||
       !bind_frontend_skyline_runtime_skin(car,request,error) ||
       !bind_frontend_sponsors(inputs,installed,car,error)) return false;
    options.width=width;options.height=height;
    // Native 0x486C80/0x487680 initial camera: raw-axis position from
    // rotating the original (0,0.75,6) offset, looking at (0,0,0.75).
    // Subsequent original road and orbit updates are recorded by the GPU path.
    options.camera.yaw_deg=155;options.camera.pitch_deg=3.01;options.camera.distance=5.961;
    options.camera.target={0,0.75,0};options.camera.fov_y_deg=45;
    options.camera.far_plane=300;options.material_state=MaterialStateMode::retail;
    options.texture_filter=TextureFilterMode::linear;
    options.vertex_color=true;options.shading=ShadingMode::none;options.car_shading=true;
    return true;
}
}
struct FrontendGpuScene::Impl {
    PreparedCarScene tunnel;
    StockFrame car;
    RenderOptions options;
    FrontendRoad road;
    RecordedCommands commands;
    std::size_t road_groups=0;
    std::vector<DrawInstanceUniform> base_car_placements;
    std::uint16_t wheel_phase=0;
    float wheel_radius=0;
    bool ready=false;
};
FrontendGpuScene::FrontendGpuScene():impl_(std::make_unique<Impl>()){}
FrontendGpuScene::~FrontendGpuScene()=default;
bool FrontendGpuScene::load(const std::filesystem::path& root,unsigned width,unsigned height,std::string& error) {
    if(impl_->ready) {error="frontend GPU scene already loaded";return false;}
    auto next=std::make_unique<Impl>();auto& p=*next;
    if(!load_scene_assets(root,width,height,p.tunnel,p.car,p.options,error))return false;
    // Original solid lookup 0x53F9B0 requests missing->DEFAULTALPHA through
    // 0x5461C0. The tunnel's zero replacement table bypasses 0x53F540;
    // 0x40C910 skips DEFAULTALPHA at 0x40CA0C, before reflection uniforms.
    // Only the authenticated absent wall-strip hash receives that policy.
    // Keep its authored geometry resident; other missing assets still fail.
    std::set<std::size_t> native_empty;
    for(std::size_t group=0;group<p.tunnel.draws.size();++group) {
        const auto& draw=p.tunnel.draws[group];
        if(draw.binding==BindingKind::unresolved && draw.texture_hash==0xC2456B43U) {
            const auto& name=p.tunnel.meshes.at(draw.mesh).name;
            if(name!="TRFE_ROAD02_04_A" && name!="TRFE_ROAD02_07_A") {
                error="native empty tunnel material outside authenticated meshes";return false;
            }
            native_empty.insert(group);
        }
    }
    for(const auto* scene:{&p.tunnel,&p.car.scene})for(const auto& draw:scene->draws)
        if(draw.drawable && draw.binding!=BindingKind::resolved &&
           !(scene==&p.tunnel && draw.binding==BindingKind::unresolved && draw.texture_hash==0xC2456B43U)) {
            std::ostringstream message;message<<"original frontend texture binding unresolved: 0x"<<std::hex<<draw.texture_hash;
            error=message.str();return false;
        }
    RecordedCommands road,car;
    if(!record_car_commands(p.tunnel,p.options,RenderLimits{},kNoHandle,road,error) ||
       !record_car_commands(p.car.scene,p.options,RenderLimits{},p.car.skinned?p.car.skin_binding.source:kNoHandle,car,error))return false;
    auto& road_draws=road.owning_draws();
    road_draws.erase(std::remove_if(road_draws.begin(),road_draws.end(),
        [&](const auto& draw){return native_empty.count(draw.group)!=0;}),road_draws.end());
    const auto originals=road_draws;p.road_groups=originals.size();
    auto& repeated=road.owning_draws();repeated.clear();repeated.reserve(p.road_groups*5);
    for(unsigned segment=0;segment<5;++segment)for(const auto& original:originals) {
        repeated.push_back(original);auto& draw=repeated.back();
        draw.name+="_FE_SEGMENT"+std::to_string(segment);draw.instance=segment;
    }
    if(!combine_race_commands(std::move(road),car,p.commands,error))return false;
    const auto& type=p.car.car_types.records.at(p.car.report.car_type);
    p.wheel_radius=type.wheels[0].rolling_radius;
    if(!std::isfinite(p.wheel_radius) || p.wheel_radius<=0) {
        error="original frontend rolling radius invalid";return false;
    }
    for(std::size_t i=p.road_groups*5;i<p.commands.owning_draws().size();++i)
        p.base_car_placements.push_back(p.commands.owning_draws()[i].placement);
    p.ready=true;impl_.swap(next);return frame(0,0,error);
}
bool FrontendGpuScene::frame(float seconds,std::uint32_t ticks,std::string& error) {
    auto& p=*impl_;if(!p.ready){error="frontend GPU scene not loaded";return false;}
    p.road.update(seconds,ticks);
    std::array<FrontendRoadPlacement,5> placements;
    if(p.road.placements(1,placements)!=5){error="original frontend road view missing";return false;}
    auto& draws=p.commands.owning_draws();
    for(unsigned segment=0;segment<5;++segment)for(std::size_t group=0;group<p.road_groups;++group) {
        auto& rows=draws[segment*p.road_groups+group].placement.rows;
        rows[12]=placements[segment].longitudinal;rows[13]=0;rows[14]=placements[segment].vertical;
    }
    p.wheel_phase=frontend_wheel_phase(p.wheel_phase,p.road.delta,p.wheel_radius);
    const float sine=frontend_fixed_sine(p.wheel_phase);
    const float cosine=frontend_fixed_sine(static_cast<std::uint16_t>(p.wheel_phase+0x4000));
    const float spin[16]={cosine,0,-sine,0,0,1,0,0,sine,0,cosine,0,0,0,0,1};
    for(std::size_t i=p.road_groups*5;i<draws.size();++i) {
        auto& draw=draws[i];const auto& instance=p.car.scene.instances.at(draw.instance);
        if(instance.part_slot!=kSlotWheel) continue;
        auto base=p.base_car_placements.at(i-p.road_groups*5);
        for(unsigned c=0;c<3;++c)base.rows[12+c]-=instance.animation_pivot[c];
        DrawInstanceUniform rotated;
        for(unsigned r=0;r<4;++r)for(unsigned c=0;c<4;++c) {
            double value=0;
            for(unsigned k=0;k<4;++k)value+=double(base.rows[r*4+k])*spin[k*4+c];
            rotated.rows[r*4+c]=static_cast<float>(value);
        }
        for(unsigned c=0;c<3;++c)rotated.rows[12+c]+=instance.animation_pivot[c];
        draw.placement=rotated;
    }
    return record_frame_state(p.options,RenderLimits{},p.commands,error);
}
const RecordedCommands& FrontendGpuScene::commands() const {
    if(!impl_->ready)throw std::logic_error("frontend GPU scene not loaded");
    return impl_->commands;
}
bool FrontendGpuScene::render_reference(std::vector<unsigned char>& rgba,std::string& error) const {
    const auto& p=*impl_;
    if(!p.ready){error="frontend GPU scene not loaded";return false;}
    auto tunnel=p.tunnel;
    auto car=p.car.scene;
    tunnel.profile=SelectionProfile::stock_car;
    tunnel.selectors.clear();tunnel.instances.clear();
    for(auto& mesh:tunnel.meshes)mesh.selector=kNoHandle;
    const auto& draws=p.commands.owning_draws();
    // Each repeated mesh receives exactly the placement sent to the GPU.
    // Material groups share the mesh placement; preserve their authored data.
    for(unsigned segment=0;segment<5;++segment) {
        std::set<std::size_t> placed;
        for(std::size_t group=0;group<p.road_groups;++group) {
            const auto& draw=draws[segment*p.road_groups+group];
            if(!placed.insert(draw.mesh).second)continue;
            PreparedInstance instance;instance.mesh=draw.mesh;
            std::copy_n(draw.placement.rows,16,instance.transform);
            tunnel.instances.push_back(instance);
        }
    }
    tunnel.draws.erase(std::remove_if(tunnel.draws.begin(),tunnel.draws.end(),
        [](const auto& draw){return draw.binding==BindingKind::unresolved &&
            draw.texture_hash==0xC2456B43U;}),tunnel.draws.end());
    tunnel.unresolved_hashes.clear();
    for(auto& mesh:tunnel.meshes)mesh.material_count=0;
    for(auto& draw:tunnel.draws)
        draw.material_index=tunnel.meshes[draw.mesh].material_count++;
    for(std::size_t i=p.road_groups*5;i<draws.size();++i) {
        const auto& draw=draws[i];
        std::copy_n(draw.placement.rows,16,car.instances.at(draw.instance).transform);
    }
    Frame output,car_frame;RenderStats stats;
    auto room_options=p.options;room_options.car_shading=false;
    if(!render_scene(tunnel,room_options,RenderLimits{},output,stats,error) ||
       !render_scene(car,p.options,RenderLimits{},car_frame,stats,error))return false;
    rgba=output.rgba();
    for(std::size_t i=0;i<output.depth().size();++i)
        if(car_frame.depth()[i]<output.depth()[i])
            std::copy_n(car_frame.rgba().data()+i*4,4,rgba.data()+i*4);
    return true;
}
bool load_frontend_scene(const std::filesystem::path& root,
    std::vector<unsigned char>& rgba,unsigned width,unsigned height,std::string& error) {
    PreparedCarScene tunnel;StockFrame car;RenderOptions options;
    if(!load_scene_assets(root,width,height,tunnel,car,options,error))return false;
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



