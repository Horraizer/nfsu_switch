#include "nfsu/race_hud.h"
#include <algorithm>
#include <cmath>

namespace nfsu {
namespace {
// Native 0x005919D0 Hermite component update, with its float stores.
void filter(float target, float dt, float& value, float& velocity) noexcept {
    const double delta=double(target)-value;
    const float a=float(double(velocity)-delta-delta);
    const float b=float(delta*3-velocity-velocity);
    const float next=float(((double(a)*dt+b)*dt+velocity)*dt+value);
    velocity=float((double(a)*3*dt+double(b)*2)*dt+velocity);
    value=next;
}
float plane(const MinimapVertex& v,int edge) {
    return edge==0?v.uvq[0]:edge==1?v.uvq[2]-v.uvq[0]:
           edge==2?v.uvq[1]:v.uvq[2]-v.uvq[1];
}
MinimapVertex mix(const MinimapVertex& a,const MinimapVertex& b,float t) {
    MinimapVertex r;
    for (int k=0;k<2;++k) r.screen[k]=a.screen[k]+(b.screen[k]-a.screen[k])*t;
    for (int k=0;k<3;++k) r.uvq[k]=a.uvq[k]+(b.uvq[k]-a.uvq[k])*t;
    for (int k=0;k<4;++k) r.color[k]=static_cast<unsigned char>(
        a.color[k]+(int(b.color[k])-a.color[k])*t);
    return r;
}
// Clip homogeneous route UVs to the source artwork, so a cropped map cannot
// sample adjacent gauge/font texels in the shared atlas. Outside is transparent.
std::size_t crop(const MinimapPiece& piece,std::array<MinimapVertex,12>& points) {
    std::copy(piece.vertices.begin(),piece.vertices.end(),points.begin());
    std::size_t count=4;
    for (int edge=0;edge<4 && count;++edge) {
        std::array<MinimapVertex,12> next{};std::size_t n=0;
        auto previous=points[count-1];float pv=plane(previous,edge);
        for (std::size_t i=0;i<count;++i) {
            const auto current=points[i];const float cv=plane(current,edge);
            if ((pv>=0)!=(cv>=0)) next[n++]=mix(previous,current,pv/(pv-cv));
            if (cv>=0) next[n++]=current;
            previous=current;pv=cv;
        }
        points=next;count=n;
    }
    return count;
}
}

void update_race_minimap_direction(MapPoint target,std::int32_t ticks,
                                   std::uint32_t resets,RaceMinimapState& state) noexcept {
    if (!state.initialized || resets!=state.resets || ticks<state.world_ticks) {
        state={};state.initialized=true;state.direction=target;
    } else if (ticks!=state.world_ticks) {
        const float dt=float(double(std::int64_t(ticks)-state.world_ticks)*
                             float(0.8999999761581421)*float(0.0002500000118743628));
        for (int k=0;k<2;++k) filter(target[k],dt,state.direction[k],state.velocity[k]);
    }
    state.world_ticks=ticks;state.resets=resets;
}

bool append_race_minimap(const RaceSession& session,const RaceHudAtlas& atlas,
                         unsigned width,unsigned height,RaceMinimapState& state,
                         RaceHudFrame& frame) noexcept {
    if (!atlas.map.valid) return true;
    if (!width || !height) return false;
    const auto& camera=session.camera();
    const MapPoint target{{float(camera.forward.x),float(camera.forward.y)}};
    update_race_minimap_direction(target,session.timing().step_ticks,
                                  session.telemetry().resets,state);
    const auto angle=std::uint16_t(chase_angle(state.direction[1],state.direction[0])+0x8000);
    // Original perspective/rotating style1, from 0x005912B0. Final scale
    // follows the current HUD's uniform 640x480 FEng canvas adapter.
    MinimapStyle style;style.index=1;style.tilt=54650;style.distance=200;
    style.offset={{0,-20}};style.extent_add={{37,75}};
    style.uv_extent={{float(0.5640140175819397),float(0.2837370038032532)}};
    style.route_alpha=float(0.20000000298023224);
    style.centered=style.heading_aware=style.enabled=true;
    const float scale=std::min(width/1280.0F,height/720.0F)*5/3;
    MinimapPresentation presentation;presentation.valid=presentation.camera_angle_valid=true;
    presentation.native_center={{86,240}};presentation.native_half_extent={{64,64}};
    presentation.screen_transform={{scale,0,0,0,scale,0}};presentation.map_angle=angle;
    const auto& car=session.car();const auto& q=car.orientation;
    MinimapActor player;player.id=0;player.raw_xy={{float(car.position.x),float(car.position.y)}};
    player.heading=chase_angle(1-2*(q.y*q.y+q.z*q.z),2*(q.x*q.y+q.w*q.z));
    // Explicit single driven port actor. No inferred retail roster/gate writers.
    player.raw_gates_valid=true;player.gate_4d0=player.gate_8a9=1;
    player.color={{172,53,0}};player.kind=1;player.texture_hash=0x7E22DED8;
    player.size_x=player.size_y=float(0.09589000046253204);
    MinimapGeometry geometry;
    if (build_minimap_geometry(atlas.map,style,presentation,player.raw_xy,&player,1,geometry)!=MinimapResult::ok)
        return false;
    // Prepare bounded pieces before publishing any vertices to the existing HUD.
    std::array<MinimapPiece,kMinimapMaxPieces> pieces{};std::size_t count=0;
    for (std::size_t i=0;i<geometry.count;++i) {
        const auto& piece=geometry.pieces[i];
        if (!piece.route) {if(count==pieces.size())return false;pieces[count++]=piece;continue;}
        std::array<MinimapVertex,12> points{};const auto n=crop(piece,points);
        for (std::size_t j=1;j+1<n;j+=2) {
            if(count==pieces.size())return false;
            pieces[count]=piece;
            pieces[count++].vertices={{points[0],points[j],points[j+1],points[std::min(j+2,n-1)]}};
        }
    }
    if (frame.quads+count>kRaceHudMaxQuads) return false;
    if (frame.lap_first_quad==kRaceHudMaxQuads) frame.lap_first_quad=frame.quads;
    for (std::size_t i=0;i<count;++i) {
        const auto& piece=pieces[i];
        const int tx=piece.route?513:780,ty=1,tw=piece.route?256:32;
        for (std::size_t j=0;j<4;++j) {
            const auto& in=piece.vertices[j];auto& out=frame.vertices[frame.quads*4+j];
            const float u=in.uvq[0]/in.uvq[2],v=in.uvq[1]/in.uvq[2];
            out.px=in.screen[0];out.py=in.screen[1];
            out.pz=piece.route?200*in.uvq[2]:1;
            out.u=(tx+.5F+u*(tw-1))/kRaceHudAtlasWidth;
            out.v=(ty+.5F+v*(tw-1))/kRaceHudAtlasSize;
            // Explicit full-byte modulation for the existing final HUD pass.
            for (int k=0;k<4;++k) out.color[k]=static_cast<unsigned char>(std::min(255,int(in.color[k])*2));
        }
        ++frame.quads;
    }
    return true;
}
} // namespace nfsu
