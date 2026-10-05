#include "nfsu/frontend.h"
#include "nfsu/frontend_scene.h"
#include "nfsu/track.h"
#include "nfsu/stock_frame.h"
#include "nfsu/frontend_motion.h"
#include "nfsu/frontend_preset.h"
#include "nfsu/race_events.h"
#include "nfsu/sha256.h"
#include <cmath>
#include "nfsu/frontend_music.h"
#include <algorithm>
#include "nfsu/race_session.h"
#include "nfsu/race_run.h"
#include "nfsu/race_gpu_scene.h"
#include "nfsu/posix_file_system.h"
#include <iostream>
#include <cstring>
#include <stdexcept>
constexpr float native_floor_sprite[4][3]={{-6.0781331062316895F,-1.393865942955017F,-26.813892364501953F},
                {-8.132206916809082F,.6465035676956177F,-23.452316284179688F},
                {-7.844092845916748F,.4596937894821167F,5.491459846496582F},
                {-5.790019512176514F,-1.580675721168518F,2.1298818588256836F}};
constexpr float native_floor_sprite1280[4][3]={
    {-6.409250259399414F,-1.0463234186172485F,-20.75663948059082F},
    {-7.693046569824219F,.22890740633010864F,-18.65565299987793F},
    {-7.512975692749023F,.11215132474899292F,-.5657939910888672F},
    {-6.229179382324219F,-1.1630796194076538F,-2.666780471801758F}};
