#include "nfsu/frontend_scene.h"
#include "nfsu/career_ride.h"
#include "nfsu/car_shadow.h"
#include "nfsu/car_neon.h"
#include <cstring>
#include <limits>
#include <cmath>
#include "nfsu/frontend_preset.h"
#include "nfsu/frontend_motion.h"
#include "nfsu/race_gpu_scene.h"
#include <sstream>
#include <map>
#include <tuple>
#include <set>
#include "nfsu/stock_frame.h"
#include "nfsu/race_events.h"
#include "nfsu/track.h"
#include <algorithm>
#include <stdexcept>
namespace nfsu {
struct FrontendSceneCarSource {
    std::string car;
    bool exhaust=false;
    StockFrameInputs inputs;
    StockFrameGeometryCache geometry;
};
namespace {
bool prepare_frontend_shadow(const StockFrameInputs& inputs,const StockFrame& car,
    CarShadowAsset& shadow,std::string& error) {
    std::vector<std::uint32_t> loaded;
    for(const auto& mesh:car.car_geometry.meshes)if(mesh.has_directory_entry)loaded.push_back(mesh.directory_hash);
    for(const auto& source:car.geometry_sources())for(const auto& mesh:source.meshes)
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
    const auto hash=bin_hash(car.report.car_type_name+"_SHADOWFE");std::vector<std::uint32_t> wanted{hash};
    RenderTextureSource source;source.bytes=&inputs.file(StockFrameFile::car_textures).bytes;
    source.packs.push_back(&car.car_pack);source.wanted=&wanted;RenderTextureSet decoded;
    if(!decode_render_texture_sources({source},{},decoded,error))return false;
    if(decoded.textures.size()!=1 || !decoded.textures[0].usable() || !decoded.textures[0].claims_hash(hash)) {
        error="original car SHADOWFE missing or unusable";return false;
    }
    shadow.texture=std::move(decoded.textures[0]);shadow.texture_name=car.report.car_type_name+"_SHADOWFE";
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
bool bind_frontend_runtime_skin(StockFrame& car,const StockSceneRequest& request,std::string& error) {
    if(!car.skinned || request.car_type_name!=car.report.car_type_name) {
        error="original frontend runtime skin unavailable";return false;
    }
    // 0x580C37..0x580C73 gives body and global source slots the same
    // installed target. 0x56C375..0x56C389 binds GLOBAL_SKIN1 to that target.
    // Reuse the already-composed surface; never compose a second skin.
    const std::vector<std::uint32_t> bound={car.skin.target_hash,bin_hash(request.car_type_name+"_SKIN1"),bin_hash("GLOBAL_SKIN1")};
    if(!bind_runtime_skin(car.skin,bound,car.skin_binding.source,car.textures,car.skin_binding,error))return false;
    std::vector<const GeometryReport*> sources={&car.car_geometry};
    for(const auto& geometry:car.geometry_sources())sources.push_back(&geometry);
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
bool bind_frontend_skyline_runtime_skin(StockFrame& car,const StockSceneRequest& request,std::string& error) {
    if(request.car_type_name!="SKYLINE") {error="original frontend runtime skin unavailable";return false;}
    return bind_frontend_runtime_skin(car,request,error);
}
bool bind_frontend_sponsors(const StockFrameInputs& inputs,const InstalledPartSelection& installed,
    StockFrame& car,std::string& error) {
    StockParts parts;
    FrontendSponsorHashes hashes;
    if(!select_installed_parts(car.parts,car.report.car_type_name,installed,parts,error) ||
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
    // The shared sets, in preparation order: GLOBALB, Cars/TEXTURES.BIN, the
    // tuned wheel textures, then the opt-in EXHAUST family textures a dressed
    // configuration reads. A caller that read no family pair has three sets
    // and never reaches the fourth entry.
    const std::vector<const std::vector<unsigned char>*> bytes={&inputs.files[2].bytes,
        &inputs.files[3].bytes,&inputs.wheel_textures.bytes,&inputs.exhaust_textures.bytes};
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
bool load_scene_car(const std::filesystem::path& root,unsigned width,unsigned height,
    StockFrame& car,RenderOptions& options,std::string& error,CarShadowAsset* shadow,
    const std::string& stock_car,const CareerRideSetup* customized,CarNeonAsset* neon,
    const std::vector<unsigned char>* front=nullptr,std::shared_ptr<const FrontendSceneCarSource>* cache=nullptr,
    StockFramePaintCache* paint_cache=nullptr) {
    const auto name=stock_car.empty()?std::string("SKYLINE"):stock_car;
    std::shared_ptr<const FrontendSceneCarSource> source=cache?*cache:nullptr;
    if(!source||source->car!=name||(customized&&!source->exhaust)) {
        auto prepared=std::make_shared<FrontendSceneCarSource>();prepared->car=name;
        CarAssets assets;
        if(!load_car_assets(root/"Global/GLOBALB.BUN",name,assets,error))return false;
        if(!read_car_frame_inputs(root,assets,prepared->inputs,true,error)) {
            error=car_frame_inputs_error(prepared->inputs,error,root);return false;
        }
        if(customized) {
            if(!read_exhaust_family_inputs(root,prepared->inputs)){error="garage exhaust family unavailable";return false;}
            prepared->exhaust=true;
        }
        if(!build_stock_frame_geometry_cache(prepared->inputs,prepared->geometry,error))return false;
        source=std::move(prepared);if(cache)*cache=source;
    }
    const auto& inputs=source->inputs;
    const auto& globals=inputs.files[2].bytes;
    const auto parts=parse_car_part_database(globals.data(),globals.size());
    InstalledPartSelection installed;
    auto request=stock_car_request(name);request.pass=CarDrawPass::front_end;
    auto skin=stock_car_skin(name);
    if(customized) {
        if(customized->request.car_type_name!=name){error="garage variant car mismatch";return false;}
        request=customized->request;request.pass=CarDrawPass::front_end;
        installed=request.installed_parts;skin=customized->skin;
    }else if(stock_car.empty()) {
        if(!front){error="special menu car needs original frontend data";return false;}
        if(!resolve_frontend_skyline_parts(*front,parts,installed,error))return false;
        request.installed_parts=installed;
        if(!resolve_frontend_skyline_skin(globals,parts,installed,skin.selection,error))return false;
    }
    if(!prepare_stock_frame(inputs,request,skin,car,error,true,&source->geometry,paint_cache) ||
       !bind_frontend_runtime_skin(car,request,error))return false;
    if((stock_car.empty()||customized) && !bind_frontend_sponsors(inputs,installed,car,error))return false;
    if(shadow && !prepare_frontend_shadow(inputs,car,*shadow,error))return false;
    if(customized&&shadow&&neon&&!prepare_car_neon(inputs,car,*shadow,*neon,error))return false;
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
    // 0x40DD00 scales the vertical half-angle by viewport height/width *
    // 6B7C74/6B7C6C. Use its vertical framing in the displayed view.
    const auto cotangent=[](double phase) {
        const auto half=static_cast<std::uint16_t>(static_cast<unsigned>(phase));
        return double(frontend_fixed_sine(static_cast<std::uint16_t>(half+0x4000U))) /
            double(frontend_fixed_sine(half));
    };
    options.camera.focal_y_override=cotangent(12463.0*double(height)/double(width)*
        double(1.100000023841858F)/double(0.9090908765792847F)*.5);
    options.camera.focal_x_override=options.camera.focal_y_override*double(height)/double(width);
    options.camera.far_plane=300;options.material_state=MaterialStateMode::retail;
    options.texture_filter=TextureFilterMode::linear;
    options.vertex_color=true;options.shading=ShadingMode::none;options.car_shading=true;
    return true;
}
bool load_scene_assets(const std::filesystem::path& root,unsigned width,unsigned height,
    PreparedCarScene& tunnel,StockFrame& car,RenderOptions& options,std::string& error,CarShadowAsset* shadow=nullptr,PreparedCarScene* capture=nullptr,PreparedCarScene* floor=nullptr,PreparedCarScene* sprites=nullptr,std::vector<MeshMarker>* road_markers=nullptr,const std::string& stock_car="",const CareerRideSetup* customized=nullptr,CarNeonAsset* neon=nullptr,std::shared_ptr<const FrontendSceneCarSource>* cache=nullptr) {
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
    return load_scene_car(root,width,height,car,options,error,shadow,stock_car,customized,neon,&front,cache);
}
}
namespace {
bool draw_frontend_neon(Frame& target,const RenderOptions& options,const CarNeonAsset& asset,
    const RecordedDraw& draw,std::string& error) {
    if(!asset.enabled||draw.material.alpha[0]<=0)return true;
    CameraFrame camera;
    if(options.explicit_camera)camera=*options.explicit_camera;
    else if(!make_camera_frame(options.camera,target.width(),target.height(),camera,error))return false;
    RasterMaterial material;material.texture=&asset.ground.texture;material.depth_write=false;material.linear_filter=true;
    material.ordinary_world=true; // Preserve native RGB2x without clamping vertex tint first.
    material.blend=render_blend_mode_of(asset.ground.texture.alpha_blend_mode)==RenderBlendMode::additive?
        RenderBlendMode::additive:RenderBlendMode::source_over;
    material.alpha_scale=draw.material.alpha[0];
    auto tinted=options;tinted.vertex_color=true;tinted.car_shading=false;RenderStats stats;Rasterizer raster(target,tinted,{},stats);
    const auto rgb=static_cast<std::uint32_t>(draw.material.alpha[2]);
    for(const auto& triangle:asset.ground.triangles) {
        ClipVertex clip[3];const unsigned indices[]={triangle.a,triangle.b,triangle.c};
        for(unsigned k=0;k<3;++k) {
            const auto& v=asset.ground.vertices[indices[k]];
            clip[k]=project_point(camera,placed_vertex(v,draw.placement.rows));clip[k].u=v.u;clip[k].v=v.v;
            // The ordinary-world raster path expands stored BGR to logical RGB.
            clip[k].r=float((rgb>>16)&255);clip[k].g=float((rgb>>8)&255);clip[k].b=float(rgb&255);
        }
        if(!raster.draw_triangle(clip,material,0x7ffd0000U,error))return false;
    }
    return true;
}
bool draw_frontend_road_sprites(Frame& target,const RenderOptions& options,
    const CameraFrame& camera,const std::vector<RecordedDraw>& draws,unsigned view,
    const PreparedCarScene& sprites,std::string& error) {
    const RenderLimits limits;
    RenderStats stats;Rasterizer raster(target,options,limits,stats);
    for(const auto& draw:draws)if(draw.frontend_sprite_view==view && draw.material.alpha[0]>0) {
        const unsigned descriptor=draw.frontend_sprite_kind==1?1:draw.frontend_sprite_kind==2?2:0;
        const auto& source=sprites.draws.at(descriptor);
        RasterMaterial material;material.texture=&sprites.textures.at(source.texture);material.blend=RenderBlendMode::additive;
        material.depth_write=false;material.linear_filter=true;
        // Authenticated world.efx multiplies all RGBA lanes by two.
        material.ordinary_world=draw.frontend_sprite_kind!=0;
        material.alpha_scale=draw.material.alpha[0]*(material.ordinary_world?2:1);
        ClipVertex corners[4];const int signs[4][2]={{-1,1},{1,1},{1,-1},{-1,-1}};
        for(unsigned i=0;i<4;++i) {
            float raw[3];for(unsigned axis=0;axis<3;++axis)raw[axis]=draw.placement.rows[12+axis]+
                signs[i][0]*draw.placement.rows[axis]+signs[i][1]*draw.placement.rows[8+axis];
            corners[i]=project_point(camera,{raw[0],raw[2],-raw[1]});
            corners[i].u=i==0 || i==3?0:1;corners[i].v=i<2?0:1;
            corners[i].r=draw.material.environment_range[3]>1.5F?draw.material.environment_color[material.ordinary_world?2:0]:110;
            corners[i].g=draw.material.environment_range[3]>1.5F?draw.material.environment_color[1]:100;
            corners[i].b=draw.material.environment_range[3]>1.5F?draw.material.environment_color[material.ordinary_world?0:2]:75;
        }
        const ClipVertex a[3]={corners[0],corners[1],corners[2]},b[3]={corners[0],corners[2],corners[3]};
        if(!raster.draw_triangle(a,material,0x7FFE0000U,error) ||
            !raster.draw_triangle(b,material,0x7FFE0001U,error))return false;
    }
    return true;
}
}
struct FrontendSceneEnvironment {
    PreparedCarScene tunnel,capture,floor,sprites;
    std::vector<MeshMarker> road_markers;
};
struct FrontendGpuScene::Impl {
    std::shared_ptr<const FrontendSceneEnvironment> environment;
    std::shared_ptr<const FrontendSceneCarSource> car_source;
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
    CarNeonAsset neon;
    std::size_t ground_draw_count=1;
    std::size_t car_draw_count=0;
    RecordedCommands commands;
    std::size_t road_groups=0;
    std::vector<DrawInstanceUniform> base_car_placements;
    std::uint16_t wheel_phase=0;
    std::uint32_t effect_ticks=0;
    float wheel_radius=0;
    float car_root_height=0;
    std::vector<std::unique_ptr<Impl>> alternatives;
    std::vector<std::vector<RecordedDraw>> car_templates;
    std::vector<const Impl*> car_template_owners;
    std::vector<std::vector<unsigned char>> paint_payloads;
    std::vector<RecordedDraw> road_templates,road_sprite_templates;
    std::size_t selected=0,requested=0,capacity=0;
    std::vector<std::string> car_types;
    unsigned transition=0;
    bool car_preparing=false;
    float transition_time=0,travel=0,travel_delta=0;
    FrontendBrowserCamera browser_camera;
    unsigned focus_slot=~0U;
    CameraFrame browser_frame;
    bool ready=false;
};
FrontendGpuScene::FrontendGpuScene():impl_(std::make_unique<Impl>()){}
FrontendGpuScene::~FrontendGpuScene()=default;
bool FrontendGpuScene::load(const std::filesystem::path& root,unsigned width,unsigned height,std::string& error,const std::string& stock_car,const CareerRideSetup* customized) {
    const bool loaded=load_cached(root,width,height,error,stock_car,customized,{},false);
    if(loaded){impl_->car.cached_geometry=nullptr;impl_->car_source.reset();}
    return loaded;
}
std::shared_ptr<const FrontendSceneEnvironment> FrontendGpuScene::environment() const {
    return impl_->environment;
}
std::shared_ptr<const FrontendSceneCarSource> FrontendGpuScene::car_source() const {return impl_->car_source;}
void FrontendGpuScene::release_preparation_cache() {impl_->car_source.reset();}
bool FrontendGpuScene::load_cached(const std::filesystem::path& root,unsigned width,unsigned height,
    std::string& error,const std::string& stock_car,const CareerRideSetup* customized,
    std::shared_ptr<const FrontendSceneEnvironment> environment,bool vehicle_only,
    std::shared_ptr<const FrontendSceneCarSource> source,StockFramePaintCache* paint_cache) {
    // The immutable room is shared across background jobs. Per-car geometry,
    // shadows, neon, skin and draw constants keep their own lifetimes.
    auto next=std::make_unique<Impl>();auto& p=*next;p.car_source=std::move(source);
    p.car_types={stock_car.empty()?"SKYLINE":stock_car};
    if(environment) {
        p.environment=std::move(environment);
        if(!load_scene_car(root,width,height,p.car,p.options,error,&p.shadow,stock_car,customized,&p.neon,nullptr,&p.car_source,paint_cache))return false;
    }else {
        auto room=std::make_shared<FrontendSceneEnvironment>();
        if(!load_scene_assets(root,width,height,room->tunnel,p.car,p.options,error,&p.shadow,&room->capture,&room->floor,&room->sprites,&room->road_markers,stock_car,customized,&p.neon,&p.car_source))return false;
        p.environment=std::move(room);
    }
    // Original solid lookup 0x53F9B0 requests missing->DEFAULTALPHA through
    // 0x5461C0. The tunnel's zero replacement table bypasses 0x53F540;
    // 0x40C910 skips DEFAULTALPHA at 0x40CA0C, before reflection uniforms.
    // Only the authenticated absent wall-strip hash receives that policy.
    // Keep its authored geometry resident; other missing assets still fail.
    std::set<std::size_t> native_empty;
    if(!vehicle_only)for(std::size_t group=0;group<p.environment->tunnel.draws.size();++group) {
        const auto& draw=p.environment->tunnel.draws[group];
        if(draw.binding==BindingKind::unresolved && draw.texture_hash==0xC2456B43U) {
            const auto& name=p.environment->tunnel.meshes.at(draw.mesh).name;
            if(name!="TRFE_ROAD02_04_A" && name!="TRFE_ROAD02_07_A") {
                error="native empty tunnel material outside authenticated meshes";return false;
            }
            native_empty.insert(group);
        }
    }
    for(const auto* scene:std::array<const PreparedCarScene*,2>{&p.environment->tunnel,&p.car.scene})for(const auto& draw:scene->draws)
        if(draw.drawable && draw.binding!=BindingKind::resolved &&
           !(scene==&p.environment->tunnel && draw.binding==BindingKind::unresolved && draw.texture_hash==0xC2456B43U)) {
            std::ostringstream message;message<<"original frontend texture binding unresolved: 0x"<<std::hex<<draw.texture_hash;
            error=message.str();return false;
        }
    RecordedCommands road,car;
    if(!record_car_commands(p.car.scene,p.options,RenderLimits{},p.car.skinned?p.car.skin_binding.source:kNoHandle,car,error))return false;
    if(vehicle_only) {
        // Preserve the full room+car assembly's diagnostic draw identity.
        for(auto& draw:car.owning_draws())draw.name="race-car/"+draw.name;
        p.commands=std::move(car);
    }else {
        if(!record_car_commands(p.environment->tunnel,p.options,RenderLimits{},kNoHandle,road,error))return false;
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
    }
    const auto& type=p.car.car_types.records.at(p.car.report.car_type);
    p.wheel_radius=type.wheels[0].rolling_radius;
    float front_wheel_rise=0.F;
    if(customized&&customized->performance.suspension>0) {
        const auto& globals=p.car_source->inputs.file(StockFrameFile::global_bun).bytes;
        const auto level=static_cast<std::size_t>(customized->performance.suspension);
        VehicleTuning tuning;
        std::vector<VehicleTuningIssue> issues;
        if(level>kVehicleTuningLevels||type.offset>globals.size()||
           kVehicleRecordSize>globals.size()-type.offset||
           !decode_vehicle_tuning(globals.data()+type.offset,kVehicleRecordSize,tuning,issues)) {
            error="garage suspension setup unavailable";
            return false;
        }
        const auto& suspension=tuning.suspension[level-1];
        front_wheel_rise=suspension.front_wheel_vertical_rise;
        for(auto& instance:p.car.scene.instances) {
            if(instance.part_slot!=kSlotWheel&&instance.part_slot!=kSlotBrake)continue;
            const float rise=instance.copy<2?front_wheel_rise:suspension.rear_wheel_vertical_rise;
            instance.animation_pivot[2]+=rise;
        }
        for(std::size_t i=p.road_groups*5;i<p.commands.owning_draws().size();++i) {
            auto& draw=p.commands.owning_draws()[i];
            const auto& instance=p.car.scene.instances.at(draw.instance);
            if(instance.part_slot==kSlotWheel||instance.part_slot==kSlotBrake)
                draw.placement.rows[14]+=instance.copy<2?front_wheel_rise:suspension.rear_wheel_vertical_rise;
        }
    }
    // Original 0x4B17F5..0x4B1825 places the whole FE car above its
    // wheel-0 contact plane. Part matrices below remain car-local.
    p.car_root_height=p.wheel_radius-(type.wheels[0].vertical+front_wheel_rise)+.02F;
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
    if(p.neon.enabled) {
        RecordedCommands neon,combined;shadow_placement.strength=1;
        if(!record_car_neon_commands(p.neon,0,shadow_placement,p.options,neon,error)||
           !combine_race_commands(std::move(p.commands),neon,combined,error))return false;
        p.commands=std::move(combined);p.ground_draw_count=2;
    }
    auto sprite_options=p.options;sprite_options.car_shading=false;
    if(!vehicle_only) {
    auto capture_options=p.options;capture_options.car_shading=false;
    RecordedCommands cube_commands;
    if(!record_car_commands(p.environment->capture,capture_options,RenderLimits{},kNoHandle,cube_commands,error))return false;
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
    if(!record_car_commands(p.environment->floor,capture_options,RenderLimits{},kNoHandle,floor_commands,error))return false;
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
    for(std::size_t mesh=0;mesh<p.environment->tunnel.meshes.size();++mesh)
        if(p.environment->tunnel.meshes[mesh].name=="TRFE_ROAD02_A")p.floor_mesh=mesh;
    if(p.floor_mesh==kNoHandle){error="native worldreflect floor absent";return false;}
    for(std::size_t i=0;i<p.road_groups*5;++i)if(complete[i].mesh==p.floor_mesh)
        complete[i].material.environment_min[3]=1;
    p.commands.frontend_light=p.lighting;p.commands.frontend_light_enabled=true;
    RecordedCommands sprite_commands;
    if(!record_car_commands(p.environment->sprites,sprite_options,RenderLimits{},kNoHandle,sprite_commands,error))return false;
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
    }
    p.commands.frontend_light=p.lighting;p.commands.frontend_light_enabled=true;
    for(auto& draw:p.commands.owning_draws())if(draw.material.surface.gates[0]!=0) {
        for(const auto& program:shader_manifest::kPrograms)
            if(std::string(program.name)=="frontend_car_textured")draw.program=&program;
    }
    // FE RenderCar wrapper56A040 constructs56A130 with null owner.
    // Original56D8D0 therefore selects head1 and resting tail6B5DA0=.7.
    // Its type3 owner branch is a separate state, not an FE-name default.
    for(const auto& marker:p.car.report.markers) {
        const bool head=marker.name_hash==0x9DB90133U || marker.name_hash==0xD09091C6U;
        const bool tail=marker.name_hash==0x31A66786U || marker.name_hash==0xA2A2FC7CU || marker.name_hash==0xBF700A79U;
        if(!head && !tail)continue;
        const auto& geometry=marker.geometry_source==0?p.car.car_geometry:p.car.geometry_sources().at(marker.geometry_source-1);
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
    if(!record_car_commands(p.environment->sprites,sprite_options,RenderLimits{},kNoHandle,car_sprites,error))return false;
    const auto templates=car_sprites.owning_draws();car_sprites.owning_draws().clear();
    for(unsigned i=0;i<p.car_glows.size();++i) {
        const unsigned descriptor=p.car_glows[i].descriptor;
        auto draw=templates.at(descriptor==0?1:descriptor==1?2:0);
        draw.name="_FE_CAR_GLOW"+std::to_string(descriptor);draw.instance=i;draw.frontend_sprite_view=1;draw.frontend_sprite_kind=descriptor+1;
        for(std::size_t variant=0;variant<draw.program->variant_count;++variant)
            if(std::string(draw.program->variants[variant].name)=="additive")draw.pipeline=draw.program->variants[variant];
        draw.pipeline.depth_write=false;draw.material.environment_range[3]=2;draw.material.alpha[3]=8;
        car_sprites.owning_draws().push_back(draw);
    }
    p.car_sprite_start=p.commands.owning_draws().size();
    RecordedCommands combined_car_sprites;
    if(!combine_race_commands(std::move(p.commands),car_sprites,combined_car_sprites,error))return false;
    p.commands=std::move(combined_car_sprites);
    p.commands.frontend_light=p.lighting;p.commands.frontend_light_enabled=true;
    p.ready=true;impl_.swap(next);return vehicle_only?true:frame(0,0,error);
}
bool FrontendGpuScene::load_browser(const std::filesystem::path& root,unsigned width,unsigned height,
    const std::vector<std::string>& cars,std::string& error) {
    return load_choices(root,width,height,cars,nullptr,error);
}
bool FrontendGpuScene::load_owned_ride(const std::filesystem::path& root,unsigned width,unsigned height,
    const std::vector<unsigned char>& globals,const CareerCatalog& catalog,const CareerProfile& profile,
    std::string& error,std::shared_ptr<const FrontendSceneEnvironment> environment) {
    std::vector<CareerRideSetup> variants(1);
    return resolve_career_ride(globals,catalog,profile,variants.front(),error)&&
        load_garage(root,width,height,variants,error,nullptr,std::move(environment));
}
bool FrontendGpuScene::load_garage(const std::filesystem::path& root,unsigned width,unsigned height,
    const std::vector<CareerRideSetup>& variants,std::string& error,FrontendGpuScene* previous,
    std::shared_ptr<const FrontendSceneEnvironment> environment,
    std::shared_ptr<const FrontendSceneCarSource> car_source) {
    if(previous==this){error="garage previous owner aliases current scene";return false;}
    std::vector<std::string> cars;
    for(const auto& variant:variants)cars.push_back(variant.request.car_type_name);
    return load_choices(root,width,height,cars,&variants,error,previous,std::move(environment),std::move(car_source));
}
bool FrontendGpuScene::load_choices(const std::filesystem::path& root,unsigned width,unsigned height,
    const std::vector<std::string>& cars,const std::vector<CareerRideSetup>* variants,std::string& error,FrontendGpuScene* previous,
    std::shared_ptr<const FrontendSceneEnvironment> environment,
    std::shared_ptr<const FrontendSceneCarSource> car_source) {
    if(cars.empty()){error="empty frontend car roster";return false;}
    FrontendGpuScene prepared;
    if(!prepared.load_cached(root,width,height,error,cars.front(),variants?&variants->front():nullptr,std::move(environment),false,std::move(car_source)))return false;
    auto& p=*prepared.impl_;p.car_types=cars;
    const auto initial=p.commands.owning_draws();
    p.road_templates.assign(initial.begin(),initial.begin()+p.road_groups*5);
    p.road_sprite_templates.assign(initial.begin()+p.sprite_start,initial.begin()+p.car_sprite_start);
    const auto car_draws=[](const Impl& v) {
        const auto& all=v.commands.owning_draws();
        std::vector<RecordedDraw> draws(all.begin()+v.road_groups*5,
            all.begin()+v.road_groups*5+v.car_draw_count+v.ground_draw_count);
        draws.insert(draws.end(),all.begin()+v.car_sprite_start,all.end());
        return draws;
    };
    const auto source=prepared.impl_->car_source;
    const auto release_sources=[](Impl& v) {
        // Upload pointers and frame updates borrow only the prepared scene,
        // ground assets and flare quads. Drop whole-file/parser/skin copies.
        v.car.cached_geometry=nullptr;
        v.car.car_geometry={};std::vector<GeometryReport>().swap(v.car.stock_geometry);
        v.car.parts={};v.car.car_types={};v.car.car_pack={};v.car.vinyl_pack={};
        std::vector<TexturePackSet>().swap(v.car.shared_sets);std::vector<std::uint32_t>().swap(v.car.wanted);
        v.car.textures={};v.car.skin={};v.car.skin_plan={};v.car.skin_binding={};v.car.report={};
    };
    p.car_templates.push_back(car_draws(p));p.car_template_owners.push_back(&p);release_sources(p);
    const unsigned paint_slot=variants&&variants->size()>1?variants->at(1).garage_paint_slot:0;
    StockFramePaintCache paint;paint.slot=paint_slot;
    // Match metadata before comparing payloads. Most Trade In resources
    // have no equivalent elsewhere in the bank.
    const auto resource_key=[](const UploadResource& resource) {
        return std::make_tuple(resource.kind,resource.bytes,resource.element_bytes,
            resource.elements,resource.texture_hash,resource.texture.width,
            resource.texture.height,resource.texture.format,resource.texture.mip_count);
    };
    std::multimap<decltype(resource_key(UploadResource{})),std::size_t> resource_index;
    const auto& base_resources=p.commands.resource_table().resources;
    for(std::size_t i=0;i<base_resources.size();++i)
        resource_index.emplace(resource_key(base_resources[i]),i);
    for(std::size_t i=1;i<cars.size();++i) {
        if(paint_slot&&i>1) {
            const auto& ride=variants->at(i);
            const auto& first=variants->at(1);
            // Only the active paint record can vary within this palette.
            auto records=ride.request.installed_parts.records;
            records[paint_slot]=first.request.installed_parts.records[paint_slot];
            if(ride.garage_paint_slot!=paint_slot||cars[i]!=cars[1]||
               records!=first.request.installed_parts.records) {
                error="garage palette changes more than one installed color";
                return false;
            }
            auto& model=*p.alternatives.front();
            auto draws=p.car_templates[1];
            std::vector<unsigned char> rgba;
            if(!prepare_stock_paint_variant(source->inputs,model.car,paint,
                ride.request.installed_parts,ride.skin,rgba,error))return false;
            auto& uploads=p.commands.owning_uploads();
            const bool recolored=!rgba.empty();
            std::size_t replacement=kNoHandle;
            for(std::size_t j=0;j<model.car_draw_count;++j) {
                auto& draw=draws[j];
                const auto& group=model.car.scene.draws.at(draw.group);
                if(paint_slot!=37&&paint_slot<42&&draw.material.surface.gates[0]!=0)
                    draw.material.surface=group.shading;
                if(!recolored||draw.texture_resource==kNoHandle||
                   uploads.resources[draw.texture_resource].texture_hash!=paint.texture_hash)continue;
                if(replacement==kNoHandle) {
                    auto resource=uploads.resources[draw.texture_resource];
                    if(resource.bytes!=rgba.size()||resource.texture.format!=TextureUploadFormat::rgba8) {
                        error="garage paint texture shape changed";
                        return false;
                    }
                    if(!std::memcmp(resource.data,rgba.data(),rgba.size()))replacement=draw.texture_resource;
                    else {
                        p.paint_payloads.push_back(std::move(rgba));
                        resource.data=p.paint_payloads.back().data();
                        resource.name="paint/"+std::to_string(i)+"/"+resource.name;
                        replacement=uploads.resources.size();
                        uploads.resources.push_back(std::move(resource));
                        auto& totals=uploads.totals;
                        ++totals.resources;++totals.textures;totals.texture_bytes+=p.paint_payloads.back().size();
                        if(uploads.resources.back().lifetime==UploadLifetime::generated) {
                            ++totals.generated_textures;totals.generated_texture_bytes+=p.paint_payloads.back().size();
                        }
                    }
                }
                draw.texture_resource=replacement;
            }
            p.car_templates.push_back(std::move(draws));p.car_template_owners.push_back(&model);
            continue;
        }
        FrontendGpuScene other;
        if(!other.load_cached(root,width,height,error,cars[i],variants?&variants->at(i):nullptr,p.environment,true,source,
            paint_slot?&paint:nullptr))return false;
        auto& v=*other.impl_;
        auto draws=car_draws(v);
        // Retain only referenced car/shadow/flare resources. The garage,
        // reflection captures and light sprites have one resident copy.
        std::map<std::size_t,std::size_t> remap;
        for(auto& d:draws)for(auto* slot:{&d.vertex_resource,&d.index_resource,&d.texture_resource}) {
            if(*slot==kNoHandle)continue;
            auto found=remap.find(*slot);
            if(found==remap.end()) {
                auto& resources=p.commands.owning_uploads().resources;
                const auto& incoming=v.commands.resource_table().resources.at(*slot);
                const auto candidates=resource_index.equal_range(resource_key(incoming));
                const auto same=std::find_if(candidates.first,candidates.second,[&](const auto& entry) {
                    return !std::memcmp(resources[entry.second].data,incoming.data,std::size_t(incoming.bytes));
                });
                if(same!=candidates.second) {
                    *slot=remap.emplace(*slot,same->second).first->second;
                    continue;
                }
                const auto next=resources.size();
                auto resource=incoming;
                resource.name="browser/"+std::to_string(i)+"/"+resource.name;
                auto& totals=p.commands.owning_uploads().totals;++totals.resources;
                if(resource.kind==UploadResourceKind::vertex_buffer){++totals.vertex_buffers;totals.vertex_bytes+=resource.bytes;}
                else if(resource.kind==UploadResourceKind::index_buffer){++totals.index_buffers;totals.index_bytes+=resource.bytes;}
                else {++totals.textures;totals.texture_bytes+=resource.bytes;
                    totals.largest_texture_bytes=std::max(totals.largest_texture_bytes,resource.bytes);
                    if(resource.lifetime==UploadLifetime::generated){++totals.generated_textures;totals.generated_texture_bytes+=resource.bytes;}}
                p.commands.owning_uploads().resources.push_back(std::move(resource));
                resource_index.emplace(resource_key(incoming),next);
                found=remap.emplace(*slot,next).first;
            }
            *slot=found->second;
        }
        p.car_templates.push_back(std::move(draws));p.car_template_owners.push_back(&v);
        v.commands={};
        if(!paint_slot)release_sources(v);
        v.car_source.reset();
        p.alternatives.push_back(std::move(other.impl_));
    }
    if(paint_slot&&!p.alternatives.empty())release_sources(*p.alternatives.front());
    for(const auto& list:p.car_templates)p.capacity=std::max(p.capacity,
        p.road_templates.size()+p.road_sprite_templates.size()+list.size());
    if(variants&&impl_->ready) {
        p.road=impl_->road;p.wheel_phase=impl_->wheel_phase;p.browser_camera=impl_->browser_camera;p.focus_slot=impl_->focus_slot;
    }
    impl_.swap(prepared.impl_);
    if(previous)previous->impl_.swap(prepared.impl_);
    return frame(0,0,error);
}
bool FrontendGpuScene::adopt_garage(FrontendGpuScene& prepared,std::string& error,std::size_t selection) {
    if(&prepared==this||!prepared.impl_->ready||selection>=prepared.impl_->car_templates.size()) {
        error="garage publication needs a separate prepared component bank";return false;
    }
    auto& incoming=*prepared.impl_;
    if(impl_->ready) {
        incoming.road=impl_->road;incoming.wheel_phase=impl_->wheel_phase;
        incoming.browser_camera=impl_->browser_camera;incoming.focus_slot=impl_->focus_slot;
        incoming.effect_ticks=impl_->effect_ticks;
        incoming.car_preparing=impl_->car_preparing;
    }
    incoming.selected=incoming.requested=selection;
    incoming.transition=0;incoming.travel=incoming.travel_delta=0;
    impl_.swap(prepared.impl_);return frame(0,impl_->effect_ticks,error);
}
bool FrontendGpuScene::select_part(std::size_t index,std::string& error) {
    auto& p=*impl_;
    if(index>=p.car_templates.size()){error="garage part outside resident bank";return false;}
    p.selected=p.requested=index;p.transition=0;p.car_preparing=false;p.travel=p.travel_delta=0;
    error.clear();return true;
}
bool FrontendGpuScene::select_car(std::size_t index,std::string& error) {
    auto& p=*impl_;
    if(index>=p.car_templates.size()){error="frontend car selection outside resident roster";return false;}
    p.requested=index;
    if(!p.transition && index!=p.selected) {
        prepare_car_change();
        p.transition=3;p.transition_time=0;p.car_preparing=false;
    }
    error.clear();return true;
}
void FrontendGpuScene::prepare_car_change() {
    auto& p=*impl_;
    if(p.ready&&!p.transition&&!p.car_preparing) {
        p.browser_camera.prepare();
        p.car_preparing=true;
    }
}
void FrontendGpuScene::cancel_car_preparation() {
    auto& p=*impl_;
    if(!p.car_preparing)return;
    p.car_preparing=false;
    p.browser_camera.focus(p.focus_slot==~0U?0U:p.focus_slot);
}
void FrontendGpuScene::road_target(float kmh) {
    if(std::isfinite(kmh)&&kmh>=0)impl_->road.target_kmh=kmh;
}
void FrontendGpuScene::focus_part(unsigned slot) {
    auto& p=*impl_;if(!p.ready||p.focus_slot==slot)return;
    if(p.browser_camera.focus(slot))p.focus_slot=slot;
}
void FrontendGpuScene::magazine_camera(const std::array<float,11>& values) {
    auto& p=*impl_;p.focus_slot=0;p.transition=0;p.car_preparing=false;p.travel=0;
    //5136F0 ->4B0340 ->487B20 mode5, including roll and native axis order.
    p.browser_camera.magazine(values);
    p.commands.frontend_car_only=true;p.road.target_kmh=p.road.speed_kmh=0;p.road.velocity=0;
}
void FrontendGpuScene::orbit(float yaw,float pitch) {
    auto& p=*impl_;if(!p.ready||(p.car_templates.empty()&&p.focus_slot==~0U)||p.transition||p.car_preparing||!std::isfinite(yaw)||!std::isfinite(pitch)||
        (yaw==0.F&&pitch==0.F))return;
    p.browser_camera.orbit(yaw,pitch);
}
std::size_t FrontendGpuScene::draw_capacity() const {
    return impl_->capacity?impl_->capacity:impl_->commands.draw_count();
}
bool FrontendGpuScene::car_transitioning() const {return impl_->transition!=0;}
const std::string& FrontendGpuScene::selected_car_type() const {return impl_->car_types.at(impl_->selected);}
bool FrontendGpuScene::frame(float seconds,std::uint32_t ticks,std::string& error) {
    auto& p=*impl_;if(!p.ready){error="frontend GPU scene not loaded";return false;}
    p.effect_ticks=ticks;
    const float old_travel=p.travel;
    if(p.transition) {
        p.transition_time+=seconds;
        if(p.transition==3) {
            // Native 4B0436 requires elapsed to exceed the prepared camera duration.
            if(p.browser_camera.elapsed>p.browser_camera.duration) {
                p.transition=1;p.transition_time=0;p.browser_camera.depart();
            }
        } else if(p.transition==1) {
            // Original4B01C0/4B0459: 25m in1.5s from rest.
            p.travel=float((2.0*25.0/(1.5*1.5))*.5*p.transition_time*p.transition_time);
            if(p.transition_time>1.5F) {
                p.selected=p.requested;p.transition=2;p.transition_time=0;p.travel=-15;p.browser_camera.arrive();
            }
        } else {
            // Original4B0549:225..270 degrees, native fixed sine.
            if(p.transition_time>1.5F) {
                p.travel=0;p.transition=0;
                p.browser_camera.tracking=false;
                if(p.requested!=p.selected){p.transition=3;p.transition_time=0;p.browser_camera.prepare();}
            } else {
                const float degrees=225.F+(p.transition_time/1.5F)*45.F;
                const auto phase=static_cast<std::uint16_t>(static_cast<int>(degrees*(65536.F/360.F)));
                p.travel=-(frontend_fixed_sine(phase)+1.F)*3.4142136573791504F*15.F;
            }
        }
    }
    if(!p.car_templates.empty()||p.focus_slot!=~0U||p.car_preparing) {
        if(p.browser_camera.tracking||p.browser_camera.manual)p.browser_camera.update(seconds);
        p.browser_frame=p.browser_camera.frame(p.travel,p.options.width,p.options.height);
        // 40DD00 uses independent focal operands. The displayed FE view
        // keeps the authored height and derives X for square pixels. The
        // offscreen floor view below still uses its own projection.
        p.browser_frame.focal_x=p.browser_frame.focal_y*double(p.options.height)/double(p.options.width);
        p.options.explicit_camera=&p.browser_frame;
    }
    p.travel_delta=p.travel-old_travel;
    const auto& vehicle=p.car_template_owners.empty()?p:*p.car_template_owners.at(p.selected);
    if(!p.car_templates.empty()) {
        auto& all=p.commands.owning_draws();all=p.road_templates;
        const auto& car=p.car_templates.at(p.selected);
        all.insert(all.end(),car.begin(),car.begin()+vehicle.car_draw_count+vehicle.ground_draw_count);
        p.sprite_start=all.size();
        all.insert(all.end(),p.road_sprite_templates.begin(),p.road_sprite_templates.end());
        p.car_sprite_start=all.size();
        all.insert(all.end(),car.begin()+vehicle.car_draw_count+vehicle.ground_draw_count,car.end());
    }
    p.road.update(seconds,ticks);
    CameraFrame primary;
    if(p.options.explicit_camera)primary=*p.options.explicit_camera;
    else if(!make_camera_frame(p.options.camera,p.options.width,p.options.height,primary,error))return false;
    float marker_positions[4][3];
    for(unsigned marker=0;marker<4;++marker)for(unsigned c=0;c<3;++c)
        marker_positions[marker][c]=p.environment->road_markers[marker].matrix[12+c];
    const auto light_count=frontend_road_lights(p.road,marker_positions,p.road_lateral,
        p.road_rgb,p.road_attenuation,p.road_lights);
    const float car_position[3]={p.travel,0,vehicle.car_root_height};
    p.lighting=frontend_point_lighting(p.road_lights.data(),light_count,car_position,&primary);
    p.commands.frontend_light=p.lighting;
    std::array<FrontendRoadPlacement,5> placements;
    if(p.road.placements(1,placements)!=5){error="original frontend road view missing";return false;}
    auto& draws=p.commands.owning_draws();
    for(unsigned segment=0;segment<5;++segment)for(std::size_t group=0;group<p.road_groups;++group) {
        auto& rows=draws[segment*p.road_groups+group].placement.rows;
        rows[12]=placements[segment].longitudinal;rows[13]=0;rows[14]=placements[segment].vertical;
    }
    p.wheel_phase=frontend_wheel_phase(p.wheel_phase,p.road.delta+p.travel_delta,vehicle.wheel_radius);
    const float sine=frontend_fixed_sine(p.wheel_phase);
    const float cosine=frontend_fixed_sine(static_cast<std::uint16_t>(p.wheel_phase+0x4000));
    const float spin[16]={cosine,0,-sine,0,0,1,0,0,sine,0,cosine,0,0,0,0,1};
    for(std::size_t i=p.road_groups*5;i<p.road_groups*5+vehicle.car_draw_count;++i) {
        auto& draw=draws[i];const auto& instance=vehicle.car.scene.instances.at(draw.instance);
        if(instance.part_slot!=kSlotWheel) {
            draw.placement=vehicle.base_car_placements.at(i-p.road_groups*5);
            draw.placement.rows[12]+=p.travel;draw.placement.rows[14]+=vehicle.car_root_height;
            continue;
        }
        auto base=vehicle.base_car_placements.at(i-p.road_groups*5);
        for(unsigned c=0;c<3;++c)base.rows[12+c]-=instance.animation_pivot[c];
        DrawInstanceUniform rotated;
        for(unsigned r=0;r<4;++r)for(unsigned c=0;c<4;++c) {
            double value=0;
            for(unsigned k=0;k<4;++k)value+=double(base.rows[r*4+k])*spin[k*4+c];
            rotated.rows[r*4+c]=static_cast<float>(value);
        }
        for(unsigned c=0;c<3;++c)rotated.rows[12+c]+=instance.animation_pivot[c];
        rotated.rows[12]+=p.travel;rotated.rows[14]+=vehicle.car_root_height;
        draw.placement=rotated;
    }
    draws[p.road_groups*5+vehicle.car_draw_count].placement.rows[12]=p.travel;
    if(vehicle.neon.enabled) {
        CarShadowPlacement placement;placement.transform.rows[12]=p.travel;placement.strength=1;
        update_car_neon_draw(vehicle.neon.color,float(double(ticks)/4000),placement,
            draws[p.road_groups*5+vehicle.car_draw_count+1]);
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
    if(!p.car_templates.empty()) {
        const auto projection=p.browser_camera.frame(p.travel,320,240);
        p.floor_camera.focal_x=projection.focal_x;p.floor_camera.focal_y=projection.focal_y;
    }
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
        const auto& marker=p.environment->road_markers.at(car_sprite?0:draw.instance%4);
        const auto& copy=copies.at(car_sprite?0:draw.instance/4);
        float point[3];
        for(unsigned axis=0;axis<3;++axis)point[axis]=car_sprite?vehicle.car_glows.at(draw.instance).point[axis]:marker.matrix[12+axis];
        if(car_sprite){point[0]+=p.travel;point[2]+=vehicle.car_root_height;}
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
            dot+=toward_eye[axis]*double(vehicle.car_glows.at(draw.instance).normal[axis]);
        else if(mode==2)for(unsigned axis=0;axis<3;++axis)
            dot+=toward_eye[axis]*double(marker.matrix[8+axis])*(axis==2?-1:1);
        // Original40DCB0 uses main display width701034/2, not cube/floor capture width.
        const auto fov=static_cast<std::uint16_t>((mode==1?0x4000U:12463U)/2);
        const double denominator=(p.options.width/2)*double(frontend_fixed_sine(fov+0x4000U))/frontend_fixed_sine(fov);
        FrontendRoadGlow quad{};
        if(car_sprite) {
            const auto& glow=vehicle.car_glows.at(draw.instance);
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
    const auto& vehicle=p.car_template_owners.empty()?p:*p.car_template_owners.at(p.selected);
    auto tunnel=p.environment->tunnel;
    auto car=vehicle.car.scene;
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
    for(std::size_t i=p.road_groups*5;i<p.road_groups*5+vehicle.car_draw_count;++i) {
        const auto& draw=draws[i];
        std::copy_n(draw.placement.rows,16,car.instances.at(draw.instance).transform);
        if(p.selected&&p.car_template_owners.size()>p.alternatives.size()+1) {
            auto& group=car.draws.at(draw.group);
            group.shading=draw.material.surface;
            if(group.binding==BindingKind::resolved&&draw.texture_resource!=kNoHandle) {
                const auto& resource=p.commands.resource_table().resources.at(draw.texture_resource);
                auto& texture=car.textures.at(group.texture);
                if(resource.texture_hash==texture.hash&&resource.bytes==texture.rgba.size())
                    std::copy_n(resource.data,texture.rgba.size(),texture.rgba.begin());
            }
        }
    }
    auto capture=p.environment->capture;capture.profile=SelectionProfile::stock_car;
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
                face+10,p.environment->sprites,error))return false;
    }
    auto floor=p.environment->floor;floor.profile=SelectionProfile::stock_car;
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
            4,p.environment->sprites,error))return false;
    Frame output,car_frame;RenderStats stats;
    auto car_options=p.options;car_options.frontend_lighting=&p.lighting;car_options.frontend_cube=cube.data();
    if(p.commands.frontend_car_only){
        car_options.background[0]=car_options.background[1]=car_options.background[2]=car_options.background[3]=0;
        if(!render_scene(car,car_options,RenderLimits{},car_frame,stats,error))return false;
        rgba=car_frame.rgba();return true;
    }
    auto room_options=p.options;room_options.car_shading=false;
    room_options.frontend_floor=&floor_frame;room_options.frontend_floor_mesh=p.floor_mesh;
    if(!render_scene(tunnel,room_options,RenderLimits{},output,stats,error) ||
       !render_scene(car,car_options,RenderLimits{},car_frame,stats,error))return false;
    CarShadowPlacement shadow_placement;shadow_placement.transform.rows[12]=p.travel;shadow_placement.alpha=184.0F/255;
    if(!render_car_shadow(vehicle.shadow,shadow_placement,p.options,output,stats,error))return false;
    if(vehicle.neon.enabled&&!draw_frontend_neon(output,p.options,vehicle.neon,
        draws[p.road_groups*5+vehicle.car_draw_count+1],error))return false;
    Frame combined;
    if(!compose_race_frame(output,car_frame,combined)){error="FE diagnostic scene compose failed";return false;}
    CameraFrame primary;
    if(p.options.explicit_camera)primary=*p.options.explicit_camera;
    if((!p.options.explicit_camera && !make_camera_frame(p.options.camera,p.options.width,p.options.height,primary,error)) ||
        !draw_frontend_road_sprites(combined,p.options,primary,p.commands.owning_draws(),
            1,p.environment->sprites,error))return false;
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
