// Original4400E0 signed XY edge test and440A40 cached road-height selection.
// Included inside namespace nfsu; collision decode/index remain the port adapter.
namespace {
float shadow_edge_test(const TrackCollisionPolygon& p,float x,float y) {
    float result=-1.F;
    const unsigned edges[4][2]={{0,1},{1,2},{3,0},{2,3}};
    const bool triangle=p.corner[2][0]==p.corner[3][0] &&
        p.corner[2][1]==p.corner[3][1] && p.corner[2][2]==p.corner[3][2];
    for(unsigned i=0;i<(triangle?3U:4U);++i) {
        const auto& a=p.corner[edges[i][0]];const auto& b=p.corner[edges[i][1]];
        const float d=(b[1]-a[1])*(x-a[0])-(y-a[1])*(b[0]-a[0]);
        result=std::max(result,d);
        if(result>0.F)break;
    }
    return result;
}
bool shadow_publish_ground(const TrackContactWorld& world,std::size_t index,
                           float x,float y,TrackContact& out) {
    const auto plane=vehicle_track_ground_plane(world,index);
    if(plane[2]==0.F)return false;
    const float z=-((plane[1]*y+plane[0]*x-plane[3])/plane[2]);
    if(!std::isfinite(z))return false;
    out={};out.polygon=index;out.point[0]=x;out.point[1]=y;out.point[2]=z;
    std::copy_n(plane.begin(),3,out.normal);out.up=std::abs(plane[2]);return true;
}
bool shadow_refresh_ground(const TrackContactWorld& world,float x,float y,
                           TrackGroundCache& cache) {
    TrackGroundCache next;next.world=&world;
    //440A40 tests the radius-8 query bounds.4407B0 admits polygons from
    //the separately expanded radius-8.5 collection bounds.
    next.bounds={x-8.F,y-8.F,x+8.F,y+8.F};
    const std::array<float,4> collection={x-8.5F,y-8.5F,x+8.5F,y+8.5F};
    // The native 17-unit square is narrower than one 32-unit leaf.
    // It can overlap at most two leaves per axis, including edge touches.
    static_assert((2*kCollisionRootHalfExtent)/(1U<<kCollisionTreeDepth)>17.F,
                  "shadow cell bound depends on native leaf width");
    std::array<std::size_t,4> cells{};std::size_t cell_count=0;
    if(!query_track_collision_cells(world.collision,collection[0],collection[1],
        collection[2],collection[3],cells.data(),cells.size(),cell_count))return false;
    const auto less=[&](std::size_t a,std::size_t b) {
        const auto& aa=world.collision.cells[a];const auto& bb=world.collision.cells[b];
        return aa.center[1]!=bb.center[1]?aa.center[1]<bb.center[1]:aa.center[0]<bb.center[0];
    };
    // Stable insertion sort keeps the original equal-key visitation order.
    for(std::size_t i=1;i<cell_count;++i) {
        const auto value=cells[i];auto at=i;
        while(at&&less(value,cells[at-1])){cells[at]=cells[at-1];--at;}
        cells[at]=value;
    }
    std::array<std::size_t,160> admitted{};std::size_t count=0;
    for(std::size_t c=0;c<cell_count;++c) {
        const auto id=cells[c];
        const auto& cell=world.collision.cells[id];
        if(std::uint64_t(cell.first_index)+cell.index_count>world.collision.indices.size())return false;
        for(unsigned i=0;i<cell.index_count && count<admitted.size();++i) {
            const auto index=world.collision.indices[cell.first_index+i];
            if(index>=world.collision.polygons.size())return false;
            if(std::find(admitted.begin(),admitted.begin()+count,index)!=admitted.begin()+count)continue;
            const auto& p=world.collision.polygons[index];
            float low[2]={p.corner[0][0],p.corner[0][1]},high[2]={low[0],low[1]};
            for(unsigned k=1;k<4;++k)for(unsigned c=0;c<2;++c) {
                low[c]=std::min(low[c],p.corner[k][c]);high[c]=std::max(high[c],p.corner[k][c]);
            }
            if(low[0]>collection[2]||high[0]<collection[0]||low[1]>collection[3]||high[1]<collection[1])continue;
            admitted[count++]=index;
        }
    }
    // Original cap160 is before this partition, not a cap160 road-only list.
    for(std::size_t i=0;i<count;++i)
        if(!(world.collision.polygons[admitted[i]].unknown0[3]&0x0AU))next.polygons[next.count++]=admitted[i];
    next.valid=true;cache=next;return true;
}
}
bool query_track_ground(const TrackContactWorld& world,const VehicleVec3& p,
                             TrackGroundCache& cache,TrackContact& hit) {
    const float x=float(p.x),y=float(p.y),z=float(p.z);
    if(!world.valid||!std::isfinite(x)||!std::isfinite(y)||!std::isfinite(z))return false;
    if(cache.world!=&world)cache={};
    //440A59 tries the last strict-interior polygon before refreshing bounds.
    if(cache.last<world.collision.polygons.size() &&
       shadow_edge_test(world.collision.polygons[cache.last],x,y)<0.F)
        return shadow_publish_ground(world,cache.last,x,y,hit);
    if(!cache.valid||x<cache.bounds[0]||x>cache.bounds[2]||y<cache.bounds[1]||y>cache.bounds[3])
        if(!shadow_refresh_ground(world,x,y,cache))return false;
    for(unsigned pass=0;pass<2;++pass) {
        const float upper=pass?std::numeric_limits<float>::max():z+1.F;
        float best=-std::numeric_limits<float>::max();std::size_t selected=cache.count;
        TrackContact candidate;
        for(std::size_t i=0;i<cache.count;++i) {
            const auto index=cache.polygons[i];const auto& polygon=world.collision.polygons[index];
            const float edge=shadow_edge_test(polygon,x,y);
            if(edge>0.F||!shadow_publish_ground(world,index,x,y,candidate))continue;
            // 440BCB requires a surface strictly below the current ceiling.
            if(candidate.point[2]<=best||candidate.point[2]>=upper)continue;
            best=candidate.point[2];selected=i;
            if(!(polygon.unknown0[3]&1U)&&edge<0.F) {cache.last=index;break;}
        }
        if(selected==cache.count)continue;
        const auto index=cache.polygons[selected];
        //440C80 moves a hit beyond index4 to the front, retaining list order.
        if(selected>4) {
            std::move_backward(cache.polygons.begin(),cache.polygons.begin()+selected,cache.polygons.begin()+selected+1);
            cache.polygons[0]=index;
        }
        return shadow_publish_ground(world,index,x,y,hit);
    }
    return false;
}
