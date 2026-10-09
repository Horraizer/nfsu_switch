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
    // Native clips to the HUD window, not to rotating texture coordinates.
    // The isolated atlas sampler wraps the route artwork without changing it.
    if (frame.quads+geometry.count>kRaceHudMaxQuads) return false;
    if (frame.lap_first_quad==kRaceHudMaxQuads) frame.lap_first_quad=frame.quads;
    for (std::size_t i=0;i<geometry.count;++i) {
        const auto& piece=geometry.pieces[i];
        const int tx=piece.route?513:780,ty=1,tw=piece.route?256:32;
        for (std::size_t j=0;j<4;++j) {
            const auto& in=piece.vertices[j];auto& out=frame.vertices[frame.quads*4+j];
            const float u=in.uvq[0]/in.uvq[2],v=in.uvq[1]/in.uvq[2];
            out.px=in.screen[0];out.py=in.screen[1];
            out.pz=piece.route?200*in.uvq[2]:0;
            out.u=(piece.route?tx+u*tw:tx+.5F+u*(tw-1))/kRaceHudAtlasWidth;
            out.v=(piece.route?ty+v*tw:ty+.5F+v*(tw-1))/kRaceHudAtlasSize;
            // Explicit full-byte modulation for the existing final HUD pass.
            for (int k=0;k<4;++k) out.color[k]=static_cast<unsigned char>(std::min(255,int(in.color[k])*2));
        }
        ++frame.quads;
    }
    return true;
}
} // namespace nfsu