void check(bool value,const char* message) { if(!value) throw std::runtime_error(message); }
int main(int argc,char** argv) {
    using namespace nfsu;
    try {
        {
            Frame reflected,output;RenderLimits limits;std::string error;
            const unsigned char reflected_color[4]={100,80,60,255};
            const unsigned char black[4]={0,0,0,255};
            check(reflected.reset(1,1,reflected_color,limits,error),"synthetic original floor target");
            check(output.reset(8,8,black,limits,error),"floor equation target");
            RenderTexture texture;texture.width=texture.height=1;texture.valid=true;
            texture.rgba={128,64,32,128};RasterMaterial state;state.texture=&texture;
            state.frontend_floor=true;state.reflected_scene=&reflected;
            state.alpha_test=true;state.alpha_ref=170;
            RenderOptions options;options.vertex_color=true;RenderStats stats;
            Rasterizer raster(output,options,limits,stats);
            ClipVertex triangle[3];triangle[0].x=-1;triangle[0].y=-1;
            triangle[1].x=1;triangle[1].y=-1;triangle[2].x=-1;triangle[2].y=1;
            for(auto& vertex:triangle) {vertex.z=.5;vertex.r=64;vertex.g=128;vertex.b=192;vertex.a=32;}
            check(raster.draw_triangle(triangle,state,0,error),"actual-source native floor equation");
            check(stats.fragments_written!=0,"native floor pixelalpha1 passes alpha test despite diffusealpha128");
            // Independent worldreflect.efx arithmetic, including original
            // vertex mask32 (undoubled) and final UNORM rounding.
            const unsigned char native_pixel[4]={71,69,52,255};
            for(std::size_t i=0;i<output.ids().size();++i)if(output.ids()[i]!=kNoTriangle)
                for(unsigned channel=0;channel<4;++channel)
                    check(output.rgba()[i*4+channel]==native_pixel[channel],
                        "native float floor modulation and independent reflection-alpha mask");
        }
        // Independent original 542C20/544390 witness: actual mode-0 CView
        // and whole-car root. Shader expectations below follow authenticated
        // car.efx equations, with supplied startup-light boundary state.
        const double raw_eye[3]={2.7437283992767334,1.8576174974441528,.7651561498641968};
        float placement[16]={1,0,0,0,0,1,0,0,0,0,1,0,0,0,.20027998089790344F,1};
        float eye[4];frontend_local_eye(placement,raw_eye,eye);
        const double native_eye[4]={2.269277334213257,1.536394476890564,.46719667315483093,1};
        for(unsigned i=0;i<4;++i)check(std::abs(eye[i]-native_eye[i])<1e-6,"native FE clamped LocalEyePos");
        CarShadingMaterial material;material.gates[1]=1;
        for(unsigned i=0;i<3;++i) {
            material.diffuse_min[i]=.2F;material.diffuse_range[i]=.4F;
            material.specular_min[i]=.3F;material.specular_range[i]=.5F;
        }
        material.diffuse_min[3]=.6F;material.diffuse_range[3]=.2F;
        material.specular_min[3]=2;
        const float position[3]={0,0,0};
        const float normals[3][3]={{0,0,1},{0,0,-1},{2,0,0}};
        const double expected[3][3]={
            {.16250151481665143,.00020057602170786372,.6336112707115158},
            {-.009559216976165772,5.485728244575609e-10,.602},
            {.5759352854850635,.14024445869762775,.9265147171733882}};
        const auto lighting=frontend_startup_lighting();
        for(unsigned n=0;n<3;++n) {
            double surface[7];frontend_surface(lighting,material,position,normals[n],eye,surface);
            for(unsigned i=0;i<3;++i) {
                check(std::abs(surface[i]-expected[n][0])<1e-6,"native harmonic diffuse without normal flip");
                check(std::abs(surface[i+3]-expected[n][1])<1e-6,"native harmonic specular without upper dot clamp");
            }
            check(std::abs(surface[6]-expected[n][2])<1e-6,"native FE diffuse alpha remains separate");
        }
        const double cube_directions[6][3]={{1,.25,.5},{-1,.25,.5},{.2,1,.6},{.2,-1,.6},{.2,.4,1},{.2,.4,-1}};
        // Asymmetric projected markers from original40AD60 native face bases,
        // not the appearance of a symmetric tunnel. Native Y faces differ.
        const double cube_uv[6][2]={{.25,.375},{.75,.375},{.6,.8},{.6,.2},{.6,.3},{.4,.3}};
        for(unsigned expected_face=0;expected_face<6;++expected_face) {
            unsigned face;double u,v;
            check(frontend_cube_coordinates(cube_directions[expected_face],face,u,v) && face==expected_face &&
                std::abs(u-cube_uv[expected_face][0])<1e-9 && std::abs(v-cube_uv[expected_face][1])<1e-9,
                "native asymmetric cube marker handedness and orientation");
            for(unsigned corner=0;corner<4;++corner) {
                double edge[3]{};unsigned component=0;
                for(unsigned axis=0;axis<3;++axis)if(axis!=expected_face/2)
                    edge[axis]=(corner&(1U<<component++))?1.:-1.;
                edge[expected_face/2]=expected_face&1?-1.000001:1.000001;
                check(frontend_cube_coordinates(edge,face,u,v) && face==expected_face,
                    "native cube edge keeps intended face");
                for(double coordinate:{u,v}) {
                    const double pixel=coordinate*128-.5;
                    const auto lo=static_cast<unsigned>(std::floor(pixel));
                    const auto hi=std::min(lo+1,127U);
                    check(pixel>=0 && pixel<=127 && lo<=127 && hi<=127,
                        "all bilinear atlas edge taps stay inside original128-square face");
                    check(coordinate==.5/128 || coordinate==127.5/128,
                        "face corners use native edge texel clamp instead of adjacent atlas tile");
                }
            }
        }
        audio::Bank bank;std::vector<audio::Segment> segments;
        for(unsigned i=0;i<27;++i) {
            bank.entries.push_back({1,i*100,0});
            audio::Segment seg;seg.offset=i*100;seg.frames=i+1;segments.push_back(seg);
        }
        // AST physical order is independent of ABK raw song IDs.
        std::reverse(segments.begin(),segments.end());
        const auto fe=audio::original_music_playlist(bank,segments,true);
        const auto ig=audio::original_music_playlist(bank,segments,false);
        check(fe.size()==7 && fe.front().offset==2000 && fe.back().offset==2600,
            "native FE mask excludes preamble and IG songs");
        check(ig.size()==19 && ig.front().offset==100 && ig.back().offset==1900,
            "native IG mask excludes FE songs");
        check(std::string(audio::original_menu_song(0).title)=="Get Low",
            "fresh-profile song metadata");
        segments.pop_back();bool rejected=false;
        try {audio::original_music_playlist(bank,segments,true);} catch(const std::exception&) {rejected=true;}
        check(rejected,"missing AST bank boundary rejected");
        // Values come from bounded execution of retail 0x4AE1F0 and
        // 0x4AE530, with ready state, 50 km/h and a 1/60-second frame.
        {
            // Independent genuine5443F0 stack quad, actual view4 road marker.
            const float point[3]={-9.519950866699219F,-1.0800316333770752F,-13.673964500427246F};
            const float eye_direction[3]={.63970947265625F,.15323638916015625F,.753186821937561F};
            const float right[3]={-.7047351002693176F,.7000333666801453F,.11533286422491074F};
            const float up[3]={-.0988493338227272F,.06409282982349396F,-.9930362105369568F};
            FrontendRoadGlow quad;
            check(frontend_road_glow(point,eye_direction,right,up,.7531868815422058F,
                .017617058008909225F,2,quad),"native floor streetlamp billboard");
            for(unsigned i=0;i<4;++i)for(unsigned axis=0;axis<3;++axis)
                check(std::abs(quad.corners[i][axis]-native_floor_sprite[i][axis])<.000004F,
                    "all native floor sprite corners and vertical stretch");
            check(quad.rgba[0]==110 && quad.rgba[1]==100 && quad.rgba[2]==75 &&
                quad.rgba[3]==140,"native descriptor interpolation and truncation");
            check(!frontend_road_glow(point,eye_direction,right,up,1,1,3,quad),
                "unknown sprite view refuses publication");
        }
        check(frontend_wheel_phase(0,.231441572F,.31F)==7787 &&
              frontend_wheel_phase(0,.229F,.31F)==7705 &&
              frontend_wheel_phase(0,1.F,.31F)==33646,
              "native wheel phase uses original constants and truncation");
        check(frontend_wheel_phase(65000,1.F,.31F)==33110,
              "native wheel phase wraps 16-bit accumulator");
        const float native_velocity[]={13.6864938736F,13.6265792847F,14.0879993439F,12.9644823074F};
        const unsigned native_seconds[]={0,1,10,30};
        for(unsigned i=0;i<4;++i) {
            FrontendRoad road;road.offset=199.9F;
            road.update(1.0F/60,native_seconds[i]*4000);
            check(std::abs(road.velocity-native_velocity[i])<0.000002F,"native road speed modulation");
            check(road.offset>=0 && road.offset<1,"native road wraps rather than discontinuously resetting");
        }
        FrontendRoad road;road.offset=200;
        road.update(0,0,true);
        check(road.offset==200,"native road preserves exact wrap boundary");
        std::array<FrontendRoadPlacement,5> placements;
        check(road.placements(1,placements)==5 && placements[0].group==0 &&
            placements[0].longitudinal==200 && placements[1].group==1 &&
            placements[4].longitudinal==-200,"native repeated road placement and parity");
        check(road.placements(4,placements)==3 && placements[0].family==1 &&
            road.placements(11,placements)==3 && placements[0].family==2 &&
            road.placements(0,placements)==0,"native view selects road families and extent");
        road.speed_kmh=10;road.target_kmh=20;
        road.update(0.25F,0,true);
        check(road.speed_kmh==12.5F && std::abs(road.velocity-13.88888931F)<0.000002F,
            "forced road speed preserves native acceleration state");
        FrontendMenu menu;
        check(menu.update({false,false,true,false})==FrontendAction::underground,"initial Underground selection");
        check(menu.update({false,true,false,false})==FrontendAction::none,"navigation has no launch side effect");
        check(menu.update({false,false,true,false})==FrontendAction::free_drive,"free driving selection");
        check(menu.update({true,true,false,false})==FrontendAction::none && menu.selected()==1,"conflicting navigation stable");
        check(menu.update({true,false,false,false})==FrontendAction::none && menu.selected()==0,"selection wraps");
        check(menu.update({false,false,true,true})==FrontendAction::exit,"back wins over accept");
        RaceRun run; std::string error;
        check(run.set_free_drive(true,error),"free-drive configuration before startup");
        if(argc==2) {
            const std::filesystem::path root=argv[1]; FrontendArt art;
            std::vector<unsigned char> front,globals;std::vector<RaceTableIssue> part_issues;
            check(load_race_image(root/"FrontEnd/FrontB.lzc",front,part_issues),"original preset image");
            check(load_race_image(root/"Global/GLOBALB.BUN",globals,part_issues),"original part database image");
            const auto database=parse_car_part_database(globals.data(),globals.size());
            InstalledPartSelection installed;
            check(resolve_frontend_skyline_parts(front,database,installed,error),error.c_str());
            StockParts selected;
            check(select_installed_parts(database,"SKYLINE",installed,selected,error),error.c_str());
            std::vector<unsigned char> native_slots;
            for(const auto& part:installed.records) {
                const auto index=!part || *part==kNoCarPart?0xFFFFFFFFU:static_cast<std::uint32_t>(*part);
                for(unsigned byte=0;byte<4;++byte)native_slots.push_back(static_cast<unsigned char>(index>>(byte*8)));
            }
            for(const auto& slot:selected.slots)native_slots.push_back(slot.enabled?1:0);
            check(sha256_hex(sha256(native_slots.data(),native_slots.size()))==
                "27DE93661A7153D9B74C74198B14C2DD8016E863A5CE641FA93B86B54AF1074F",
                "native FE_SKYLINE 120 assigned slots and installation enable bytes");
            BodySkinSelection skin;
            check(resolve_frontend_skyline_skin(globals,database,installed,skin,error),error.c_str());
            check(skin.base_color==skin_pack_paint(145,63,16,128) &&
                skin.vinyl_layers[3] && skin.vinyl_layers[3]->texture=="EDDIE" &&
                !skin.vinyl_layers[3]->remap,"native FE_SKYLINE paint and Eddie vinyl");
            FrontendSponsorHashes sponsors;
            check(resolve_frontend_sponsors(database,selected,sponsors,error),error.c_str());
            const std::uint32_t native_doors[]={0x8A571203,0xB015F6F2,0xB891CDB4,0x0F4108B2};
            for(unsigned panel=0;panel<7;++panel)for(unsigned decal=0;decal<8;++decal) {
                const auto expected=panel==2 && decal==0?0xB8949CAFU:
                    (panel==3 || panel==4) && decal<4?native_doors[decal]:0x66994F4AU;
                check(sponsors[panel][decal]==expected,"native original 56C3D0 sponsor substitutions");
            }
            CarAssets skyline_assets;
            check(load_car_assets(root/"Global/GLOBALB.BUN","SKYLINE",skyline_assets,error),error.c_str());
            StockFrameInputs skyline_inputs;StockFrame skyline;
            check(read_car_frame_inputs(root,skyline_assets,skyline_inputs,true,error),error.c_str());
            auto skyline_request=stock_car_request("SKYLINE");
            skyline_request.installed_parts=installed;skyline_request.pass=CarDrawPass::front_end;
            auto skyline_skin=stock_car_skin("SKYLINE");skyline_skin.selection=skin;
            check(prepare_stock_frame(skyline_inputs,skyline_request,skyline_skin,skyline,error,true),error.c_str());
            const auto original_materials=skyline.scene.draws;
            check(bind_frontend_skyline_runtime_skin(skyline,skyline_request,error),error.c_str());
            check(original_materials.size()==skyline.scene.draws.size(),"texture-only FE rebind preserves material groups");
            bool shaded_material=false;
            for(std::size_t i=0;i<original_materials.size();++i) {
                const auto& expected=original_materials[i].shading;
                const auto& actual=skyline.scene.draws[i].shading;
                check(std::memcmp(&expected,&actual,sizeof(expected))==0,
                    "GLOBAL_SKIN1 rebind preserves decoded material gates and vectors");
                shaded_material|=actual.gates[0]!=0 && actual.gates[1]!=0 &&
                    (actual.diffuse_min[0]!=0 || actual.diffuse_range[0]!=0);
            }
            check(shaded_material,"original FE materials retain nonzero diffuse gate and vectors");
            unsigned global_draws=0;
            for(const auto& draw:skyline.scene.draws)if(draw.texture_hash==0xDDA11455U) {
                check(draw.binding==BindingKind::resolved &&
                    skyline.scene.textures.at(draw.texture).source==skyline.skin_binding.source,
                    "native GLOBAL_SKIN1 uses existing generated body surface");++global_draws;
            }
            check(global_draws!=0,"authored GLOBAL_SKIN1 material retained");
            check(bind_frontend_sponsors(skyline_inputs,installed,skyline,error),error.c_str());
            for(unsigned slot:{26U,27U}) {
                unsigned bound=0;
                for(const auto& instance:skyline.scene.instances)if(instance.part_slot==slot)
                    for(const auto& draw:skyline.scene.draws)if(draw.mesh==instance.mesh && draw.drawable)
                        for(auto hash:native_doors)if(draw.texture_hash==hash) {
                            check(draw.binding==BindingKind::resolved &&
                                skyline.scene.textures.at(draw.texture).claims_hash(hash),
                                "widebody door uses native sponsor texture pixels");++bound;
                        }
                check(bound==4,"original widebody door renders four selected sponsors");
            }
            FrontendGpuScene gpu_scene;
            if(!gpu_scene.load(root,1280,720,error))throw std::runtime_error(error);
            // Independent original 486ECD/487BA0/4870C0/487680 execution:
            // mode0 saved entry, VIEW inverse and raw -> world basis conversion.
            // Independently captured genuine40AD60 six input VIEW matrices,
            // raw -> world basis and original negative vertical projection.
            // FOV follows genuine CRT initializer689920 and original40DD00.
            const double native_cube_basis[6][9]={
                {0.7025555372238159,-0.02402954176068306,-0.7112331986427307,-0.0988493338227272,-0.9930362105369568,-0.06409282982349396,-0.7047401070594788,0.1153336837887764,-0.7000383734703064},
                {-0.7025555372238159,0.02402954176068306,0.7112331986427307,-0.0988493338227272,-0.9930362105369568,-0.06409282982349396,0.7047401070594788,-0.1153336837887764,0.7000383734703064},
                {-0.7047401070594788,0.1153336837887764,-0.7000383734703064,0.7025580406188965,-0.024029627442359924,-0.711235761642456,-0.09884968400001526,-0.9930397868156433,-0.06409306079149246},
                {-0.7047401070594788,0.1153336837887764,-0.7000383734703064,-0.7025580406188965,0.024029627442359924,0.711235761642456,0.09884968400001526,0.9930397868156433,0.06409306079149246},
                {-0.7047401070594788,0.1153336837887764,-0.7000383734703064,-0.0988493338227272,-0.9930362105369568,-0.06409282982349396,-0.7025555372238159,0.02402954176068306,0.7112331986427307},
                {0.7047376036643982,-0.11533327400684357,0.7000358700752258,-0.0988493338227272,-0.9930362105369568,-0.06409282982349396,0.7025530338287354,-0.024029456079006195,-0.7112306356430054}};
            const auto& cube_commands=gpu_scene.commands();
            check(!cube_commands.frontend_cube_draws.empty(),"original Y-family cube resources resident");
            check(validate_recorded_commands(cube_commands,error),error.c_str());
            for(unsigned face=0;face<6;++face) {
                const auto& frame=cube_commands.frontend_cube_frames[face];
                check(frame.projection[0]==1 && frame.projection[1]==1 &&
                    frame.projection[2]==.5F && frame.projection[3]==64,
                    "all six native cube projections use original CRT FOV and clipping planes");
                for(unsigned axis=0;axis<3;++axis)
                    check(std::abs(frame.right[axis]-native_cube_basis[face][axis])<1e-5 &&
                        std::abs(frame.up[axis]-native_cube_basis[face][3+axis])<1e-5 &&
                        std::abs(frame.forward[axis]-native_cube_basis[face][6+axis])<1e-5,
                        "all six cube capture bases match native asymmetric orientation");
            }
            const auto native_road_region=parse_track_region(front.data(),381824,0);
            const TrackMesh* native_cube_mesh=nullptr;
            for(const auto& mesh:native_road_region.meshes)if(mesh.name=="TRFE_ROAD02_Y")native_cube_mesh=&mesh;
            check(native_cube_mesh && native_cube_mesh->valid,"authenticated original Y solid is decoded");
            unsigned cube_segments=0;
            for(const auto& draw:cube_commands.frontend_cube_draws) {
                if(draw.frontend_sprite_view)continue;
                check(draw.instance<3,"original cube view uses native three translated copies");
                const auto& vertices=cube_commands.resource_table().resources.at(draw.vertex_resource);
                check(vertices.elements==native_cube_mesh->vertices.size(),"cube geometry vertex count matches original Y solid");
                const auto* decoded=reinterpret_cast<const MeshVertex*>(vertices.data);
                for(std::size_t v=0;v<vertices.elements;++v) {
                    const auto& original=native_cube_mesh->vertices[v];
                    check(decoded[v].px==original.px && decoded[v].py==original.py && decoded[v].pz==original.pz &&
                        decoded[v].u==original.u && decoded[v].v==original.v,
                        "every resident cube vertex and UV comes from authenticated Y solid");
                }
                cube_segments|=1U<<draw.instance;
                check(draw.texture_resource!=kNoHandle,"original cube diffuse texture is bound");
            }
            check(cube_segments==7,"all original cube road segments are represented");
            unsigned native_glows[6]={2,2,0,2,2,1},active_glows[6]={};
            for(const auto& draw:cube_commands.frontend_cube_draws)if(draw.frontend_sprite_view && draw.material.alpha[0]>0)
                ++active_glows[draw.frontend_sprite_view-10];
            for(unsigned face=0;face<6;++face)check(active_glows[face]==native_glows[face],
                "native actual cube frustum streetlamp visibility per face");
            unsigned floor_glows=0,primary_glows=0;
            const RecordedDraw* native_floor_glow=nullptr;
            for(const auto& draw:cube_commands.reflections)if(draw.frontend_sprite_view && draw.material.alpha[0]>0) {
                ++floor_glows;if(!native_floor_glow)native_floor_glow=&draw;
            }
            for(const auto& draw:cube_commands.owning_draws())if(draw.frontend_sprite_view && draw.material.alpha[0]>0)++primary_glows;
            check(floor_glows==5 && primary_glows==0,"initial native primary point/floor box culling, not mode suppression");
            check(native_floor_glow && native_floor_glow->instance==6,"native first visible floor marker and segment");
            if(native_floor_glow) {
                const auto& resource=cube_commands.resource_table().resources.at(native_floor_glow->texture_resource);
                check(resource.texture_hash==0x3394FE62U && resource.bytes!=0,
                    "authored HEADLIGHTGLOW receives resident original pixels");
                check(!native_floor_glow->pipeline.depth_write,
                    "native sprite additive pass has no depth write");
                const int signs[4][2]={{-1,1},{1,1},{1,-1},{-1,-1}};
                for(unsigned i=0;i<4;++i)for(unsigned axis=0;axis<3;++axis) {
                    const auto& rows=native_floor_glow->placement.rows;
                    const float point=rows[12+axis]+signs[i][0]*rows[axis]+signs[i][1]*rows[8+axis];
                    check(std::abs(point-native_floor_sprite1280[i][axis])<.0002F,
                        "actual resident floor sprite placement matches all native corners");
                }
                check(std::abs(native_floor_glow->material.alpha[0]-140.F/255)<.000001F,
                    "actual native floor sprite opacity survives publication");

            }

            const TrackMesh* native_floor=nullptr;const TrackMesh* native_z=nullptr;
            for(const auto& mesh:native_road_region.meshes) {
                if(mesh.name=="TRFE_ROAD02_A")native_floor=&mesh;
                if(mesh.name=="TRFE_ROAD02_Z")native_z=&mesh;
            }
            check(native_floor && native_floor->vertex_format_flags==0x40 && native_z && native_z->valid,
                "original worldreflect selector belongs to authored base floor and Z capture is valid");
            check((cube_commands.frame.options[2]&8U)!=0 && cube_commands.reflection_draw_count()!=0,
                "native FE floor target is separately enabled beside car cube");
            const auto& floor_frame=cube_commands.frontend_floor_frame;
            check(std::abs(floor_frame.projection[0]-1.2961782217025757)<1e-6 &&
                std::abs(floor_frame.projection[1]-1.660086750984192)<1e-6,
                "original320x240 view4 projection uses shared main CView");
            for(unsigned axis=0;axis<3;++axis)
                check(floor_frame.eye[axis]==cube_commands.frame.eye[axis] &&
                    floor_frame.right[axis]==cube_commands.frame.right[axis] &&
                    floor_frame.up[axis]==cube_commands.frame.up[axis],"floor capture shares native primary pose");
            unsigned z_segments=0,floor_receivers=0;
            for(const auto& draw:cube_commands.reflections) {
                if(draw.frontend_sprite_view)continue;
                check(draw.instance<3 && draw.material.surface.gates[0]==0,
                    "native FE reflected mode3 uses three Z solids without invented mirrored car");
                z_segments|=1U<<draw.instance;
                const auto& resource=cube_commands.resource_table().resources.at(draw.vertex_resource);
                check(resource.elements==native_z->vertices.size(),"resident reflection geometry has original Z vertex count");
                const auto* vertices=reinterpret_cast<const MeshVertex*>(resource.data);
                for(std::size_t v=0;v<resource.elements;++v)
                    check(vertices[v].px==native_z->vertices[v].px && vertices[v].py==native_z->vertices[v].py &&
                        vertices[v].pz==native_z->vertices[v].pz && vertices[v].u==native_z->vertices[v].u &&
                        vertices[v].v==native_z->vertices[v].v,"every Z capture vertex/UV is authored retail geometry");
            }
            check(z_segments==7,"all native Z-family segments resident");
            for(const auto& draw:cube_commands.owning_draws())if(draw.material.environment_min[3]==1) {
                ++floor_receivers;const auto& resource=cube_commands.resource_table().resources.at(draw.vertex_resource);
                check(resource.elements==native_floor->vertices.size(),"worldreflect mask only binds authenticated floor solid");
                const auto* vertices=reinterpret_cast<const MeshVertex*>(resource.data);
                for(std::size_t v=0;v<resource.elements;++v)for(unsigned c=0;c<4;++c)
                    check(vertices[v].color[c]==native_floor->vertices[v].color[c],
                        "floor retains native RGB and independent undoubled reflection-alpha mask");
            }
            check(floor_receivers!=0,"worldreflect receiver connected");
            const auto& entry_frame=gpu_scene.commands().frame;
            const double native_eye[]={2.743728462142889,.7651561591125922,-1.8576175162722057};
            const double native_right[]={-.7047351002693176,.11533286422491074,-.7000333666801453};
            const double native_up[]={.0988493338227272,.9930362105369568,.06409282982349396};
            const double native_forward[]={-.7025505304336548,.02402937039732933,.71122807264328};
            for(unsigned axis=0;axis<3;++axis) {
                check(std::abs(entry_frame.eye[axis]-native_eye[axis])<1e-5,
                    "main entry native camera eye, not constructor default");
                check(std::abs(entry_frame.right[axis]-native_right[axis])<1e-5 &&
                    std::abs(entry_frame.up[axis]-native_up[axis])<1e-5 &&
                    std::abs(entry_frame.forward[axis]-native_forward[axis])<1e-5,
                    "main entry native camera basis preserves original roll");
            }
            // Genuine original 40DD00 primary view1, 1280x720 viewport.
            check(std::abs(entry_frame.projection[0]-1.2961782217025757)<1e-6 &&
                std::abs(entry_frame.projection[1]-2.32236385345459)<1e-6,
                "main entry original independent fixed-angle projection scales");
            const auto initial_commands=gpu_scene.commands().owning_draws();
            check(!initial_commands.empty(),"original frontend resident GPU submission");
            const auto shadow=std::find_if(initial_commands.begin(),initial_commands.end(),
                [](const auto& d){return d.name.find("_FE_SHADOW")!=std::string::npos;});
            check(shadow!=initial_commands.end(),"original SHADOWFE resident draw connected");
            if(shadow!=initial_commands.end()) {
                const auto& resources=gpu_scene.commands().resource_table().resources;
                const auto& vertices=resources.at(shadow->vertex_resource);
                const auto& texture=resources.at(shadow->texture_resource);
                check(vertices.elements==16 && texture.texture_hash==bin_hash("SKYLINE_SHADOWFE"),
                    "FE shadow uses original texture and sixteen native samples");
                // Independently executed original 567890/56AB99/56EB33 slices,
                // authenticated FE_SKYLINE parts and original solid headers.
                const double x[]={-2.746955156326294,-.8996659517288208,.9476232528686523,2.794912338256836};
                const double y[]={-1.1095234155654907,-.3698313236236572,.36986076831817627,1.1095528602600098};
                const auto* v=reinterpret_cast<const MeshVertex*>(vertices.data);
                for(unsigned i=0;i<16;++i) {
                    const double z=(((i>>1)^i)&5)==5?.09999999403953552:.019999999552965164;
                    check(std::abs(v[i].px-x[i%4])<.00001 && std::abs(v[i].pz-y[i/4])<.00001 &&
                        std::abs(v[i].py-z)<.000001,"all FE shadow vertices match original footprint and ground consumer");
                    check(std::abs(v[i].u-(i%4)/3.)<.000001 && std::abs(v[i].v-(i/4)/3.)<.000001,
                        "original FE shadow UV orientation retained");
                }
                check(std::abs(shadow->material.alpha[0]-184./255)<.000001 && !shadow->pipeline.depth_write,
                    "native fixed FE alpha92 receives original Modulate2x and no depth writes");
            }
            for(const auto& draw:initial_commands)
                check(draw.program && std::string(draw.program->name).find("fallback")==std::string::npos,
                    "native frontend submits no placeholder fallback material");
            // Independent native 4B17F5..4B1825 witness with original
            // SKYLINE wheel-0 radius/vertical and normal main-view flag.
            for(const auto& draw:initial_commands) {
                if(draw.frontend_sprite_view || draw.name.find("_FE_")!=std::string::npos)continue;
                const auto& instance=skyline.scene.instances.at(draw.instance);
                check(std::abs(draw.placement.rows[14]-instance.transform[14]-.20027998089790344)<.00001,
                    "whole FE car receives original contact-plane root height");
            }
            check(gpu_scene.frame(1.0F/60,4000,error),error.c_str());
            const auto& moving_commands=gpu_scene.commands().owning_draws();
            check(moving_commands.size()==initial_commands.size() &&
                moving_commands.front().placement.rows[12]!=initial_commands.front().placement.rows[12],
                "resident original road translates without rebuilding draw definitions");
            // Independently executed 0x40BA40 at native phase 7253, with
            // authenticated SKYLINE wheel-0 radius and the frame above.
            const double native_cosine=.7678149938583374;
            const double native_sine=.6406716108322144;
            std::array<bool,4> rolling_copies{},stationary_brakes{};
            for(std::size_t i=0;i<moving_commands.size();++i) {
                const auto& draw=moving_commands[i];
                if(draw.frontend_sprite_view || draw.name.find("_FE_")!=std::string::npos)continue;
                const auto& instance=skyline.scene.instances.at(draw.instance);
                if(instance.part_slot==17) {
                    check(instance.copy<4,"original rolling wheel index");
                    rolling_copies[instance.copy]=true;
                    const auto& mesh=skyline.scene.meshes.at(instance.mesh);
                    check(!mesh.vertices.empty(),"original wheel vertex retained");
                    for(std::size_t v:{std::size_t(0),mesh.vertices.size()-1}) {
                        const auto& point=mesh.vertices[v];
                        const double slot[3]={point.px,point.pz,point.py};
                        double before[3]{},actual[3]{};
                        for(unsigned c=0;c<3;++c) {
                            before[c]=instance.transform[12+c];actual[c]=draw.placement.rows[12+c];
                            for(unsigned k=0;k<3;++k) {
                                before[c]+=slot[k]*instance.transform[k*4+c];
                                actual[c]+=slot[k]*draw.placement.rows[k*4+c];
                            }
                        }
                        const double x=before[0]-instance.animation_pivot[0];
                        const double z=before[2]-instance.animation_pivot[2];
                        const double expected[3]={x*native_cosine+z*native_sine+instance.animation_pivot[0],
                            before[1],-x*native_sine+z*native_cosine+instance.animation_pivot[2]+.20027998089790344};
                        for(unsigned c=0;c<3;++c)
                            check(std::abs(actual[c]-expected[c])<.00001,
                                "all four original wheel vertices match native spin axis/sign/pivot");
                    }
                } else if(instance.part_slot==16) {
                    check(instance.copy<4,"original stationary brake index");
                    stationary_brakes[instance.copy]=true;
                    for(unsigned c=0;c<16;++c)
                        check(draw.placement.rows[c]==initial_commands[i].placement.rows[c],
                            "brake matrices do not inherit wheel rolling");
                }
            }
            check(std::all_of(rolling_copies.begin(),rolling_copies.end(),[](bool b){return b;}) &&
                std::all_of(stationary_brakes.begin(),stationary_brakes.end(),[](bool b){return b;}),
                "native rolling witness covers all four wheels and all four stationary brakes");
            audio::FrontendSfx sfx;
            check(sfx.load(root,error),error.c_str());
            std::vector<std::int16_t> unchanged(1024*2,1234);
            sfx.mix(unchanged.data(),1024);
            check(std::all_of(unchanged.begin(),unchanged.end(),[](auto v){return v==1234;}),
                "idle effects preserve in-place music samples");
            std::vector<std::int16_t> up(24000*2,0),down(up.size(),0),restart(up.size(),0);
            sfx.play(audio::FrontendSound::up);sfx.mix(up.data(),24000);
            sfx.reset();sfx.play(audio::FrontendSound::down);sfx.mix(down.data(),24000);
            check(std::any_of(up.begin(),up.end(),[](auto v){return v!=0;}) &&
                  std::any_of(down.begin(),down.end(),[](auto v){return v!=0;}) && up!=down,
                "original up and down cues are audible and distinct");
            sfx.reset();sfx.play(audio::FrontendSound::up);sfx.mix(restart.data(),24000);
            check(restart==up,"effect reset starts original cue from its first sample");
            sfx.play(audio::FrontendSound::select);sfx.reset();
            std::fill(restart.begin(),restart.end(),0);sfx.mix(restart.data(),24000);
            check(std::all_of(restart.begin(),restart.end(),[](auto v){return v==0;}),
                "effect reset retires queued voices before menu reentry");

            // Exercise retail bank indexing and decoder-thread failure cleanup
            // repeatedly without depending on an attached audio device.
            audio::FrontendMusic music;
            for(unsigned attempt=0;attempt<3;++attempt) {
                check(!music.start(root,error,[]{return false;}),"decoder setup failure must reach caller");
                check(!error.empty(),"decoder setup failure has user-visible reason");
                music.stop();
            }
            if(!art.load(root,error)) throw std::runtime_error(error);
            std::vector<unsigned char> pixels(1280*720*4,0);
            art.draw({pixels.data(),1280,720,1280*4},menu);
            check(pixels[3]==255 && pixels[(719*1280+1279)*4+3]==255,"menu fills full image");
            // GPU-only loading must retain authored textures/font coordinates
            // and update selection without a CPU background or texture reload.
            FrontendArt gpu_art;
            check(gpu_art.load(root,error,false),error.c_str());
            const auto& atlas=gpu_art.atlas();
            check(atlas.width && atlas.height && atlas.rgba.size()==atlas.width*atlas.height*4ULL,
                "resident original frontend atlas contains complete pixel storage");
            FrontendUiFrame ui;
            FrontendMenu gpu_menu;
            gpu_art.record_ui(1280,720,gpu_menu,ui);
            check(!ui.additive.empty() && ui.vertices.size()==ui.additive.size()*4 &&
                ui.additive.size()<=kFrontendUiMaxQuads,"authored UI fits resident overlay capacity");
            // Independent MainMenu.fng resource5 circle submissions: authored
            // positions and alpha, before any temporal-state approximation.
            // Missing CIRCLE atlas binding used to silently omit all four.
            const double native_circles[4][4][2]={
                {{556.4586791992188,33.027889251708984},{580.9721069335938,12.458686828613281},{601.5413208007812,36.972110748291016},{577.0278930664062,57.54131317138672}},
                {{522.4586791992188,33.027889251708984},{546.9721069335938,12.458686828613281},{567.5413208007812,36.972110748291016},{543.0278930664062,57.54131317138672}},
                {{533.8563842773438,29.14358901977539},{517.8563842773438,56.85640335083008},{490.1435852050781,40.85641098022461},{506.1435852050781,13.143595695495605}},
                {{456.1435852050781,29.143592834472656},{483.8564147949219,13.143593788146973},{499.8564147949219,40.856407165527344},{472.1435852050781,56.856407165527344}}};
            const unsigned circle_alpha[4]={60,104,74,68};
            for(unsigned circle=0;circle<4;++circle) {
                bool found=false;
                for(std::size_t i=0;i<ui.vertices.size();i+=4) {
                    bool same=true;
                    for(unsigned corner=0;corner<4;++corner) {
                        const auto& v=ui.vertices[i+corner];
                        same &= std::abs(v.px-native_circles[circle][corner][0]*2)<.001 &&
                                std::abs(v.py-native_circles[circle][corner][1]*1.5)<.001;
                    }
                    if(!same)continue;
                    found=true;
                    for(unsigned corner=0;corner<4;++corner) {
                        const auto& v=ui.vertices[i+corner];
                        check(v.color[0]==206 && v.color[1]==232 && v.color[2]==242 &&
                              v.color[3]==circle_alpha[circle],
                              "original header circle native color and alpha");
                    }
                    check(ui.vertices[i+2].u>ui.vertices[i].u && ui.vertices[i+2].v>ui.vertices[i].v,
                        "original circle binds nonempty resident texture coordinates");
                }
                check(found,"original rotated header circle matches genuine FEng submission");
            }
            unsigned outside_markers=0;
            for(std::size_t i=0;i<ui.vertices.size();i+=4) {
                if(ui.vertices[i].color[0]!=254 || ui.vertices[i].color[1]!=0 ||
                    ui.vertices[i].color[2]!=0) continue;
                // Genuine original FEng image submit places the three authored
                // red control markers wholly outside clipX [-1,1]. Keep their
                // geometry; production viewport clipping excludes them.
                for(unsigned corner=0;corner<4;++corner) {
                    const double clip_x=2*double(ui.vertices[i+corner].px)/1280-1;
                    const double native_x=(corner==0 || corner==3)?-1.084375023841858:-1.0343749523162842;
                    check(std::abs(clip_x-native_x)<1e-6 && clip_x<-1,
                        "authored menu marker remains outside original viewport");
                }
                ++outside_markers;
            }
            check(outside_markers==3,"native off-canvas controls retained without visible red squares");
            const double native_logo[4][2]={{-1.1467323303222656,.9117122888565063},
                {.29381564259529114,.9117122888565063},
                {.29381564259529114,.6716209650039673},
                {-1.1467323303222656,.6716209650039673}};
            const double native_text[4][2]={{.40000003576278687,.2124999612569809},
                {.4437500238418579,.2124999612569809},
                {.4437500238418579,.14583328366279602},
                {.40000003576278687,.14583328366279602}};
            const auto native_quad_present=[&](const double (&pin)[4][2]) {
                for(std::size_t i=0;i<ui.vertices.size();i+=4) {
                    bool same=true;
                    for(unsigned corner=0;corner<4;++corner)
                        same &= std::abs(2*double(ui.vertices[i+corner].px)/1280-1-pin[corner][0])<1e-6 &&
                            std::abs(1-2*double(ui.vertices[i+corner].py)/720-pin[corner][1])<1e-6;
                    if(same)return true;
                }
                return false;
            };
            check(native_quad_present(native_logo),"onscreen authored logo matches original FEng clip corners");
            check(native_quad_present(native_text),"onscreen original font glyph matches original FEng clip corners");
            bool atlas_pixels=false,restored_color=false;
            for(const auto& vertex:ui.vertices) {
                check(std::isfinite(vertex.px) && std::isfinite(vertex.py) &&
                    vertex.u>=0 && vertex.u<=1 && vertex.v>=0 && vertex.v<=1,
                    "original font and FEng quads have valid atlas coordinates");
                const auto x=std::min(atlas.width-1,static_cast<unsigned>(vertex.u*atlas.width));
                const auto y=std::min(atlas.height-1,static_cast<unsigned>(vertex.v*atlas.height));
                restored_color |= vertex.color[0]>127 || vertex.color[1]>127 || vertex.color[2]>127;
                check(vertex.color[0]%2==0 && vertex.color[1]%2==0 && vertex.color[2]%2==0 && vertex.color[3]%2==0,
                    "GPU overlay preserves native half-byte quantization and restores Modulate2x");
                atlas_pixels |= atlas.rgba[(y*atlas.width+x)*4+3]!=0;
            }
            check(restored_color,"native modulation restored before dedicated GPU UI shader");
            check(atlas_pixels,"UI vertices bind decoded original texture pixels");
            const auto initial_ui=ui.vertices;
            const auto* resident_pixels=atlas.rgba.data();
            FrontendInput navigation;navigation.down=true;gpu_menu.update(navigation);
            gpu_art.record_ui(1280,720,gpu_menu,ui);
            bool highlight_changed=ui.vertices.size()!=initial_ui.size();
            if(!highlight_changed)for(std::size_t i=0;i<ui.vertices.size();++i) {
                const auto& a=ui.vertices[i];const auto& b=initial_ui[i];
                highlight_changed |= a.color[0]!=b.color[0] || a.color[1]!=b.color[1] || a.color[2]!=b.color[2] ||
                                     a.px!=b.px || a.py!=b.py;
            }
            check(highlight_changed && atlas.rgba.data()==resident_pixels,
                "selection updates original highlight quads with resident texture storage");
            gpu_menu.song("Lil Jon","Get Low","Kings of Crunk");
            gpu_art.record_ui(1280,720,gpu_menu,ui);
            check(ui.vertices.size()==ui.additive.size()*4 && !ui.vertices.empty(),
                "current song text reaches GPU original font submission");
            const double native_temporal[4][4][2]={
                {{462,19},{494,19},{494,51},{462,51}},
                {{462,50.9936408996582},{462.00634765625,18.999996185302734},{494,19.006359100341797},{493.99365234375,51.000003814697266}},
                {{494,51.000003814697266},{462,50.999996185302734},{462,18.9999942779541},{494,19.0000057220459}},
                {{493.99365234375,19.00000762939453},{494,50.99365234375},{462.00634765625,50.99999237060547},{462,19.00634765625}}};
            const unsigned temporal_alpha[4]={80,46,10,44};
            for(unsigned phase=0;phase<5;++phase) {
                gpu_art.advance(phase?.5F:0.F);gpu_art.record_ui(1280,720,gpu_menu,ui);
                const unsigned expected=phase%4;bool found=false;
                for(std::size_t i=0;i<ui.vertices.size();i+=4) {
                    bool same=true;
                    for(unsigned corner=0;corner<4;++corner) {
                        const auto& v=ui.vertices[i+corner];
                        same &= std::abs(v.px-native_temporal[expected][corner][0]*2)<.002 &&
                                std::abs(v.py-native_temporal[expected][corner][1]*1.5)<.002 &&
                                v.color[3]==temporal_alpha[expected];
                    }
                    found |= same;
                }
                check(found,"original timed circle quaternion/color and two-second loop match genuine FEng");
                check(atlas.rgba.data()==resident_pixels,"authored temporal UI reuses resident texture atlas");
            }
            // Circle(58) has the sole authored 45-degree quaternion pair.
            // Genuine 4FC640 uses sine phases4095/4095/8191 at its midpoint.
            gpu_art.advance(.75F);gpu_art.record_ui(1280,720,gpu_menu,ui);
            const double native_short_arc[4][2]={{489.3743896484375,34.99565887451172},
                {512.0043334960938,12.374380111694336},{534.6256103515625,35.00434112548828},
                {511.99566650390625,57.62561798095703}};
            bool short_arc_found=false;
            for(std::size_t i=0;i<ui.vertices.size();i+=4) {
                bool same=true;
                for(unsigned corner=0;corner<4;++corner) {
                    const auto& v=ui.vertices[i+corner];
                    same &= std::abs(v.px-native_short_arc[corner][0]*2)<.002 &&
                            std::abs(v.py-native_short_arc[corner][1]*1.5)<.002 && v.color[3]==60;
                }
                short_arc_found |= same;
            }
            check(short_arc_found,"authored circle short-arc interpolation matches independent native image corners");
            RaceGpuScene scene;
            check(run.prepare_metadata(root,error),error.c_str());
            check(!run.set_free_drive(false,error),"mode cannot change after metadata preparation");
            check(scene.load(posix_file_system(),root,error),error.c_str());
            check(run.start(root,scene.track(),error),error.c_str());
            const auto& session=run.session();
            check(session.course().route.laps==0,"free-drive configuration reaches actual runtime route");
            RaceRouteDriver driver(RaceDrivingLine::event1_forward);
            unsigned ticks=0;
            for(;ticks<90000 && race_laps_completed(session.progress())<3;++ticks)
                check(run.simulate(driver.sample(session),error) && run.commit(error),error.c_str());
            std::cout << "free-drive observed laps " << race_laps_completed(session.progress()) << " max " << session.progress().max_lap << " steps " << ticks << " resets " << session.telemetry().resets << std::endl;
            check(race_laps_completed(session.progress())>=3,"free driving passes former two-lap finish");
            check(!session.progress().finished && session.timing().outcome==RaceOutcome::running,"free driving still running");
            std::cout << "PASS: authenticated original menu texture load/render; free driving past three laps " << ticks << " ticks\n";
        }
        std::cout << "PASS: menu input launch, wrap, conflict and exit ordering\n";
        return 0;
    } catch(const std::exception& e) { std::cerr << "FAIL: " << e.what() << '\n'; return 1; }
}
