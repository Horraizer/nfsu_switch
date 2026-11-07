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

Event0SetupResult parse_race_grid_setup(Event0SetupImage front, Event0SetupImage global,
                                    Event0SetupImage track, unsigned number, const std::string& car, unsigned difficulty,unsigned race_index) {
    Event0SetupResult result;
    try {
        const auto events = parse_race_event_table(front.data, front.size, setup_limits());
        const auto tracks = parse_track_info_table(global.data, global.size, setup_limits());
        if (!events.ok() || !tracks.ok()) {
            result.issues = events.issues;
            result.issues.insert(result.issues.end(), tracks.issues.begin(), tracks.issues.end());
            return result;
        }
        SupportedRaceSelection selected;
        if (difficulty>2 || !supported_race_selection(number,race_index,selected) || selected.type==RaceType::drift)
            throw std::runtime_error("unsupported career grid selection");
        const auto* event = find_race_event(events, number);
        if (!event) throw std::runtime_error("event0 missing");
        if (event->races.size()!=selected.count || race_index>=event->races.size() || (number==0 && (event->type!=0 || event->races[race_index].track!=1003 ||
            event->races[race_index].direction!=0 || event->races[race_index].laps!=2))) {
            result.status = Event0SetupStatus::unsupported_input;
            throw std::runtime_error("unsupported new-career event0 race selection");
        }
        const auto* track_info = find_track_info(tracks, event->races[race_index].track);
        if (!track_info) throw std::runtime_error("event0 track missing");
        auto descriptor = std::make_shared<Event0SetupDescriptor>();
        descriptor->race_index=race_index;descriptor->racer_count=1;
        descriptor->difficulty=static_cast<std::uint8_t>(difficulty);
        descriptor->event=*event; descriptor->race=event->races[race_index]; descriptor->track=*track_info;
        const auto* raw = front.data + event->offset;
        for (std::size_t i=0; i<83; ++i) descriptor->raw_event_words[i]=u32(raw+4*i);
        const auto opponents=u32(raw+0x128);
        if(opponents>3)throw std::runtime_error("unsupported career opponent count");
        descriptor->racer_count=opponents+1;
        if ( u32(raw+0x134)>3 || u32(raw+0x138)<1 || u32(raw+0x138)>descriptor->racer_count ||
            (number==0 && (u32(raw+0x134)!=1 || u32(raw+0x138)!=2 || track_info->lap_skip!=0))) {
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
            if (u32(p+8)!=name_hash(event->races[race_index].direction ? "BackwardStartingPosition" : "ForwardStartingPosition",false)) continue;
            Event0GridMarker marker; marker.offset=at; marker.index=u32(p+0xC);
            if (marker.index>=8) throw std::runtime_error("grid index outside consumer bounds");
            for (unsigned i=0;i<3;++i) {
                marker.raw_position[i]=f32(p+0x10+4*i);
                if (!std::isfinite(marker.raw_position[i])) throw std::runtime_error("non-finite grid position");
            }
            marker.raw_angle=u16(p+0x20);
            if (!markers.emplace(marker.index,marker).second) throw std::runtime_error("duplicate forward grid index");
        }
        for (std::size_t i=0;i<descriptor->racer_count;++i) {
            auto& actor=descriptor->actors[i];
            const auto* p=raw+(i==0 ? 0xD0 : 0xD4+(i-1)*0x1C);
            const auto hash=u32(p+(i==0 ? 0 : 0xC));
            const auto found=presets.find(hash);
            if (i==0 && number!=0) {
                if(car.empty())throw std::runtime_error("career grid needs owned car");
                actor.visual.car=car;
            } else {
                if (hash==0 || found==presets.end() || found->second.name.empty() || found->second.car.empty())
                    throw std::runtime_error("unresolved race preset identity");
                actor.visual=found->second;
            } actor.label_from_profile=i==0;
            actor.config_id=static_cast<std::int8_t>(i==0 ? 0 : 99+i);
            actor.role=i==0 ? Event0Role::human : Event0Role::opponent;
            actor.event_source_offset=event->offset+(i==0 ? 0xD0 : 0xD4+(i-1)*0x1C);
            actor.initial_grid=static_cast<std::uint8_t>(i==0 ? descriptor->racer_count : i);
            const auto desired=descriptor->desired_player_grid;
            actor.final_grid=i==0 ? desired : (actor.initial_grid==desired ? descriptor->racer_count : actor.initial_grid);
            const auto marker=markers.find(actor.final_grid-1U);
            if (marker==markers.end()) throw std::runtime_error("missing racer grid marker");
            actor.marker=marker->second;
            descriptor->grid_config_ids[actor.final_grid-1U]=actor.config_id;
            if (i==0) {
                VehicleUpgradeLevels levels;
                std::string error;
                // 0x004B60F2..0x004B6102 writes every level; this is not
                // performance1_levels(), whose named CLI preset is narrower.
                if (number==0 && !parse_performance_levels("weight=1,suspension=1,engine-a=1,induction=1,nitrous=1,engine-b=1,gearbox=1,tyres=1,grip=1,aero-a=1,aero-b=1",levels,error))
                    throw std::runtime_error(error);
                actor.performance=levels;
            } else {
                actor.event_label=text(p,12);
                for (unsigned j=0;j<3;++j) actor.difficulty_context[j]=static_cast<std::int16_t>(u16(p+0x10+2*j));
                actor.selected_context=actor.difficulty_context[descriptor->difficulty];
                actor.tuning_selector=static_cast<std::int16_t>(u16(p+0x16));
                actor.raw_ai_byte=p[0x18];
                actor.ai_mode=actor.raw_ai_byte<3?actor.raw_ai_byte:1;
                if (actor.tuning_selector< -1 || actor.tuning_selector>10) {
                    result.status=Event0SetupStatus::unsupported_input;
                    throw std::runtime_error("unsupported fixed rival tuning path");
                }
            }
        }
        descriptor->traffic.selector=u32(raw+0x134);
        descriptor->traffic.track_policy=track_info->traffic_pool;
        const auto count=race_traffic_pool_size(*track_info,descriptor->traffic.selector,unsigned(descriptor->race.direction));
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

Event0SetupResult parse_event0_setup(Event0SetupImage front, Event0SetupImage global, Event0SetupImage track) {
    return parse_race_grid_setup(front,global,track,0,"",1);
}

Event0SetupResult load_race_grid_setup(const std::filesystem::path& front,
                                   const std::filesystem::path& global,
                                   const std::filesystem::path& track, unsigned number,
                                   const std::string& car, unsigned difficulty,unsigned race_index) {
    Event0SetupResult result;
    SupportedRaceSelection selected;
    if(!supported_race_selection(number,race_index,selected)) {
        result.issues.push_back({0,"unsupported grid track identity"});return result;
    }
    const char* track_hash=nullptr;
    switch(selected.slot.track) {
    case 1003:track_hash=recovered_hashes[2];break;
    case 1102:track_hash="1DD0F11B5799C08A49BBB06BE351BEF129318B54BE4E4BDC5C7E6AF54CA0D754";break;
    case 1103:track_hash="6128DF74A33360FEDDB22413B3EB82769F4B555D2BD46A68BEBB853134D7C2F4";break;
    case 1104:track_hash="FBD60160F9E2E9AE3CDBE8C66F98FF689C0AA9A603F7E844770A6C57E070E3DE";break;
    case 1210:track_hash="E87732474D40017212212AB553E1653E9812DA04A4589F1D4543C1007B19B179";break;
    case 1002:track_hash="6A76803B3E55E25A5A4CB59BCAEBDD92D12FAC5DC8BD12E1C98A0C7346BDD915";break;
    case 1005:track_hash="DDD23BAC0BA444322E300A8C40EAF5CCEB8C64CC7E0E44AA322366D3E35D5C54";break;
    case 1006:track_hash="4876DEAF0A17C627E79DED55DE77EA96C22F60DDEECDB186DA839AD55F4D4414";break;
    case 1201:track_hash="51A456A02DA63126D0888AFF0C7BF4C8874A97CD1CB22A18DE3B84CC5570A122";break;
    case 1202:track_hash="1B50438A3738A5778F4860577E29458FC708FCAD049718500109D0C2B2F7D51B";break;
    case 1001:track_hash="195842A08F98860FC37C8ACFF97E7064B0FF4F611B58E86D8FE344C809EC11B1";break;
    case 1008:track_hash="C7977D61895A23DF1A5CB3F497AEA53F2D377A65D3B4EC72EE549E89D26CD414";break;
    case 1105:track_hash="D444D24CF357C50E7691E04DF07F2B31ECD53A8539A8002D8E1D084F6FD01560";break;
    case 1106:track_hash="A459305718A5F6A786E8FEF86284F80AB00A2361DF4AC57E0E86105ECC121F04";break;
    case 1206:track_hash="B5FD49D6023F7C0B6B2C491E0D050E6CCA3DA698316098F1B5304D2353B69654";break;
    case 1004:track_hash="1FDF3DED190A638A2FE7F77BFD1E461819ED848D3A7CEE501AAB5EB879016936";break;
    case 1007:track_hash="2EC14D83DF55C2D5F638E020A9BA552CFE9FB3FD7F386B86332FA84F37F522A3";break;
    case 1107:track_hash="7ACA762D38BF54CF8197087FCD4390048717B70FB43215BB5478C77672F6369E";break;
    case 1108:track_hash="3CF9C6A2CD4987BF92B1BB11E1A8E7D1AE9723021CD39856C6A338C33F179BF8";break;
    case 1109:track_hash="D046220DF55177E84B742F89AEB54523274C7A35476FFD052BAD24DB9026D604";break;
    case 1214:track_hash="4EF5FB81BBD8F30B4E7D83C1E0169ED33E9CDB39366C0899F624F34894B12671";break;
    case 1207:track_hash="6BA1C1FB14354FBC85A560890BA829FDCA7B528D7E69A2423865CCDFEE7B5B5A";break;
    default:result.issues.push_back({0,"grid track identity absent"});return result;
    }
    const std::array<std::filesystem::path,3> paths={front,global,track};
    std::array<std::vector<unsigned char>,3> images;
    for (std::size_t i=0;i<3;++i) {
        if (!load_race_image(paths[i],images[i],result.issues,setup_limits())) return result;
        if (sha256_hex(sha256(images[i].data(),images[i].size()))!=(i==2 ? track_hash : recovered_hashes[i])) {
            result.issues.push_back({0,"setup input does not match recovered decoded identity"}); return result;
        }
    }
    result=parse_race_grid_setup({images[0].data(),images[0].size()},
                             {images[1].data(),images[1].size()},
                             {images[2].data(),images[2].size()},number,car,difficulty,race_index);
    if (result.ok()) {
        auto authenticated=std::make_shared<Event0SetupDescriptor>(*result.descriptor);
        authenticated->authenticated_inputs=true;
        result.descriptor=std::move(authenticated);
    }
    return result;
}
Event0SetupResult load_event0_setup(const std::filesystem::path& front,
                                   const std::filesystem::path& global,
                                   const std::filesystem::path& track) {
    return load_race_grid_setup(front,global,track,0,"",1);
}
} // namespace nfsu
