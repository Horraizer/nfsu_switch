#include "nfsu/ai_path_body.h"
#include "nfsu/player_powertrain.h"
#include "nfsu/race_route.h"
#include <algorithm>
#include <cmath>

namespace nfsu {
float ai_path_body_project(const AiDriverPath& path,const PlayerRigidVector& position) noexcept {
    if(path.count<2)return 0.F;
    // 59D430 starts at node zero and stops when the distance first grows.
    const auto distance=[&](unsigned i) {
        const auto& p=path.points[i];
        const float dx=position[0]-p[0],dy=position[1]-p[1],dz=position[2]-p[2];
        return std::sqrt((dz*dz+dy*dy)+dx*dx);
    };
    unsigned nearest=0;
    for(float best=distance(0);nearest+1<path.count;++nearest) {
        const float candidate=distance(nearest+1);
        if(candidate>=best)break;
        best=candidate;
    }
    // 59D540 finds the perpendicular on the 3D spline by secant iteration.
    float parameter=float(nearest),step=1.F,previous_move=0.F,previous_along=0.F;
    for(unsigned iteration=0;iteration<998;++iteration) {
        const auto sample=ai_path_body_sample(path,parameter);
        const float px=sample.point[0],py=sample.point[1],pz=sample.point[2];
        const float dx=sample.tangent[0],dy=sample.tangent[1],dz=sample.tangent[2];
        const float slope=std::sqrt((dz*dz+dy*dy)+dx*dx);
        if(!(slope>0.F))break;
        const float along=(((position[2]-pz)*dz+(position[1]-py)*dy)+
                           (position[0]-px)*dx)/slope;
        if(std::abs(along)<.005F)break;
        const float rate=previous_move==0.F?slope:-(along-previous_along)/previous_move;
        if(rate==0.F)break;
        const float move=std::clamp(along/rate,-step,step);
        parameter+=move;
        if(move*previous_move<0.F)step*=.5F;
        previous_move=move;
        previous_along=along;
    }
    return parameter;
}

void ai_path_body_speed_target(AiPathBodyState& state,const VehicleHandling& h,
    const std::array<float,64>& acceleration,const AiPathBodyDemand& input) noexcept {
    state.rate=input.forward*state.points_per_metre;
    // 463A0A reloads the stored path rate before456830 divides by Actor404.
    const float speed=(state.rate/state.points_per_metre)/input.scale;
    constexpr float maximum=98.34600067138672F;
    const float coordinate=speed*(63.F/maximum);
    float drive;
    // 583EA0 truncates the index but uses a floor fraction. Coordinates
    // in(-1,0) therefore interpolate the first segment instead of clamping.
    if(coordinate<=-1.F)drive=acceleration.front();
    else if(coordinate>=63.F)drive=acceleration.back();
    else {
        const auto index=static_cast<unsigned>(static_cast<int>(coordinate));
        const float fraction=coordinate-std::floor(coordinate);
        drive=(1.F-fraction)*acceleration[index]+fraction*acceleration[index+1];
    }
    const float drag=(1.F-input.drafting)*h.drag*speed*speed/h.mass;
    if(input.gearing_limit>0.F&&speed>=input.gearing_limit&&drive>drag)drive=drag;
    const auto tangent=ai_path_body_sample(state.path,state.parameter).tangent;
    // 4638A0 normalizes with567370, storing binary32 before applying gravity.
    const float length=std::sqrt((tangent[0]*tangent[0]+tangent[1]*tangent[1])+tangent[2]*tangent[2]);
    const float slope=length==0.F?0.F:-9.800000190734863F*(tangent[2]*(1.F/length));
    const float cap=((drive-drag)+slope)*state.points_per_metre;
    const float duration=input.duration>0.F?input.duration:.5F;
    const float target=(input.desired*state.points_per_metre-state.rate)/duration;
    // 463A25 checks the upper cap before the -20 m/s2 floor.
    state.acceleration=target>cap?cap:std::max(-20.F*state.points_per_metre,target);
}

void AiPathBodyChannel::set(float v) noexcept {
    target=v;
    if(target!=value)status=2;
}
void AiPathBodyChannel::step(float dt) noexcept {
    // 593EC0 restarts continuous channels from their current value and
    // derivative. The lateral channel instead retains a running segment.
    if(!status)return;
    if(status==2) {
        phase=0;
        if(!continuous)status=1;
        const float delta=target-value;
        c=derivative;d=value;
        a=(target_derivative+derivative)-(delta+delta);
        b=delta*3.F-target_derivative-(derivative+derivative);
    }
    phase=dt/duration+phase;
    if(phase<end) {
        value=((phase*a+b)*phase+c)*phase+d;
        derivative=((b+b)+phase*a*3.F)*phase+c;
    } else {
        status=0;
        if(end>=1.F){value=target;derivative=target_derivative;}
    }
}
AiPathBodySample ai_path_body_sample(const AiDriverPath& path,float parameter) noexcept {
    // 59CDB0 performs two separate matrix products. Combining the cubic
    // coefficients changes the signed residual of a constant coordinate,
    // which becomes a full direction after567370 normalization.
    constexpr float basis[3][4][4]={
        {{0,-.5F,1,-.5F},{1,0,-2.5F,1.5F},{0,.5F,2,-1.5F},{0,0,-.5F,.5F}},
        {{-.5F,2,-1.5F,0},{0,-5,4.5F,0},{.5F,4,-4.5F,0},{0,-1,1.5F,0}},
        {{2,-3,0,0},{-5,9,0,0},{4,-9,0,0},{-1,3,0,0}}
    };
    const float whole=std::floor(parameter),t=parameter-whole,t2=t*t;
    const float powers[]={1,t,t2,t2*t};
    const int index=int(whole),last=int(path.count)-1;
    float points[4][3];
    for(int j=0;j<4;++j) {
        const int at=index+j-1;
        for(unsigned k=0;k<3;++k) {
            if(at<0)points[j][k]=(path.points[0][k]-path.points[1][k])*float(-at)+path.points[0][k];
            else if(at>last)points[j][k]=(path.points[last][k]-path.points[last-1][k])*float(at-last)+path.points[last][k];
            else points[j][k]=path.points[at][k];
        }
    }
    AiPathBodySample result;
    PlayerRigidVector* outputs[]={&result.point,&result.tangent,&result.curvature};
    for(unsigned derivative=0;derivative<3;++derivative) {
        float weights[4];
        for(unsigned j=0;j<4;++j) {
            const auto& b=basis[derivative][j];
            const float first=powers[3]*b[3]+powers[1]*b[1];
            weights[j]=j?((first+powers[0]*b[0])+powers[2]*b[2]):
                         ((first+powers[2]*b[2])+powers[0]*b[0]);
        }
        for(unsigned k=0;k<3;++k) {
            const float first=points[3][k]*weights[3]+points[1][k]*weights[1];
            (*outputs[derivative])[k]=k?((first+points[0][k]*weights[0])+points[2][k]*weights[2]):
                                        ((first+points[2][k]*weights[2])+points[0][k]*weights[0]);
        }
    }
    return result;
}

namespace {
struct Motion {
    AiDriverPath& path;
    float &parameter,&rate,&acceleration,&points_per_metre,&elapsed;
    std::array<AiPathBodyChannel,5>& channels;
    std::array<float,30>& curvature_history;
    float &curvature_sum,&curvature_average;
    unsigned &curvature_count,&curvature_cursor;
    bool& sliding;
    explicit Motion(AiPathBodyState& s):path(s.path),parameter(s.parameter),rate(s.rate),
        acceleration(s.acceleration),points_per_metre(s.points_per_metre),elapsed(s.elapsed),
        channels(s.channels),curvature_history(s.curvature_history),curvature_sum(s.curvature_sum),
        curvature_average(s.curvature_average),curvature_count(s.curvature_count),
        curvature_cursor(s.curvature_cursor),sliding(s.sliding){}
    std::array<float,3> velocity(float time) const{
        auto s=ai_path_body_sample(path,(time*acceleration*.5F+rate)*time+parameter);
        for(auto& v:s.tangent)v*=time*acceleration+rate;
        return s.tangent;
    }
    static std::array<float,3> right(std::array<float,3> v){
        const float n=std::sqrt(v[1]*v[1]+v[0]*v[0]);
        if(n==0)return {1,0,0};
        const float r=1.F/n;return {v[1]*r,-v[0]*r,0};
    }
    float force(const VehicleHandling& h,float lateral){
        float demand=std::abs(lateral);
        const float sign=demand>0.00001F?lateral/demand:0.F;
        demand=(h.tyres[0].lateral_scale+h.tyres[1].lateral_scale)*.5F*h.mass*demand;
        const float friction=sliding?h.tyres[0].sliding_friction+h.tyres[1].sliding_friction:
            h.tyres[0].static_friction+h.tyres[1].static_friction;
        const float load=(9.8F*h.mass+(rate/points_per_metre)*(h.downforce_front+h.downforce_rear)*.5F)*friction*.5F;
        std::array<float,4> x{0,(1.F-.1F)*load,(.1F+1.F)*load,1.5F*load};
        const std::array<float,4> y=sliding?std::array<float,4>{0,.4F,.6F,1.F}:std::array<float,4>{0,.1F,.4F,.7F};
        const bool next=load<demand;
        if(sliding!=next){
            for(unsigned j=3;j<5;++j){
                auto& c=channels[j];c.duration=next?1.F:.3F;
                if(!next){c.derivative*=1.6F;if(c.derivative!=c.target_derivative)c.status=2;}
            }sliding=next;
        }
        if(demand<=x[0])return sign*y[0];
        if(demand>=x[3])return sign*y[3];
        for(unsigned j=0;j<3;++j)if(x[j]<=demand&&demand<x[j+1]){
            const float width=x[j+1]-x[j];
            const float fraction=std::abs(width)<=1e-6F?.5F:(demand-x[j])/width;
            return sign*(fraction*(y[j+1]-y[j])+y[j]);
        }return sign*y[0];
    }
    AiPathBodyStep advance(float dt,const VehicleHandling& h){
        elapsed+=dt;const float speed=(elapsed*acceleration+rate)/points_per_metre;
        const auto target=velocity(elapsed+.5F);
        channels[1].set(target[0]);channels[2].set(target[1]);
        channels[1].step(dt);channels[2].step(dt);
        const float ahead=elapsed+.3F;
        const auto perpendicular=right(velocity(ahead));
        const float r=ahead*acceleration+rate;
        auto s=ai_path_body_sample(path,(ahead*acceleration*.5F+rate)*ahead+parameter);
        for(unsigned k=0;k<3;++k)s.curvature[k]=s.curvature[k]*(r*r)+s.tangent[k]*acceleration;
        const float lat=(perpendicular[0]*s.curvature[0]+perpendicular[2]*s.curvature[2])+perpendicular[1]*s.curvature[1];
        if(curvature_count<30)++curvature_count;
        curvature_sum=(lat+curvature_sum)-curvature_history[curvature_cursor];
        curvature_history[curvature_cursor]=lat;
        curvature_average=curvature_sum/float(curvature_count);
        if(++curvature_cursor>=30)curvature_cursor=0;
        const float f=force(h,curvature_average);
        const float drift=.3F*f*speed;
        channels[3].set(perpendicular[0]*drift);channels[4].set(perpendicular[1]*drift);
        channels[3].step(dt);channels[4].step(dt);
        const auto heading=track_angle(channels[3].value+channels[1].value,channels[4].value+channels[2].value);
        const auto side=right(velocity(elapsed));channels[0].set(-f);channels[0].step(dt);
        s=ai_path_body_sample(path,(elapsed*acceleration*.5F+rate)*elapsed+parameter);
        if((elapsed*acceleration*.5F+rate)*elapsed+parameter<0.F)
            s.point[2]=std::max(s.point[2],path.points[0][2]);
        return {{s.point[0]+side[0]*channels[0].value,s.point[1]+side[1]*channels[0].value,s.point[2]},heading,speed};
    }
};
}

void ai_path_body_seed(AiPathBodyState& s,std::uint16_t heading,float speed,float yaw) noexcept {
    s={};
    const float derivative=.4F*yaw*speed;
    const float values[]={player_angle_sin(std::uint16_t(heading+16384))*speed,
                          player_angle_sin(heading)*speed};
    const float derivatives[]={player_angle_sin(std::uint16_t(heading+32768))*derivative,
                               player_angle_sin(std::uint16_t(heading+16384))*derivative};
    for(unsigned k=0;k<2;++k) {
        auto& c=s.channels[k+1];c.value=values[k];c.derivative=derivatives[k];
        if(c.value!=c.target||c.derivative!=c.target_derivative)c.status=2;
        s.channels[k+3].duration=.3F;
    }
}
void ai_path_body_publish(AiPathBodyState& s,const PlayerRigidVector& position,
                          const PlayerRigidVector& velocity) noexcept {
    s.elapsed=0;
    const auto p=ai_path_body_sample(s.path,s.parameter);
    const float length=std::sqrt((p.tangent[0]*p.tangent[0]+p.tangent[1]*p.tangent[1])+p.tangent[2]*p.tangent[2]);
    const float inverse=length==0.F?0.F:1.F/length;
    const auto t=length==0.F?PlayerRigidVector{1,0,0}:PlayerRigidVector{
        p.tangent[0]*inverse,p.tangent[1]*inverse,p.tangent[2]*inverse};
    auto& lateral=s.channels[0];
    lateral.value=-(t[1]*(p.point[0]-position[0])-t[0]*(p.point[1]-position[1]));
    lateral.derivative=.5F*(t[1]*velocity[0]-t[0]*velocity[1]);
    if(lateral.value!=lateral.target||lateral.derivative!=lateral.target_derivative)lateral.status=2;
    lateral.continuous=false;lateral.duration=.5F;
    s.channels[1].duration=.4F;s.channels[2].duration=.4F;
}
AiPathBodyStep ai_path_body_step(AiPathBodyState& s,const VehicleHandling& h,float dt) noexcept {
    return Motion(s).advance(dt,h);
}
float rival_path_parameter(float parameter,float rate,float acceleration,float time) {
    return (.5F*(time*acceleration)+rate)*time+parameter;
}

bool ai_path_body_departure(const AiPathBodyState& state,const VehicleHandling& h) noexcept {
    const auto& path=state.path;
    const float parameter=state.parameter,rate=state.rate,acceleration=state.acceleration,elapsed=state.elapsed;
    // 463D64 uses the speed saved by463910, not its accelerated derivative.
    const float start_speed=rate/state.points_per_metre;
    const float downward_acceleration=9.800000190734863F+
        (h.downforce_front+h.downforce_rear)*start_speed*.5F/h.mass;
    const auto sample=[&](float time) {
        const float at=rival_path_parameter(parameter,rate,acceleration,time);
        auto result=ai_path_body_sample(path,at);
        // 4645D0 clamps only the sampled height before the first node;
        // 464660 still supplies the unmodified spline derivative.
        if(at<0.F)result.point[2]=std::max(result.point[2],path.points[0][2]);
        return result;
    };
    auto anchor=sample(elapsed);
    float anchor_time=0;
    float anchor_speed=float(anchor.tangent[2])*(rate+elapsed*acceleration);
    // 463D50 checks the spline at nine 50 ms intervals. When the path
    // catches up to the trajectory, the next trajectory starts there.
    for(int i=1;i<10;++i) {
        const float ahead=(float(i)*.5F)*.10000000149011612F;
        const float time=elapsed+ahead,delta=ahead-anchor_time;
        const auto next=sample(time);
        const float height=(anchor_speed-downward_acceleration*delta)*delta+float(anchor.point[2]);
        if(height-float(next.point[2])>1.F)return true;
        if(height<=next.point[2]) {
            anchor_time=ahead;
            anchor=next;
            anchor_speed=float(next.tangent[2])*(rate+time*acceleration);
        }
    }
    return false;
}
AiPathBodyMotion ai_path_body_motion(const PlayerRigidVector& previous,
    const PlayerRigidVector& current,std::uint16_t previous_heading,
    std::uint16_t heading,float scheduled_speed,float dt) noexcept {
    AiPathBodyMotion result;
    for(unsigned k=0;k<3;++k)result.velocity[k]=current[k]-previous[k];
    const auto turn=static_cast<std::int16_t>(heading-previous_heading);
    constexpr float radians_per_word=float(6.28318530717958647692/65536);
    const double inverse_dt=1.0/dt;
    result.yaw=float(double(turn)*radians_per_word*inverse_dt);
    if(std::abs(dt)>.0001F) {
        for(auto& component:result.velocity)component=float(component*inverse_dt);
    } else {
        // 5673E0 divides the vector length by the signed requested speed
        // before inverting. The zero-vector fallback also keeps that sign.
        const auto& v=result.velocity;
        const double length=std::sqrt((double(v[0])*v[0]+double(v[1])*v[1])+double(v[2])*v[2]);
        const double ratio=length/scheduled_speed;
        // The original masked x87 produces NaN for zero/zero. Do not publish
        // nonfinite motion to the world for a stationary degenerate spline.
        if(length==0&&scheduled_speed==0)result.velocity={};
        else if(ratio==0)result.velocity={scheduled_speed,0,0};
        else for(auto& component:result.velocity)component=float(component*(1.0/ratio));
    }
    const auto& v=result.velocity;
    result.magnitude=float(std::sqrt((double(v[0])*v[0]+double(v[1])*v[1])+double(v[2])*v[2]));
    return result;
}
} // namespace nfsu
