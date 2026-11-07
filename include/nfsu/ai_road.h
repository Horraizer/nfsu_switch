#pragma once
#include "nfsu/ai_path.h"
#include "nfsu/race_route.h"
#include <array>

namespace nfsu {
// Canonical cells for the direction-specific original road virtuals. Apply
// once to a private parsed copy; point/event identities and raw progress stay.
bool ai_road_orient(RacingLineTrack&,int direction) noexcept;
// Connection chunk34122, record size32. Indexing must use the same order as
// RoutePoint.event_a/event_b (file order or original post-load permutation).
struct AiRoadEvent {
    float progress=0;
    std::uint16_t target_point=0;
    std::uint8_t marker=0,target_route=0,type=0,direction=0,disabled=0,context_flag=0;
    std::uint16_t source_point=0;
    std::uint8_t source_route=0;
    std::uint8_t traffic_type=0; // event+0C, separate from racer byte0B
    std::uint32_t obstacle_type=0;
    std::int32_t obstacle_enter=0,obstacle_leave=0;
};
bool ai_road_events(const unsigned char*,std::size_t,std::vector<AiRoadEvent>&) noexcept;
// Original4430B0/442980/442150 load-time projections. Keep file-order
// connections and their point references together (no sort is needed by
// the indexed policy). Must precede direction orientation.
bool ai_road_initialize(const RaceRouteTrack&,RacingLineTrack&,
                        std::vector<AiRoadEvent>&,std::string& error);
//456E90 over a direction-oriented road. Returns original categories11..16,
//or -1 when no enabled lane survives the inclusive exclusion interval.
int ai_road_nearest_lane(const Route&,unsigned point,const AiDriverPoint&,
                         int exclude_first=255,int exclude_last=255) noexcept;
struct AiLaneTransitionInput {
    int track_number=0,direction=0;
    float progress=0; // actual actor814, original raw path coordinate
    int controller_kind=2; // drag5A75C0 skips chain flags for owner2
};
//5A7700/5A7820 after actual destination5A75C0 admission. Lane-to-lane changes
//walk all adjacent edges;1206/1214 use the executable's fixed divider rows.
//Both interval endpoints are inclusive. Ordinary/absent destinations and
//unrestricted tracks do not consume divider progress. Refusal preserves out.
bool ai_road_lane_transition(const AiLaneTransitionInput&,int from,int to,
                              bool destination_available,bool& out) noexcept;
struct DragLaneState {
    int mode=2,exclude_first=255,exclude_last=255,category=11;
};
struct AiRoadChain;
//45D800/45D970/4605E0, held lane controls (retail7003D8=1). The caller
//supplies route and context availability plus actual track/direction/progress.
//Owner2 uses header availability even when future chain flags are false.
bool drag_lane_step(const Route&,unsigned point,const AiDriverPoint&,
                    const AiRoadChain&,const std::array<bool,7>& available,
                      int held_direction,float travelled_distance,DragLaneState&,const AiLaneTransitionInput& = {}) noexcept;
struct AiRoadPolicy {
    // Original458BD0: eight packed decision slots. +478 is a distinct marker.
    std::array<std::uint32_t,8> cache{};
    int marker=-1;
};
enum class AiRoadDecisionPolicy {racer,traffic};
struct AiRoadPolicyInput {
    int direction=0,role=2;
    std::int16_t decision_start=-1; // active setup +B8; -1 uses435CA0 gate
    float race_seconds=0;
    bool race_time_hold=false; // actual435CA0 result; not a finish flag
    std::array<float,3> probabilities{}; // selected difficulty row779FF8
    AiRoadDecisionPolicy policy=AiRoadDecisionPolicy::racer; // actual virtual44/48 pair
    void* obstacle_context=nullptr;
    bool(*obstacle_decision)(void*,const AiRoadEvent&,bool&)=nullptr;
};
// 458BD0/462840/4629E0, ordinary events excluding the separate type9 action.
// rng is the shared WORLD+2D0 state, not a per-rival replacement generator.
// False indicates unsupported inputs/capacity; all outputs remain unchanged.
// Traffic callback583BB0 reads event+0C, with a strict half-range test.
// The owner supplies world+2D0 and owns decision caching/admission.
bool ai_traffic_road_decide(unsigned traffic_type,std::uint32_t& rng) noexcept;
bool ai_road_decide(const AiRoadEvent&,unsigned event_index,const AiRoadPolicyInput&,
                    AiRoadPolicy&,std::uint32_t& rng,bool& take) noexcept;
// 458CC0: release decisions after the actor passes their original lap-aware
// progress window. The retained threshold is original global6B753C.
bool ai_road_expire(const std::vector<AiRoadEvent>&,float progress,float track_length,
                    bool circuit,bool same_direction,float retain_distance,AiRoadPolicy&) noexcept;
struct AiRoadCursor {std::int16_t route=0;std::uint16_t point=0;};
enum class AiRoadAdvance {advanced,end,unsupported};
// 5A53C0/5A6F50 forward-chain topology. Route reverse traversal is the loaded
// route +15 flag supplied separately; it is not the race direction.
AiRoadAdvance ai_road_next(const RacingLineTrack&,const std::vector<AiRoadEvent>&,
                          bool route_reverse,const AiRoadPolicyInput&,
                          AiRoadPolicy&,std::uint32_t& rng,AiRoadCursor&,bool& junction) noexcept;
struct AiRoadChain {
    std::array<AiPathRoadPoint,40> cells{};
    std::array<bool,40> junction{};
    unsigned count=0;
    int search_origin=-1; // original chain+AC, set before growth by5A5730
};
// 5A58D0 forward-chain growth. The context owner supplies an existing cell,
// category and resolved horizon; selection and backward bounds are separate.
// Fixed capacity, no allocation or filesystem access in this update.
bool ai_road_extend(const RacingLineTrack&,const std::vector<AiRoadEvent>&,
                    const std::array<bool,128>& route_reverse,int category,float horizon,
                    const AiRoadPolicyInput&,AiRoadPolicy&,std::uint32_t& rng,AiRoadChain&) noexcept;
// 5A5C30 backward width propagation. Curvature is the original5A6860 result
// selected by the spatial context; it is not a lateral car offset.
//5A6CE0/5A67D0: the existing context-event flag gates the final forward cell.
bool ai_road_stop_event_reached(const AiRoadChain&,bool event_flag,float distance,bool&) noexcept;
bool ai_road_propagate_bounds(AiRoadChain&,int category,float curvature) noexcept;
// 5A6150 compact-chain nearest search. Flags1 requires a point ahead, flags2
// a point behind, using each road basis and the original scan skip.
bool ai_road_nearest(const AiRoadChain&,const AiDriverPoint& position,int start,
                     unsigned flags,int& result) noexcept;
// 5A6860: signed bend of the three authored-lane samples, with original
// horizon selection and fallback indices (not a radius approximation).
bool ai_road_curvature(const AiRoadChain&,unsigned first,int category,
                       float lookahead,float& result) noexcept;
// 5A5730/5A57F0/5A6080: update the chain for a spatially selected road point.
// Caller owns spatial admission, action/lane policy and actor notifications.
bool ai_road_refresh(const RacingLineTrack&,const std::vector<AiRoadEvent>&,
                     const std::array<bool,128>& route_reverse,AiRoadCursor selected,
                     int category,const AiDriverPoint& position,bool keep_previous,
                     float initial_horizon,float retained_horizon,
                     const AiRoadPolicyInput&,AiRoadPolicy&,std::uint32_t& rng,AiRoadChain&,
                     int bounds_category=-1) noexcept;
struct AiRoadSelectionInput {
    AiDriverPoint position{};
    float progress=0,maximum_distance=0;
    int first_route=0,last_route=99,direction=0,role=2,excluded_route=-1;
    bool require_enabled_lane=false,allow_outside_width=false;
};
// 44ADC0/442690 ordinary spatial road admission, two progress-filter passes.
// Category/action selection and controller notifications remain caller-owned.
bool ai_road_select(const RacingLineTrack&,const AiRoadSelectionInput&,
                    bool& found,AiRoadCursor& selected) noexcept;
struct AiRoadLocation {unsigned cell=0;float distance=0,lateral=0;};
// 5A65B0 ordinary forward cells. Longitudinal projection uses the preceding
// cell at a crossing; lateral projection uses the selected cell's lane.
// This does not implement 5A6700's linked alternate-cell lateral override.
bool ai_road_locate(const AiRoadChain&,const AiDriverPoint&,int category,
                    AiRoadLocation&) noexcept;
// 5A6700 uses the preceding cell for lateral projection (5A5510 links+68).
bool ai_road_locate_actor(const AiRoadChain&,const AiDriverPoint&,int category,
                          AiRoadLocation&) noexcept;
//583910 numeric publication on direction-oriented compact road cells.
// Caller supplies the actual selected cell, road coordinate and retained
// action/cap state. This does not select traffic actions or admit obstacles.
struct AiTrafficSpeedInput {
    unsigned first=0;
    int category=11;
    float distance=0,absolute_speed=0,cap=0;
    bool stopped=false,route_speed=true;
    std::array<float,3> action{}; // speed, duration, auxiliary
};
bool ai_traffic_speed(const AiRoadChain&,const AiTrafficSpeedInput&,
                      std::array<float,3>& output) noexcept;
struct AiRoadFlags {
    std::array<bool,7> enabled{{true,true,true,true,true,true,true}};
    int first_limited=-1,first_obstacle=-1;
    bool event_flag=false;
};
bool ai_road_flags(const RacingLineTrack&,const std::vector<AiRoadEvent>&,
                   const AiRoadChain&,int category,AiRoadFlags&) noexcept;
// Ordinary forward category admission shared by45AD30 and5A75C0.
bool ai_road_category_available(const RacingLineTrack&,AiRoadCursor,int category) noexcept;
struct AiRoadContext {
    bool selected=false;
    AiRoadCursor road{};
    int category=0,nearest=-1;
    AiRoadChain chain;
    AiRoadFlags flags;
};
struct AiRoadContextInput {
    AiRoadSelectionInput selection;
    float body_width=0,back_margin=0;
    float initial_horizon=0,retained_horizon=0;
    bool keep_previous=true,allow_wide=false;
    int ride_category=-1; // actual ride+424 for compact slot0; -1 retains old API
    int controller_kind=3; // actor+24, not physical selector+28
    int track_number=0; // actual setup6C, for5A7700 divider admission
};
struct AiRoadHorizons {float initial=0,retained=0;};
//462390/458B90/57A910. The caller supplies current7006EC/7006F0;
//the original racer virtual50 takes their maximum and at least2 seconds.
bool ai_road_horizons(float absolute_speed,float maneuver_seconds,
                      float prediction_seconds,AiRoadHorizons&) noexcept;
struct AiRoadContextResult {
    bool reselected=false,clear_width=false,category_available=false;
    bool reset_maneuver=false; //44D337 adjacent-lane admission, not nearest fallback
    std::array<std::int16_t,40> passed_events{}; // -1 is the original null event
    unsigned passed_count=0;
};
// Spatial owner44CF30 through44D2F4. Inputs are original controller virtual
// results. Racer policy retains its category; traffic583B20 chooses the
// nearest enabled lane on reselection and549130 leaves passed markers alone.
// Traffic may enter with category-1 only while no road/chain is selected.
// Supply ride_category separately from its selected lane. Emits notifications
// for the caller; neighbor publication57A910 is a subsequent distinct step.
bool ai_road_update_context(const RacingLineTrack&,const std::vector<AiRoadEvent>&,
    const std::array<bool,128>& route_reverse,const AiRoadContextInput&,
    const AiRoadPolicyInput&,AiRoadPolicy&,std::uint32_t&,AiRoadContext&,
    AiRoadContextResult&) noexcept;
} // namespace nfsu
