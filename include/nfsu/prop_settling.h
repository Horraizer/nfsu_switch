#pragma once
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>
#include <algorithm>
namespace nfsu {
// PC 1.4 USA 597EA0 / 597B80, finite velocity and clock inputs.
// Contact ownership/creation comes from the caller's contact adapter.
inline float prop_speed(const std::array<float,3>& v) noexcept {
    return std::sqrt((v[0]*v[0]+v[1]*v[1])+v[2]*v[2]);
}
inline unsigned prop_motion_state(const std::array<float,3>& velocity,
    const std::array<float,3>& angular,bool contact,unsigned previous,
    float since_car_contact) noexcept {
    const float speed=prop_speed(velocity),spin=prop_speed(angular);
    if(speed>2.0F || spin>2.0F || (!contact && previous!=2))return 0;
    if(speed>0.8F || spin>0.8F || since_car_contact<0.1F)return 1;
    return 2;
}
inline bool prop_can_settle(const std::array<float,3>& velocity,
    const std::array<float,3>& angular,bool contact,float since_car_contact,
    float contact_age) noexcept {
    return since_car_contact>0.1F && contact && contact_age>0.025F &&
        prop_speed(velocity)<0.1F && prop_speed(angular)<0.1F;
}
// 420720/420AB0 retain a contact record until last-touch age reaches 7000D8.
inline bool prop_contact_expired(float last_touch_age) noexcept {return last_touch_age>=0.3F;}
struct PropGroundContact {std::uint32_t polygon,created,touched;};
inline float prop_clock_age(std::uint32_t now,std::uint32_t then) noexcept {
    return static_cast<float>(static_cast<std::int32_t>(now-then))*0.00025F;
}
inline void prop_touch_ground(std::vector<PropGroundContact>& contacts,
    std::uint32_t polygon,std::uint32_t now) {
    for(auto& contact:contacts)if(contact.polygon==polygon){contact.touched=now;return;}
    contacts.push_back({polygon,now,now});
}
inline void prop_expire_ground(std::vector<PropGroundContact>& contacts,std::uint32_t now) {
    contacts.erase(std::remove_if(contacts.begin(),contacts.end(),[&](const auto& contact){
        return prop_contact_expired(prop_clock_age(now,contact.touched));
    }),contacts.end());
}
// 596649..596686: bounds are COM-relative; the initial lift is physical.
inline float prop_conversion_lift(float ground,float minimum,float centre,
    float up,float translation) noexcept {
    return std::max(0.F,(ground-((minimum+centre)*up+translation))+.05F);
}
// 597614..59765D: render uses the old compensation, then active bodies decay it.
inline float prop_next_lift(float lift,bool awake,float activation,float dt) noexcept {
    return awake&&activation==0?std::max(0.F,lift-.2F*dt):lift;
}
// 597FA0: the classifier and active-body retirement are separate operations.
inline void prop_rest_momenta(unsigned state,std::array<float,3>& linear,
    std::array<float,3>& angular) noexcept {
    if(state==2){linear={};angular={};}
    else if(state==1)for(unsigned k=0;k<3;++k){linear[k]*=0.95F;angular[k]*=0.95F;}
}
} // namespace nfsu
