#include "nfsu/race_session.h"
#include "nfsu/race_setup.h"
#include "nfsu/handling_telemetry.h"
#include "nfsu/race_hud.h"
#include "nfsu/race_events.h"
#include "nfsu/track_residency.h"
#include "nfsu/track_render.h"
#include "nfsu/track_reflections.h"
#include "nfsu/stock_frame.h"
#include "nfsu/screen_text.h"
#include "nfsu/race_window.h"
#include "nfsu/race_audio.h"
#include "nfsu/race_particles.h"
#include "nfsu/car_shadow.h"
#include "nfsu/light_glows.h"
#include "nfsu/arg_path.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iomanip>
#include <locale>
#include <map>
#include <memory>
#include <set>
#include <sstream>
#include <stdexcept>

namespace fs = std::filesystem;
using namespace nfsu;
namespace {
using Clock = std::chrono::steady_clock;
struct Cost {
    double ms = 0, maximum = 0;
    std::uint64_t count = 0;
    void add(Clock::time_point begin) {
        const double elapsed = std::chrono::duration<double,std::milli>(Clock::now()-begin).count();
        ms += elapsed; maximum = std::max(maximum,elapsed); ++count;
    }
    void print(const char* name) const {
        std::cout << "cost " << name << " calls " << count << " total-ms " << ms
                  << " mean-ms " << (count ? ms/count : 0) << " max-ms " << maximum << '\n';
    }
};
void require(bool okay, const std::string& error) { if (!okay) throw std::runtime_error(error); }
void write_text(const fs::path& path, const std::string& text) {
    require(!fs::exists(path),"output already exists");
    std::ofstream out(path,std::ios::binary); out << text;
    require(bool(out),"cannot write output");
}
// A path as UTF-8 text. generic_string() would convert to the narrow
// encoding, which throws for a character outside it (docs/PATH_ARGUMENTS.md).
std::string utf8_text(const fs::path& path) {
    const auto text = path.generic_u8string();
    return std::string(text.begin(), text.end());
}
bool within(const fs::path& path, const fs::path& root) {
    // Make relative arguments absolute before normalizing their existing
    // prefix. An unresolved output is refused; an unresolvable root falls
    // back to its normalized absolute spelling for containment checks.
    const auto absolute = [](const fs::path& value) {
        return fs::u8path(fs::absolute(value).generic_u8string());
    };
    const auto inside = [](const fs::path& p, const fs::path& r) {
        // Windows comparisons ignore case, as this desktop's files do.
        auto ps = utf8_text(p), rs = utf8_text(r);
#ifdef _WIN32
        for (char& c : ps) if (c >= 'A' && c <= 'Z') c += 'a'-'A';
        for (char& c : rs) if (c >= 'A' && c <= 'Z') c += 'a'-'A';
#endif
        return ps == rs || (ps.size() > rs.size() && ps.compare(0,rs.size(),rs)==0 && ps[rs.size()]=='/');
    };
    auto p = absolute(path), r = absolute(root);
    // Reject a direct child before canonicalizing.
    if (inside(p.lexically_normal(),r.lexically_normal())) return true;
    require(fs::is_directory(r),"root directory missing");
    const auto resolved = [](const fs::path& value) {
        try { return fs::weakly_canonical(value); }
        catch (const fs::filesystem_error&) {
            // MinGW Release can reject a newly created directory. A lexical
            // fallback is safe only if no existing ancestor is a link.
            fs::path ancestor=value.root_path();
            for (const auto& part : value.relative_path()) {
                ancestor /= part;
                std::error_code ec;
                if (fs::is_symlink(fs::symlink_status(ancestor,ec))) throw;
            }
            return value.lexically_normal();
        }
    };
    return inside(resolved(p),resolved(r));
}

// Actual reads follow residency loads, and releases destroy their parsed
// bytes. Only the current set is assembled for rendering; no whole-track
// geometry or decoded-texture cache grows across laps.
class RaceScene {
public:
    RaceParticles particles;
    bool particles_enabled = true;
    TrackStreamFiles files;
    const TrackStreamTable& table;
    std::map<std::size_t,std::unique_ptr<TrackStreamLoad>> sections;
    TrackStreamLoad texture_spillover;
    bool texture_spillover_ready = false;
    std::array<std::vector<unsigned char>,3> texture_bytes;
    std::array<TexturePackSet,3> texture_packs;
    RenderTextureSet textures;
    PreparedTrackScene track;
    TrackDrawList list;
    StockFrameInputs car_inputs;
    StockFrame car;
    RaceHudAtlas hud_atlas;
    CarShadowAsset shadow;
    std::vector<PreparedInstance> base;
    CarGlowAnchors glow_anchors{};
    bool glows = false;
    bool dirty = true;
    bool tuned = false; // Render selection only.
    bool car_shading = false;
    std::uint64_t loads = 0, releases = 0, bytes_read = 0;
    Cost io_cost, prepare_cost, render_cost;
    explicit RaceScene(const TrackStreamTable& t, bool effects) : particles_enabled(effects), table(t) {}
    void load_car(const fs::path& root, const fs::path& texture) {
        std::string error;
        require(load_race_hud_atlas(root,hud_atlas,error),error);
        if (!read_stock_frame_inputs(root,car_inputs,tuned))
            throw std::runtime_error(car_inputs.read_error(root));
        require(prepare_race_car_frame(car_inputs,root,tuned ? tuned_240sx_request() : stock_240sx_request(),
                tuned ? tuned_240sx_skin() : stock_240sx_skin(),car,error,car_shading),error);
        require(prepare_car_shadow(car_inputs,car.car_pack,shadow,error),error);
        base = car.scene.instances;
        glow_anchors = collect_car_glow_anchors(car);
        const std::array<fs::path,3> paths = {
            texture, root/"Global/INGAMEB.BUN", root/"Global/GLOBALB.BUN"};
        for (std::size_t i=0;i<paths.size();++i) {
            texture_packs[i] = load_texture_pack_set(paths[i],texture_bytes[i]);
            require(texture_packs[i].complete,"track texture source is incomplete: "+utf8_text(paths[i]));
        }
    }
    void apply(const TrackResidencyStep& step) {
        if (step.loads.empty() && step.releases.empty()) return;
        const auto begin = Clock::now(); std::string error;
        for (auto r : step.releases) { sections.erase(r); ++releases; }
        for (auto r : step.loads) {
            auto load = std::make_unique<TrackStreamLoad>();
            TrackStreamSelection selection;
            selection.records = {r}; selection.reasons = {TrackStreamReason::zone_list};
            require(load_track_stream_sections(table,selection,files,{},*load,error),error);
            require(load->sections.size()==1 && load->sections[0].loaded,"stream section did not load");
            bytes_read += load->bytes_read; ++loads;
            sections[r] = std::move(load);
        }
        dirty = true; io_cost.add(begin);
    }
    void prepare() {
        if (!dirty) return;
        const auto begin = Clock::now(); std::string error;
        TrackRegionReport geometry; TrackObjectReport objects;
        geometry.mesh_table_valid = true;
        for (const auto& entry : sections) {
            for (auto mesh : entry.second->geometry.meshes) {
                mesh.index = geometry.meshes.size(); geometry.meshes.push_back(std::move(mesh));
            }
            for (auto section : entry.second->objects.sections) {
                section.index = objects.sections.size(); objects.sections.push_back(std::move(section));
            }
        }
        const auto wanted = track_region_texture_hashes(geometry);
        std::vector<RenderTextureSource> sources;
        for (const auto& entry : sections) {
            std::vector<RenderTextureSource> part; std::vector<std::size_t> owners;
            track_stream_texture_sources(*entry.second,&wanted,part,owners);
            sources.insert(sources.end(),part.begin(),part.end());
        }
        if (!texture_spillover_ready) {
            std::vector<std::size_t> resident;
            for (const auto& entry : sections) resident.push_back(entry.first);
            TrackStreamSelection selected;
            require(select_track_texture_spillover_1003(table,resident,selected,error),error);
            TrackStreamLoadLimits limits;
            limits.max_total_bytes = kStreamingRegionBudgetPlaceholder;
            require(load_track_stream_sections(table,selected,files,limits,
                                               texture_spillover,error),error);
            require(texture_spillover.ok(),"event-1 texture spillover parse failed");
            for (const auto& section : texture_spillover.sections)
                require(section.loaded && section.geometry_roots==0 &&
                        section.scenery_sections==0 && section.texture_packs!=0,
                        "event-1 texture spillover section is not texture-only");
            texture_spillover_ready = true;
        }
        std::vector<RenderTextureSource> extra_sources;
        std::vector<std::size_t> extra_owners;
        track_stream_texture_sources(texture_spillover,&wanted,extra_sources,extra_owners);
        for (std::size_t i=0;i<extra_sources.size();++i)
            if (sections.count(texture_spillover.sections[extra_owners[i]].record)==0)
                sources.push_back(extra_sources[i]);
        for (std::size_t i=0;i<texture_packs.size();++i) {
            RenderTextureSource source; source.bytes = &texture_bytes[i];
            for (const auto& pack : texture_packs[i].packs) source.packs.push_back(&pack);
            source.complete = texture_packs[i].complete; source.wanted = &wanted;
            sources.push_back(std::move(source));
        }
        RenderTextureLimits limits; limits.max_total_bytes = 512ULL*1024*1024;
        require(decode_render_texture_sources(sources,limits,textures,error),error);
        TrackSceneOptions options; options.region.max_texture_bytes = limits.max_total_bytes;
        require(build_track_scene(geometry,objects,textures,options,track,error),error);
        std::vector<TrackVisibleInstance> visible;
        for (const auto& section : track.sections) for (std::size_t k=0;k<section.source_instances;++k) {
            TrackVisibleInstance item; item.section_index=section.section_index; item.instance_index=k;
            visible.push_back(item);
        }
        TrackDrawListOptions order; order.order=TrackDrawOrder::retail_bucket;
        require(build_track_draw_list(track,visible,order,list,error),error);
        dirty=false; prepare_cost.add(begin);
    }
    WetRoadLamps lamps;
    bool wet_road_reflections = false;
    std::vector<unsigned char> render(const RaceSession& session) {
        prepare(); const auto begin=Clock::now(); std::string error;
        RenderOptions options; options.width=640; options.height=360;
        options.material_state = MaterialStateMode::retail;
        options.car_shading=car_shading;
        require(drive_render_camera(session.camera(),ChaseFovMode::vertical_4_3,640,360,options.camera,error),error);
        Frame background; RenderStats stats; TrackRenderStats track_stats;
        RenderOptions track_options = options;
        track_options.sky = true;
        track_options.wet_road_reflections = wet_road_reflections;
        track_options.wet_lamps = wet_road_reflections ? &lamps : nullptr;
        track_options.material_state = MaterialStateMode::retail;
        require(render_track_draw_list(track,list,track_options,{},background,stats,track_stats,error),error);
        const auto shadow_pose = place_car_shadow(session);
        require(render_car_shadow(shadow,shadow_pose,track_options,background,stats,error),error);
        require(drive_place_instances(base,session.car(),session.spec(),car.scene.instances,
                                      &session.animation()),"car placement failed");
        require(render_stock_frame(car,options,error),error);
        require(!tuned || car.stats.triangles_fallback == 0,
                "tuned race car has fallback triangles");
        Frame composed;
        require(compose_race_frame(background,car.frame,composed),"frame sizes differ");
        if (glows) {
            CameraFrame camera;
            require(make_camera_frame(options.camera,options.width,options.height,camera,error),error);
            const auto sprites = place_car_glows(glow_anchors,car.scene,camera,
                                                 session.animation().braking);
            require(draw_car_glows(composed,options,sprites,stats,error),error);
        }
        std::vector<unsigned char> rgba = composed.rgba();
        if (particles_enabled)
            require(draw_race_particles(rgba,options,particles,error,&background,&car.frame),error);
        RaceHudFrame hud;
        build_race_hud_frame(session,640,360,hud);
        composite_race_hud(hud_atlas,hud,rgba.data(),640,360,640*4);
        render_cost.add(begin); return rgba;
    }
};

int run(int argc, char** argv) {
    fs::path root,input,output,generate;
    bool live=false, smoke=false, tuned=false;
    bool particles_enabled=true, particle_events=false;
    bool glows=false;
    bool car_shading=false;
    std::set<std::size_t> frames;
    bool wet_road_reflections=false;
    bool explicit_performance=false, handling_telemetry=false;
    VehicleUpgradeLevels performance;
    // The path arguments arrive in the ANSI code page on Windows; convert
    // them as the other tools do, so a path outside ASCII opens.
    std::string argument_error;
    require(nfsu::check_arguments(argc,argv,1,argument_error),argument_error);
    for (int i=1;i<argc;++i) {
        const std::string a=argv[i];
        if (a=="--help") {
            std::cout << "nfsu_race --root RETAIL (--input RECORDING | --generate RECORDING | --live)\n"
                         "  [--out NEW_DIRECTORY --frames 180,600,6000,22000,40000]\n"
                         "  [--particles=on|--particles=off] [--particle-events] [--tuned] [--glows]\n"
                         "  [--car-shading on|off]\n"
                         "  [--wet-road-reflections on|off]\n"
                         "  [--performance-levels name=0..3,...] [--handling-telemetry]\n"
                         "  live mode plays retail race audio when the device and banks are available\n";
            return 0;
        }
        if (a=="--tuned") { tuned=true; continue; }
        if (a=="--handling-telemetry") { handling_telemetry=true; continue; }
        if (a=="--live") { live=true; continue; }
        if (a=="--live-smoke") { live=true; smoke=true; continue; }
        if (a=="--particles=on") { particles_enabled=true; continue; }
        if (a=="--particles=off") { particles_enabled=false; continue; }
        if (a=="--particle-events") { particle_events=true; continue; }
        if (a=="--glows") { glows=true; continue; }
        require(i+1<argc,"missing option value"); const std::string value=argv[++i];
        if (a=="--performance-levels") {
            require(!explicit_performance,"duplicate --performance-levels");
            require(parse_performance_levels(value,performance,argument_error),argument_error);
            explicit_performance=true;
        }
        else if (a=="--root") root=nfsu::argument_path(argv[i]);
        else if (a=="--input") input=nfsu::argument_path(argv[i]);
        else if (a=="--generate") generate=nfsu::argument_path(argv[i]);
        else if (a=="--out") output=nfsu::argument_path(argv[i]);
        else if (a=="--car-shading") {
            require(value=="on" || value=="off","--car-shading needs on or off");
            car_shading=value=="on";
        }
        else if (a=="--wet-road-reflections") {
            require(value=="on" || value=="off", "--wet-road-reflections must be on or off");
            wet_road_reflections = value=="on";
        }
        else if (a=="--frames") {
            std::istringstream in(value); std::string token;
            while (std::getline(in,token,',')) {
                require(!token.empty() && token.find_first_not_of("0123456789")==std::string::npos,"bad frame step");
                const auto n=std::stoull(token); require(n>0 && n<=kRaceInputMaxTicks,"frame step out of range");
                frames.insert(static_cast<std::size_t>(n));
            }
        } else throw std::runtime_error("unknown option: "+a);
    }
    require(!root.empty() && int(live)+int(!input.empty())+int(!generate.empty())==1,"choose one input mode and --root");
    require(frames.empty() || !output.empty(),"--frames requires --out");
    require(!live || (output.empty() && frames.empty()),"live mode does not export files");
    require(fs::is_directory(root),"retail root is not a directory");
    for (const auto& path : {output,generate}) if (!path.empty()) {
        require(!fs::exists(path),"output already exists");
        require(!within(path,root),"output must be outside the retail tree");
    }
    std::string error; std::vector<RaceInputRun> runs;
    if (!input.empty()) {
        require(fs::file_size(input)<=4*1024*1024,"input exceeds 4 MiB");
        std::ifstream in(input,std::ios::binary);
        const std::string text((std::istreambuf_iterator<char>(in)),{});
        require(bool(in) && parse_race_input(text,runs,error),error);
    }
    std::vector<unsigned char> image;
    const auto events=load_race_event_table(root/"Frontend/FRONTB.LZC",image);
    const auto tracks=load_track_info_table(root/"Global/GLOBALB.BUN",image);
    const auto* event=find_race_event(events,1);
    require(event && event->races.size()==1,"event 1 missing or not one race");
    const auto& slot=event->races[0]; const auto* info=find_track_info(tracks,slot.track);
    require(info && slot.track==1003 && slot.laps==2 && slot.direction==0,"event 1 is not the supported forward two-lap 1003 circuit");
    const auto trackb=root/"Tracks"/track_bundle_file_name(slot.track);
    VehicleTrackCourse course;
    require(load_vehicle_track_course(trackb,slot.laps,slot.direction,info->lap_skip,0,course,error),error);
    const auto zones=load_track_zones(trackb,image);
    const auto table=parse_track_stream_table(image.data(),image.size());
    const auto costs=track_residency_table_costs(table);
    TrackResidencyOptions options; options.budget_bytes=track_stream_pool_bytes(table);
    TrackResidencySession residency; std::vector<TrackResidencyStep> startup;
    require(residency.start(table,zones,slot.track,costs,options,startup,error),error);
    RaceScene scene(table,particles_enabled); scene.tuned=tuned;
    scene.files.stream=root/"Tracks"/track_stream_file_name(*info);
    scene.glows = glows;
    scene.car_shading=car_shading;
    scene.wet_road_reflections=wet_road_reflections;
    scene.files.pack=root/"Tracks"/track_stream_pack_file_name(*info);
    scene.files.has_pack=fs::exists(scene.files.pack);
    scene.load_car(root,root/"Tracks"/track_texture_file_name(slot.track));
    if (wet_road_reflections) require(load_wet_road_lamps(root, course.world, scene.lamps, error),error);
    Sha256 sequence; const std::string version="NFSU_RACE_STATE 1\n";
    sequence.update(version.data(),version.size());
    for (const auto& step : startup) {
        scene.apply(step); const auto line=track_residency_step_line(table,step)+"\n";
        sequence.update(line.data(),line.size());
    }
    VehicleSpec spec=stock_240sx_vehicle_spec();
    if (explicit_performance) {
        require(load_race_vehicle_spec(root/"Global/GLOBALB.BUN","240SX",performance,spec,error),error);
        std::cout << "performance-levels " << performance_levels_text(performance) << '\n';
    }
    if (handling_telemetry && !explicit_performance) std::cout << "performance-levels legacy-stock\n";
    HandlingTelemetryLog handling_log(handling_telemetry ? stdout : nullptr);
    RaceSession session(course,spec); require(session.start(error),error);
    audio::RaceAudioService sound;
    if (live && !smoke) {
        std::string diagnostic;
        sound.start(root, diagnostic);
        std::cout << "audio: " << diagnostic << '\n';
    }
    RaceRouteDriver driver(RaceDrivingLine::event1_forward); std::vector<RaceInputRun> recording;
    std::ofstream states, trajectory;
    if (!output.empty()) {
        require(fs::create_directory(output),"cannot create output directory");
        states.open(output/"states.txt",std::ios::binary); require(bool(states),"cannot create state hashes");
        trajectory.open(output/"trajectory.csv",std::ios::binary);
        require(bool(trajectory),"cannot create trajectory audit");
        trajectory.imbue(std::locale::classic()); trajectory << std::setprecision(17);
        trajectory << "tick,physics,lap,x,y,z,speed,up,contacts,road_wheels,resets\n";
    }
    Cost simulation,streaming,hashing;
    std::size_t run_index=0,run_tick=0; std::uint64_t resets=0;
    auto tick=[&](const GamepadFrame& pad) {
        const auto consumed_us=static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(
            Clock::now().time_since_epoch()).count());
        auto begin=Clock::now(); require(session.advance(pad,error),error); simulation.add(begin);
        require(handling_log.write(session,pad,consumed_us,live),"handling telemetry write failed");
        const auto previous_smoke=scene.particles.smoke_count();
        const auto previous_sparks=scene.particles.spark_count();
        if (particles_enabled) scene.particles.advance(session);
        if (particle_events &&
            (scene.particles.smoke_count()!=previous_smoke ||
             scene.particles.spark_count()!=previous_sparks ||
             session.telemetry().impulses))
            std::cout << "particle tick " << session.timing().step << " physics "
                      << scene.particles.step() << " smoke " << scene.particles.smoke_count()
                      << " sparks " << scene.particles.spark_count() << " impulses "
                      << (session.physics_telemetry()[0].impulses+
                          session.physics_telemetry()[1].impulses) << '\n';
        if (live && !smoke) sound.on_tick(audio::race_audio_snapshot(session));
        resets=session.telemetry().resets;
        if (trajectory.is_open()) for (const auto& t : session.physics_telemetry()) {
            if (!t.step) continue; // Countdown holds the pose without physics.
            const auto& q=t.orientation;
            const double up=1-2*(q.x*q.x+q.y*q.y);
            const double v=vehicle_sqrt(t.velocity.x*t.velocity.x+t.velocity.y*t.velocity.y);
            const auto wheels=std::count(t.wheel_surface.begin(),t.wheel_surface.end(),VehicleTrackSurface::road);
            trajectory << session.timing().step << ',' << t.step << ',' << t.lap << ','
                << t.position.x << ',' << t.position.y << ',' << t.position.z << ','
                << v << ',' << up << ',' << t.contacts << ',' << wheels << ',' << t.resets << '\n';
        }
        begin=Clock::now(); const auto& car=session.car();
        TrackResidencySample sample; sample.position[0]=float(car.position.x); sample.position[1]=float(car.position.y);
        sample.velocity[0]=float(car.velocity.x); sample.velocity[1]=float(car.velocity.y);
        TrackResidencyStep step; require(residency.advance(sample,step,error),error); streaming.add(begin);
        scene.apply(step);
        begin=Clock::now(); auto record=session.record();
        const auto line=track_residency_step_line(table,step)+"\n";
        record.insert(record.end(),line.begin(),line.end());
        for (const auto& entry : scene.sections) {
            const auto r=static_cast<std::uint32_t>(entry.first);
            for (unsigned k=0;k<4;++k) record.push_back(static_cast<std::uint8_t>(r>>(8*k)));
        }
        const auto digest=sha256(record.data(),record.size());
        sequence.update(digest.data(),digest.size());
        if (states.is_open()) states << session.timing().step << ' ' << sha256_hex(digest) << '\n';
        hashing.add(begin);
    };
    if (live) {
        require(run_race_window([&](const GamepadFrame& pad) {
            if (session.timing().outcome==RaceOutcome::running) tick(pad);
        },[&]() { return scene.render(session); },error,smoke),error);
    } else {
        while (session.timing().outcome==RaceOutcome::running && session.timing().step<kRaceInputMaxTicks) {
            GamepadFrame pad;
            if (!generate.empty()) { pad=driver.sample(session); append_race_input(recording,pad); }
            else {
                require(run_index<runs.size(),"recording ended before race finished");
                pad=race_input_frame(runs[run_index],session.timing().step);
                if (++run_tick==runs[run_index].ticks) { ++run_index; run_tick=0; }
            }
            tick(pad);
            if (frames.erase(session.timing().step)) {
                const auto rgba=scene.render(session); FrameWriteResult result;
                const auto path=output/("frame-"+std::to_string(session.timing().step)+".tga");
                require(write_rgba_tga(640,360,rgba,path,{},result),result.error);
                std::ifstream in(path,std::ios::binary); const std::string bytes((std::istreambuf_iterator<char>(in)),{});
                std::cout << "frame " << session.timing().step << " tga-sha256 " << sha256_hex(sha256(bytes.data(),bytes.size())) << '\n';
            }
        }
        require(session.timing().outcome==RaceOutcome::finished,"driver did not finish within tick limit");
        require(frames.empty(),"requested frame is after the finish");
        require(input.empty() || (run_index==runs.size() && run_tick==0),"recording has trailing input after finish");
        if (!generate.empty()) write_text(generate,format_race_input(recording));
    }
    if (states.is_open()) { states.flush(); require(bool(states),"state hash write failed"); }
    if (trajectory.is_open()) { trajectory.flush(); require(bool(trajectory),"trajectory write failed"); }
    sound.stop();
    const auto& result=session.timing().racers[0];
    std::cout << "event 1 track 1003 laps " << result.highest_lap << " outcome " << race_outcome_name(session.timing().outcome)
              << " steps " << session.timing().step << " resets " << resets << '\n';
    for (int i=1;i<=result.highest_lap && i<kRaceLapSlots;++i)
        std::cout << "lap " << i << " ticks " << result.lap_ticks[i] << " time " << format_race_ticks(result.lap_ticks[i]) << '\n';
    std::cout << "total ticks " << result.finish_ticks << " time " << format_race_ticks(result.finish_ticks)
              << "\nstate-sha256 " << sha256_hex(sequence.finish()) << '\n'
              << "stream loads " << scene.loads << " releases " << scene.releases << " bytes-read " << scene.bytes_read << '\n';
    simulation.print("simulation"); streaming.print("residency"); hashing.print("hashing");
    scene.io_cost.print("stream-io"); scene.prepare_cost.print("scene-prepare"); scene.render_cost.print("render-hud");
    return 0;
}
}
int main(int argc,char** argv) {
    try { return run(argc,argv); }
    catch (const std::exception& e) { std::cerr << "error: " << e.what() << '\n'; return 1; }
}
