#include <set>
void verify_zone_backing_finish(const std::filesystem::path& root,RaceRun& run,RaceGpuScene& scene) {
    std::string error;
    const auto need=[&](bool okay){if(!okay)throw std::runtime_error("zone backing: "+error);};
    std::set<unsigned> tracks;
    for(unsigned event=0;event<=113;++event)for(unsigned round=0;round<kRaceEventRaceSlots;++round) {
        SupportedRaceSelection selected;if(supported_race_selection(event,round,selected))tracks.insert(unsigned(selected.slot.track));
    }
    unsigned zones_checked=0,loads_checked=0;
    for(auto track:tracks) {
        std::vector<unsigned char> bytes;const auto zones=load_track_zones(root/"Tracks"/track_bundle_file_name(static_cast<std::int16_t>(track)),bytes);
        const auto table=parse_track_stream_table(bytes.data(),bytes.size());
        need(zones.ok()&&table.ok());TrackStreamSelection backing;
        need(select_track_stream_sections(table,zones,track,TrackStreamSet::zones,backing,error));
        const std::set<std::size_t> prepared(backing.records.begin(),backing.records.end());
        TrackResidencyOptions options;options.budget_bytes=track_stream_pool_bytes(table);
        TrackResidencySession native;std::vector<TrackResidencyStep> startup;
        need(native.start(table,zones,track,track_residency_table_costs(table),options,startup,error));
        const auto covered=[&](const TrackResidencyStep& step) {
            need(step.requested<=kTrackStreamRequestLimit);
            for(auto index:step.loads){need(prepared.count(index)!=0);++loads_checked;}
        };
        for(const auto& step:startup)covered(step);
        std::set<int> visited;
        for(const auto& polygon:zones.polygons) {
            if(!polygon.vertex_count||!visited.insert(polygon.key).second)continue;
            TrackResidencySample sample;
            for(int k=0;k<polygon.vertex_count;++k)for(unsigned axis=0;axis<2;++axis)
                sample.position[axis]+=polygon.vertices[k][axis]/polygon.vertex_count;
            for(unsigned k=0;k<160;++k) {
                TrackResidencyStep step;need(native.advance(sample,step,error));covered(step);
                if(k>=8&&step.set_loaded)break;
            }
            ++zones_checked;
        }
    }
    need(tracks.size()>20&&zones_checked>1000&&loads_checked>1000);
    std::cout<<"BACKING_CORPUS tracks="<<tracks.size()<<" zone_samples="<<zones_checked<<" covered_loads="<<loads_checked<<'\n';

    // Reproduce the native zone707 request beyond the first sprint finish.
    // The old permanent-only guard refuses section724 at this location.
    need((scene.track().track_number==1102||scene.track().track_number==1003)&&scene.track().backing_set==TrackStreamSet::zones);
    bool has_geometry=false;for(const auto& instance:scene.track().scene.instances)
        has_geometry|=instance.section_number==724;
    need(has_geometry);
    for(unsigned k=0;k<20;++k) {
        need(run.simulate(race_input_frame({1,0,0},run.session().timing().step),error));
        auto& state=const_cast<VehicleState&>(run.session().car());
        state.position.x=2310;state.position.y=-440;state.velocity.x=0;state.velocity.y=-40;
        need(run.commit(error));
    }
    auto& camera=const_cast<ChaseCameraView&>(run.session().camera());
    camera.eye={2310,-430,67};camera.forward={0,-1,0};camera.up={0,0,1};
    camera.heading=0xc000;camera.pitch=0;camera.fov=16384;
    RaceGpuFrame frame;need(scene.frame(run.session(),frame,error));need(validate_recorded_commands(frame.commands,error));
    need(frame.track_draws>0);
    std::cout<<"BACKING_FINISH sections="<<scene.track().sections<<" payload="<<scene.track().streaming_bytes
        <<" cache="<<scene.cache_bytes()<<" resources="<<frame.commands.resource_table().resources.size()
        <<" live="<<run.resident_count()<<" drawn="<<frame.track_draws<<'\n';
    std::cout<<"PASS native adjacent-zone request has actual uploaded geometry; live256-record cap retained\n";
}
