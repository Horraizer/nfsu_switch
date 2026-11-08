#include "nfsu/frontend_motion.h"
#include <algorithm>
#include <cmath>
namespace nfsu {
float frontend_fixed_sine(std::uint16_t phase) noexcept {
    // Float constants are the original executable's binary32 values. Wider
    // arithmetic preserves its x87 intermediates until the returned float.
    double x = double(phase) * double(9.58738019107841e-5F);
    double sign = 1;
    if(x >= double(4.71238899230957F)) x -= double(6.2831854820251465F);
    else if(x >= double(1.5707963705062866F)) {
        x -= double(3.1415927410125732F); sign = -1;
    }
    const double x2=x*x, x5=x2*x*x2, x7=x5*x2;
    return float((x7*x2*double(2.7557318844628753e-6F) +
        ((x5*double(0.008333333767950535F) +
        (x-x2*x*double(0.1666666716337204F))) -
        x7*double(0.00019841270113829523F))) * sign);
}
namespace {
std::uint16_t browser_phase(double degrees) noexcept {
    // Native CRT truncation precedes the signed integer division by360.
    return static_cast<std::uint16_t>(static_cast<int>(degrees*65536)/360);
}
float browser_angle(float from,float to) noexcept {
    const float a=std::abs(from-to),b=std::abs(from-(to+360)),c=std::abs(from-(to-360));
    if(b<a && b<c)return to+360;
    if(c<a && c<b)return to-360;
    return to;
}
}
void FrontendBrowserCamera::begin(float pitch,float yaw,float scale) noexcept {
    translation_source=translation;target_source=target;
    translation_destination={{0,.75F,0}};target_destination={{0,0,.75F}};
    source=pose;destination={{browser_angle(pose[0],pitch),browser_angle(pose[1],yaw),6,0,45}};
    duration=float(std::max(double(.3F),(std::abs(double(source[4])-destination[4])*2+
        std::abs(double(source[1])-destination[1])+std::abs(double(source[0])-destination[0]))*double(.0055F)));
    duration*=scale;elapsed=0;tracking=true;manual=false;
}
bool FrontendBrowserCamera::focus(unsigned slot) noexcept {
    // 4AEA50 selects the first matching native slot row from 6FB0B0.
    std::array<float,11> preset{{93,115,6,0,45,0,.75F,0,0,0,.75F}};
    switch(slot) {
    case 0:break;
    case 16:preset={{87.23899841308594F,176.68699645996094F,6.0F,0.0F,19.93899917602539F,-3.1080000400543213F,0.7080000042915344F,0.4620000123977661F,0.0F,-0.2150000035762787F,0.5230000019073486F}};break;
    case 39:preset={{92.76100158691406F,159.01800537109375F,6.0F,0.0F,14.355999946594238F,0.0F,0.75F,0.0F,1.656000018119812F,0.4909999966621399F,0.3070000112056732F}};break;
    case 1:preset={{87.50800323486328F,259.02899169921875F,6.0F,0.0F,42.67499923706055F,0.0F,1.0F,0.0F,0.061000000685453415F,-0.36800000071525574F,0.5989999771118164F}};break;
    case 2:preset={{88.86900329589844F,112.09600067138672F,6.0F,0.0F,40.98500061035156F,0.0F,1.0F,0.0F,-0.061000000685453415F,-0.574999988079071F,0.7599999904632568F}};break;
    case 10:preset={{84.18499755859375F,314.5849914550781F,6.0F,0.0F,40.98500061035156F,0.0F,1.0F,0.0F,0.2150000035762787F,0.03099999949336052F,1.0149999856948853F}};break;
    case 11:preset={{96.0739974975586F,19.87700080871582F,6.0F,0.0F,36.55400085449219F,2.4539999961853027F,1.5950000286102295F,-0.3070000112056732F,-0.061000000685453415F,1.718000054359436F,0.4909999966621399F}};break;
    case 6:preset={{96.0739974975586F,301.2919921875F,6.0F,0.0F,51.42900085449219F,0.0F,1.0F,0.0F,1.1039999723434448F,-0.4909999966621399F,0.859000027179718F}};break;
    case 8:preset={{93.0459976196289F,250.33900451660156F,6.0F,0.0F,45.0F,0.0F,1.0F,0.0F,-0.061000000685453415F,-0.6129999756813049F,0.574999988079071F}};break;
    case 7:preset={{98.28199768066406F,242.59100341796875F,6.0F,0.0F,35.01499938964844F,0.0F,1.0F,0.0F,0.0F,0.0F,1.0F}};break;
    case 17:preset={{91.65599822998047F,37.54600143432617F,6.0F,0.0F,15.460000038146973F,0.0F,0.75F,0.0F,3.128999948501587F,0.6129999756813049F,0.3070000112056732F}};break;
    case 12:preset={{103.80400085449219F,289.32501220703125F,6.0F,0.0F,34.3390007019043F,0.0F,1.0F,0.0F,-0.09600000083446503F,-0.16099999845027924F,0.6110000014305115F}};break;
    case 15:preset={{83.0770034790039F,96.29199981689453F,6.0F,0.0F,30.63800048828125F,0.0F,1.0F,0.0F,-0.6460000276565552F,-0.4560000002384186F,0.5849999785423279F}};break;
    case 116:preset={{94.96900177001953F,302.0570068359375F,6.0F,0.0F,49.2400016784668F,0.0F,1.0F,0.0F,1.1039999723434448F,-0.4909999966621399F,0.859000027179718F}};break;
    case 115:preset={{95.86699676513672F,302.9649963378906F,6.0F,0.0F,40.486000061035156F,0.0F,1.0F,0.0F,0.03099999949336052F,0.2770000100135803F,0.5460000038146973F}};break;
    case 13:preset={{88.7969970703125F,283.0169982910156F,6.0F,0.0F,37.202999114990234F,0.0F,1.0F,0.0F,1.0429999828338623F,0.0F,0.7360000014305115F}};break;
    case 14:preset={{86.75599670410156F,111.28399658203125F,6.0F,0.0F,30.0F,0.0F,1.0F,0.0F,1.2000000476837158F,-1.0770000219345093F,1.2619999647140503F}};break;
    case 20:preset={{97.47699737548828F,251.4459991455078F,6.0F,0.0F,30.63800048828125F,0.0F,1.0F,0.0F,0.0F,0.0F,1.0F}};break;
    case 21:preset={{107.44599914550781F,110.76899719238281F,6.0F,0.0F,45.95759963989258F,0.0F,1.0F,0.0F,-1.753999948501587F,-0.03099999949336052F,1.0F}};break;
    case 22:preset={{98.58499908447266F,221.52999877929688F,6.0F,0.0F,24.368999481201172F,0.0F,1.0F,0.0F,0.03099999949336052F,0.09200000017881393F,0.33799999952316284F}};break;
    case 23:preset={{94.15399932861328F,321.2309875488281F,6.0F,0.0F,27.691999435424805F,0.0F,1.0F,0.0F,-0.03099999949336052F,-0.4000000059604645F,0.5230000019073486F}};break;
    case 24:preset={{91.15399932861328F,127.38500213623047F,6.0F,0.0F,36.55400085449219F,0.0F,1.0F,0.0F,-1.4459999799728394F,0.6460000276565552F,0.5460000038146973F}};break;
    case 25:preset={{94.15399932861328F,57.599998474121094F,6.0F,0.0F,36.55400085449219F,0.0F,1.0F,0.0F,-0.4000000059604645F,0.33799999952316284F,0.4000000059604645F}};break;
    case 19:preset={{114.26100158691406F,286.68701171875F,6.0F,0.0F,37.54600143432617F,0.0F,1.0F,0.0F,1.534000039100647F,0.18400000035762787F,0.7979999780654907F}};break;
    case 30:preset={{108.55400085449219F,299.0769958496094F,6.0F,0.0F,49.84600067138672F,0.0F,0.75F,0.0F,0.4620000123977661F,-0.5849999785423279F,0.4620000123977661F}};break;
    case 38:preset={{104.90799713134766F,119.26399993896484F,6.0F,0.0F,34.233001708984375F,0.061000000685453415F,0.9200000166893005F,0.061000000685453415F,-0.7979999780654907F,-0.42899999022483826F,0.5519999861717224F}};break;
    case 41:preset={{85.03099822998047F,219.75399780273438F,6.0F,0.0F,25.39900016784668F,0.061000000685453415F,0.9200000166893005F,0.061000000685453415F,0.6129999756813049F,-0.42899999022483826F,0.5519999861717224F}};break;
    case 40:preset={{82.8219985961914F,72.88300323486328F,6.0F,0.0F,34.233001708984375F,0.061000000685453415F,0.9200000166893005F,0.061000000685453415F,-0.7979999780654907F,0.7360000014305115F,0.5519999861717224F}};break;
    case 37:preset={{111.53399658203125F,258.4049987792969F,6.0F,0.0F,35.3380012512207F,0.0F,0.75F,0.0F,0.859000027179718F,-0.3070000112056732F,0.36800000071525574F}};break;
    case 31:preset={{92.10399627685547F,224.82000732421875F,7.495999813079834F,0.0F,35.0F,0.0F,1.0F,0.0F,0.0F,0.0F,1.0F}};break;
    case 32:preset={{84.0F,51.79999923706055F,7.495999813079834F,0.0F,35.0F,0.0F,1.0F,0.0F,0.0F,0.0F,1.0F}};break;
    case 33:preset={{90.50499725341797F,314.77801513671875F,7.495999813079834F,0.0F,35.0F,0.0F,1.0F,0.0F,0.0F,0.0F,1.0F}};break;
    case 34:preset={{88.09200286865234F,133.8800048828125F,7.495999813079834F,0.0F,35.0F,0.0F,1.0F,0.0F,0.0F,0.0F,1.0F}};break;
    default:return false;
    }
    source=pose;translation_source=translation;target_source=target;
    for(unsigned i=0;i<5;++i)destination[i]=preset[i];
    for(unsigned i:{0U,1U,3U})destination[i]=browser_angle(pose[i],destination[i]);
    for(unsigned i=0;i<3;++i){translation_destination[i]=preset[5+i];target_destination[i]=preset[8+i];}
    duration=float(std::max(double(.3F),(std::abs(double(source[4])-destination[4])*2+
        std::abs(double(source[1])-destination[1])+std::abs(double(source[0])-destination[0]))*double(.0055F)));
    elapsed=0;tracking=true;manual=false;return true;
}
void FrontendBrowserCamera::magazine(const std::array<float,11>& values)noexcept {
    tracking=false;manual=false;pose={{values[1],values[0],values[2],values[7],values[6]}};
    translation={{values[3],values[4],values[5]}};target={{values[8],values[9],values[10]}};
}
void FrontendBrowserCamera::prepare() noexcept {begin(std::max(95.F,pose[0]),55,2);}
void FrontendBrowserCamera::depart() noexcept {begin(115,0,3.2F);}
void FrontendBrowserCamera::arrive() noexcept {begin(93,115,2);elapsed=duration;}
void FrontendBrowserCamera::orbit(float yaw,float pitch) noexcept {
    if(!std::isfinite(yaw)||!std::isfinite(pitch)||(yaw==0&&pitch==0))return;
    tracking=false;manual=true;
    pose[1]=std::fmod(pose[1]+yaw,360.F);if(pose[1]<0)pose[1]+=360.F;
    pose[0]=std::clamp(pose[0]+pitch,85.F,140.F);pose[2]=6;
}
void FrontendBrowserCamera::update(float seconds) noexcept {
    if(manual) {
        if(!std::isfinite(seconds)||seconds<=0)return;
        // Original487F50 synchronizes all four return times. Angular rates
        // are75/50 degrees/s; each vector returns at2.2 units/s. The slowest
        // channel sets their shared fraction. Small vectors use native.001.
        const auto neutral_translation=std::array<float,3>{{0,.75F,0}};
        const auto neutral_target=std::array<float,3>{{0,0,.75F}};
        const float roll=browser_angle(pose[3],0);
        const auto vector_time=[&](const auto& value,const auto& neutral) {
            if(std::abs(value[0]-neutral[0])<.001F&&
               std::abs(value[1]-neutral[1])<.001F&&std::abs(value[2]-neutral[2])<.001F)return 0.F;
            double square=0;for(unsigned i=0;i<3;++i)square+=(double(value[i])-neutral[i])*(double(value[i])-neutral[i]);
            return float(std::sqrt(square)/float(seconds*2.2F));
        };
        const float translation_time=vector_time(translation,neutral_translation);
        const float target_time=vector_time(target,neutral_target);
        const float time=std::max({float(std::abs(double(pose[3])-roll)/(double(seconds)*75)),
            float(std::abs(double(pose[4])-45)/(double(seconds)*50)),
            translation_time,target_time});
        if(time==0)return;
        const double fraction=std::min(1.,double(1.F/time));
        pose[3]=float(pose[3]+(double(roll)-pose[3])*fraction);
        pose[4]=float(pose[4]+(45.-pose[4])*fraction);
        for(unsigned i=0;i<3;++i) {
            if(translation_time>0)
                translation[i]=float(translation[i]+(double(neutral_translation[i])-translation[i])*fraction);
            if(target_time>0)
                target[i]=float(target[i]+(double(neutral_target[i])-target[i])*fraction);
        }
        return;
    }
    elapsed+=seconds;
    const float fraction=duration>0 && elapsed<duration?elapsed/duration:1;
    const double blend=(double(frontend_fixed_sine(browser_phase(double(fraction)*180-90)))+1)*.5;
    for(unsigned i=0;i<pose.size();++i)pose[i]=float((1-blend)*source[i]+blend*destination[i]);
    for(unsigned i=0;i<3;++i) {
        translation[i]=float((1-blend)*translation_source[i]+blend*translation_destination[i]);
        target[i]=float((1-blend)*target_source[i]+blend*target_destination[i]);
    }
    for(unsigned i:{1U,3U}) {if(pose[i]>360)pose[i]-=360;if(pose[i]<0)pose[i]+=360;}
}
CameraFrame FrontendBrowserCamera::frame(float travel,unsigned width,unsigned height) const noexcept {
    // 487680: Rz(yaw)*Rx(pitch), translation(0,.75,distance),
    // inverse eye, then look-at the car's raw(travel,0,.75) target.
    const auto sine=[](double degrees){return double(frontend_fixed_sine(browser_phase(degrees)));};
    const double sp=sine(pose[0]),cp=sine(pose[0]+90.),sy=sine(pose[1]),cy=sine(pose[1]+90.);
    // The native sine polynomial is not exactly orthonormal; invert its
    // two rotation blocks rather than treating transpose as inverse.
    const double pitch_norm=sp*sp+cp*cp,yaw_norm=sy*sy+cy*cy;
    const double y=(-double(translation[1])*cp-(double(pose[2])+translation[2])*sp)/pitch_norm;
    const double raw_x=(-double(translation[0])*cy+y*sy)/yaw_norm,raw_y=(double(translation[0])*sy+y*cy)/yaw_norm;
    const double raw_z=(double(translation[1])*sp-(double(pose[2])+translation[2])*cp)/pitch_norm;
    CameraFrame out;out.eye={raw_x,raw_z,-raw_y};
    const Vec3 aim{double(target[0])+(tracking?double(travel):0),target[2],-double(target[1])};
    const auto unit=[](Vec3 v){const double n=std::sqrt(v.x*v.x+v.y*v.y+v.z*v.z);return Vec3{v.x/n,v.y/n,v.z/n};};
    out.forward=unit({aim.x-out.eye.x,aim.y-out.eye.y,aim.z-out.eye.z});
    out.right=unit({-out.forward.z,0,out.forward.x});
    out.up={out.right.y*out.forward.z-out.right.z*out.forward.y,
        out.right.z*out.forward.x-out.right.x*out.forward.z,
        out.right.x*out.forward.y-out.right.y*out.forward.x};
    if(pose[3]!=0){
        const double sr=sine(pose[3]),cr=sine(pose[3]+90.);
        out.right=unit({cr*out.right.x+sr*out.up.x,cr*out.right.y+sr*out.up.y,cr*out.right.z+sr*out.up.z});
        out.up={out.right.y*out.forward.z-out.right.z*out.forward.y,
            out.right.z*out.forward.x-out.right.x*out.forward.z,out.right.x*out.forward.y-out.right.y*out.forward.x};
    }
    const unsigned fov=browser_phase(pose[4]);
    const auto cot=[](double phase){const auto half=static_cast<std::uint16_t>(phase);
        return double(frontend_fixed_sine(static_cast<std::uint16_t>(half+0x4000)))/frontend_fixed_sine(half);};
    out.focal_x=cot(fov*double(1.1F)*.5);
    out.focal_y=cot(fov*double(height)/width*double(1.1F)/double(.9090908765792847F)*.5);
    out.near_plane=.05;out.far_plane=300;return out;
}
std::uint16_t frontend_wheel_phase(std::uint16_t phase,float travel,float radius) noexcept {
    if(!std::isfinite(travel) || !std::isfinite(radius) || radius<=0) return phase;
    const double increment=double(travel)/(double(radius)*double(6.283180236816406F))*double(65535.9453125F);
    if(increment < -2147483648.0 || increment >= 2147483648.0) return phase;
    // Native CRT conversion truncates before the 16-bit accumulator wraps.
    return static_cast<std::uint16_t>(unsigned(phase)+static_cast<std::uint32_t>(static_cast<std::int32_t>(increment)));
}
void FrontendRoad::update(float seconds,std::uint32_t ticks,bool forced) noexcept {
    if(!std::isfinite(seconds) || seconds<0) return;
    // 0x4AE1F0 uses the previous speed for this frame's velocity.
    float kmh=speed_kmh;
    if(speed_kmh<target_kmh) speed_kmh=std::min(target_kmh,speed_kmh+seconds*10);
    else if(speed_kmh>target_kmh) speed_kmh=std::max(target_kmh,speed_kmh-seconds*10);
    if(forced) kmh=50;
    else {
        const double time=double(ticks)*double(0.00025F);
        const auto phase=[](double value) {
            return static_cast<std::uint16_t>(static_cast<std::int64_t>(value));
        };
        const float wobble = float(2.0 * frontend_fixed_sine(phase((time*.125+51)*double(10430.3779296875F))) +
            3.0 * frontend_fixed_sine(phase((time*double(0.2F)+117)*double(10430.3779296875F))));
        const float attenuation=kmh<5?0:kmh<20?(kmh-5)/15:1;
        kmh += wobble*attenuation;
    }
    const double exact_velocity=double(kmh)*1000*double(0.00027777778450399637F);
    velocity=float(exact_velocity);
    const double exact_delta=double(seconds)*exact_velocity;
    delta=float(exact_delta); offset=float(double(offset)+exact_delta);
    while(offset>200) offset-=200; // The native comparison is strictly >.
}
unsigned FrontendRoad::placements(unsigned view,std::array<FrontendRoadPlacement,5>& out) const noexcept {
    unsigned extent=0,family=0;
    if(view>=1 && view<=3) extent=2;
    else if(view>=4 && view<=5) {extent=1;family=1;}
    else if(view>=10 && view<=15) {extent=1;family=2;}
    else return 0;
    const int segment=int(double(offset)/200*2);
    unsigned group=unsigned((segment-int(extent))&1);
    float start=float(double(offset)-double(segment)*100-double(extent)*100);
    for(unsigned i=0;i<extent*2+1;++i) {
        out[i]={group,family,-start,0.291F};group^=1;start+=100;
    }
    return extent*2+1;
}
unsigned frontend_road_lights(const FrontendRoad& road,const float (&markers)[4][3],
    float lateral,const float (&rgb)[3],const float (&attenuation)[3],
    std::array<FrontendPointLight,32>& out) noexcept {
    out={};std::array<FrontendRoadPlacement,5> placements;
    const unsigned count=road.placements(1,placements);unsigned used=0;
    for(unsigned copy=0;copy<count;++copy)for(unsigned marker=0;marker<4;++marker) {
        auto& light=out[used++];
        light.position[0]=markers[marker][0]+placements[copy].longitudinal;
        light.position[1]=markers[marker][1]+lateral;
        light.position[2]=markers[marker][2]+placements[copy].vertical;
        for(unsigned c=0;c<3;++c){light.rgb[c]=rgb[c];light.attenuation[c]=attenuation[c];}
    }
    return used;
}

}
