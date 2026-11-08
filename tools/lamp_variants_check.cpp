#include "nfsu/arg_path.h"
#include "nfsu/career_ride.h"
#include "nfsu/light_glows.h"
#include "nfsu/stock_view.h"
#include <iostream>
#include <iomanip>
#include <stdexcept>

namespace {
void require(bool ok,const std::string& error) {
    if(!ok)throw std::runtime_error(error);
}
}
int main(int argc,char** argv) {
    using namespace nfsu;
    try {
        std::string error;
        require(argc==2 && check_arguments(argc,argv,1,error),"retail root required: "+error);
        const auto root=argument_path(argv[1]);
        CareerCatalog catalog;
        require(catalog.load_free_drive(root,error),error);
        std::size_t variants=0,emitters=0;
        const std::uint32_t hashes[]={0x9DB90133U,0xD09091C6U,0x31A66786U,0xBF700A79U,0xA2A2FC7CU};
        std::cout<<std::setprecision(10);
        for(const auto& car:catalog.cars()) {
            CarAssets assets;StockFrameInputs inputs;StockFrameGeometryCache cache;
            require(load_car_assets(root/"Global/GLOBALB.BUN",car.name,assets,error),error);
            require(read_car_frame_inputs(root,assets,inputs,true,error),error);
            require(build_stock_frame_geometry_cache(inputs,cache,error),error);
            for(const auto& part:catalog.tuning().parts().records) {
                if(part.car_type_hash!=bin_hash(car.name) || (part.slot!=13 && part.slot!=14))continue;
                CareerProfile profile;
                require(catalog.free_profile(car.name,profile,error),error);
                profile.ride.installed[static_cast<unsigned>(part.slot)]=part.part_name_hash;
                CareerRideSetup ride;StockFrame frame;
                require(resolve_career_ride(inputs.file(StockFrameFile::global_bun).bytes,catalog,profile,ride,error),error);
                require(prepare_stock_frame(inputs,ride.request,ride.skin,frame,error,true,&cache),error);
                std::cout<<"PART "<<car.name<<' '<<part.slot<<' '<<part.name<<'\n';
                // Verify every material group of the actual selected solid, not
                // merely that the desired part name exists in the database.
                const auto& selected=frame.report.slots[static_cast<unsigned>(part.slot)];
                require(selected.outcome==StockSlotOutcome::drawn,"lamp solid not drawn");
                require(selected.record==part.index,"requested lamp kit was not selected");
                for(const auto& draw:frame.scene.draws)if(draw.mesh==selected.mesh && draw.drawable && draw.index_count)
                    require(draw.texture!=kNoHandle,"selected lamp has an unresolved texture");
                const auto anchors=collect_car_glow_anchors(frame);
                const auto& geometry=selected.geometry_source==0?frame.car_geometry:
                    frame.geometry_sources().at(selected.geometry_source-1);
                const auto& mesh=geometry.meshes.at(selected.source_mesh);
                std::array<std::size_t,5> counts{};
                for(const auto& marker:mesh.markers)for(unsigned group=0;group<5;++group) {
                    if(marker.name_hash!=hashes[group])continue;
                    const auto index=counts[group]++;++emitters;
                    require(index<anchors[group].size(),"authored emitter missing from collected group");
                    const auto& a=anchors[group][index];
                    require(a.slot.x==marker.matrix[12] && a.slot.y==marker.matrix[13] && a.slot.z==marker.matrix[14] &&
                            a.normal.x==marker.matrix[8] && a.normal.y==marker.matrix[9] && a.normal.z==marker.matrix[10],
                            "authored emitter position or normal changed during collection");
                    std::cout<<"SOURCE "<<group;
                    for(float f:marker.matrix)std::cout<<' '<<f;
                    std::cout<<'\n';
                }
                for(unsigned group=0;group<5;++group) {
                    if((part.slot==13)!=(group<2))continue;
                    require(counts[group]==anchors[group].size(),"authored lamp emitter was lost");
                    for(const auto& a:anchors[group])
                        std::cout<<"ANCHOR "<<group<<' '<<a.slot.x<<' '<<a.slot.y<<' '<<a.slot.z
                                 <<' '<<a.normal.x<<' '<<a.normal.y<<' '<<a.normal.z<<'\n';
                }
                OrbitCamera camera=stock_view_default_options().camera;
                camera.yaw_deg=part.slot==13?0:180;camera.pitch_deg=5;camera.distance=8;camera.target={0,.5,0};
                CameraFrame basis;
                require(make_camera_frame(camera,1280,720,basis,error),error);
                OriginalCarGlows sprites(original_car_glow_count(anchors));
                const auto* allocation=sprites.data();
                for(double brake:{0.,1.}) {
                    place_original_car_glows(anchors,frame.scene,basis,1280,brake,false,sprites);
                    require(sprites.data()==allocation,"lamp update allocated new frame storage");
                    std::size_t live=0;
                    for(const auto& s:sprites)if(s.rgba[3])++live;
                    require(live>0,"selected lamp emits no visible sprites");
                }
                ++variants;
            }
        }
        require(variants==200,"all twenty cars and ten front/rear kits must be covered");
        std::cout<<"PASS lamp variants "<<variants<<" authored emitters "<<emitters<<'\n';
        return 0;
    } catch(const std::exception& e) {
        std::cerr<<"FAIL: "<<e.what()<<'\n';return 1;
    }
}
