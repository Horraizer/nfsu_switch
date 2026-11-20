#pragma once

#include "deko_renderer.h"
#include "nfsu/career.h"
#include "nfsu/lan_scene.h"
#include "nfsu/platform.h"
#include <cerrno>
#include <cstdio>
#include <fcntl.h>
#include <unistd.h>

namespace nfsu {

class SplitDiagnostics final : public TimingService {
public:
    ~SplitDiagnostics() {
        if(file_)std::fclose(file_);
    }
    std::uint64_t now_us() override { return armTicksToNs(armGetSystemTick())/1000; }

    bool open(const FreeDriveCourse& course,const LanRaceOptions& options,unsigned traffic,
              SplitLayout layout,SplitEffects effects,unsigned height,const DekoRenderer& renderer,
              std::uint64_t audio,std::string& error) {
        char path[96]{};
        for(unsigned index=0;index<10000;++index) {
            std::snprintf(path,sizeof path,"sdmc:/nfsu-split-profile-%04u.csv",index);
            const int fd=::open(path,O_WRONLY|O_CREAT|O_EXCL,0666);
            if(fd>=0) {
                file_=fdopen(fd,"w");
                if(!file_)::close(fd);
                break;
            }
            if(errno!=EEXIST)break;
        }
        if(!file_){error="Cannot create the split diagnostics CSV on SD";return false;}
        std::setvbuf(file_,buffer_.data(),_IOFBF,buffer_.size());
        std::printf("SPLIT_PROFILE %s\n",path);std::fflush(stdout);
        std::fprintf(file_,"# build=%s,track=%u,direction=%u,event=%u,round=%u,laps=%u,tier=%u,traffic=%u,layout=%s,particles=%u,reflections=%u,height=%u,gpu_bytes=%llu,audio_bytes=%llu\n",
            NFSU_RACE_BUILD_TAG,unsigned(course.track),unsigned(course.direction),course.event,course.round,
            options.laps,options.tier,traffic,layout==SplitLayout::side_by_side?"left-right":"top-bottom",
            unsigned(effects.particles),unsigned(effects.reflections),height,
            static_cast<unsigned long long>(renderer.gpu_bytes()),static_cast<unsigned long long>(audio));
        std::fputs("frame,step,height,ticks,interval_ms,input_ms,simulation_ms,particles_ms,audio_ms,planner_ms,drivers_ms,p1_physics_ms,p2_physics_ms,contacts_ms,traffic_ms,props_ms,prop_contacts_ms,contact_candidates_ms,contact_queries_ms,contact_response_ms,contact_requeries_ms,tail_ms,acquire_ms,fence_ms,queue_ms,gpu_frame,gpfifo,gpu_bytes,p1_distance,p2_distance",file_);
        constexpr const char* names[]={"track_draws","car_draws","particles_reserved","glows_reserved","triangles","reflection_draws","hud_quads",
            "scene_ms","cull_ms","list_ms","car_ms","reflections_cpu_ms","visuals_cpu_ms","validate_ms","uniform_ms",
            "record_ms","submit_ms","gpu_ms","reflections_gpu_ms","world_gpu_ms","dynamic_gpu_ms","upscale_gpu_ms","hud_gpu_ms"};
        for(unsigned player=1;player<=2;++player)for(const auto* name:names)std::fprintf(file_,",p%u_%s",player,name);
        std::fputc('\n',file_);
        if(std::fflush(file_)==0)return true;
        error="Cannot write the split diagnostics header";return false;
    }
    void begin(double interval) {
        interval_ms=interval;ticks=0;input_ms=simulation_ms=particle_ms=audio_ms=0;
        simulation={};
    }
    void add_simulation(const RaceSimulationProfile& step) {
        ++ticks;
        simulation.planner+=step.planner;simulation.drivers+=step.drivers;
        simulation.player+=step.player;simulation.rivals+=step.rivals;
        simulation.contacts+=step.contacts;simulation.traffic_motion+=step.traffic_motion;
        simulation.prop_motion+=step.prop_motion;simulation.prop_contacts+=step.prop_contacts;
        simulation.contact_candidates+=step.contact_candidates;simulation.contact_queries+=step.contact_queries;
        simulation.contact_response+=step.contact_response;simulation.contact_requeries+=step.contact_requeries;
        simulation.tail+=step.tail;
    }
    void view(unsigned player,const RaceGpuFrame& frame,const RaceHudFrame& hud) {
        views[player]={frame.track_draws,frame.car_draws,frame.particle_draws,frame.glow_draws,
            frame.triangles,frame.commands.reflection_draw_count(),hud.quads};
    }
    bool write(const RaceSession& world,unsigned height,const DekoSplitTiming& timing,const DekoRenderer& renderer) {
        const bool has_gpu=timing.views[0].gpu_ms>=0&&timing.views[1].gpu_ms>=0;
        std::fprintf(file_,"%llu,%llu,%u,%u",static_cast<unsigned long long>(++ordinal),
                     static_cast<unsigned long long>(world.timing().step),height,ticks);
        for(double ms:{interval_ms,input_ms,simulation_ms,particle_ms,audio_ms,simulation.planner,simulation.drivers,
            simulation.player,simulation.rivals,simulation.contacts,simulation.traffic_motion,simulation.prop_motion,
            simulation.prop_contacts,simulation.contact_candidates,simulation.contact_queries,simulation.contact_response,
            simulation.contact_requeries,simulation.tail,timing.acquire_ms,timing.fence_ms,timing.queue_ms})
            std::fprintf(file_,",%.4f",ms);
        std::fprintf(file_,",%llu,%u,%llu,%.3f,%.3f",static_cast<unsigned long long>(has_gpu?ordinal-1:0),
            renderer.gpfifo_usage().last_entries,static_cast<unsigned long long>(renderer.gpu_bytes()),
            world.progress().coordinate.distance,world.rival(0).progress().coordinate.distance);
        for(unsigned p=0;p<2;++p) {
            for(auto value:views[p])std::fprintf(file_,",%llu",static_cast<unsigned long long>(value));
            const auto& cpu=cpu_view[p];const auto& present=timing.views[p];
            for(double ms:{present.source_ms,cpu.cull_ms,cpu.list_ms,cpu.car_ms,cpu.road_reflections_ms,cpu.race_visuals_ms,
                present.validate_ms,present.uniforms_ms,present.record_ms,present.submit_ms,present.gpu_ms,
                present.reflections_gpu_ms,present.world_gpu_ms,present.dynamic_gpu_ms,present.upscale_gpu_ms,present.hud_gpu_ms})
                std::fprintf(file_,",%.4f",ms);
        }
        std::fputc('\n',file_);
        if(ordinal%120==0)return std::fflush(file_)==0;
        return !std::ferror(file_);
    }
    void suspend() { if(file_)std::fflush(file_); }

    std::array<RaceFrameProfile,2> cpu_view{};
    double input_ms=0,simulation_ms=0,particle_ms=0,audio_ms=0;
private:
    std::FILE* file_=nullptr;
    std::array<char,65536> buffer_{};
    std::array<std::array<std::uint64_t,7>,2> views{};
    RaceSimulationProfile simulation{};
    std::uint64_t ordinal=0;
    unsigned ticks=0;
    double interval_ms=0;
};

} // namespace nfsu
