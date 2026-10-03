#pragma once
#include "nfsu/race_events.h"
#include "nfsu/race_setup.h"
#include <array>
#include <memory>
#include <optional>

namespace nfsu {
// Input stage only. Published storage is owned and const; no retail pointers,
// controller/body resources, installed meshes or simulated traffic are created.
enum class Event0Role : std::uint8_t { human = 1, opponent = 2, traffic = 3 };
enum class PresetPairRule { unresolved, paint, vinyl, decal, unused };
struct Event0PresetPair {
    std::uint32_t slot = 0, value = 0;
    // Dispatch category, not a claim that this hash resolves to a mesh/RGB.
    PresetPairRule rule = PresetPairRule::unused;
};
struct Event0Preset {
    std::uint64_t offset = 0;
    std::uint32_t hash = 0;
    std::string name, car;
    std::array<Event0PresetPair, 120> pairs{};
};
struct Event0GridMarker {
    std::uint64_t offset = 0;
    std::uint32_t index = 0;
    std::array<float, 3> raw_position{};
    std::uint16_t raw_angle = 0;
    // Raw height and angle have no established final body/CG convention.
};
struct Event0ActorConfig {
    std::int8_t config_id = 0;
    Event0Role role = Event0Role::human;
    std::string event_label; // human label comes from profile, not preset
    bool label_from_profile = false;
    Event0Preset visual;
    std::optional<VehicleUpgradeLevels> performance; // rivals unresolved
    std::array<std::int16_t, 3> difficulty_context{};
    std::int16_t selected_context = 0, tuning_selector = 0;
    std::uint8_t raw_ai_byte = 0;
    std::uint64_t event_source_offset = 0;
    std::uint8_t initial_grid = 0, final_grid = 0; // one-based
    Event0GridMarker marker;
};
struct Event0TrafficRequest {
    std::uint32_t selector = 0;
    std::array<std::int8_t, 8> track_policy{};
    std::uint8_t requested_configs = 0;
    std::int8_t first_config_id = 50;
    Event0Role role = Event0Role::traffic;
    std::uint8_t car_type_class = 2, duplicate_retry_limit = 100;
    // Model choices, seed order, routes, activation and live count unresolved.
    std::optional<std::uint32_t> live_count;
};
enum class Event0Unsupported {
    adaptive_rival_performance, rng_sequence, preset_composition,
    mesh_disable, ground_projection_and_body_pose, actor_actuation,
    traffic_models_routes_and_activation
};
struct Event0SetupDescriptor {
    RaceEventRecord event;
    TrackInfoRecord track;
    RaceSlot race;
    std::array<std::uint32_t, 83> raw_event_words{};
    std::uint8_t difficulty = 1;
    std::uint32_t difficulty_producer_va = 0x005A0A65;
    std::uint32_t runtime_mode = 1;
    float selected_ai_scalar = 0;
    std::uint8_t desired_player_grid = 0;
    std::array<Event0ActorConfig, 4> actors{}; // configuration order
    std::array<std::int8_t, 4> grid_config_ids{}; // factory order
    Event0TrafficRequest traffic;
    std::array<std::string, 3> decoded_sha256{}; // FrontB, GLOBALB, TRACKB1003
    bool authenticated_inputs = false;
    std::vector<Event0Unsupported> unsupported;
    bool runtime_ready() const noexcept { return unsupported.empty() && authenticated_inputs; }
};
enum class Event0SetupStatus { invalid_input, unsupported_input, descriptor_ready };
struct Event0SetupResult {
    Event0SetupStatus status = Event0SetupStatus::invalid_input;
    std::shared_ptr<const Event0SetupDescriptor> descriptor;
    std::vector<RaceTableIssue> issues;
    bool ok() const noexcept { return status == Event0SetupStatus::descriptor_ready && descriptor && issues.empty(); }
};
struct Event0SetupImage { const unsigned char* data = nullptr; std::size_t size = 0; };
// Structural parser for tooling/tests. Hashes are recorded; it never asserts
// authentication. Only the loader authenticates the recovered retail substrate.
Event0SetupResult parse_event0_setup(Event0SetupImage front, Event0SetupImage global,
                                    Event0SetupImage track);
// Exactly these three files, read-only. Accepts compressed or decoded packs,
// checks recovered decoded identities, and owns no input beyond the call.
Event0SetupResult load_event0_setup(const std::filesystem::path& front,
                                   const std::filesystem::path& global,
                                   const std::filesystem::path& track);
} // namespace nfsu
