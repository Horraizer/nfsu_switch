// Uncached scenery selection before command uniform allocation. Some complete
// sprint views exceed the separate diagnostic executor's transient budget;
// the actual prepared renderer has static uniforms and is not subject to it.
Draws reference_selection(const std::filesystem::path& root,const TrackSlice& track,
    const RaceSession& world,const RaceGpuFrame& frame,unsigned mask,bool split=false,
    unsigned player=0,SplitLayout layout=SplitLayout::stacked) {
    const auto& driver=split&&player?world.rival(0):world;
    const auto lens=split?split_viewport(640,480,layout,player):SplitViewport{0,0,640,480};
    RenderOptions options;options.width=frame.commands.width;options.height=frame.commands.height;
    if(split) {
        // LanScene publishes the shared full target after recording the view.
        const auto viewport=split_viewport(options.width,options.height,layout,player);
        options.width=viewport.width;options.height=viewport.height;
    }
    std::string error;
    need(drive_render_camera(driver.camera(),ChaseFovMode::retail_widescreen,
        options.width,options.height,options.camera,error,double(lens.width)/lens.height),error);
    options.camera.focal_x_override=options.camera.focal_y_override*double(options.height)/options.width;
    options.camera.far_plane=frame.far_plane;
    CameraFrame basis;need(make_camera_frame(options.camera,options.width,options.height,basis,error),error);
    need(frame.commands.frame.eye[0]==float(basis.eye.x)&&frame.commands.frame.eye[1]==float(basis.eye.y)&&
         frame.commands.frame.eye[2]==float(basis.eye.z),"uncached reference must use actual frame eye");
    need(frame.commands.frame.projection[0]==float(basis.focal_x)&&frame.commands.frame.projection[1]==float(basis.focal_y),
         "uncached reference must use actual frame projection: actual="+std::to_string(frame.commands.frame.projection[0])+","+
         std::to_string(frame.commands.frame.projection[1])+" expected="+std::to_string(float(basis.focal_x))+","+std::to_string(float(basis.focal_y)));
    TrackCamera camera;
    camera.eye[0]=float(basis.eye.x);camera.eye[1]=float(basis.eye.y);camera.eye[2]=float(basis.eye.z);
    camera.target[0]=float(options.camera.target.x);camera.target[1]=float(options.camera.target.y);camera.target[2]=float(options.camera.target.z);
    camera.up[0]=float(basis.up.x);camera.up[1]=float(basis.up.y);camera.up[2]=float(basis.up.z);
    camera.aspect=float(basis.focal_y/basis.focal_x);
    camera.vertical_fov=float(options.camera.fov_y_deg*(3.14159265358979323846/180));
    camera.near_plane=float(options.camera.near_plane);camera.far_plane=float(frame.far_plane);
    TrackFrustum frustum;need(track_frustum_look_at(camera,frustum),"uncached reference frustum");
    std::vector<unsigned char> image;
    const auto zones=load_track_zones(root/"Tracks"/track_bundle_file_name(int(track.track_number)),image);
    need(zones.valid,"uncached reference source zones");
    const float xy[2]={camera.eye[0],-camera.eye[2]};const auto zone=find_track_zone(zones,xy);
    TrackCullOptions culling;culling.instance_mask=static_cast<std::uint8_t>(mask);
    if(zone==kTrackZoneNone)culling.residency.rule=TrackResidencyRule::no_zone;
    else need(track_zone_residency(zones,zone,culling.residency),"uncached reference residency");
    const auto visible=cull_track_region(track.objects,frustum,culling);
    need(visible.ok()&&visible.policy_held,"uncached original tree selection");
    TrackDrawList list;TrackDrawListOptions order;order.order=TrackDrawOrder::retail_bucket;
    need(build_track_draw_list(track.scene,visible,order,list,error),error);
    Draws wanted;
    for(const auto& entry:list.entries) {
        if(!track.scene.region.draws[entry.draw].index_count||frame.commands.movable_track_instance(entry.instance))continue;
        wanted.emplace_back(entry.instance,entry.draw);
    }
    std::sort(wanted.begin(),wanted.end());
    return wanted;
}
