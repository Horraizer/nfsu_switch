#pragma once
#include <array>

namespace nfsu {
//47D1DB..47D28B after view-matrix inversion: derivative of the published
//world eye, using unscaled frame time. It is not the car's body velocity.
inline std::array<float,3> camera_motion_velocity(const std::array<float,3>& eye,
    const std::array<float,3>& previous,float dt,bool cut,
    std::array<float,3> retained={}) noexcept {
    if(cut)return {};
    if(!(dt>0))return retained;
    const float inverse=1.F/dt;
    for(unsigned k=0;k<3;++k)retained[k]=(eye[k]-previous[k])*inverse;
    return retained;
}
// Original scalar5AC449 normal transform used by40CD40. Native screen Y
// points down, so it is the negative of the portable camera's up vector.
inline std::array<float,3> camera_motion_to_view(const std::array<float,3>& v,
    const std::array<float,3>& right,const std::array<float,3>& up,
    const std::array<float,3>& forward) noexcept {
    return {{(v[1]*right[1]+v[2]*right[2])+v[0]*right[0],
        (v[1]*(-up[1])+v[0]*(-up[0]))+v[2]*(-up[2]),
        (v[1]*forward[1]+v[0]*forward[0])+v[2]*forward[2]}};
}
class CameraMotion {
public:
    void reset() noexcept {valid_=false;eye_={};velocity_={};}
    void update(const std::array<float,3>& eye,float real_dt,bool cut=false) noexcept {
        velocity_=camera_motion_velocity(eye,eye_,real_dt,cut||!valid_,velocity_);
        eye_=eye;valid_=true;
    }
    const std::array<float,3>& velocity() const noexcept {return velocity_;}
private:
    std::array<float,3> eye_{},velocity_{};
    bool valid_=false;
};
} // namespace nfsu
