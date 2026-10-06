#include "nfsu/frontend_scene.h"
#include "nfsu/car_shadow.h"
#include <cstring>
#include <limits>
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
namespace {
bool prepare_frontend_shadow(const StockFrameInputs& inputs,const StockFrame& car,
    CarShadowAsset& shadow,std::string& error) {
    std::vector<std::uint32_t> loaded;
    for(const auto& mesh:car.car_geometry.meshes)if(mesh.has_directory_entry)loaded.push_back(mesh.directory_hash);
    for(const auto& source:car.stock_geometry)for(const auto& mesh:source.meshes)
        if(mesh.has_directory_entry)loaded.push_back(mesh.directory_hash);
    std::sort(loaded.begin(),loaded.end());loaded.erase(std::unique(loaded.begin(),loaded.end()),loaded.end());
    float low[3]={std::numeric_limits<float>::max(),std::numeric_limits<float>::max(),std::numeric_limits<float>::max()};
    float high[3]={-low[0],-low[1],-low[2]};
    const auto& bytes=inputs.file(StockFrameFile::car_geometry).bytes;
    auto u32=[&](std::size_t at){std::uint32_t v=0;for(unsigned c=0;c<4;++c)v|=std::uint32_t(bytes.at(at+c))<<(c*8);return v;};
    // Constructor 0x56AB08..0x56AB70 reduces LOD-A solid-header bounds
    // for slots 0..11, including disabled models and zero absent models.
    for(unsigned slot=0;slot<12;++slot) {
        float a[3]{},b[3]{};const auto record=car.report.slots[slot].record;
        if(record!=kNoCarPart) {
            std::array<PartModel,kCarPartLodCount> models;
            if(!resolve_part_models(car.parts.records.at(record),loaded,models,error))return false;
            if(models[0].resolved) {
                auto mesh=std::find_if(car.car_geometry.meshes.begin(),car.car_geometry.meshes.end(),
                    [&](const auto& m){return m.has_directory_entry && m.directory_hash==models[0].hash;});
                if(mesh==car.car_geometry.meshes.end()){error="FE footprint model outside car geometry";return false;}
                std::size_t cursor=mesh->offset+8,end=mesh->offset+8+u32(mesh->offset+4);bool found=false;
                while(cursor<end) {
                    if(end-cursor<8){error="FE footprint chunk truncated";return false;}
                    const auto tag=u32(cursor),size=u32(cursor+4);const auto payload=cursor+8;
                    if(size>end-payload){error="FE footprint chunk exceeds solid";return false;}
                    if(tag==kMeshMetadataId) {
                        const auto aligned=(payload+15)&~std::size_t(15);
                        if(payload+size<aligned+0x3c){error="FE footprint header truncated";return false;}
                        for(unsigned c=0;c<3;++c) {
                            auto v=u32(aligned+0x20+c*4);std::memcpy(a+c,&v,4);
                            v=u32(aligned+0x30+c*4);std::memcpy(b+c,&v,4);
                            if(!std::isfinite(a[c]) || !std::isfinite(b[c]) || a[c]>b[c]){error="FE footprint bounds invalid";return false;}
                        }
                        found=true;break;
                    }
                    cursor=payload+size;
                }
                if(!found){error="FE footprint solid header absent";return false;}
            }
        }
        for(unsigned c=0;c<3;++c){low[c]=std::min(low[c],a[c]);high[c]=std::max(high[c],b[c]);}
    }
    for(unsigned c=0;c<3;++c){low[c]-=.1F;high[c]+=.1F;}
    const auto hash=bin_hash("SKYLINE_SHADOWFE");std::vector<std::uint32_t> wanted{hash};
    RenderTextureSource source;source.bytes=&inputs.file(StockFrameFile::car_textures).bytes;
    source.packs.push_back(&car.car_pack);source.wanted=&wanted;RenderTextureSet decoded;
    if(!decode_render_texture_sources({source},{},decoded,error))return false;
    if(decoded.textures.size()!=1 || !decoded.textures[0].usable() || !decoded.textures[0].claims_hash(hash)) {
        error="original SKYLINE_SHADOWFE missing or unusable";return false;
    }
    shadow.texture=std::move(decoded.textures[0]);shadow.texture_name="SKYLINE_SHADOWFE";
    // FE branch 0x56EB40 uses X=1.115 and Y=1; original grid height
    // 0x56EFA6..0x56EFD7 is fixed ground .02, with four lifted samples.
    low[0]*=1.115F;high[0]*=1.115F;
    const float dx=(high[0]-low[0])*(1.0F/3),dy=(high[1]-low[1])*(1.0F/3);
    float y=low[1];std::size_t triangle=0;
    for(unsigned row=0;row<4;++row,y+=dy) {
        float x=low[0];
        for(unsigned column=0;column<4;++column,x+=dx) {
            const auto i=row*4+column;auto& v=shadow.vertices[i];v.px=x;v.pz=y;
            v.py=.02F+((((i>>1)^i)&5)==5?.08F:0.F);
            v.u=column*(1.0F/3);v.v=row*(1.0F/3);
            v.color[0]=v.color[1]=v.color[2]=128;v.color[3]=92;
            if(row<3 && column<3) {
                const auto a=static_cast<std::uint16_t>(i);
                shadow.triangles[triangle++]={a,static_cast<std::uint16_t>(a+1),static_cast<std::uint16_t>(a+4)};
                shadow.triangles[triangle++]={static_cast<std::uint16_t>(a+1),static_cast<std::uint16_t>(a+5),static_cast<std::uint16_t>(a+4)};
            }
        }
    }
    return true;
}
}
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
    const auto enriched=car.scene.draws;
    if(!prepare_stock_car_scene(sources,car.textures,car.parts,car.car_types,request,
        SceneLimits{},car.scene,car.report,error))return false;
    // A runtime skin alias changes texture ownership, not material selection.
    // prepare_stock_car_scene starts a new scene; retain the material vectors
    // already applied by prepare_stock_frame after checking stable identity.
    if(car.scene.draws.size()!=enriched.size()){error="FE skin rebind changed material group count";return false;}
    for(std::size_t i=0;i<enriched.size();++i) {
        auto& draw=car.scene.draws[i];const auto& original=enriched[i];
        if(draw.mesh!=original.mesh || draw.material_index!=original.material_index ||
            draw.material_key_hash!=original.material_key_hash) {
            error="FE skin rebind changed material group identity";return false;
        }
        draw.shading=original.shading;
    }
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
    PreparedCarScene& tunnel,StockFrame& car,RenderOptions& options,std::string& error,CarShadowAsset* shadow=nullptr,PreparedCarScene* capture=nullptr,PreparedCarScene* floor=nullptr,PreparedCarScene* sprites=nullptr,std::vector<MeshMarker>* road_markers=nullptr) {
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
            for(unsigned c=0;c<4;++c) vertex.color[c]=floor && source_mesh.vertex_format_flags==0x40?v.color[c]:static_cast<unsigned char>(std::min(255U,unsigned(v.color[c])*2));
            vertex.u=v.u;vertex.v=v.v;mesh.vertices.push_back(vertex);
        }
        geometry.meshes.push_back(std::move(mesh));
    }
    if(road_markers) {
        const auto u32=[&](std::size_t offset) {
            std::uint32_t value=0;for(unsigned i=0;i<4;++i)value|=std::uint32_t(front.at(offset+i))<<(8*i);
            return value;
        };
        for(const auto& mesh:region.meshes)if(mesh.name=="TRFE_ROAD02_A") {
            const auto begin=std::size_t(mesh.offset)+8;
            const auto end=begin+u32(std::size_t(mesh.offset)+4);
            if(end>381824){error="FE road marker owner truncated";return false;}
            for(std::size_t offset=begin;offset<end;) {
                if(end-offset<8){error="FE road marker header truncated";return false;}
                const auto tag=u32(offset),bytes=u32(offset+4);const auto payload=offset+8;
                if(bytes>end-payload){error="FE road marker chunk truncated";return false;}
                if(tag==kMarkerListId) {
                    const auto aligned=(payload+kMarkerDataAlignment-1)&~(kMarkerDataAlignment-1);
                    if(aligned>payload+bytes || (payload+bytes-aligned)%kMarkerRecordSize ||
                        (payload+bytes-aligned)/kMarkerRecordSize>kMaxMarkersPerMesh) {
                        error="FE road marker table invalid";return false;
                    }
                    for(auto i=payload;i<aligned;++i)if(front[i]!=0x11) {
                        error="FE road marker alignment invalid";return false;
                    }
                    for(auto i=aligned;i<payload+bytes;i+=kMarkerRecordSize) {
                        MeshMarker marker;marker.offset=i;marker.name_hash=u32(i);
                        for(unsigned c=0;c<16;++c) {
                            const auto bits=u32(i+16+c*4);std::memcpy(&marker.matrix[c],&bits,4);
                            if(!std::isfinite(marker.matrix[c])){error="FE road marker nonfinite";return false;}
                        }
                        if(marker.name_hash==bin_hash("TRFE_ROAD02_FLARE"))road_markers->push_back(marker);
                    }
                }
                offset=payload+bytes;
            }
        }
        if(road_markers->size()!=4){error="FE original road flare markers absent";return false;}
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
    if(sprites) {
        GeometryReport sprite_geometry;
        const std::uint32_t hashes[3]={0x3394FE62U,0x8868656FU,0x88D8E8A2U};
        const char* names[3]={"_FE_ROAD_GLOW","_FE_CAR_GLOW0","_FE_CAR_GLOW1"};
        for(unsigned kind=0;kind<3;++kind) {
            Mesh quad;quad.index=kind;quad.name=names[kind];quad.valid=true;
            quad.texture_hashes.push_back(hashes[kind]);
            for(unsigned i=0;i<4;++i) {
                MeshVertex v;v.px=i==0 || i==3?-1:1;v.py=i<2?1:-1;
                v.u=i==0 || i==3?0:1;v.v=i<2?0:1;
                v.color[0]=110;v.color[1]=100;v.color[2]=75;v.color[3]=255;
                quad.vertices.push_back(v);
            }
            quad.triangles={{0,1,2},{0,2,3}};MeshMaterial material;material.index_count=6;material.raw[12]=6;
            quad.materials.push_back(material);sprite_geometry.meshes.push_back(std::move(quad));
        }
        if(!prepare_car_scene(sprite_geometry,textures,MeshSelection{},SceneLimits{},*sprites,error))return false;
        for(const auto& draw:sprites->draws)if(draw.binding!=BindingKind::resolved) {
            error="original car/road flare texture absent";return false;
        }
    }

    if(capture) {
        MeshSelection cube_selection;
        for(const auto& mesh:geometry.meshes)if(mesh.name=="TRFE_ROAD02_Y")
            cube_selection.selectors.push_back({MeshSelectorKind::name,0,mesh.name,0});
        if(cube_selection.selectors.size()!=1){error="original FE cube road solid missing";return false;}
        if(!prepare_car_scene(geometry,textures,cube_selection,SceneLimits{},*capture,error))return false;
    }
    if(floor) {
        MeshSelection selected;selected.selectors.push_back({MeshSelectorKind::name,0,"TRFE_ROAD02_Z",0});
        if(!prepare_car_scene(geometry,textures,selected,SceneLimits{},*floor,error))return false;
    }
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
    if(shadow && !prepare_frontend_shadow(inputs,car,*shadow,error))return false;
    options.width=width;options.height=height;
    // Main entry 0x487BA0 mode0 -> 0x4870C0 selects saved table0 at
    // 0x7788E0, not constructor camera fields. These pose values invert the
    // genuine 0x487680 VIEW matrix, then map raw (x,y,z) -> world (x,z,-y).
    // Preserve its roll, target and eye through the shared CPU/GPU camera.
    options.camera.yaw_deg=-45.35166721096892;
    options.camera.pitch_deg=-1.37691385807865;
    options.camera.distance=3.905382401886139;
    options.camera.target={0,0.859000027179718,0.9200000166893005};
    options.camera.roll_deg=-6.62474634639572;
    // Original 0x48753D publishes fixed FOV12463. Primary-view projection
    // 0x40DD00 scales horizontal half-angle by 6B7C70 and vertical half-angle
    // by viewport height/width * 6B7C74/6B7C6C, truncating before native sine.
    const auto cotangent=[](double phase) {
        const auto half=static_cast<std::uint16_t>(static_cast<unsigned>(phase));
        return double(frontend_fixed_sine(static_cast<std::uint16_t>(half+0x4000U))) /
            double(frontend_fixed_sine(half));
    };
    options.camera.focal_x_override=cotangent(12463.0*double(1.100000023841858F)*.5);
    options.camera.focal_y_override=cotangent(12463.0*double(height)/double(width)*
        double(1.100000023841858F)/double(0.9090908765792847F)*.5);
    options.camera.far_plane=300;options.material_state=MaterialStateMode::retail;
    options.texture_filter=TextureFilterMode::linear;
    options.vertex_color=true;options.shading=ShadingMode::none;options.car_shading=true;
    return true;
}
}
namespace {
bool draw_frontend_road_sprites(Frame& target,const RenderOptions& options,
    const CameraFrame& camera,const std::vector<RecordedDraw>& draws,unsigned view,
    const PreparedCarScene& sprites,std::string& error) {
    const RenderLimits limits;
    RenderStats stats;Rasterizer raster(target,options,limits,stats);
    for(const auto& draw:draws)if(draw.frontend_sprite_view==view && draw.material.alpha[0]>0) {
        const unsigned descriptor=draw.frontend_sprite_kind==1?1:draw.frontend_sprite_kind==2?2:0;
        const auto& source=sprites.draws.at(descriptor);
        RasterMaterial material;material.texture=&sprites.textures.at(source.texture);material.blend=RenderBlendMode::additive;
        material.depth_write=false;material.linear_filter=true;material.alpha_scale=draw.material.alpha[0];
        ClipVertex corners[4];const int signs[4][2]={{-1,1},{1,1},{1,-1},{-1,-1}};
        for(unsigned i=0;i<4;++i) {
            float raw[3];for(unsigned axis=0;axis<3;++axis)raw[axis]=draw.placement.rows[12+axis]+
                signs[i][0]*draw.placement.rows[axis]+signs[i][1]*draw.placement.rows[8+axis];
            corners[i]=project_point(camera,{raw[0],raw[2],-raw[1]});
            corners[i].u=i==0 || i==3?0:1;corners[i].v=i<2?0:1;
            corners[i].r=draw.material.environment_range[3]>1.5F?draw.material.environment_color[0]:110;
            corners[i].g=draw.material.environment_range[3]>1.5F?draw.material.environment_color[1]:100;
            corners[i].b=draw.material.environment_range[3]>1.5F?draw.material.environment_color[2]:75;
        }
        const ClipVertex a[3]={corners[0],corners[1],corners[2]},b[3]={corners[0],corners[2],corners[3]};
        if(!raster.draw_triangle(a,material,0x7FFE0000U,error) ||
            !raster.draw_triangle(b,material,0x7FFE0001U,error))return false;
    }
    return true;
}
}
struct FrontendGpuScene::Impl {
    PreparedCarScene tunnel,capture,floor,sprites;
    std::vector<MeshMarker> road_markers;
    struct CarGlow {float point[3],normal[3];unsigned descriptor;float opacity;};
    std::vector<CarGlow> car_glows;
    std::size_t car_sprite_start=0;
    std::size_t sprite_start=0,cube_sprite_start=0,floor_sprite_start=0;
    CameraFrame floor_camera;
    std::size_t floor_mesh=kNoHandle;
    std::array<CameraFrame,6> cube_cameras;
    StockFrame car;
    RenderOptions options;
    FrontendLighting lighting=frontend_startup_lighting();
    FrontendRoad road;
    std::array<FrontendPointLight,32> road_lights;
    // Original735E90..735EA8 zero-BSS startup values are unresolved.
    // Preserve explicit zeros until an authenticated setter is established.
    float road_lateral=0,road_rgb[3]{},road_attenuation[3]{};
    CarShadowAsset shadow;
    std::size_t car_draw_count=0;
    RecordedCommands commands;
    std::size_t road_groups=0;
    std::vector<DrawInstanceUniform> base_car_placements;
    std::uint16_t wheel_phase=0;
    float wheel_radius=0;
    float car_root_height=0;
    bool ready=false;
};
FrontendGpuScene::FrontendGpuScene():impl_(std::make_unique<Impl>()){}
FrontendGpuScene::~FrontendGpuScene()=default;
bool FrontendGpuScene::load(const std::filesystem::path& root,unsigned width,unsigned height,std::string& error) {
    if(impl_->ready) {error="frontend GPU scene already loaded";return false;}
    auto next=std::make_unique<Impl>();auto& p=*next;
    if(!load_scene_assets(root,width,height,p.tunnel,p.car,p.options,error,&p.shadow,&p.capture,&p.floor,&p.sprites,&p.road_markers))return false;
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
    // Original 0x4B17F5..0x4B1825 places the whole FE car above its
    // wheel-0 contact plane. Part matrices below remain car-local.
    p.car_root_height=p.wheel_radius-type.wheels[0].vertical+.02F;
    if(!std::isfinite(p.wheel_radius) || p.wheel_radius<=0) {
        error="original frontend rolling radius invalid";return false;
    }
    for(std::size_t i=p.road_groups*5;i<p.commands.owning_draws().size();++i)
        p.base_car_placements.push_back(p.commands.owning_draws()[i].placement);
    for(std::size_t i=p.road_groups*5;i<p.commands.owning_draws().size();++i)
        p.commands.owning_draws()[i].placement.rows[14]+=p.car_root_height;
    p.car_draw_count=p.commands.owning_draws().size()-p.road_groups*5;
    RecordedCommands shadow_commands;CarShadowPlacement shadow_placement;
    // Native FE alpha byte is 92; world.efx Modulate2x doubles alpha.
    shadow_placement.alpha=184.0F/255;
    if(!record_car_shadow_commands(p.shadow,shadow_placement,p.options,shadow_commands,error))return false;
    shadow_commands.owning_draws()[0].name="_FE_SHADOW";
    RecordedCommands with_shadow;
    if(!combine_race_commands(std::move(p.commands),shadow_commands,with_shadow,error))return false;
    p.commands=std::move(with_shadow);
    auto capture_options=p.options;capture_options.car_shading=false;
    RecordedCommands cube_commands;
    if(!record_car_commands(p.capture,capture_options,RenderLimits{},kNoHandle,cube_commands,error))return false;
    const auto cube_templates=cube_commands.owning_draws();
    cube_commands.owning_draws().clear();
    for(unsigned segment=0;segment<3;++segment)for(const auto& original:cube_templates) {
        cube_commands.owning_draws().push_back(original);
        cube_commands.owning_draws().back().instance=segment;
    }
    const auto visible_count=p.commands.owning_draws().size();
    RecordedCommands resident_cube;
    if(!combine_race_commands(std::move(p.commands),cube_commands,resident_cube,error))return false;
    p.commands=std::move(resident_cube);
    auto& all_draws=p.commands.owning_draws();
    p.commands.frontend_cube_draws.assign(all_draws.begin()+visible_count,all_draws.end());
    all_draws.resize(visible_count);
    RecordedCommands floor_commands;
    if(!record_car_commands(p.floor,capture_options,RenderLimits{},kNoHandle,floor_commands,error))return false;
    const auto floor_templates=floor_commands.owning_draws();floor_commands.owning_draws().clear();
    for(unsigned segment=0;segment<3;++segment)for(const auto& draw:floor_templates) {
        floor_commands.owning_draws().push_back(draw);floor_commands.owning_draws().back().instance=segment;
    }
    const auto count=p.commands.owning_draws().size();
    RecordedCommands resident_floor;
    if(!combine_race_commands(std::move(p.commands),floor_commands,resident_floor,error))return false;
    p.commands=std::move(resident_floor);
    auto& complete=p.commands.owning_draws();
    p.commands.reflections.assign(complete.begin()+count,complete.end());complete.resize(count);
    for(std::size_t mesh=0;mesh<p.tunnel.meshes.size();++mesh)
        if(p.tunnel.meshes[mesh].name=="TRFE_ROAD02_A")p.floor_mesh=mesh;
    if(p.floor_mesh==kNoHandle){error="native worldreflect floor absent";return false;}
    for(std::size_t i=0;i<p.road_groups*5;++i)if(complete[i].mesh==p.floor_mesh)
        complete[i].material.environment_min[3]=1;
    for(auto& draw:p.commands.owning_draws())if(draw.material.surface.gates[0]!=0) {
        for(const auto& program:shader_manifest::kPrograms)
            if(std::string(program.name)=="frontend_car_textured")draw.program=&program;
    }
    p.commands.frontend_light=p.lighting;p.commands.frontend_light_enabled=true;
    RecordedCommands sprite_commands;
    auto sprite_options=p.options;sprite_options.car_shading=false;
    if(!record_car_commands(p.sprites,sprite_options,RenderLimits{},kNoHandle,sprite_commands,error))return false;
    auto sprite=sprite_commands.owning_draws().at(0);sprite.name="_FE_ROAD_GLOW";
    for(std::size_t i=0;i<sprite.program->variant_count;++i)
        if(std::string(sprite.program->variants[i].name)=="additive")sprite.pipeline=sprite.program->variants[i];
    sprite.pipeline.depth_write=false;sprite.material.environment_range[3]=1;
    sprite_commands.owning_draws().assign(20,sprite);
    p.sprite_start=p.commands.owning_draws().size();
    RecordedCommands resident_sprites;
    if(!combine_race_commands(std::move(p.commands),sprite_commands,resident_sprites,error))return false;
    p.commands=std::move(resident_sprites);
    auto& sprites=p.commands.owning_draws();
    for(unsigned i=0;i<20;++i) {sprites[p.sprite_start+i].instance=i;sprites[p.sprite_start+i].frontend_sprite_view=1;}
    p.floor_sprite_start=p.commands.reflections.size();p.cube_sprite_start=p.commands.frontend_cube_draws.size();
    for(unsigned i=0;i<12;++i) {
        auto draw=sprites[p.sprite_start];draw.instance=i;draw.frontend_sprite_view=4;
        p.commands.reflections.push_back(draw);
    }
    for(unsigned face=0;face<6;++face)for(unsigned i=0;i<12;++i) {
        auto draw=sprites[p.sprite_start];draw.instance=i;draw.frontend_sprite_view=face+10;
        p.commands.frontend_cube_draws.push_back(draw);
    }
    // FE RenderCar wrapper56A040 constructs56A130 with null owner.
    // Original56D8D0 therefore selects head1 and resting tail6B5DA0=.7.
    // Its type3 owner branch is a separate state, not an FE-name default.
    for(const auto& marker:p.car.report.markers) {
        const bool head=marker.name_hash==0x9DB90133U || marker.name_hash==0xD09091C6U;
        const bool tail=marker.name_hash==0x31A66786U || marker.name_hash==0xA2A2FC7CU || marker.name_hash==0xBF700A79U;
        if(!head && !tail)continue;
        const auto& geometry=marker.geometry_source==0?p.car.car_geometry:p.car.stock_geometry.at(marker.geometry_source-1);
        const auto& record=geometry.meshes.at(marker.source_mesh).markers.at(marker.index);
        for(unsigned descriptor=head?0:2;descriptor<(head?2:3);++descriptor) {
            Impl::CarGlow glow;glow.descriptor=descriptor;glow.opacity=head?1:.699999988079071F;
            for(unsigned axis=0;axis<3;++axis) {
                glow.point[axis]=record.matrix[12+axis];glow.normal[axis]=record.matrix[8+axis];
            }
            p.car_glows.push_back(glow);
        }
    }
    if(p.car_glows.empty()){error="original FE car flare markers absent";return false;}
    RecordedCommands car_sprites;
    if(!record_car_commands(p.sprites,sprite_options,RenderLimits{},kNoHandle,car_sprites,error))return false;
    const auto templates=car_sprites.owning_draws();car_sprites.owning_draws().clear();
    for(unsigned i=0;i<p.car_glows.size();++i) {
        const unsigned descriptor=p.car_glows[i].descriptor;
        auto draw=templates.at(descriptor==0?1:descriptor==1?2:0);
        draw.name="_FE_CAR_GLOW"+std::to_string(descriptor);draw.instance=i;draw.frontend_sprite_view=1;draw.frontend_sprite_kind=descriptor+1;
        for(std::size_t variant=0;variant<draw.program->variant_count;++variant)
            if(std::string(draw.program->variants[variant].name)=="additive")draw.pipeline=draw.program->variants[variant];
        draw.pipeline.depth_write=false;draw.material.environment_range[3]=2;
        car_sprites.owning_draws().push_back(draw);
    }
    p.car_sprite_start=p.commands.owning_draws().size();
    RecordedCommands combined_car_sprites;
    if(!combine_race_commands(std::move(p.commands),car_sprites,combined_car_sprites,error))return false;
    p.commands=std::move(combined_car_sprites);
    p.commands.frontend_light=p.lighting;p.commands.frontend_light_enabled=true;
    p.ready=true;impl_.swap(next);return frame(0,0,error);
}
bool FrontendGpuScene::frame(float seconds,std::uint32_t ticks,std::string& error) {
    auto& p=*impl_;if(!p.ready){error="frontend GPU scene not loaded";return false;}
    p.road.update(seconds,ticks);
    float marker_positions[4][3];
    for(unsigned marker=0;marker<4;++marker)for(unsigned c=0;c<3;++c)
        marker_positions[marker][c]=p.road_markers[marker].matrix[12+c];
    const auto light_count=frontend_road_lights(p.road,marker_positions,p.road_lateral,
        p.road_rgb,p.road_attenuation,p.road_lights);
    const float car_position[3]={0,0,p.car_root_height};
    p.lighting=frontend_point_lighting(p.road_lights.data(),light_count,car_position);
    p.commands.frontend_light=p.lighting;
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
    for(std::size_t i=p.road_groups*5;i<p.road_groups*5+p.car_draw_count;++i) {
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
        rotated.rows[14]+=p.car_root_height;
        draw.placement=rotated;
    }
    if(!record_frame_state(p.options,RenderLimits{},p.commands,error))return false;
    std::array<FrontendRoadPlacement,5> cube_placements;
    if(p.road.placements(10,cube_placements)!=3){error="original FE cube road placement missing";return false;}
    for(auto& draw:p.commands.frontend_cube_draws) {
        if(draw.frontend_sprite_view)continue;
        const auto& placement=cube_placements.at(draw.instance);
        draw.placement.rows[12]=placement.longitudinal;draw.placement.rows[14]=placement.vertical;
    }
    // 40AD60 face orientations are derived from the primary CView. Genuine
    // CRT initializer689920 sets78BD40=4000. Original40DD00 cube views10..15
    // produce unit focal scales, with native702AA4/AA8 clipping planes.
    CameraFrame primary;
    if(!make_camera_frame(p.options.camera,p.options.width,p.options.height,primary,error))return false;
    const auto negate=[](Vec3 v){return Vec3{-v.x,-v.y,-v.z};};
    const Vec3 rights[6]={negate(primary.forward),primary.forward,primary.right,primary.right,primary.right,negate(primary.right)};
    const Vec3 ups[6]={negate(primary.up),negate(primary.up),negate(primary.forward),primary.forward,negate(primary.up),negate(primary.up)};
    const Vec3 forwards[6]={primary.right,negate(primary.right),negate(primary.up),primary.up,primary.forward,negate(primary.forward)};
    for(unsigned face=0;face<6;++face) {
        auto& camera=p.cube_cameras[face];camera.eye={0,0,0};
        camera.right=rights[face];camera.up=ups[face];camera.forward=forwards[face];
        camera.focal_x=1;camera.focal_y=1;camera.near_plane=.5;camera.far_plane=64;
        auto cube_options=p.options;cube_options.width=cube_options.height=128;
        cube_options.car_shading=false;cube_options.explicit_camera=&camera;
        RecordedCommands cube_frame;
        if(!record_frame_state(cube_options,RenderLimits{},cube_frame,error))return false;
        p.commands.frontend_cube_frames[face]=cube_frame.frame;
    }
    p.commands.frame.options[2]|=8U;
    p.floor_camera=primary;
    // Original shared CView78D270 with floor descriptor71AA68,320x240;
    // genuine40DD00 view4 projection with native FOV12463.
    p.floor_camera.focal_x=1.2961782217025757;p.floor_camera.focal_y=1.660086750984192;
    auto floor_options=p.options;floor_options.width=320;floor_options.height=240;
    floor_options.explicit_camera=&p.floor_camera;floor_options.car_shading=false;
    RecordedCommands floor_frame;
    if(!record_frame_state(floor_options,RenderLimits{},floor_frame,error))return false;
    p.commands.frontend_floor_frame=floor_frame.frame;
    std::array<FrontendRoadPlacement,5> floor_placements;
    if(p.road.placements(4,floor_placements)!=3){error="original FE floor placements absent";return false;}
    for(auto& draw:p.commands.reflections) {
        if(draw.frontend_sprite_view)continue;
        auto& rows=draw.placement.rows;rows[12]=floor_placements.at(draw.instance).longitudinal;
        rows[13]=0;rows[14]=floor_placements.at(draw.instance).vertical;
    }
    const auto update_sprite=[&](RecordedDraw& draw,const CameraFrame& camera) {
        const auto view=draw.frontend_sprite_view;
        std::array<FrontendRoadPlacement,5> copies;p.road.placements(view,copies);
        const bool car_sprite=draw.frontend_sprite_kind!=0;
        const auto& marker=p.road_markers.at(car_sprite?0:draw.instance%4);
        const auto& copy=copies.at(car_sprite?0:draw.instance/4);
        float point[3];
        for(unsigned axis=0;axis<3;++axis)point[axis]=car_sprite?p.car_glows.at(draw.instance).point[axis]:marker.matrix[12+axis];
        if(car_sprite)point[2]+=p.car_root_height;
        else {point[0]+=copy.longitudinal;point[2]=view==4?-point[2]:point[2]+copy.vertical;}
        const double world[3]={point[0],point[2],-point[1]};
        const double delta[3]={world[0]-camera.eye.x,world[1]-camera.eye.y,world[2]-camera.eye.z};
        const double distance2=delta[0]*delta[0]+delta[1]*delta[1]+delta[2]*delta[2];
        const unsigned mode=view==4?2:view>=10?1:0;
        bool visible=distance2>0 && distance2<=(mode==1?4000:40000);
        const double extent=mode==2?10:mode==1?2:0;
        const double right3[3]={camera.right.x,camera.right.y,camera.right.z};
        const double up3[3]={camera.up.x,camera.up.y,camera.up.z};
        const double forward3[3]={camera.forward.x,camera.forward.y,camera.forward.z};
        double depth=0;for(unsigned axis=0;axis<3;++axis)depth+=delta[axis]*forward3[axis];
        const auto plane=[&](const double (&normal)[3],double constant) {
            double value=constant,radius=0;
            for(unsigned axis=0;axis<3;++axis){value+=delta[axis]*normal[axis];radius+=extent*std::abs(normal[axis]);}
            return mode==0?value>0:value+radius>=0;
        };
        for(unsigned axis=0;axis<2;++axis)for(int sign:{-1,1}) {
            double normal[3];for(unsigned c=0;c<3;++c)normal[c]=forward3[c]+
                sign*(axis==0?right3[c]*camera.focal_x:up3[c]*camera.focal_y);
            visible=visible && plane(normal,0);
        }
        double far_normal[3]={-forward3[0],-forward3[1],-forward3[2]};
        visible=visible && plane(forward3,-camera.near_plane) && plane(far_normal,camera.far_plane);
        const double distance=distance2>0?std::sqrt(distance2):1;
        const float toward_eye[3]={float(-delta[0]/distance),float(delta[2]/distance),float(-delta[1]/distance)};
        const float right[3]={float(camera.right.x),float(-camera.right.z),float(camera.right.y)};
        const float up[3]={float(-camera.up.x),float(camera.up.z),float(-camera.up.y)};
        // Original5447C0 mode2 mirrors the marker normal Z. Its road
        // mode0/1 facing vector remains zero at544CA3, not the marker normal.
        double dot=0;
        if(car_sprite)for(unsigned axis=0;axis<3;++axis)
            dot+=toward_eye[axis]*double(p.car_glows.at(draw.instance).normal[axis]);
        else if(mode==2)for(unsigned axis=0;axis<3;++axis)
            dot+=toward_eye[axis]*double(marker.matrix[8+axis])*(axis==2?-1:1);
        // Original40DCB0 uses main display width701034/2, not cube/floor capture width.
        const auto fov=static_cast<std::uint16_t>((mode==1?0x4000U:12463U)/2);
        const double denominator=(p.options.width/2)*double(frontend_fixed_sine(fov+0x4000U))/frontend_fixed_sine(fov);
        FrontendRoadGlow quad;
        if(car_sprite) {
            const auto& glow=p.car_glows.at(draw.instance);
            visible=visible && frontend_car_glow(point,toward_eye,right,up,float(std::max(0.,dot)),
                float(depth/denominator),mode,glow.descriptor,glow.opacity,quad);
            for(unsigned c=0;c<3;++c)draw.material.environment_color[c]=quad.rgba[c];
        } else visible=visible && frontend_road_glow(point,toward_eye,right,up,float(std::max(0.,dot)),
            float(depth/denominator),mode,quad);
        auto& rows=draw.placement.rows;std::fill_n(rows,16,0);rows[15]=1;
        if(visible)for(unsigned axis=0;axis<3;++axis) {
            rows[axis]=(quad.corners[1][axis]-quad.corners[0][axis])*.5F;
            rows[8+axis]=(quad.corners[0][axis]-quad.corners[3][axis])*.5F;
            rows[12+axis]=(quad.corners[0][axis]+quad.corners[2][axis])*.5F;
        }
        draw.material.alpha[0]=visible?float(quad.rgba[3])/255:0;
    };
    for(std::size_t i=p.sprite_start;i<draws.size();++i)update_sprite(draws[i],primary);
    for(auto& draw:p.commands.reflections)if(draw.frontend_sprite_view)update_sprite(draw,p.floor_camera);
    for(auto& draw:p.commands.frontend_cube_draws)if(draw.frontend_sprite_view)
        update_sprite(draw,p.cube_cameras[draw.frontend_sprite_view-10]);
    const double raw_eye[3]={p.commands.frame.eye[0],-p.commands.frame.eye[2],p.commands.frame.eye[1]};
    for(auto& draw:draws)if(draw.material.surface.gates[0]!=0)
        frontend_local_eye(draw.placement.rows,raw_eye,draw.material.environment_color);
    return true;
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
    for(std::size_t i=p.road_groups*5;i<p.road_groups*5+p.car_draw_count;++i) {
        const auto& draw=draws[i];
        std::copy_n(draw.placement.rows,16,car.instances.at(draw.instance).transform);
    }
    auto capture=p.capture;capture.profile=SelectionProfile::stock_car;
    capture.selectors.clear();capture.instances.clear();
    for(auto& mesh:capture.meshes)mesh.selector=kNoHandle;
    for(unsigned segment=0;segment<3;++segment) {
        PreparedInstance instance;instance.mesh=0;
        for(const auto& draw:p.commands.frontend_cube_draws)if(draw.instance==segment) {
            std::copy_n(draw.placement.rows,16,instance.transform);break;
        }
        capture.instances.push_back(instance);
    }
    std::array<Frame,6> cube;
    RenderStats capture_stats;
    for(unsigned face=0;face<6;++face) {
        auto options=p.options;options.car_shading=false;options.width=options.height=128;
        options.explicit_camera=&p.cube_cameras[face];
        options.background[0]=options.background[1]=options.background[2]=0;options.background[3]=255;
        if(!render_scene(capture,options,RenderLimits{},cube[face],capture_stats,error) ||
            !draw_frontend_road_sprites(cube[face],options,p.cube_cameras[face],p.commands.frontend_cube_draws,
                face+10,p.sprites,error))return false;
    }
    auto floor=p.floor;floor.profile=SelectionProfile::stock_car;
    floor.selectors.clear();floor.instances.clear();for(auto& mesh:floor.meshes)mesh.selector=kNoHandle;
    for(unsigned segment=0;segment<3;++segment) {
        PreparedInstance instance;instance.mesh=0;
        for(const auto& draw:p.commands.reflections)if(draw.instance==segment) {
            std::copy_n(draw.placement.rows,16,instance.transform);break;
        }
        floor.instances.push_back(instance);
    }
    Frame floor_frame;auto floor_options=p.options;floor_options.car_shading=false;
    floor_options.width=320;floor_options.height=240;floor_options.explicit_camera=&p.floor_camera;
    floor_options.background[0]=floor_options.background[1]=floor_options.background[2]=0;floor_options.background[3]=255;
    if(!render_scene(floor,floor_options,RenderLimits{},floor_frame,capture_stats,error) ||
        !draw_frontend_road_sprites(floor_frame,floor_options,p.floor_camera,p.commands.reflections,
            4,p.sprites,error))return false;
    Frame output,car_frame;RenderStats stats;
    auto car_options=p.options;car_options.frontend_lighting=&p.lighting;car_options.frontend_cube=cube.data();
    auto room_options=p.options;room_options.car_shading=false;
    room_options.frontend_floor=&floor_frame;room_options.frontend_floor_mesh=p.floor_mesh;
    if(!render_scene(tunnel,room_options,RenderLimits{},output,stats,error) ||
       !render_scene(car,car_options,RenderLimits{},car_frame,stats,error))return false;
    CarShadowPlacement shadow_placement;shadow_placement.alpha=184.0F/255;
    if(!render_car_shadow(p.shadow,shadow_placement,p.options,output,stats,error))return false;
    Frame combined;
    if(!compose_race_frame(output,car_frame,combined)){error="FE diagnostic scene compose failed";return false;}
    CameraFrame primary;
    if(!make_camera_frame(p.options.camera,p.options.width,p.options.height,primary,error) ||
        !draw_frontend_road_sprites(combined,p.options,primary,p.commands.owning_draws(),
            1,p.sprites,error))return false;
    rgba=combined.rgba();
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
