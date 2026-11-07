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
    if (session.start_cinematic() || (session.is_drift() && !session.timing().go_valid) || !atlas.map.valid) return true;
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
    // Explicit port roster. No inferred retail live gate writers.
    player.raw_gates_valid=true;player.gate_4d0=player.gate_8a9=1;
    player.color={{172,53,0}};player.kind=1;player.texture_hash=0x7E22DED8;
    player.size_x=player.size_y=float(0.09589000046253204);
    std::array<MinimapActor,6> actors{};std::size_t actor_count=0;
    const auto& course=session.course();
    //58E8C8..58E9D2: circuit draws one START_LINE; point-to-point adds
    // the separate finish. Native start/finish angles already describe art
    // orientation; convert to the actor-heading convention of the kernel.
    const auto line=[&](int boundary,std::uint16_t orientation,std::int16_t id) {
        if(boundary<0 || std::size_t(boundary)>=course.route.boundaries.size())return;
        const int index=course.route.boundaries[std::size_t(boundary)].marker;
        if(index<0 || std::size_t(index)>=course.track.markers.size())return;
        const auto& marker=course.track.markers[std::size_t(index)];
        auto& a=actors[actor_count++];a=player;a.id=id;a.kind=13;
        a.raw_xy={{marker.position[0],marker.position[1]}};
        a.heading=std::uint16_t(0x4000-orientation);
        a.texture_hash=0x72974A14;a.color={{132,169,235}}; //7313E6
        a.size_x=0.024999000132083893F;a.size_y=0.1349949985742569F;
    };
    line(course.route.start,course.route.reverse && course.route.point_to_point?atlas.map.finish_angle:atlas.map.start_angle,-1);
    if(course.route.point_to_point)
        line(course.route.finish,course.route.reverse?atlas.map.start_angle:atlas.map.finish_angle,-2);
    for(std::size_t i=0;i<session.rival_count();++i) {
        const auto& s=session.rival(i).car();const auto& r=s.orientation;
        auto& actor=actors[actor_count++];actor=player;actor.id=unsigned(i+1);
        //435746..435756 passes hide=true for eliminated non-human actors;
        //43535D clears8A9, which58EA00 tests before drawing their map marker.
        //Keep the result row for standings; this gate hides only the marker.
        actor.gate_8a9=session.timing().racers.at(i+1).status==RacerStatus::knocked_out?0:1;
        actor.raw_xy={{float(s.position.x),float(s.position.y)}};
        actor.heading=chase_angle(1-2*(r.y*r.y+r.z*r.z),2*(r.x*r.y+r.w*r.z));
        // Original 58EA88: rival palette7313DD and marker kind5.
        actor.color={{0,66,255}};actor.kind=5;
    }
    //58E810 submits the single human after remote racers: overlapping
    //markers must keep the player on top, independent of roster order.
    actors[actor_count++]=player;
    MinimapGeometry geometry;
    if (build_minimap_geometry(atlas.map,style,presentation,player.raw_xy,actors.data(),
        actor_count,geometry)!=MinimapResult::ok)
        return false;
    // Native clips to the HUD window, not to rotating texture coordinates.
    // The isolated atlas sampler wraps the route artwork without changing it.
    if (frame.quads+geometry.count>kRaceHudMaxQuads) return false;
    if (frame.lap_first_quad==kRaceHudMaxQuads) frame.lap_first_quad=frame.quads;
    for (std::size_t i=0;i<geometry.count;++i) {
        const auto& piece=geometry.pieces[i];
        const bool line=piece.texture_hash==0x72974A14;
        const int tx=piece.route?513:line?820:780,ty=1,tw=piece.route?256:line?16:32;
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
