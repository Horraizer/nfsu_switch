#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace nfsu {
struct RacingLineTrack;
struct AiRoadChain;
struct AiNeighborEntry;
// Original34123 records are0x170 bytes, with22 role3 actor slots50..71.
// This is intersection/right-of-way state, not a visual signal animation.
struct AiTrafficIntersectionEntry {
    bool active=false;
    int direction=-1;
    float distance=0,time=0;
};
struct AiTrafficIntersection {
    int kind=0;
    std::array<int,2> routes{{-1,-1}},closest{{-1,-1}};
    std::array<int,2> points{{-1,-1}}; // signed16 raw route-point indices at8/A
    int selected=-1;
    std::array<AiTrafficIntersectionEntry,22> actors{};
};
//442DD0 retains the original table. Parsing never resets active state or
//changes retail bytes. Invalid length/consumed fields refuse transactionally.
bool ai_traffic_intersections(const unsigned char*,std::size_t,
                              std::vector<AiTrafficIntersection>&) noexcept;
//5AADF0 registration. Non-role3 is an original no-op, before ID admission.
//Strict earlier-time comparison preserves the existing nearest on ties.
bool ai_traffic_intersection_register(AiTrafficIntersection&,int actor_id,int role,
                                     int route,float distance,float time) noexcept;
//44CA70/5AAEA0: clear one entry, refresh only a closest slot naming it.
bool ai_traffic_intersection_clear(AiTrafficIntersection&,int actor_id) noexcept;
//5AAF10, before teleporter/controller updates: release an unoccupied selected
//direction. Negative distance still counts until the actual actor clears it.
bool ai_traffic_intersection_step(AiTrafficIntersection&) noexcept;
//5AAE80 reads the named entry even if inactive; caller owns its lifecycle.
bool ai_traffic_intersection_time(const AiTrafficIntersection&,unsigned direction,
                                 float&) noexcept;
//5AACA0 permits lane0/1 from actual kind/selection and nearest lane0 time.
bool ai_traffic_intersection_open(const AiTrafficIntersection&,unsigned direction,
                                 bool&) noexcept;
//Actual5AAD10 return.44EFD0 inverts this when filtering two traffic actors.
//Non-traffic roles return true before either actor ID is consumed.
bool ai_traffic_intersection_pair(const AiTrafficIntersection&,int first_id,int first_role,
                                 int second_id,int second_role,bool&) noexcept;
//5AAB20 over RAW, un-oriented road data. Both road directions share the
//other road's forward edges; only the self projection basis changes.
bool ai_traffic_intersection_extent(const RacingLineTrack&,const AiTrafficIntersection&,
                                    int route,unsigned direction,float&) noexcept;
struct AiTrafficIntersectionRef {bool present=false;unsigned index=0;};
//44D570 from first_limited's raw route/point mask. A known absent cell is a
//valid absence; invalid indices/data are a refusal, not an absent crossing.
bool ai_traffic_intersection_reference(const RacingLineTrack&,const AiRoadChain&,
    int first_limited,std::size_t intersection_count,AiTrafficIntersectionRef&) noexcept;
//5A6E10 on an admitted first_limited cell. Missing/invalid producer state
//refuses; it is never a fabricated clear road. Neighbor distance/half length
//and compact cell coordinate must belong to the same original body epoch.
bool ai_traffic_intersection_distance(const RacingLineTrack&,
    const std::vector<AiTrafficIntersection>&,const AiRoadChain&,int first_limited,
    const AiNeighborEntry&,unsigned direction,float&) noexcept;
//583C80/401C40 arrival publication; maximum(distance/speed,0), no rounding.
bool ai_traffic_intersection_arrival(float distance,float signed_speed,float&) noexcept;
} // namespace nfsu
