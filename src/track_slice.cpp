#include "nfsu/track_slice.h"
#include "nfsu/draw_execution.h"
#include "nfsu/race_events.h"
#include "nfsu/race_route.h"
#include "nfsu/texture_decode.h"

#include <algorithm>
#include <cmath>
#include <utility>

#include <fstream>

namespace nfsu {

namespace {

// Placed-material hashes a circuit's permanent set requests that no loaded
// source holds: resident stream sections, the texture-only Y spillover, the
// track's own TEX pack, INGAMEB and GLOBALB, in that order. Read out of the
// owner's retail installation by loading every supported circuit event with
// this pipeline and listing what stayed unresolved beyond the two hashes
// docs/TRACK_MATERIALS.md documents without a texture-info record
// (`0x2D1A7A07`, `0x74CFB820`), which stay unresolved exactly as event 1
// ships them. The original loader's generic fallback substitutes the GLOBALB
// DEFAULTALPHA record for any missed hash (speed.exe 0x53F9C0/0x5461C0, the
// pointer chain docs/ATLANTIC.md traces); each hash here is the same
// recovered rule applied to one more verified-absent material, and the
// per-circuit tests pin the substitution. 0x828DCF0B is Atlantic's power
// tower; 0x2F93D9E1 and 0x6D1AFA74 are track 1007's and 1008's own absent
// materials.
std::vector<std::uint32_t> absent_placed_hashes(std::uint32_t track) {
    switch (track) {
    case 1005: return {0x828DCF0BU};
    case 1007: return {0x2F93D9E1U, 0x828DCF0BU};
    case 1008: return {0x6D1AFA74U, 0x828DCF0BU};
    // The L1RB drift city tracks place the same 19 hashes no source of
    // their sets provides - verified absent from the stream sections, both
    // stream files' packs, TEX1301TRACK, INGAMEB, GLOBALB, INGAMEA and
    // DYNTEX (the scratch hash-scan of the authoring session). The L1RC
    // drift city tracks place a different 56, verified absent the same
    // way against their own region's sources.
    case 1301: case 1302: case 1303: case 1304: return {
        0x05FA675U, 0x1C99A500U, 0x31403B27U, 0x31403B2AU, 0x31403B2BU,
        0x31403F69U, 0x31403F6CU, 0x490472FAU, 0x490472FBU, 0x59479E38U,
        0x59479E39U, 0x59479E3AU, 0x59479E3BU, 0x59479E3CU, 0x59479E3DU,
        0x6CCD8E3EU, 0xBC64FC61U, 0xCAD1BC2FU, 0xE5500D49U};
    case 1305: case 1306: case 1307: case 1308: return {
        0x017B16EE5U, 0x1ADA1600U, 0x1ADA1620U, 0x249C09BDU, 0x2660517BU,
        0x31403B28U, 0x31403B2CU, 0x31403F69U, 0x31403F6CU, 0x490472FAU,
        0x490472FBU, 0x4D4FF26EU, 0x4D556E38U, 0x4F0046D5U, 0x558888A9U,
        0x558888ABU, 0x5671B2B5U, 0x59D41788U, 0x5C9E0EF0U, 0x613A1326U,
        0x6CCD8E3EU, 0x6D5BBB0BU, 0x6E7C9B65U, 0x7273F516U, 0x76B8B2AFU,
        0x798875C8U, 0x7ECCB6D7U, 0x8612A877U, 0x8612A890U, 0x089FD9A2U,
        0x98AF887CU, 0x9BBD21C1U, 0x9DAC57E2U, 0xA9CF103BU, 0xAD960D3DU,
        0xBC64FC61U, 0xC356E407U, 0xC42945C7U, 0xC567BAC7U, 0xCAD1BC2FU,
        0xD220B1D2U, 0xD220BA53U, 0xD2427068U, 0xD85A1222U, 0xD85A1223U,
        0xD9CD6BF6U, 0xDF06401AU, 0xE5500D43U, 0xE5500D49U, 0xE5A6EF40U,
        0xEBE15E8EU, 0xF1DEFFD4U, 0xFA5F0CF1U, 0xFC1C4688U, 0x0FCB10CU,
        0x00FE562FU};
    // The drag tracks' absent sets are small and per shared image: 1201 and
    // 1210 one four-hash set, 1202 its own pair, 1206 and 1214 one
    // five-hash set, and 1207 four more (verified absent against each
    // track's own region sources by the scratch hash-scan; the sets appear
    // only in other regions' texture-only Y0 sections, which these tracks
    // never load). 1207 also places 0x74CFB820, which stays unresolved
    // exactly as tracks 1007 and 1008 ship it.
    case 1201: case 1210: return {
        0x31403F69U, 0x31403F6CU, 0x6D37FD79U, 0xE5500D49U};
    case 1202: return {0x1ADA161EU, 0xB5185FD3U};
    case 1206: case 1214: return {
        0x2FD08513U, 0x2FD08514U, 0x2FD08515U, 0x2FD2B698U, 0x2FD2B699U};
    case 1207: return {0x3EAE8FC3U, 0x5E449534U, 0x647E15CEU, 0x0A1B0C98U};
    // The sprint tracks place the L1RA family's own absent hashes,
    // verified absent against each track's own section sources the same
    // way (1102 and 1103 place nothing absent). 1106, 1107 and 1108 also
    // place 0x74CFB820, which stays unresolved as ever.
    case 1104: case 1105: return {0x828DCF0BU};
    case 1106: case 1107: case 1108: case 1109: return {
        0x2F93D9E1U, 0x828DCF0BU};
    default: return {};
    }
}

} // namespace

bool load_track_slice(FileSystemService& files, const std::filesystem::path& root,
                      TrackSlice& slice, std::string& error, AssetCheckObserver* observer,
                      TrackSliceLoadObserver* loading, std::uint32_t event_number, bool world_mips,unsigned race_index,
                      TrackStreamSet backing_set) {
    slice = {}; error.clear();
    if(backing_set!=TrackStreamSet::permanent&&backing_set!=TrackStreamSet::zones) {
        error="unsupported race backing set";return false;
    }
    TrackSlice result;
    result.backing_set=backing_set;
    AssetManifest manifest;
    SupportedRaceSelection choice;
    std::string manifest_name;
    if(!supported_race_selection(event_number,race_index,choice)||
       !first_lap_asset_set_name(std::uint32_t(choice.slot.track),manifest_name)) {
        error="unsupported track event or tournament round";return false;
    }
    if (!retail_asset_manifest(manifest_name, manifest) ||
        !check_asset_install(files, root, manifest, result.assets, error, observer)) return false;
    if (result.assets.verdict != AssetVerdict::complete) {
        error = std::string("first-lap asset check: ") + asset_verdict_name(result.assets.verdict);
        for (const auto& file : result.assets.files)
            if (file.status != AssetStatus::ok) error += "\n" + file.expected.path + ": " + file.detail;
        return false;
    }
    const auto checkpoint = [&](const char* phase) {
        if (loading) loading->checkpoint(phase);
    };
    checkpoint("track table");
    const auto path = [&](const std::string& relative) -> std::filesystem::path {
        for (const auto& file : result.assets.files)
            if (asset_names_match(file.expected.path, relative)) return root / file.found_as;
        return {};
    };
    SupportedRace race;
    if (!load_supported_race(path("FrontEnd/FrontB.lzc"), path("Global/GLOBALB.BUN"),
                             event_number, race, error,race_index)) return false;
    result.event_number = event_number;result.race_index=race_index;
    result.track_number = static_cast<std::uint32_t>(race.slot.track);
    const auto trackb = path("Tracks/" + track_bundle_file_name(race.slot.track));
    {
        std::vector<unsigned char> image;
        const auto zones = load_track_zones(trackb, image);
        const auto table = parse_track_stream_table(image.data(), image.size());
        if(!track_world_motion_models(image,result.world_models,error))return false;
        TrackStreamSelection selected;
        if (!zones.ok() || !table.ok() ||
            !select_track_stream_sections(table, zones, result.track_number, backing_set, selected, error)) {
            if (error.empty()) error = "invalid event-1 track table or visibility zones";
            return false;
        }
        // The native live request remains capped at256. A static GPU backing
        // union may cover more mutually exclusive zones without making them
        // simultaneously live in the original residency model.
        const std::size_t backing_limit=backing_set==TrackStreamSet::zones?1024:256;
        if (selected.records.size() > backing_limit || selected.route_numbers_outside || selected.route_zones_missing) {
            error = "event-1 path requires sections outside the bounded permanent set";
            return false;
        }
        TrackStreamFiles streams;
        // The region stream the track's record names ("STREAM%s.BUN" with
        // its region): L1RA for the circuits, L1RB for drift track 1301.
        streams.stream = path("Tracks/" + track_stream_file_name(race.track));
        streams.has_pack = true;
        streams.pack = path("Tracks/" + track_stream_pack_file_name(race.track));
        // Permanent1003 keeps the event-1 loader policy its pinned replay and
        // frame hashes were recorded with. Expanded backing and other tracks load the
        // bounded way Atlantic introduced (issue #327): source bytes are
        // released section by section and only meshes a valid instance can
        // select survive, which bounds the loader peak of sets larger than
        // event 1's.
        const bool bounded_loader = result.track_number != 1003 || backing_set==TrackStreamSet::zones;
        TrackStreamLoadLimits limits;
        limits.max_total_bytes = kPersistentBudgetPlaceholder;
        limits.release_non_texture_bytes = true;
        limits.release_source_bytes = bounded_loader;
        limits.poll = [&] { checkpoint("stream sections"); };
        TrackStreamLoad load;
        if (!load_track_stream_sections(table, selected, streams, limits, load, error) || !load.ok()) {
            if (error.empty()) error = "event-1 streamed section parse failed";
            return false;
        }
        checkpoint("sections parsed and merged");
        result.sections = selected.records.size();
        result.input_bytes = load.bytes_read;
        if (bounded_loader && !retain_instanced_track_meshes(load.geometry,load.objects,error,result.world_models)) return false;
        // The merged reports own their arrays. Drop per-section parser
        // copies and non-texture source ranges before texture preparation.
        std::vector<unsigned char>().swap(image);
        for (auto& section : load.sections) {
            section.geometry = {};
            section.objects = {};
            if (section.packs.packs.empty()) std::vector<unsigned char>().swap(section.bytes);
        }
        for(const auto& section:load.sections)for(const auto& a:section.animations)
            if(std::none_of(result.animations.begin(),result.animations.end(),[&](const auto& old){return old.hash==a.hash;}))result.animations.push_back(a);
        auto wanted = track_region_texture_hashes(load.geometry);
        result.animations.erase(std::remove_if(result.animations.begin(),result.animations.end(),[&](const auto& a){
            return std::none_of(a.frames.begin(),a.frames.end(),[&](auto hash){return std::binary_search(wanted.begin(),wanted.end(),hash);});
        }),result.animations.end());
        for(const auto& a:result.animations)wanted.insert(wanted.end(),a.frames.begin(),a.frames.end());
        std::sort(wanted.begin(),wanted.end());wanted.erase(std::unique(wanted.begin(),wanted.end()),wanted.end());
        const auto absent = absent_placed_hashes(result.track_number);
        if (!absent.empty()) {
            wanted.push_back(kTrackDefaultAlphaHash);
            std::sort(wanted.begin(), wanted.end());
            wanted.erase(std::unique(wanted.begin(), wanted.end()), wanted.end());
        }
        std::vector<RenderTextureSource> sources;
        std::vector<std::size_t> owners;
        track_stream_texture_sources(load, &wanted, sources, owners);
        // Only source order is needed now. Decode each source from a fresh
        // range read below, so the two section sets never retain raw bytes
        // alongside decoded GPU blocks.
        for (auto& source : sources) { source.packs.clear(); source.bytes = nullptr; }
        for (auto& section : load.sections) {
            section.packs = {};
            std::vector<unsigned char>().swap(section.bytes);
        }
        const std::size_t regular_sources = owners.size();
        TrackStreamSelection spillover_selection;
        if (!select_track_texture_spillover_l1ra(table, selected.records, result.track_number,
                                                 spillover_selection, error)) return false;
        TrackStreamLoad spillover;
        TrackStreamLoadLimits spillover_limits;
        spillover_limits.max_total_bytes = kStreamingRegionBudgetPlaceholder;
        spillover_limits.release_non_texture_bytes = true;
        spillover_limits.poll = [&] { checkpoint("texture spillover sections"); };
        if (!load_track_stream_sections(table, spillover_selection, streams,
                                        spillover_limits, spillover, error) || !spillover.ok()) {
            if (error.empty()) error = "event-1 texture spillover parse failed";
            return false;
        }
        for (const TrackStreamSection& section : spillover.sections) {
            if (!section.loaded || section.geometry_roots != 0 ||
                section.scenery_sections != 0 || section.texture_packs == 0) {
                error = "event-1 texture spillover section is not texture-only";
                return false;
            }
        }
        result.texture_spillover_sections = spillover.sections.size();
        result.texture_spillover_bytes = spillover.bytes_read;
        std::vector<RenderTextureSource> extra_sources;
        std::vector<std::size_t> extra_owners;
        track_stream_texture_sources(spillover, &wanted, extra_sources, extra_owners);
        sources.insert(sources.end(), extra_sources.begin(), extra_sources.end());
        for (auto& source : sources) { source.packs.clear(); source.bytes = nullptr; }
        for (auto& section : spillover.sections) {
            section.packs = {};
            std::vector<unsigned char>().swap(section.bytes);
        }
        const std::size_t stream_sources = sources.size();
        const std::string names[] = {"Tracks/" + track_texture_file_name(race.slot.track), "Global/INGAMEB.BUN", "Global/GLOBALB.BUN"};
        // Common race effects (including the neon turn barriers) live in
        // INGAMEB, not in the track stream. Read metadata before freezing the
        // wanted texture set; only used families retain their actual frames.
        auto definitions=result.animations;
        for(const auto& section:spillover.sections)for(const auto& a:section.animations) {
            const auto prior=std::find_if(definitions.begin(),definitions.end(),[&](const auto& old){return old.hash==a.hash;});
            if(prior==definitions.end())definitions.push_back(a);
            else if(prior->fps!=a.fps||prior->clock!=a.clock||prior->frames!=a.frames) {
                error="conflicting spillover animation definition";return false;
            }
        }
        for(const auto& name:names) {
            checkpoint("common animation metadata");
            if(!load_texture_animations(path(name),definitions,error))return false;
        }
        definitions.erase(std::remove_if(definitions.begin(),definitions.end(),[&](const auto& a){
            return std::none_of(a.frames.begin(),a.frames.end(),[&](auto hash){return std::binary_search(wanted.begin(),wanted.end(),hash);});
        }),definitions.end());
        result.animations=std::move(definitions);
        for(const auto& a:result.animations)wanted.insert(wanted.end(),a.frames.begin(),a.frames.end());
        std::sort(wanted.begin(),wanted.end());wanted.erase(std::unique(wanted.begin(),wanted.end()),wanted.end());
        sources.resize(sources.size() + 3);
        std::vector<unsigned char> source_bytes;
        TexturePackSet source_packs;
        RenderTextureLimits texture_limits;
        texture_limits.gpu_blocks = true;
        texture_limits.retain_mips = world_mips;
        texture_limits.max_total_bytes = world_mips ?
            kResidentRegionsPlaceholder*kStreamingRegionBudgetPlaceholder : kStreamingRegionBudgetPlaceholder;
        texture_limits.before_allocate = [&](std::uint64_t) { checkpoint("texture decoding"); };
        texture_limits.load_source = [&](std::size_t source, RenderTextureSource& descriptor,
                                         std::string& problem) {
            checkpoint("texture source read");
            if (source < stream_sources) {
                const auto& section = source < regular_sources ?
                    load.sections[owners[source]] :
                    spillover.sections[extra_owners[source - regular_sources]];
                std::ifstream input(section.file_index == kTrackStreamFileStream ?
                                    streams.stream : streams.pack, std::ios::binary);
                source_bytes.resize(static_cast<std::size_t>(section.size));
                input.seekg(static_cast<std::streamoff>(section.offset));
                if (!input.read(reinterpret_cast<char*>(source_bytes.data()),
                                static_cast<std::streamsize>(source_bytes.size()))) {
                    problem = "cannot re-read selected track texture range"; return false;
                }
                checkpoint("texture source parse");
                source_packs = parse_texture_pack_set(source_bytes.data(), source_bytes.size());
            } else {
                const auto& name = names[source - stream_sources];
                checkpoint(name.c_str());
                source_packs = load_texture_pack_set(path(name), source_bytes);
            }
            if (!source_packs.complete) { problem = "incomplete track texture source"; return false; }
            descriptor.bytes = &source_bytes; descriptor.wanted = &wanted;
            descriptor.complete = source_packs.complete;
            descriptor.packs.clear();
            for (const auto& pack : source_packs.packs) descriptor.packs.push_back(&pack);
            checkpoint("texture source ready");
            return true;
        };
        texture_limits.after_source = [&](std::size_t) {
            source_packs = {};
            std::vector<unsigned char>().swap(source_bytes);
            checkpoint("texture source released");
        };
        RenderTextureSet textures;
        if (!decode_render_texture_sources(sources, texture_limits, textures, error)) return false;
        for (const std::uint32_t missing : absent)
            if (!bind_track_default_alpha(textures, missing, error)) return false;
        checkpoint("textures decoded");
        // RenderTexture owns every retained block. No scene preparation
        // consults the pack parser or its borrowed source bytes again.
        sources.clear();
        std::vector<TrackStreamSection>().swap(load.sections);
        std::vector<TrackStreamSection>().swap(spillover.sections);
        TrackSceneOptions options;
        for(const auto& a:result.animations)options.region.animation_texture_hashes.insert(
            options.region.animation_texture_hashes.end(),a.frames.begin(),a.frames.end());
        options.region.max_texture_bytes = kStreamingRegionBudgetPlaceholder;
        std::uint64_t mip_extra=0;
        if(world_mips)for(const auto& texture:textures.textures)if(texture.usable()) {
            const auto format=texture.storage==RenderTextureStorage::dxt1?PixelFormat::dxt1:
                texture.storage==RenderTextureStorage::dxt3?PixelFormat::dxt3:PixelFormat::a8r8g8b8;
            std::uint64_t top=0;
            if(!level_size(format,texture.width,texture.height,top) || top>texture.payload().size()) {
                error="invalid world mip payload budget";return false;
            }
            mip_extra+=texture.payload().size()-top;
        }
        if(world_mips && (mip_extra>textures.decoded_bytes ||
            textures.decoded_bytes-mip_extra>kStreamingRegionBudgetPlaceholder)) {
            error="world top-level payload exceeds unchanged streaming limit";return false;
        }
        options.region.max_texture_bytes+=mip_extra;
        if(choice.slot.direction==1)apply_reverse_track_scenery(load.objects);
        checkpoint("scene preparation");
        if (!build_track_scene(std::move(load.geometry), load.objects, std::move(textures), options, result.scene, error)) return false;
        checkpoint("scene prepared");
        result.texture_bytes = result.scene.region.cost.texture_bytes;
        result.streaming_bytes = result.scene.region.cost.vertex_buffer_bytes +
            result.scene.region.cost.index_buffer_bytes + result.texture_bytes +
            result.scene.instances.size() * sizeof(PreparedTrackInstance);
        // A fully backed zone union replaces runtime geometry reads with one
        // bounded static preparation. It uses the existing128MiB persistent
        // preparation ceiling, not three24MiB live-stream slots.
        const auto preparation_budget=backing_set==TrackStreamSet::zones?kPersistentBudgetPlaceholder:
            kResidentRegionsPlaceholder*kStreamingRegionBudgetPlaceholder+2*mip_extra;
        if (result.streaming_bytes + textures.decoded_bytes > preparation_budget) {
            error = "track texture preparation exceeds the streaming payload plan: prepared="+
                std::to_string(result.streaming_bytes)+" decoded="+std::to_string(textures.decoded_bytes)+
                " mip_extra="+std::to_string(mip_extra); return false;
        }
        result.objects = std::move(load.objects);
        // Raw section bytes, parser mesh copies, external packs and the
        // temporary texture set die here, before collision loading and GPU allocation.
    }
    checkpoint("collision loading");
    result.contacts = load_track_contact_world(trackb);
    if (!result.contacts.valid || result.contacts.memory.resident_bytes > kPersistentBudgetPlaceholder) {
        error = "track collision is invalid or exceeds the persistent payload plan"; return false;
    }
    checkpoint("collision loaded");
    std::vector<TrackResidencyVertex> vertices;
    std::vector<TrackResidencySample> samples;
    if (event_number != 8 && event_number != 3) {
        if (!parse_track_residency_path(kTrackResidencyHandPath1003, vertices, error)) return false;
    } else {
        std::vector<unsigned char> route_bytes;
        const auto route = load_race_route_track(trackb, route_bytes);
        TrackPathSpline spline;
        if (!build_track_path_spline(route, 0, spline, error)) return false;
        for (std::size_t i = 0; i <= spline.count*16; ++i) {
            float point[3]; evaluate_track_path(spline, static_cast<float>(i)/16, 0, point);
            TrackResidencyVertex vertex; vertex.slot[0] = point[0]; vertex.slot[1] = point[1];
            vertices.push_back(vertex);
        }
    }
    if (!sample_track_residency_path(vertices, 30.0, 60, samples, result.path, error)) return false;
    checkpoint("path sampled");
    slice = std::move(result);
    return true;
}

bool track_slice_view(const TrackResidencyPathPoint& point, double yaw_offset,
                      double far_plane, RenderOptions& options,
                      TrackFrustum& frustum, std::string& error) {
    options = {};
    options.sky = true;
    options.width = 1280; options.height = 720;
    auto& camera = options.camera;
    camera.target = {point.slot[0] + 20 * point.direction[0], point.slot[2] + 3,
                    -(point.slot[1] + 20 * point.direction[1])};
    camera.yaw_deg = std::atan2(point.direction[1], -point.direction[0]) *
                     (180.0 / 3.14159265358979323846) + yaw_offset;
    camera.pitch_deg = 5; camera.distance = 20; camera.fov_y_deg = 60;
    camera.near_plane = 0.5; camera.far_plane = far_plane;
    CameraFrame basis;
    if (!make_camera_frame(camera, options.width, options.height, basis, error)) return false;
    TrackCamera cull;
    cull.eye[0] = static_cast<float>(basis.eye.x); cull.eye[1] = static_cast<float>(basis.eye.y);
    cull.eye[2] = static_cast<float>(basis.eye.z);
    cull.target[0] = static_cast<float>(camera.target.x); cull.target[1] = static_cast<float>(camera.target.y);
    cull.target[2] = static_cast<float>(camera.target.z);
    cull.up[0] = 0; cull.up[1] = 1; cull.up[2] = 0;
    cull.vertical_fov = static_cast<float>(camera.fov_y_deg * (3.14159265358979323846 / 180.0));
    cull.aspect = 1280.0F / 720.0F; cull.near_plane = 0.5F;
    cull.far_plane = static_cast<float>(far_plane);
    if (!track_frustum_look_at(cull, frustum)) { error = "invalid track slice frustum"; return false; }
    return true;
}

bool make_track_slice_frame(const TrackSlice& slice, std::size_t sample,
                            double yaw_offset, TrackSliceFrame& frame, std::string& error) {
    frame = {}; error.clear();
    if (sample >= slice.path.size()) { error = "track slice sample outside the path"; return false; }
    const auto& point = slice.path[sample];
    TrackSliceFrame result;
    result.sample = sample;
    float slot[3] = {static_cast<float>(point.slot[0]), static_cast<float>(point.slot[1]),
                     static_cast<float>(point.slot[2])};
    result.contact_status = query_track_contact_point(slice.contacts, slot, 2, 20, result.contact);
    if (result.contact_status == TrackContactStatus::refused) { error = "track contact query refused"; return false; }
    for (double far_plane = 250; far_plane >= 31.25; far_plane *= 0.5) {
        RenderOptions options;
        TrackFrustum frustum;
        if (!track_slice_view(point, yaw_offset, far_plane, options, frustum, error)) return false;
        const auto culled = cull_track_region(slice.objects, frustum);
        if (!culled.ok() || !culled.policy_held) { error = "track visibility policy degraded"; return false; }
        TrackDrawList list;
        TrackDrawListOptions list_options;
        list_options.order = TrackDrawOrder::retail_bucket;
        if (!build_track_draw_list(slice.scene, culled, list_options, list, error)) return false;
        if (list.entries.size() > kTrackSliceDrawLimit) continue;
        options.material_state = MaterialStateMode::retail;
        if (!record_track_commands(slice.scene, list, options, {}, result.commands, error)) return false;
        if (result.commands.draw_count() > kTrackSliceDrawLimit) continue;
        DrawExecutionPlan plan;
        if (!plan_draw_execution(result.commands, plan, error)) return false;
        result.visible = culled.visible.size(); result.triangles = 0;
        for (const auto& draw : result.commands.owning_draws()) result.triangles += draw.index_count/3;
        result.unplaced = list.cost.visible_unplaced; result.far_plane = far_plane;
        frame = std::move(result);
        return true;
    }
    error = "track slice exceeds 2047 draws even at the minimum view distance";
    return false;
}
} // namespace nfsu
