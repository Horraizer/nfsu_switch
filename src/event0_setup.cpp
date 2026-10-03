#include "nfsu/event0_setup.h"
#include "nfsu/sha256.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <map>
#include <set>
#include <stdexcept>

namespace nfsu {
namespace {
RaceTableLimits setup_limits() { return {32ULL*1024*1024, 32ULL*1024*1024, 16384}; }
std::uint32_t u32(const unsigned char* p) {
    return std::uint32_t(p[0]) | (std::uint32_t(p[1])<<8) |
           (std::uint32_t(p[2])<<16) | (std::uint32_t(p[3])<<24);
}
std::uint16_t u16(const unsigned char* p) { return std::uint16_t(p[0] | (unsigned(p[1])<<8)); }
float f32(const unsigned char* p) {
    const auto bits = u32(p); float value; std::memcpy(&value, &bits, 4); return value;
}
std::uint32_t name_hash(const std::string& name, bool fold = true) {
    std::uint32_t value = 0xFFFFFFFFU;
    for (unsigned char ch : name) {
        if (fold && ch >= 'a' && ch <= 'z') ch = static_cast<unsigned char>(ch - 'a' + 'A');
        value = value * 33U + ch;
    }
    return value;
}
std::string text(const unsigned char* p, std::size_t length, bool empty = false) {
    const auto end = std::find(p, p+length, 0);
    if (end == p+length || (!empty && end == p)) throw std::runtime_error("unterminated/empty setup string");
    for (auto c=p; c!=end; ++c) if (*c<32 || *c>126) throw std::runtime_error("non-ASCII setup string");
    return std::string(reinterpret_cast<const char*>(p), static_cast<std::size_t>(end-p));
}
RaceChunkPayload payload(Event0SetupImage image, std::uint32_t id,
                         std::size_t stride, std::size_t align,
                         std::vector<RaceTableIssue>& issues) {
    RaceChunkPayload span;
    if (!find_race_chunk(image.data, image.size, id, span, issues, setup_limits()))
        throw std::runtime_error("invalid setup chunk layout");
    span.begin = (span.begin + align - 1) / align * align;
    if (span.begin >= span.end || (span.end-span.begin)%stride != 0 ||
        (span.end-span.begin)/stride > 4096)
        throw std::runtime_error("empty, partial or oversized setup table");
    return span;
}
PresetPairRule pair_rule(std::uint32_t slot) {
    if (slot >= 120) return PresetPairRule::unused;
    if (slot >= 31 && slot <= 34) return PresetPairRule::vinyl;
    if (slot >= 59 && slot <= 114) return PresetPairRule::decal;
    if (slot == 30) return PresetPairRule::paint;
    return PresetPairRule::unresolved;
}
constexpr const char* recovered_hashes[] = {
    "94A714D1E8E3BEF7F77AEDCEE4FDC6350E285B06059B92D392687710E64E5D77",
    "39DF38FAC2BE7CA0F0D0A92E0113CAAEDA5B1C60E14436C612B3D0874AFC8DB9",
    "2B8A91ADD76C172A56A50AD0C10239F75E2102C8796A33F255F9E8C10CFFF6B9"};
} // namespace

Event0SetupResult parse_event0_setup(Event0SetupImage front, Event0SetupImage global,
                                    Event0SetupImage track) {
    Event0SetupResult result;
    try {
        const auto events = parse_race_event_table(front.data, front.size, setup_limits());
        const auto tracks = parse_track_info_table(global.data, global.size, setup_limits());
        if (!events.ok() || !tracks.ok()) {
            result.issues = events.issues;
            result.issues.insert(result.issues.end(), tracks.issues.begin(), tracks.issues.end());
            return result;
        }
        const auto* event = find_race_event(events, 0);
        if (!event) throw std::runtime_error("event0 missing");
        if (event->type != 0 || event->races.size()!=1 || event->races[0].track!=1003 ||
            event->races[0].direction!=0 || event->races[0].laps!=2) {
            result.status = Event0SetupStatus::unsupported_input;
            throw std::runtime_error("unsupported new-career event0 race selection");
        }
        const auto* track_info = find_track_info(tracks, event->races[0].track);
        if (!track_info) throw std::runtime_error("event0 track missing");
        auto descriptor = std::make_shared<Event0SetupDescriptor>();
        descriptor->event=*event; descriptor->race=event->races[0]; descriptor->track=*track_info;
        const auto* raw = front.data + event->offset;
        for (std::size_t i=0; i<83; ++i) descriptor->raw_event_words[i]=u32(raw+4*i);
        if (u32(raw+0x128)!=3 || u32(raw+0x134)!=1 || u32(raw+0x138)!=2 || track_info->lap_skip!=0) {
            result.status=Event0SetupStatus::unsupported_input;
            throw std::runtime_error("unsupported event0 roster/grid/traffic policy");
        }
        for (unsigned i=0; i<3; ++i) if (!std::isfinite(f32(raw+0x13C+4*i)))
            throw std::runtime_error("non-finite event AI scalar");
        descriptor->selected_ai_scalar=f32(raw+0x13C+4*descriptor->difficulty);
        descriptor->desired_player_grid=static_cast<std::uint8_t>(u32(raw+0x138));
        const auto presets_span=payload(front, 0x30220, 0x408, 1, result.issues);
        std::map<std::uint32_t, Event0Preset> presets;
        for (auto at=presets_span.begin; at<presets_span.end; at+=0x408) {
            const auto* p=front.data+at;
            Event0Preset preset; preset.offset=at;
            preset.car=text(p+8,32,true); preset.name=text(p+0x28,32,true);
            preset.hash=name_hash(preset.name);
            for (std::size_t i=0; i<120; ++i) {
                const auto slot=u32(p+0x48+8*i), value=u32(p+0x4C+8*i);
                if (slot>120 && slot!=0xFFFFFFFFU) throw std::runtime_error("preset slot outside consumer bounds");
                preset.pairs[i]={slot,value,pair_rule(slot)};
            }
            if (!presets.emplace(preset.hash,std::move(preset)).second)
                throw std::runtime_error("duplicate preset hash");
        }
        const auto markers_span=payload(track,0x34146,48,16,result.issues);
        std::map<std::uint32_t, Event0GridMarker> markers;
        for (auto at=markers_span.begin; at<markers_span.end; at+=48) {
            const auto* p=track.data+at;
            if (u32(p+8)!=name_hash("ForwardStartingPosition",false)) continue;
            Event0GridMarker marker; marker.offset=at; marker.index=u32(p+0xC);
            if (marker.index>=8) throw std::runtime_error("grid index outside consumer bounds");
            for (unsigned i=0;i<3;++i) {
                marker.raw_position[i]=f32(p+0x10+4*i);
                if (!std::isfinite(marker.raw_position[i])) throw std::runtime_error("non-finite grid position");
            }
            marker.raw_angle=u16(p+0x20);
            if (!markers.emplace(marker.index,marker).second) throw std::runtime_error("duplicate forward grid index");
        }
        for (std::size_t i=0;i<4;++i) {
            auto& actor=descriptor->actors[i];
            const auto* p=raw+(i==0 ? 0xD0 : 0xD4+(i-1)*0x1C);
            const auto hash=u32(p+(i==0 ? 0 : 0xC));
            const auto found=presets.find(hash);
            if (hash==0 || found==presets.end() || found->second.name.empty() || found->second.car.empty())
                throw std::runtime_error("unresolved event0 preset identity");
            actor.visual=found->second; actor.label_from_profile=i==0;
            actor.config_id=static_cast<std::int8_t>(i==0 ? 0 : 99+i);
            actor.role=i==0 ? Event0Role::human : Event0Role::opponent;
            actor.event_source_offset=event->offset+(i==0 ? 0xD0 : 0xD4+(i-1)*0x1C);
            actor.initial_grid=static_cast<std::uint8_t>(i==0 ? 4 : i);
            actor.final_grid=actor.initial_grid==2 ? 4 : (i==0 ? 2 : actor.initial_grid);
            const auto marker=markers.find(actor.final_grid-1U);
            if (marker==markers.end()) throw std::runtime_error("missing racer grid marker");
            actor.marker=marker->second;
            descriptor->grid_config_ids[actor.final_grid-1U]=actor.config_id;
            if (i==0) {
                VehicleUpgradeLevels levels;
                std::string error;
                // 0x004B60F2..0x004B6102 writes every level; this is not
                // performance1_levels(), whose named CLI preset is narrower.
                if (!parse_performance_levels("weight=1,suspension=1,engine-a=1,induction=1,nitrous=1,engine-b=1,gearbox=1,tyres=1,grip=1,aero-a=1,aero-b=1",levels,error))
                    throw std::runtime_error(error);
                actor.performance=levels;
            } else {
                actor.event_label=text(p,8);
                for (unsigned j=0;j<3;++j) actor.difficulty_context[j]=static_cast<std::int16_t>(u16(p+0x10+2*j));
                actor.selected_context=actor.difficulty_context[descriptor->difficulty];
                actor.tuning_selector=static_cast<std::int16_t>(u16(p+0x16));
                actor.raw_ai_byte=p[0x18];
                if (actor.tuning_selector!=-1) {
                    result.status=Event0SetupStatus::unsupported_input;
                    throw std::runtime_error("unsupported fixed rival tuning path");
                }
            }
        }
        descriptor->traffic.selector=u32(raw+0x134);
        const auto* track_raw=global.data+track_info->offset;
        for (unsigned i=0;i<8;++i) descriptor->traffic.track_policy[i]=static_cast<std::int8_t>(track_raw[0xC0+i]);
        const auto count=descriptor->traffic.track_policy[2*descriptor->traffic.selector+unsigned(descriptor->race.direction)];
        if (count<0) throw std::runtime_error("negative traffic request");
        descriptor->traffic.requested_configs=static_cast<std::uint8_t>(count);
        // IDs are signed bytes; 50+i must remain representable.
        if (count>78) throw std::runtime_error("traffic configuration ID overflow");
        descriptor->decoded_sha256={sha256_hex(sha256(front.data,front.size)),
            sha256_hex(sha256(global.data,global.size)),sha256_hex(sha256(track.data,track.size))};
        descriptor->unsupported={Event0Unsupported::adaptive_rival_performance,
            Event0Unsupported::rng_sequence,Event0Unsupported::preset_composition,
            Event0Unsupported::mesh_disable,Event0Unsupported::ground_projection_and_body_pose,
            Event0Unsupported::actor_actuation,Event0Unsupported::traffic_models_routes_and_activation};
        result.descriptor=std::move(descriptor); result.status=Event0SetupStatus::descriptor_ready;
    } catch (const std::exception& error) {
        result.descriptor.reset(); result.issues.push_back({0,error.what()});
    }
    return result;
}

Event0SetupResult load_event0_setup(const std::filesystem::path& front,
                                   const std::filesystem::path& global,
                                   const std::filesystem::path& track) {
    Event0SetupResult result;
    const std::array<std::filesystem::path,3> paths={front,global,track};
    std::array<std::vector<unsigned char>,3> images;
    for (std::size_t i=0;i<3;++i) {
        if (!load_race_image(paths[i],images[i],result.issues,setup_limits())) return result;
        if (sha256_hex(sha256(images[i].data(),images[i].size()))!=recovered_hashes[i]) {
            result.issues.push_back({0,"setup input does not match recovered decoded identity"}); return result;
        }
    }
    result=parse_event0_setup({images[0].data(),images[0].size()},
                             {images[1].data(),images[1].size()},
                             {images[2].data(),images[2].size()});
    if (result.ok()) {
        auto authenticated=std::make_shared<Event0SetupDescriptor>(*result.descriptor);
        authenticated->authenticated_inputs=true;
        result.descriptor=std::move(authenticated);
    }
    return result;
}
} // namespace nfsu
