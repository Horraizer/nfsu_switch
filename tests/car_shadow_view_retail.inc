// Exercise the actual scene adapter, streamed vertices and draw visibility.
// Native pixel sizes are independent PC executable witnesses, not a second
// implementation of the corrected formula in this test.
#include "car_shadow_view_native.inc"
void verify_shadow_view_fade(RaceGpuScene& scene,RaceSession& session,
                            RaceTrafficActor* traffic,const std::string& traffic_name,bool reference=false) {
    if(reference && traffic)throw std::runtime_error("shadow reference fixture must have one car");
    std::string error;
    TrackContact ground;CarShadowGroundCache cache;
    if(!query_track_ground(session.course().world,session.car().position,cache,ground))
        throw std::runtime_error("shadow view regression has no starting road");
    const VehicleVec3 origin{ground.point[0],ground.point[1],ground.point[2]+.3};
    auto& player=const_cast<VehicleState&>(session.car());
    auto& camera=const_cast<ChaseCameraView&>(session.camera());
    const auto saved_player=player;
    const auto saved_camera=camera;
    VehicleState saved_traffic;bool saved_active=false;
    const auto pose=[&](VehicleState& car,const VehicleSpec& spec) {
        const auto& com=spec.handling.centre_of_mass;
        car.orientation={1,0,0,0};
        car.position={origin.x+com[0],origin.y+com[1],origin.z+com[2]};
    };
    pose(player,session.spec());
    if(traffic) {
        saved_traffic=traffic->car;saved_active=traffic->active;
        pose(traffic->car,traffic->setup.spec);traffic->active=true;
    }
    camera.pov=0;camera.heading=0;camera.pitch=0;camera.forward={1,0,0};camera.up={0,0,1};
    const double height=(origin.z-ground.point[2])/3;
    for(const auto& native:shadow_view_native) {
        if(native.width!=512 || !native.fov || native.two_players || native.height ||
           native.position[1] || native.position[2])continue;
        camera.fov=static_cast<std::uint16_t>(native.fov);
        camera.eye={origin.x-native.position[0],origin.y,origin.z};
        RaceGpuFrame frame;
        if(!scene.frame(session,frame,error,nullptr,reference) || !validate_recorded_commands(frame.commands,error))
            throw std::runtime_error("shadow view scene regression: "+error);
        const auto expected=car_shadow_silhouette_alpha(native.pixels,height);
        unsigned found=0;
        for(std::size_t d=0;d<frame.commands.draw_count();++d) {
            const auto view=frame.commands.draw_view(d);
            if(view.definition.name!="race-shadow/shadow silhouette" && view.definition.name!=traffic_name)continue;
            ++found;
            const auto& stream=frame.commands.resource_table().resources.at(view.definition.vertex_resource);
            const auto* vertices=reinterpret_cast<const MeshVertex*>(stream.data);
            if((view.material.alpha[0]>0)!=(expected>0) ||
               (expected && vertices[0].color[3]!=expected))
                throw std::runtime_error("production shadow fade differs from native view size/LOD: "+view.definition.name);
        }
        if(found!=(traffic?2U:1U))throw std::runtime_error("shadow fade regression lost a prepared layer");
    }
    player=saved_player;camera=saved_camera;
    if(traffic){traffic->car=saved_traffic;traffic->active=saved_active;}
}
