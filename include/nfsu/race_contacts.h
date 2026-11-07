#pragma once
#include "nfsu/car_contact.h"
#include "nfsu/vehicle_track.h"
namespace nfsu {
// Port world adapter: bounds rejection, convex witness query and recovered
// kind-0 response and46EA50 ordered binary32 feature reduction.
struct RaceContactBox {
    VehicleVec3 low, high;
    // Retain the selected CV vertices. Bounds are only the broad rejection.
    std::vector<std::array<float,3>> points;
    std::vector<std::vector<unsigned>> neighbors;
};
struct RaceHullQuery {
    ContactVector witness_a{},witness_b{};
    double distance=0;
};
struct RaceHullCache {
    std::array<unsigned,4> a{},b{};
    unsigned count_a=0,count_b=0;
    bool initialized=false;
    bool polygon_expanded=false;
    //472830/472950 query cache, independent of feature initialization.
    bool cached=false;
    ContactVector origin_a{},origin_b{};
    RaceHullQuery query;
};
struct RaceContactContext {
    ContactRecord& record;
    std::uint32_t& world_seed;
    std::uint32_t tick;
    //46B73F: refresh corrected sampled bodies before computing the impulse.
    bool (*refresh)(void*,unsigned,VehicleState&)=nullptr;
    void* refresh_owner=nullptr;
    bool drag=false;
    std::array<unsigned,2> physical_selectors{{1,1}};
    std::array<std::uint32_t,2> body_names{};
};
bool query_race_hulls(const VehicleState&,const VehicleSpec&,const RaceContactBox&,
    const VehicleState&,const VehicleSpec&,const RaceContactBox&,RaceHullQuery&,RaceHullCache* cache=nullptr);
// Original473480/473A90 static polygon shape. Expanded queries include the
// second face one unit behind the stored polygon normal.
bool query_race_track_polygon(const VehicleState&,const VehicleSpec&,const RaceContactBox&,
    const std::array<ContactVector,4>& corners,const ContactVector& normal,bool expanded,
    RaceHullQuery&,RaceHullCache* cache=nullptr);
bool race_hull_support(const VehicleState&,const VehicleSpec&,const RaceContactBox&,
    const ContactVector& direction,unsigned start,unsigned& index,ContactVector& point);
// Pose retained at the start of one world update. Resets/spawns invalidate it.
struct RaceContactPose {
    VehicleVec3 position;VehicleQuat orientation;VehicleVec3 velocity;double time=0;
    PlayerRigidMatrix axes{};
};
PlayerRigidMatrix race_contact_axes(const VehicleState&);
// Prepared kind3 response only. Admission/witness and queue ordering belong
// to the caller; failure leaves the vehicle unchanged.
bool apply_race_track_contact(VehicleState&,const VehicleSpec&,
    const ContactGeometry&,float surface_friction,ContactResponse&);
RaceContactPose race_contact_pose(const VehicleState&);
// Original468FE0/4692F0: linear matrix/position and velocity samples. The
// matrix is deliberately not orthonormalized before the contact solve.
bool sample_race_contact(VehicleState&,const VehicleSpec&,const RaceContactPose&,
                         const VehicleState&,float remaining);
struct RaceContactJob {
    bool admitted=false;
    float remaining=0;
    unsigned queries=0;
    RaceHullQuery query;
    VehicleVec3 correction{};
    std::array<ContactVector,2> actor_up{};
};
struct RaceTrackContactJob {
    bool admitted=false;
    float remaining=0;
    unsigned queries=0;
    RaceHullQuery query;
    ContactGeometry geometry;
    VehicleVec3 correction{};
};
//468A50/468EC0 body bounds and retained reference for polygon contacts.
struct RaceTrackBodyBounds {
    float radius=0,below=0,above=0;
    ContactVector centre{};
};
RaceTrackBodyBounds race_track_body_bounds(const VehicleSpec&,const RaceContactBox&);
ContactVector race_track_side_reference(const RaceTrackBodyBounds&,const RaceContactPose&);
//43FF30 polygon plane and469A00 oriented cylinder admission.
ContactVector race_track_polygon_normal(const std::array<ContactVector,4>&);
bool race_track_plane_admitted(const VehicleState&,const RaceTrackBodyBounds&,
    const std::array<ContactVector,4>&,const ContactVector&);
// Original4407B0: retained XY search rectangle and ordered160-polygon cache.
struct RaceTrackPolygonCache {
    std::array<float,4> bounds{};
    std::vector<std::size_t> polygons;
    bool valid=false;
};
bool update_race_track_polygon_cache(const TrackContactWorld&,const RaceContactPose&,
    RaceTrackPolygonCache&,float supplied_radius=-1);
bool race_track_candidates(const TrackContactWorld&,const VehicleState&,
    const RaceTrackBodyBounds&,const RaceTrackPolygonCache&,
    const std::vector<std::size_t>& registered,std::vector<std::size_t>&);
// side_reference is original body+110 (the retained hull-bounds centre),
// independent of the COM sampled while searching backwards in time.
bool search_race_track_contact(const VehicleState&,const VehicleSpec&,const RaceContactBox&,
    const RaceContactPose&,const std::array<ContactVector,4>& corners,
    const ContactVector& normal,const ContactVector& side_reference,
    RaceTrackContactJob&,RaceHullCache* cache=nullptr,bool initial_expanded=false,
    bool prop_body=false,bool fixed=false);
enum class RaceContactPartner { car, prop, pinned_prop, fixed_world };
bool search_race_contact(const VehicleState&,const VehicleSpec&,const RaceContactBox&,
    const VehicleState&,const VehicleSpec&,const RaceContactBox&,
    const RaceContactPose&,const RaceContactPose&,RaceContactJob&,RaceHullCache* cache=nullptr,
    RaceContactPartner partner=RaceContactPartner::car,
    RaceContactPartner own=RaceContactPartner::car);
struct RacePairContact {
    bool overlapping=false, applied=false;
    double depth=0;
    bool searched=false;
    double contact_fraction=1;
    double remaining_a=0,remaining_b=0;
    RaceContactPose resume_a,resume_b;
    ContactVector impulse{};
    std::array<ContactVector,2> prop_impulses{};
    std::array<float,2> prop_magnitudes{};
    unsigned prop_impulse_count=0;
    // Output-only sound context, captured before impulse/separation publication.
    VehicleVec3 point{},position_a{},position_b{},velocity_a{},velocity_b{};
};
// A rejected/separating pair must not consume the frame's motion history.
inline void update_race_contact_pose(RaceContactPose& pose,const VehicleState& state,
                                    const RacePairContact& contact) {
    if(contact.applied)pose=race_contact_pose(state);
}
RaceContactBox race_contact_box(const VehicleHull&);
struct RacePropImpact {
    float activation = 0;
    ContactMaterial material;
};
bool resolve_race_pair(VehicleState&,const VehicleSpec&,const RaceContactBox&,
                       VehicleState&,const VehicleSpec&,const RaceContactBox&,
                       RacePairContact&,RacePropImpact* prop=nullptr,RaceContactContext* context=nullptr);
// Original46AE60 two-flag remaining-time search and prepared46B5E0 response.
// Optional prepared job avoids changing witnesses after queue ordering.
bool resolve_swept_race_pair(VehicleState&,const VehicleSpec&,const RaceContactBox&,
    VehicleState&,const VehicleSpec&,const RaceContactBox&,RacePairContact&,
    const RaceContactPose&,const RaceContactPose&,RaceContactContext* context=nullptr,
    const RaceContactJob* prepared=nullptr,std::array<bool,2> fixed_world={});
bool resolve_swept_race_prop(VehicleState&,const VehicleSpec&,const RaceContactBox&,
    VehicleState&,const VehicleSpec&,const RaceContactBox&,RacePropImpact&,
    const RaceContactPose&,const RaceContactPose&,const RaceContactJob&,
    RaceContactContext&,RacePairContact&);
bool resolve_swept_race_rigid_pair(VehicleState&,const VehicleSpec&,const RaceContactBox&,RacePropImpact*,
    VehicleState&,const VehicleSpec&,const RaceContactBox&,RacePropImpact*,
    const RaceContactPose&,const RaceContactPose&,const RaceContactJob&,
    RaceContactContext&,RacePairContact&,std::array<bool,2> fixed_world={});
// Prepared static job, including original rewind/correction and pair record.
// The owning world resumes the remaining interval and invalidates other jobs.
bool resolve_swept_race_track_contact(VehicleState&,const VehicleSpec&,
    const RaceContactPose&,const RaceTrackContactJob&,float surface_friction,
    RaceContactContext&,RacePairContact&);
bool resolve_swept_race_prop_track(VehicleState&,const VehicleSpec&,const RacePropImpact&,
    const RaceContactPose&,const RaceTrackContactJob&,float surface_friction,
    RaceContactContext&,RacePairContact&);
}
