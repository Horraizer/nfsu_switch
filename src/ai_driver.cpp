#include "nfsu/ai_driver.h"
#include "nfsu/race_route.h"

#include <algorithm>
#include <cmath>

namespace nfsu {
namespace {
// 700528/544/560 and 70057C/598/5B4. These are mode tables, not tuning.
constexpr float accelerate_p[] = {.1F,.1F,.1F,.1F,.005F,.1F,.1F};
constexpr float accelerate_i[] = {.02F,.02F,.02F,.02F,.005F,.02F,.02F};
constexpr float accelerate_d[] = {.02F,.02F,.01F,.01F,.01F,.01F,.01F};
constexpr float brake_p[] = {.1F,.1F,.1F,.1F,.005F,.1F,.1F};
constexpr float brake_i[] = {.02F,.02F,.02F,.02F,0,.02F,.02F};
constexpr float brake_d[] = {.02F,.02F,.01F,.01F,.008F,.01F,.01F};

// 70617C -> 7004F4, evaluated in the operation order of 583EA0.
float pedal_slew(float speed) noexcept {
    constexpr float curve[] = {1,.5F,.25F,.2F,.2F,.2F,.2F,.2F,.2F,.2F};
    const float coordinate = speed * .05624999850988388F;
    if (coordinate >= 9) return curve[9];
    const int index = static_cast<int>(coordinate);
    const float fraction = coordinate - static_cast<float>(index);
    return (1-fraction)*curve[index] + fraction*curve[index+1];
}

template<std::size_t N> float uniform_curve(const float (&values)[N],
                                           float scale, float value) noexcept {
    const float coordinate = value*scale;
    // 583EA0 clamps the TRUNCATED index, then computes a floor fraction.
    // Thus a coordinate in(-1,0) samples the first segment; it is not clamped.
    if (coordinate <= -1) return values[0];
    if (coordinate >= N-1) return values[N-1];
    const int index = static_cast<int>(coordinate);
    const float fraction = coordinate-(coordinate<0?-1.F:static_cast<float>(index));
    return (1-fraction)*values[index] + fraction*values[index+1];
}

float two_point_curve(float x, float x0, float y0, float x1, float y1) noexcept {
    if (x <= x0) return y0;
    if (x >= x1) return y1;
    return ((x-x0)/(x1-x0))*(y1-y0)+y0;
}

bool finite(const AiDriverError& e) noexcept {
    return std::isfinite(e.current) && std::isfinite(e.integral) &&
        std::isfinite(e.derivative);
}

bool finite(const AiDriverPoint& p) noexcept {
    return std::isfinite(p[0]) && std::isfinite(p[1]) && std::isfinite(p[2]);
}
AiDriverPoint difference(const AiDriverPoint& a,const AiDriverPoint& b) noexcept {
    return {a[0]-b[0],a[1]-b[1],a[2]-b[2]};
}
float length(const AiDriverPoint& p) noexcept {
    return std::sqrt((p[2]*p[2]+p[1]*p[1])+p[0]*p[0]);
}
float angle_error(std::uint16_t a,std::uint16_t b) noexcept {
    float v=float(static_cast<std::uint16_t>(a-b))*.0000958738019107841F;
    if(v>3.141590118408203F) v-=6.283180236816406F;
    else if(v< -3.141590118408203F) v+=6.283180236816406F;
    return v;
}
// 564BD0 computes both components from the same reduced angle. Its cosine
// polynomial is not the shifted564B10 sine approximation.
AiDriverPoint heading_vector(std::uint16_t heading) noexcept {
    float x=float(heading)*9.58738019107841e-5F,sign=1;
    if(x>=4.71238899230957F)x-=6.2831854820251465F;
    else if(x>=1.5707963705062866F){x-=3.1415927410125732F;sign=-1;}
    const float x2=x*x,x3=x2*x,x4=x2*x2,x5=x3*x2,x6=x4*x2;
    const float x7=x5*x2,x8=x6*x2;
    float s=x-x3*.1666666716337204F;
    s+=x5*.008333333767950535F;s-=x7*.00019841270113829523F;
    s+=(x7*x2)*2.7557318844628753e-6F;
    float c=1.F-.5F*x2;
    c+=x4*.0416666679084301F;c-=x6*.0013888889225199819F;
    c+=x8*2.4801587642286904e-5F;c-=(x8*x2)*2.755731998149713e-7F;
    return {c*sign,s*sign,0};
}
} // namespace
AiDriverPoint ai_driver_heading(std::uint16_t heading) noexcept {return heading_vector(heading);}

bool ai_driver_target(const AiDriverPath& path,const AiDriverPoint& position,
                      const AiDriverPoint& velocity,std::uint16_t heading,
                      float forward_speed,float absolute_speed,int category,
                      AiDriverTarget& result,unsigned traffic_mode) noexcept {
    if((traffic_mode!=0&&traffic_mode!=3&&traffic_mode!=4) || path.count>path.points.size() || !finite(position) ||
        !finite(velocity) || !std::isfinite(forward_speed) ||
        !std::isfinite(absolute_speed) || absolute_speed<0 || category<0 || category>3)
        return false;
    for(std::size_t j=0;j<path.count;++j) if(!finite(path.points[j])) return false;
    constexpr float lookahead[] = {8,12,17,25,33,39,45.8F,49.4F,53.1F,55.9F};
    constexpr float categories[] = {.9F,.9F};
    //45FB30 uses linear lookahead for traffic modes3/4, not the
    //racer's speed/category curves. The mode changes on physics activation.
    const float distance=traffic_mode==3?std::max(forward_speed*.550000011920929F,5.F):
        traffic_mode==4?std::max(forward_speed*.8999999761581421F,9.F):
        uniform_curve(lookahead,.05624999850988388F,forward_speed)*
        uniform_curve(categories,.3333333432674408F,float(category));
    AiDriverTarget next;
    if(path.count<2) {
        // 45FB30 ignores the sole path point.456A80 uses heading below5;
        // 456B20 otherwise scales the full actor movement vector via5673E0.
        const float scale=std::max(5.F,distance);
        AiDriverPoint offset;
        if(forward_speed<5) {
            offset=heading_vector(heading);
            for(float& v:offset)v*=scale;
        }else {
            const float norm=std::sqrt((velocity[0]*velocity[0]+velocity[1]*velocity[1])+velocity[2]*velocity[2]);
            const float denominator=norm/scale;
            if(denominator==0)offset={scale,0,0};
            else {const float inverse=1.F/denominator;
                for(unsigned k=0;k<3;++k)offset[k]=velocity[k]*inverse;}
        }
        for(unsigned k=0;k<3;++k)next.point[k]=position[k]+offset[k];
    }else {
    // 459710: choose the first sufficiently distant point ahead of velocity;
    // if none qualifies, the original returns the final path point.
    AiDriverPoint direction{1,0,0};
    const float norm=length(velocity);
    if(norm!=0) for(unsigned k=0;k<3;++k)direction[k]=velocity[k]/norm;
    std::size_t first=path.count-1;
    for(std::size_t j=0;j<path.count;++j) {
        const auto delta=difference(path.points[j],position);
        const float square=delta[1]*delta[1]+delta[0]*delta[0];
        const float norm2=std::sqrt(square);
        const float x=norm2==0 ? 1.F : (1.F/norm2)*delta[0];
        const float y=norm2==0 ? 0.F : (1.F/norm2)*delta[1];
        if(direction[1]*y+direction[0]*x>0 && square>16) {first=j;break;}
    }
    const auto delta=difference(path.points[first],position);
    float end_distance=std::sqrt((delta[1]*delta[1]+delta[2]*delta[2])+delta[0]*delta[0]);
    float start_distance=0;
    AiDriverPoint start=position;
    std::size_t end=first;
    while(end_distance<distance && end+1<path.count) {
        start=path.points[end];start_distance=end_distance;++end;
        end_distance=end_distance+length(difference(path.points[end],start));
    }
    if(end_distance<distance) {
        const auto last=difference(path.points[end],path.points[end-1]);
        const float norm3=std::sqrt((last[0]*last[0]+last[1]*last[1])+last[2]*last[2]);
        if(norm3==0) return false;
        const float scale=1.F/(norm3/(distance-end_distance));
        for(unsigned k=0;k<3;++k)next.point[k]=path.points[end][k]+last[k]*scale;
    } else {
        const float span=end_distance-start_distance;
        if(span==0) return false;
        const float fraction=(distance-start_distance)/span;
        for(unsigned k=0;k<3;++k)next.point[k]=(path.points[end][k]-start[k])*fraction+start[k];
    }
    }
    const auto target_heading=track_angle(next.point[0]-position[0],next.point[1]-position[1]);
    next.heading_error=angle_error(heading,target_heading);
    const float blend=two_point_curve(absolute_speed,10,0,20,1);
    if(blend>0) {
        const auto velocity_heading=track_angle(velocity[0],velocity[1]);
        next.velocity_error=(1-blend)*next.heading_error+
            angle_error(velocity_heading,target_heading)*blend;
    } else next.velocity_error=next.heading_error;
    if(!finite(next.point)||!std::isfinite(next.heading_error)||!std::isfinite(next.velocity_error))return false;
    result=next;return true;
}

bool ai_driver_slip_limit(bool hold, std::uint32_t role, float race_seconds,
                          bool has_status, std::int16_t finish_place) noexcept {
    return !hold && role != 3 && race_seconds > 10 &&
        (!has_status || finish_place == 0);
}

bool ai_driver_pedals(const AiDriverPedalInput& in,
                      AiDriverPedalState& state) noexcept {
    if (in.mode >= 7 || !std::isfinite(in.desired_speed) ||
        !std::isfinite(in.absolute_speed) || in.absolute_speed < 0 ||
        !std::isfinite(in.throttle_cap) || in.throttle_cap < 0 ||
        !finite(in.speed_error) || !finite(in.slip_error) ||
        !std::isfinite(state.throttle) || !std::isfinite(state.brake)) return false;
    const float desired = std::fabs(in.desired_speed);
    AiDriverPedalState next;
    if (!in.special && desired < 5) {
        if (std::fabs(desired-in.absolute_speed) <= 1) {
            next = {};
        } else if (desired > in.absolute_speed) {
            next.throttle = 1;
        } else {
            next.brake = 1;
        }
    } else {
        const bool accelerate = desired + 2 > in.absolute_speed;
        const auto mode = in.mode;
        const float p = accelerate ? accelerate_p[mode] : brake_p[mode];
        const float i = accelerate ? accelerate_i[mode] : brake_i[mode];
        const float d = accelerate ? accelerate_d[mode] : brake_d[mode];
        const float derivative = std::clamp(in.speed_error.derivative,-2.F,2.F);
        const float change = (derivative*d + in.speed_error.integral*i) +
            in.speed_error.current*p;
        const float slew = pedal_slew(in.absolute_speed);
        float value = (state.throttle-state.brake) + std::clamp(change,-slew,slew);
        value = std::clamp(value,accelerate ? 0.F : -1.F,
                          accelerate ? in.throttle_cap : 0.F);
        float limit = 1;
        if (in.slip_limit) {
            const auto& e = in.slip_error;
            const float sp = value < 0 ? .02F : -.2F;
            const float si = value < 0 ? .02F : -.02F;
            const float sd = value < 0 ? .005F : -.005F;
            limit = ((e.derivative*sd + e.integral*si) + e.current*sp) + 1;
        }
        if (value < 0) next.brake = in.slip_limit ? std::min(-value,limit) : -value;
        else next.throttle = in.slip_limit ? std::min(value,limit) : value;
    }
    next.throttle = std::clamp(next.throttle,0.F,in.throttle_cap);
    next.brake = std::clamp(next.brake,0.F,1.F);
    if (!std::isfinite(next.throttle) || !std::isfinite(next.brake)) return false;
    state = next;
    return true;
}

AiDriverHistory::AiDriverHistory(std::uint8_t samples) noexcept
    : length_(samples == 5 ? 5 : 4) {}

void AiDriverHistory::Ring::push(float value, std::uint8_t length) noexcept {
    if (count < length) ++count;
    // The original stores the addition before subtracting the replaced item.
    sum = sum + value;
    sum = sum - values[next];
    values[next] = value;
    average = sum / static_cast<float>(count);
    if (++next >= length) next = 0;
}

void AiDriverHistory::update(float value, float dt, bool suppress_derivative) noexcept {
    if (!std::isfinite(value) || !std::isfinite(dt) || dt <= 0) return;
    const float previous = current_;
    const float difference = value-previous;
    current_ = value;
    const float integral = (difference*.5F + previous)*dt;
    const float derivative = suppress_derivative ? 0 : difference/dt;
    time_.push(dt,length_);
    integral_.push(integral,length_);
    derivative_.push(derivative,length_);
}

AiDriverError AiDriverHistory::error() const noexcept {
    const float integral = integral_.count == 0 ? 0 :
        (integral_.sum*static_cast<float>(integral_.count))/(time_.sum*30.F);
    return {current_,integral,derivative_.average};
}

AiDriverHistorySnapshot AiDriverHistory::snapshot() const noexcept {
    AiDriverHistorySnapshot out;
    const auto copy=[](const Ring& in,AiDriverHistorySnapshot::Ring& to) {
        to.values=in.values;to.sum=in.sum;to.average=in.average;
        to.count=in.count;to.next=in.next;
    };
    copy(time_,out.time);copy(integral_,out.integral);copy(derivative_,out.derivative);
    out.current=current_;out.samples=length_;return out;
}

bool AiDriverHistory::restore(const AiDriverHistorySnapshot& in) noexcept {
    if((in.samples!=4&&in.samples!=5)||!std::isfinite(in.current))return false;
    for(const auto* ring:{&in.time,&in.integral,&in.derivative}) {
        if(ring->count>in.samples||ring->next>=in.samples||
           !std::isfinite(ring->sum)||!std::isfinite(ring->average))return false;
        for(float value:ring->values)if(!std::isfinite(value))return false;
    }
    const auto copy=[](const AiDriverHistorySnapshot::Ring& from,Ring& out) {
        out.values=from.values;out.sum=from.sum;out.average=from.average;
        out.count=from.count;out.next=from.next;
    };
    copy(in.time,time_);copy(in.integral,integral_);copy(in.derivative,derivative_);
    current_=in.current;length_=in.samples;return true;
}

void AiDriverHistory::clear_error_samples() noexcept {
    reset_error_samples(0);
}
void AiDriverHistory::reset_error_samples(float value) noexcept {
    current_=0;
    for(auto* ring:{&integral_,&derivative_}) {
        ring->values.fill(value);
        ring->sum=static_cast<float>(length_)*value;
        ring->count=0;
    }
}

namespace {
//45F5E3..45F6DC, common grounded/unsupported-front-wheel slew.
float traffic_steering_maximum(float speed) {
    constexpr float lock[]={45,30,20,12,8,6,4.5F,4.2F,4.1F,4};
    return uniform_curve(lock,.05624999850988388F,speed);
}
std::uint16_t steering_slew(float requested,float speed,float dt,std::uint16_t word,float maximum=45.F,float yaw_scale=1.F) {
    constexpr float rate[] = {4,2,1,.5F,.27F,.14F,.1F,.1F,.1F,.1F};
    constexpr float axis_scale=.05624999850988388F;
    float previous=float(word)*.0054931640625F;
    if (previous>180) previous-=360;
    if(maximum<=0)return 0;
    const float desired=std::clamp(requested/(maximum*.01745329238474369F),-1.F,1.F);
    previous=previous/maximum;
    float slew=uniform_curve(rate,axis_scale,speed)*4.F;
    if ((previous>0 && previous>desired) || (previous<0 && previous<desired)) slew*=2.F;
    slew*=std::max(dt,.0001F);
    const float degrees=((std::clamp(desired-previous,-slew,slew)+previous)*maximum)*yaw_scale;
    const auto quantized=static_cast<std::int32_t>(degrees*65536.F)/360;
    return static_cast<std::uint16_t>(quantized);
}
}
static bool ground_steering(float heading_error, float velocity_error,
                              float speed, int category, float dt,
                              AiDriverSteeringState& state,bool reverse_command,bool moving_reverse,unsigned traffic_mode,float yaw_scale=1.F) noexcept {
    if (!std::isfinite(heading_error) || !std::isfinite(velocity_error) ||
        !std::isfinite(speed) || speed < 0 || category < 0 || category > 3 ||
        !std::isfinite(dt) || dt <= 0) return false;
    // 7060C8, 7060DC, 706104, category multipliers 70612C/140/154.
    constexpr float p[] = {.328F,.22F,.148F,.115F,.09F,.074F,.057F,.049F,.043F,.04F};
    constexpr float d[] = {0,.075829F,.075829F,.073826F,.060403F,.04698F,.04698F,.040268F,.040268F,.040268F};
    constexpr float i[] = {.21F,.244F,.267F,.29F,.305F,.321F,.328F,.336F,.341F,.344F};
    constexpr float pc[] = {1,1.2F}, dc[] = {1,2}, ic[] = {3,3};
    constexpr float axis_scale = .05624999850988388F;
    constexpr float category_scale = .3333333432674408F;
    AiDriverSteeringState next = state;
    next.heading.update(heading_error,dt);
    next.velocity_heading.update(velocity_error,dt);
    const auto h=next.heading.error(), v=next.velocity_heading.error();
    // 45EF49 blends heading and velocity-heading histories. The separate
    // lateral history at driver+234 is not one of these two operands.
    const float blend = two_point_curve(std::fabs(h.current-v.current),
        .1745329201221466F,.3F,.5235987901687622F,1);
    const float other=1-blend;
    const float error=other*v.current + blend*h.current;
    const float integral=std::clamp(other*v.integral + blend*h.integral,
        -.10471975803375244F,.10471975803375244F);
    const float derivative=std::clamp(other*v.derivative + blend*h.derivative,-1.F,1.F);
    const bool traffic=traffic_mode!=0;
    const float kp=traffic ? (traffic_mode==4?.8500000238418579F:.699999988079071F) : uniform_curve(p,axis_scale,speed)*uniform_curve(pc,category_scale,float(category));
    const float kd=traffic ? (traffic_mode==4?.11999999731779099F:0.F) : uniform_curve(d,axis_scale,speed)*uniform_curve(dc,category_scale,float(category));
    const float ki=traffic ? (traffic_mode==4?.7900000214576721F:1.4199999570846558F) : uniform_curve(i,axis_scale,speed)*uniform_curve(ic,category_scale,float(category));
    const float requested=std::clamp((-kp*error-derivative*kd)-integral*ki,
        -.7853981852531433F,.7853981852531433F);
    const float maximum=traffic?traffic_steering_maximum(moving_reverse?-speed:speed):45.F;
    next.word=steering_slew(reverse_command?-requested:requested,moving_reverse?-speed:speed,dt,state.word,maximum,yaw_scale);
    state=next;
    return true;
}

bool ai_driver_ground_steering(float h,float v,float speed,int category,float dt,
                              AiDriverSteeringState& state,bool reverse_command,bool moving_reverse) noexcept {
    return ground_steering(h,v,speed,category,dt,state,reverse_command,moving_reverse,false);
}
//45EA80 selector2 -> mode3. Original constant steering coefficients and
// special pedal branch; no racer slip clamp or selector3 tuning substituted.
bool ai_driver_traffic_step(const AiDriverForwardInput& in,float dt,AiDriverForwardState& state,float yaw_scale) noexcept {
    if(!std::isfinite(in.desired_speed)||in.desired_speed<0||!std::isfinite(dt)||dt<=0||!std::isfinite(yaw_scale))return false;
    auto next=state;
    if(!ai_driver_target(in.path,in.position,in.velocity,in.heading,in.forward_speed,
                        in.absolute_speed,in.category,next.target,3)||
       !ground_steering(next.target.heading_error,next.target.velocity_error,
                       in.absolute_speed,in.category,dt,next.steering,false,false,3,yaw_scale))return false;
    next.speed.update(in.desired_speed-in.absolute_speed,dt);
    AiDriverPedalInput pedal;pedal.desired_speed=in.desired_speed;pedal.absolute_speed=in.absolute_speed;
    pedal.throttle_cap=1;pedal.speed_error=next.speed.error();pedal.mode=3;
    if(in.desired_speed<2)next.pedals={0,1};
    else if(!ai_driver_pedals(pedal,next.pedals))return false;
    if(!in.enabled)next.pedals={};
    state=next;return true;
}

namespace {
bool driver_step(const AiDriverForwardInput& in,float dt,
                 AiDriverForwardState& state,bool signed_control,bool drag=false,bool traffic=false,float yaw_scale=1.F) noexcept {
    if((!signed_control&&(in.forward_speed<0 || in.desired_speed<0 || in.path.count<2)) ||
        !std::isfinite(in.desired_speed) || !std::isfinite(in.rear_drive_fraction) ||
        !std::isfinite(in.route_distance) || !std::isfinite(in.race_seconds) ||
        !std::isfinite(dt) || dt<=0 || !std::isfinite(yaw_scale)) return false;
    for(float s:in.wheel_slip) if(!std::isfinite(s))return false;
    AiDriverForwardState next=state;
    bool driven_contact=false;
    for(unsigned k=0;k<4;++k)if(in.contact[k] &&
        (in.rear_drive_fraction>.9F ? k>=2 : in.rear_drive_fraction<.1F ? k<2 : true))
        driven_contact=true;
    bool invert=false,waiting=false;
    next.gear_request=-1;
    if(signed_control&&driven_contact&&std::fabs(in.desired_speed)>=2) {
        const float signed_speed=in.forward_speed<0?-in.absolute_speed:in.absolute_speed;
        if(in.desired_speed>0) {
            if(signed_speed< -7)next.direction_ready=false;
            else if(signed_speed> -2)next.direction_ready=true;
            invert=!next.direction_ready;
        } else {
            if(signed_speed>7)next.direction_ready=false;
            else if(signed_speed<2)next.direction_ready=true;
            invert=next.direction_ready;
            if(next.direction_ready)next.gear_request=0;
        }
        waiting=!next.direction_ready;
    }
    if(!ai_driver_target(in.path,in.position,in.velocity,in.heading,in.forward_speed,
                        in.absolute_speed,in.category,next.target,traffic?4U:0U))return false;
    if(in.contact[0]||in.contact[1]) {
        if(!ground_steering(next.target.heading_error,next.target.velocity_error,
                                      in.absolute_speed,in.category,dt,next.steering,invert,in.forward_speed<0,
                                      traffic?4U:0U,yaw_scale))return false;
    } else {
        //45F2B4..45F360: front wheels unsupported. Do not advance the two
        // grounded steering histories. Counter the heading/velocity mismatch
        // with the original five-degree cap, then use the common slew.
        float difference=0;
        if(in.forward_speed>1.F) {
            const auto velocity_heading=track_angle(in.velocity[0],in.velocity[1]);
            difference=float(static_cast<std::uint16_t>(in.heading-velocity_heading))*9.58738019107841e-5F;
            if(difference>3.141590118408203F)difference-=6.283180236816406F;
        }
        const float requested=std::clamp(-difference,5.F*-.01745329238474369F,5.F*.01745329238474369F);
        next.steering.word=steering_slew(invert?-requested:requested,in.forward_speed<0?-in.absolute_speed:in.absolute_speed,dt,state.steering.word,traffic?traffic_steering_maximum(in.forward_speed<0?-in.absolute_speed:in.absolute_speed):45.F,yaw_scale);
    }
    next.speed.update(std::fabs(in.desired_speed)-in.absolute_speed,dt);
    float slip=0;
    // 45E7B0 excludes samples inside the deadband; it neither clamps nor
    // discards large slip. The clamp is used only for its equality test.
    for(float s:in.wheel_slip)if(s< -4 || s>4)slip+=s;
    //45E82C..45E83F uses actor signed speed to orient the slip sum.
    if(in.forward_speed<0)slip=-slip;
    next.slip.update(slip,dt);
    if(driven_contact) {
        AiDriverPedalInput p;p.mode=traffic?4U:drag?1U:0U;
        p.desired_speed=in.desired_speed;p.absolute_speed=in.absolute_speed;
        p.throttle_cap=in.throttle_cap;p.speed_error=next.speed.error();p.slip_error=next.slip.error();
        p.slip_limit=ai_driver_slip_limit(false,traffic?3U:2U,in.race_seconds,false,0);
        // Ordinary desired-speed near-zero branch in 45F790 is distinct from
        // the low-target branch inside 45FC90.
        if((signed_control&&std::fabs(in.desired_speed)<2)||
           (!signed_control&&in.desired_speed<2&&in.absolute_speed<2)) {
            next.pedals={0,1};
        } else if(waiting) {
            next.speed.clear_error_samples();next.pedals={0,.9F};
        } else if(!ai_driver_pedals(p,next.pedals))return false;
    } else {
        // Ordinary selector3 owner state is zero. 45F566 calls 45E710;
        // leaving the speed samples alive changes pedals after recontact.
        next.pedals={};
        next.speed.clear_error_samples();
    }
    if(!in.enabled)next.pedals={};
    //45FAC8..45FAE9, including the exact2.0 boundary (native witness).
    if(in.route_distance<=2)next.steering.word=0;
    state=next;return true;
}
} // namespace
bool ai_driver_forward_step(const AiDriverForwardInput& in,float dt,AiDriverForwardState& state) noexcept {
    return driver_step(in,dt,state,false);
}
bool ai_driver_ordinary_step(const AiDriverForwardInput& in,float dt,AiDriverForwardState& state) noexcept {
    return driver_step(in,dt,state,true);
}
bool ai_driver_drag_step(const AiDriverForwardInput& in,float dt,AiDriverForwardState& state) noexcept {
    return driver_step(in,dt,state,true,true);
}
bool ai_driver_traffic_impact_step(const AiDriverForwardInput& in,float dt,AiDriverForwardState& state,float yaw_scale) noexcept {
    //45EA80..45EB50: selector3/controller4 chooses mode4. Keep the
    //same histories across the traffic-mover transition; ordinary support
    //and direction gates now apply, with role3 excluding the racer slip cap.
    return driver_step(in,dt,state,true,false,true,yaw_scale);
}
bool ai_driver_recovery_reset(const AiDriverForwardInput& in,AiDriverForwardState& state) noexcept {
    if(!finite(state.target.point)||!finite(in.position)||!finite(in.velocity)||
       !std::isfinite(in.absolute_speed)||in.absolute_speed<0)return false;
    const auto heading=track_angle(state.target.point[0]-in.position[0],state.target.point[1]-in.position[1]);
    const float error=angle_error(in.heading,heading);
    const float blend=two_point_curve(in.absolute_speed,10,0,20,1);
    const float velocity_error=blend>0?(1-blend)*error+angle_error(track_angle(in.velocity[0],in.velocity[1]),heading)*blend:error;
    state.steering.heading.reset_error_samples(error);
    state.steering.velocity_heading.reset_error_samples(velocity_error);
    return true;
}
} // namespace nfsu
