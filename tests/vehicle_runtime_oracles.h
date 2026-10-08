#pragma once
#include "nfsu/player_powertrain.h"
#include <vector>

namespace candidate_oracles {
struct GearCopy { int before,request,mode,after; };
struct Strut { float x,rate,partner,preload,overflow,bump,force; };
struct Contact { nfsu::VehicleVec3 arm; };
extern std::vector<GearCopy> gears;
extern std::vector<Strut> struts;
extern std::vector<Contact> contacts;
extern unsigned failures,distributions,parking_calls,service_calls;
void begin_step();
void end_step(const nfsu::VehicleState& before,const nfsu::VehicleState& after,
              const nfsu::VehicleInput& input);
}
