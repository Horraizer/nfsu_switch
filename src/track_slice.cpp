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

bool load_track_slice(FileSystemService& files, const std::filesystem::path& root,
                      TrackSlice& slice, std::string& error, AssetCheckObserver* observer,
                      TrackSliceLoadObserver* loading, std::uint32_t event_number) {
    slice = {}; error.clear();
    TrackSlice result;
    AssetManifest manifest;
    if (event_number != 1 && event_number != 8) {
        error = "supported track events are 1 and 8"; return false;
    }
    if (!retail_asset_manifest(event_number == 1 ? "first-lap" : "first-lap-1005", manifest) ||
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
                             event_number, race, error)) return false;
    result.event_number = event_number;
    result.track_number = static_cast<std::uint32_t>(race.slot.track);
    const auto trackb = path("Tracks/" + track_bundle_file_name(race.slot.track));
    {
        std::vector<unsigned char> image;
        const auto zones = load_track_zones(trackb, image);
        const auto table = parse_track_stream_table(image.data(), image.size());
        TrackStreamSelection selected;
        if (!zones.ok() || !table.ok() ||
            !select_track_stream_sections(table, zones, result.track_number, TrackStreamSet::permanent, selected, error)) {
            if (error.empty()) error = "invalid event-1 track table or visibility zones";
            return false;
        }
        if (selected.records.size() > 256 || selected.route_numbers_outside || selected.route_zones_missing) {
            error = "event-1 path requires sections outside the bounded permanent set";
            return false;
        }
        TrackStreamFiles streams;
        streams.stream = path("Tracks/STREAML1RA.BUN");
        streams.has_pack = true;
        streams.pack = path("Tracks/" + track_stream_pack_file_name(race.track));
        TrackStreamLoadLimits limits;
        limits.max_total_bytes = kPersistentBudgetPlaceholder;
        limits.release_non_texture_bytes = true;
        limits.release_source_bytes = event_number==8;
        limits.poll = [&] { checkpoint("stream sections"); };
        TrackStreamLoad load;
        if (!load_track_stream_sections(table, selected, streams, limits, load, error) || !load.ok()) {
            if (error.empty()) error = "event-1 streamed section parse failed";
            return false;
        }
        checkpoint("sections parsed and merged");
        result.sections = selected.records.size();
        result.input_bytes = load.bytes_read;
        if (event_number==8 && !retain_instanced_track_meshes(load.geometry,load.objects,error)) return false;
        // The merged reports own their arrays. Drop per-section parser
        // copies and non-texture source ranges before texture preparation.
        std::vector<unsigned char>().swap(image);
        for (auto& section : load.sections) {
            section.geometry = {};
            section.objects = {};
            if (section.packs.packs.empty()) std::vector<unsigned char>().swap(section.bytes);
        }
        auto wanted = track_region_texture_hashes(load.geometry);
        if (event_number == 8) {
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
        sources.resize(sources.size() + 3);
        std::vector<unsigned char> source_bytes;
        TexturePackSet source_packs;
        RenderTextureLimits texture_limits;
        texture_limits.gpu_blocks = true;
        texture_limits.max_total_bytes = kStreamingRegionBudgetPlaceholder;
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
        if (event_number == 8 && !bind_track_default_alpha(textures, 0x828DCF0BU, error)) return false;
        checkpoint("textures decoded");
        // RenderTexture owns every retained block. No scene preparation
        // consults the pack parser or its borrowed source bytes again.
        sources.clear();
        std::vector<TrackStreamSection>().swap(load.sections);
        std::vector<TrackStreamSection>().swap(spillover.sections);
        TrackSceneOptions options;
        options.region.max_texture_bytes = kStreamingRegionBudgetPlaceholder;
        checkpoint("scene preparation");
        if (!build_track_scene(std::move(load.geometry), load.objects, std::move(textures), options, result.scene, error)) return false;
        checkpoint("scene prepared");
        result.texture_bytes = result.scene.region.cost.texture_bytes;
        result.streaming_bytes = result.scene.region.cost.vertex_buffer_bytes +
            result.scene.region.cost.index_buffer_bytes + result.texture_bytes +
            result.scene.instances.size() * sizeof(PreparedTrackInstance);
        if (result.streaming_bytes + textures.decoded_bytes >
            kResidentRegionsPlaceholder * kStreamingRegionBudgetPlaceholder) {
            error = "track texture preparation exceeds the streaming payload plan"; return false;
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
    if (event_number == 1) {
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
