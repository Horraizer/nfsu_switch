#pragma once
#include "nfsu/input.h"
#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace nfsu {
// Explicit fields for the port's LAN protocol, never ABI dumps.
constexpr std::size_t kLanDatagramLimit=1200;
constexpr unsigned kLanPort=34783;
constexpr std::uint32_t kLanConfig=0x04000302;
struct LanCourseOption {const char* name;unsigned event,round;};
constexpr LanCourseOption kLanCourses[]={
    {"Olympic Square",1,0},{"Olympic Square Reverse",6,1},
    {"Atlantic",8,0},{"Atlantic Reverse",52,0}
};
constexpr const char* kLanPerformance[]={"Stock","Street","Pro","Extreme"};
struct LanRide {
    std::uint32_t car=0; // Retail car name hash; zero selects the default Civic.
    std::array<std::uint32_t,120> installed{};
    bool operator==(const LanRide& other)const noexcept {
        return car==other.car&&installed==other.installed;
    }
};
struct LanRaceOptions {
    std::uint8_t course=0,laps=2,tier=0;
    std::array<LanRide,2> rides{};
    bool operator==(const LanRaceOptions& other)const noexcept {
        return course==other.course&&laps==other.laps&&tier==other.tier&&rides==other.rides;
    }
};
constexpr bool valid_lan_options(const LanRaceOptions& options) noexcept {
    return options.course<sizeof(kLanCourses)/sizeof(kLanCourses[0])&&
        options.laps>=1&&options.laps<=6&&options.tier<sizeof(kLanPerformance)/sizeof(kLanPerformance[0]);
}
enum class LanMessageKind : std::uint8_t { hello=1,welcome,ready,input,snapshot,bye };
struct LanCarFrame {
    std::array<float,3> position{},velocity{},angular_velocity{};
    std::array<float,4> orientation{{1,0,0,0}},compression{},wheel_phase{};
    float steering=0,rpm=0,progress=0,parameter=0,throttle=0,drive_load=0,turbo=0;
    std::int32_t gear=2,lap=-1,highest_lap=0,checkpoint=-1,highest_checkpoint=-1;
    std::int32_t lap_start=0,best_lap=0,finish_ticks=0;
    std::uint32_t nitrous_capacity=0,nitrous_remaining=0;
    std::uint8_t place=0,status=0;
    bool braking=false,finished=false,nitrous_active=false;
};
struct LanRaceFrame {
    std::uint32_t step=0;
    std::int32_t world_ticks=0,race_ticks=0,start_ticks=0,countdown_start=0;
    std::uint8_t stage=3,outcome=0,countdown=0;
    bool go=false;
    std::array<LanCarFrame,2> cars{};
};
struct LanMessage {
    LanMessageKind kind=LanMessageKind::hello;
    std::uint32_t session=0,sequence=0,ack=0,config=kLanConfig;
    LanRaceOptions race;
    bool launch=false;
    LanRide guest_ride;
    bool guest_ready=false;
    GamepadState input;
    LanRaceFrame frame;
};
bool lan_encode(const LanMessage&,std::vector<std::uint8_t>&,std::string&);
bool lan_decode(const std::uint8_t*,std::size_t,LanMessage&,std::string&);
// Serial arithmetic admits wraparound and rejects duplicates/old datagrams.
bool valid_lan_frame(const LanRaceFrame&) noexcept;
bool lan_newer(std::uint32_t value,std::uint32_t previous) noexcept;
}
