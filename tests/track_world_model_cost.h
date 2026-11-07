// Old static payload pins exclude the newly resident authored world models.
#pragma once
#include "nfsu/track_slice.h"
#include <algorithm>
struct ExplicitWorldModelCost {std::uint64_t textures=0,total=0;};
inline ExplicitWorldModelCost explicit_world_model_cost(const nfsu::TrackSlice& track) {
    // This original unbounded baseline already retained every library solid.
    if(track.track_number==1003&&track.backing_set==nfsu::TrackStreamSet::permanent)return {};
    const auto& scene=track.scene;const auto& region=scene.region;
    std::vector<bool> world(region.meshes.size(),false),used(region.textures.size(),false),other(used);
    ExplicitWorldModelCost cost;
    for(std::size_t m=0;m<region.meshes.size();++m) {
        world[m]=std::find(track.world_models.begin(),track.world_models.end(),region.meshes[m].mesh_hash)!=track.world_models.end()
            &&scene.first_instance_of_mesh[m]==nfsu::kNoHandle;
        if(world[m])cost.total+=region.meshes[m].vertices.size()*sizeof(nfsu::TrackVertex)+region.meshes[m].triangles.size()*6;
    }
    for(const auto& draw:region.draws)if(draw.binding==nfsu::BindingKind::resolved) {
        if(world[draw.mesh])used[draw.texture]=true;else other[draw.texture]=true;
    }
    for(std::size_t t=0;t<region.textures.size();++t)if(used[t]&&!other[t])cost.textures+=region.textures[t].payload().size();
    cost.total+=cost.textures;return cost;
}
