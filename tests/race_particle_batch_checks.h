#pragma once
#include "nfsu/race_gpu_scene.h"
#include <cmath>
#include <stdexcept>
namespace nfsu::test {
struct ParticleBatchWitness {std::size_t alpha=0,sparks=0;bool large=false;std::array<bool,4> owners{};};
inline ParticleBatchWitness check_particle_batches(const RecordedCommands& commands,const RaceParticles& particles) {
    const auto check=[](bool ok,const char* why){if(!ok)throw std::runtime_error(why);};
    CameraFrame camera;
    const auto& f=commands.frame;
    camera.eye={f.eye[0],f.eye[1],f.eye[2]};camera.right={f.right[0],f.right[1],f.right[2]};
    camera.up={f.up[0],f.up[1],f.up[2]};camera.forward={f.forward[0],f.forward[1],f.forward[2]};
    camera.focal_x=f.projection[0];camera.focal_y=f.projection[1];
    camera.near_plane=f.projection[2];camera.far_plane=f.projection[3];
    ParticleBatchWitness result;unsigned found=0;
    std::size_t vertex_resource=kNoHandle,index_resource=kNoHandle,texture_resource=kNoHandle;
    for(std::size_t j=0;j<commands.draw_count();++j) {
        const auto view=commands.draw_view(j);const auto& d=view.definition;
        const bool alpha=d.name.find("race-car/particle/smoke/")==0;
        const bool spark=d.name.find("race-car/particle/spark/")==0;
        if(!alpha&&!spark)continue;
        ++found;
        check(view.material.fallback[3]==9 && !d.pipeline.depth_write,"particle batch lost native color/depth state");
        const auto& resource=commands.resource_table().resources.at(d.vertex_resource);
        check(resource.streamed_vertices && resource.streamed_elements && resource.elements==4*kRaceWaterPool,
              "vehicle particle vertices must use one bounded live stream");
        check(*resource.streamed_elements<=resource.elements,"particle upload exceeds its allocation");
        const auto* v=reinterpret_cast<const MeshVertex*>(resource.data);
        if(vertex_resource==kNoHandle){vertex_resource=d.vertex_resource;index_resource=d.index_resource;texture_resource=d.texture_resource;}
        check(vertex_resource==d.vertex_resource && index_resource==d.index_resource && texture_resource==d.texture_resource,
              "alpha and additive vehicle batches must share immutable resources and one vertex stream");
        check(d.first_index+d.index_count<=commands.resource_table().resources.at(d.index_resource).elements,
              "empty or full particle batch exceeds the shared index buffer");
        const auto& sheet=commands.resource_table().resources.at(d.texture_resource);
        check(sheet.texture.width==512 && sheet.texture.height==512,"vehicle particle atlas sheet dimensions");
        std::size_t visible=0;
        for(std::size_t i=0;i<(spark?particles.spark_count():particles.smoke_count());++i) {
            const auto& p=spark?particles.spark(i):particles.smoke(i);
            VehicleVec3 at;double diameter=0,opacity=0;std::array<unsigned,3> tint{};
            if(!particles.visual(p,at,diameter,opacity,tint)||opacity<=0)continue;
            const auto clip=project_point(camera,{at.x,at.z,-at.y});
            if(clip.w<=camera.near_plane || clip.w>camera.far_plane ||
               std::abs(clip.x)>clip.w+.5*diameter*camera.focal_x ||
               std::abs(clip.y)>clip.w+.5*diameter*camera.focal_y)continue;
            const auto first=4*(d.first_index/6+visible++);
            check(first+4<=*resource.streamed_elements,"live batch indices reference unavailable vertices");
            VehicleVec3 centre{};
            for(unsigned k=0;k<4;++k){centre.x+=v[first+k].px*.25;centre.y+=v[first+k].pz*.25;centre.z+=v[first+k].py*.25;
                check(v[first+k].color[0]==tint[0] && v[first+k].color[1]==tint[1] && v[first+k].color[2]==tint[2] &&
                      v[first+k].color[3]==unsigned(std::lround(opacity*255)),"batch changed authored per-particle RGBA");}
            check(std::hypot(centre.x-at.x,centre.y-at.y,centre.z-at.z)<.0006,"particle batch centre differs from CPU position");
            const auto& a=v[first];const auto& b=v[first+1];
            check(std::abs(std::hypot(a.px-b.px,a.py-b.py,a.pz-b.pz)-diameter)<.0008,
                  "particle batch clipped the authored diameter");
            result.large|=diameter*camera.focal_y*720/(2*clip.w)>48;
            if(p.water_owner<result.owners.size())result.owners[p.water_owner]=true;
        }
        check((view.material.alpha[0]>0)==(visible>0),"empty/live batch visibility mismatch");
        check(d.index_count==6*std::max<std::size_t>(visible,1),"vehicle draw count is not its complete live quad range");
        (spark?result.sparks:result.alpha)=visible;
    }
    check(found==kRaceVehicleParticleDraws,"vehicle particles require exactly two batch commands");
    return result;
}
} // namespace nfsu::test
