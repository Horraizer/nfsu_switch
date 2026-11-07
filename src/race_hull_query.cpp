#include "nfsu/race_contacts.h"
#include <algorithm>
#include <cmath>
#include <limits>

namespace nfsu {
namespace {
using V=VehicleVec3;
V add(V a,V b){return {a.x+b.x,a.y+b.y,a.z+b.z};}
V sub(V a,V b){return {a.x-b.x,a.y-b.y,a.z-b.z};}
V mul(V a,double s){return {a.x*s,a.y*s,a.z*s};}
double dot(V a,V b){return (a.x*b.x+a.y*b.y)+a.z*b.z;}
struct Shape {
    const RaceContactBox& hull;
    bool polygon=false;
    V origin,com;
    std::array<V,3> axes;
    Shape(const VehicleState& s,const VehicleSpec& spec,const RaceContactBox& h,bool p=false):hull(h),polygon(p),origin(s.position) {
        const auto& c=spec.handling.centre_of_mass;com={c[0],c[1],c[2]};
        const auto basis=race_contact_axes(s);
        for(unsigned i=0;i<3;++i)axes[i]={basis[i][0],basis[i][1],basis[i][2]};
    }
    V point(unsigned index)const {
        if(polygon) {const auto& p=hull.points[index];return {p[0],p[1],p[2]};}
        V p;
        if(hull.points.empty()) p={index&1?hull.high.x:hull.low.x,
            index&2?hull.high.y:hull.low.y,index&4?hull.high.z:hull.low.z};
        else {const auto& v=hull.points[index];p={v[0],v[1],v[2]};}
        // 472E40 shifts selected shape vertices by the COM in binary32.
        p={float(p.x-com.x),float(p.y-com.y),float(p.z-com.z)};
        //5ABDB5 scalar transform: each product/add has binary32 precision,
        //in Z,Y,X order before the translation. World-coordinate cancellation
        //must not silently use the port's double vector expression.
        const auto component=[&](double x,double y,double z,double translation) {
            const float zy=float(p.z*z)+float(p.y*y);
            const float xyz=zy+float(p.x*x);
            return xyz+float(translation);
        };
        return {component(axes[0].x,axes[1].x,axes[2].x,origin.x),
                component(axes[0].y,axes[1].y,axes[2].y,origin.y),
                component(axes[0].z,axes[1].z,axes[2].z,origin.z)};
    }
    unsigned support(V direction,unsigned start)const {
        if(polygon) {
            //473A90 scans in storage order, retaining the first strict maximum.
            //The unrolled first lane and scalar tail use X/Z/Y; other lanes
            //use Y/Z/X. These orders matter for nearly coplanar witnesses.
            const auto d=ContactVector{float(direction.x),float(direction.y),float(direction.z)};
            const unsigned grouped=unsigned(hull.points.size())&~3U;
            unsigned best=0;float maximum=-9.160000137128778e36F;
            for(unsigned i=0;i<hull.points.size();++i) {
                const auto& p=hull.points[i];
                const float value=(i>=grouped||i%4==0)?
                    (p[0]*d[0]+p[2]*d[2])+p[1]*d[1]:
                    (p[1]*d[1]+p[2]*d[2])+p[0]*d[0];
                if(value>maximum||maximum==-9.160000137128778e36F){maximum=value;best=i;}
            }
            return best;
        }
        const auto product=[&](V axis) {
            return (float(direction.z)*float(axis.z)+float(direction.y)*float(axis.y))
                +float(direction.x)*float(axis.x);
        };
        const ContactVector local{product(axes[0]),product(axes[1]),product(axes[2])};
        if(hull.points.empty()) return (local[0]>=0?1:0)|(local[1]>=0?2:0)|(local[2]>=0?4:0);
        const auto value=[&](unsigned i) {
            const auto& p=hull.points[i];
            //472E40 moves points before472F30 evaluates the support direction.
            const float x=float(p[0]-com.x),y=float(p[1]-com.y),z=float(p[2]-com.z);
            return (local[2]*z+local[1]*y)+local[0]*x;
        };
        if(!hull.neighbors.empty()) {
            //472F30 uses a FIFO list of better adjacent vertices and returns
            //the first local maximum. Equal support does not enqueue a vertex.
            std::array<unsigned,256> queue{};
            std::array<bool,256> visited{};
            unsigned first=0,last=1;queue[0]=start;visited[start]=true;
            bool walk=hull.points.size()>16;
            while(first<last) {
                const unsigned current=queue[first++];
                if(hull.neighbors[current].empty())continue;
                const float current_value=value(current);
                bool maximum=true;
                for(const unsigned adjacent:hull.neighbors[current]) {
                    if(value(adjacent)>current_value) {
                        maximum=false;
                        if(walk&&!visited[adjacent])queue[last++]=adjacent;
                    }
                    visited[adjacent]=true;
                }
                walk=false; // retail735FD4=0 after the first visited vertex.
                if(maximum)return current;
            }
        }
        unsigned best=0;float maximum=value(0);
        for(unsigned i=1;i<hull.points.size();++i) {
            const float v=value(i);
            if(v>maximum){maximum=v;best=i;}
        }
        return best;
    }
};
struct Vertex {V point;unsigned id;};
struct Feature {std::array<Vertex,4> vertices{};unsigned count=0;};
bool append(Feature& f,const Shape& shape,V direction,unsigned& support) {
    const unsigned id=shape.support(direction,support);support=id;
    for(unsigned i=0;i<f.count;++i)if(f.vertices[i].id==id)return false;
    if(f.count==4)return false;
    f.vertices[f.count++]={shape.point(id),id};return true;
}
#include "race_hull_features.inc"

}
bool race_hull_support(const VehicleState& s,const VehicleSpec& spec,const RaceContactBox& h,
    const ContactVector& direction,unsigned start,unsigned& index,ContactVector& point) {
    const unsigned count=h.points.empty()?8:unsigned(h.points.size());
    if(count>256||start>=count)return false;
    if(!h.neighbors.empty()) {
        if(h.neighbors.size()!=count)return false;
        for(const auto& edges:h.neighbors)for(unsigned id:edges)if(id>=count)return false;
    }
    for(float x:direction)if(!std::isfinite(x))return false;
    const Shape shape(s,spec,h);
    index=shape.support({direction[0],direction[1],direction[2]},start);
    const auto p=shape.point(index);point={float(p.x),float(p.y),float(p.z)};
    return true;
}
static bool query_hulls(const VehicleState& sa,const VehicleSpec& as,const RaceContactBox& ah,
    const VehicleState& sb,const VehicleSpec& bs,const RaceContactBox& bh,RaceHullQuery& result,RaceHullCache* cache,bool polygon) {
    for(const auto* h:{&ah,&bh}) {
        if(h->points.size()>256)return false;
        if(!h->neighbors.empty()) {
            if(h->neighbors.size()!=h->points.size())return false;
            for(const auto& edges:h->neighbors)for(unsigned id:edges)
                if(id>=h->points.size())return false;
        }
    }
    const Shape a(sa,as,ah),b(sb,bs,bh,polygon);
    unsigned support_a=0,support_b=0;
    RaceHullCache state=cache?*cache:RaceHullCache{};
    const ContactVector origin_a{{float(sa.position.x),float(sa.position.y),float(sa.position.z)}};
    const ContactVector origin_b{{float(sb.position.x),float(sb.position.y),float(sb.position.z)}};
    const auto unchanged=[](const ContactVector& a,const ContactVector& b) {
        for(unsigned k=0;k<3;++k)if(!(std::abs(a[k]-b[k])<=.001F))return false;
        return true;
    };
    if(state.cached&&unchanged(origin_a,state.origin_a)&&unchanged(origin_b,state.origin_b)) {
        result=state.query;return true;
    }
    Feature fa,fb;
    if(state.initialized) {
        if(state.count_a>4||state.count_b>4||!state.count_a||!state.count_b)return false;
        fa.count=state.count_a;fb.count=state.count_b;
        for(unsigned i=0;i<fa.count;++i){
            if(state.a[i]>=(ah.points.empty()?8:ah.points.size()))return false;
            fa.vertices[i]={a.point(state.a[i]),state.a[i]};
        }
        for(unsigned i=0;i<fb.count;++i){
            if(state.b[i]>=(bh.points.empty()?8:bh.points.size()))return false;
            fb.vertices[i]={b.point(state.b[i]),state.b[i]};
        }
        //472042 refreshes retained vertices through the same temporary passed
        //to the next support call.472FBC starts at its last vertex ID, not0.
        support_a=fa.vertices[fa.count-1].id;
        support_b=fb.vertices[fb.count-1].id;
    } else {
        const auto direction=sub(b.point(0),a.point(0));
        append(fa,a,direction,support_a);append(fb,b,mul(direction,-1),support_b);
    }
    V wa=dv(state.query.witness_a),wb=dv(state.query.witness_b),direction{};double squared=0;
    if(!closest(fa,fb,wa,wb,direction,squared))return false;
    // 472026 and472164 use squared distance, before the support loop.
    // This is the original query's contact tolerance, not a new hull inflation.
    const bool near=squared<=double(.001F);
    if(!near)state.initialized=true;
    for(;;) {
        if(near||squared==0)break;
        const auto old_fa=fa,old_fb=fb;const auto old_a=wa,old_b=wb;
        const double old_distance=squared;
        const bool added_a=append(fa,a,direction,support_a);
        const bool added_b=append(fb,b,mul(direction,-1),support_b);
        if(!added_a&&!added_b)break;
        if(!closest(fa,fb,wa,wb,direction,squared))return false;
        // 472321 restores the previous feature on non-improvement.
        if(!(squared<old_distance)){fa=old_fa;fb=old_fb;wa=old_a;wb=old_b;squared=old_distance;break;}
    }
    RaceHullQuery out;
    out.witness_a={float(wa.x),float(wa.y),float(wa.z)};
    out.witness_b={float(wb.x),float(wb.y),float(wb.z)};
    out.distance=near?0:float(vehicle_sqrt(squared));
    state.count_a=fa.count;state.count_b=fb.count;
    for(unsigned i=0;i<fa.count;++i)state.a[i]=fa.vertices[i].id;
    for(unsigned i=0;i<fb.count;++i)state.b[i]=fb.vertices[i].id;
    if(!std::isfinite(out.distance))return false;
    state.cached=true;state.origin_a=origin_a;state.origin_b=origin_b;state.query=out;
    result=out;if(cache)*cache=state;return true;
}
bool query_race_hulls(const VehicleState& sa,const VehicleSpec& as,const RaceContactBox& ah,
    const VehicleState& sb,const VehicleSpec& bs,const RaceContactBox& bh,RaceHullQuery& result,RaceHullCache* cache) {
    return query_hulls(sa,as,ah,sb,bs,bh,result,cache,false);
}
bool query_race_track_polygon(const VehicleState& s,const VehicleSpec& spec,const RaceContactBox& h,
    const std::array<ContactVector,4>& corners,const ContactVector& normal,bool expanded,
    RaceHullQuery& result,RaceHullCache* cache) {
    for(const auto& p:corners)for(float x:p)if(!std::isfinite(x))return false;
    for(float x:normal)if(!std::isfinite(x))return false;
    bool triangle=true;
    for(unsigned k=0;k<3;++k)if(std::abs(corners[2][k]-corners[3][k])>.001F)triangle=false;
    const unsigned count=triangle?3:4;
    RaceContactBox polygon;
    polygon.points.assign(corners.begin(),corners.begin()+count);
    if(expanded)for(unsigned i=0;i<count;++i) {
        ContactVector p;for(unsigned k=0;k<3;++k)p[k]=corners[i][k]-normal[k];
        polygon.points.push_back(p);
    }
    const VehicleState fixed;const VehicleSpec fixed_spec;
    //4729AC invalidates the retained feature when the polygon's expansion
    //flag changes; a newly available back face cannot reuse an old simplex.
    if(cache&&cache->polygon_expanded!=expanded) {
        cache->initialized=false;cache->cached=false;cache->polygon_expanded=expanded;
    }
    return query_hulls(s,spec,h,fixed,fixed_spec,polygon,result,cache,true);
}
}
