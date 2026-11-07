#pragma once
#include "nfsu/track_contact.h"
#include "nfsu/vehicle.h"

namespace nfsu {
// Original4407B0/440A40 ordered ground cache. Each camera or car owns its cache.
// Terrain taps allocate nothing; the polygon list refreshes outside its bounds.
struct TrackGroundCache {
    const TrackContactWorld* world=nullptr;
    std::array<float,4> bounds{};
    std::array<std::size_t,160> polygons{};
    std::size_t count=0,last=std::size_t(-1);
    bool valid=false;
};
bool query_track_ground(const TrackContactWorld&,const VehicleVec3&,
                             TrackGroundCache&,TrackContact&);
} // namespace nfsu
