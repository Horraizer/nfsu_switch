#include "nfsu/ai_traffic_intersection.h"
#include "nfsu/ai_neighbors.h"
#include "nfsu/race_route.h"
#include "nfsu/arg_path.h"
#include "nfsu/sha256.h"
#include <cstring>
#include <fstream>
#include <iostream>
#include <iterator>
#include <limits>

using namespace nfsu;
namespace {
#include "traffic_intersection_native.inc"
#include "traffic_intersection_geometry_native.inc"
unsigned failures=0;
void check(bool yes,const char* message){if(!yes){++failures;std::cerr<<"FAIL: "<<message<<'\n';}}
float f(unsigned word){float value;std::memcpy(&value,&word,4);return value;}
unsigned bits(float value){unsigned word;std::memcpy(&word,&value,4);return word;}
AiTrafficIntersection restore(const NativeIntersectionSnapshot& s){
    AiTrafficIntersection i;i.kind=s.kind;i.points={int(std::int16_t(s.points_word&65535)),int(std::int16_t(s.points_word>>16))};i.selected=s.selected;
    for(unsigned k=0;k<2;++k){i.routes[k]=s.routes[k];i.closest[k]=s.closest[k];}
    for(unsigned k=0;k<22;++k){const auto& a=s.actors[k];i.actors[k]={a.active!=0,a.direction,f(a.distance),f(a.time)};}
    return i;
}
bool equal(const AiTrafficIntersection& a,const AiTrafficIntersection& b){
    if(a.kind!=b.kind||a.routes!=b.routes||a.closest!=b.closest||a.points!=b.points||a.selected!=b.selected)return false;
    for(unsigned k=0;k<22;++k){const auto& x=a.actors[k];const auto& y=b.actors[k];
        if(x.active!=y.active||x.direction!=y.direction||bits(x.distance)!=bits(y.distance)||bits(x.time)!=bits(y.time))return false;}
    return true;
}
void put(std::vector<unsigned char>& data,unsigned at,unsigned word){for(unsigned k=0;k<4;++k)data[at+k]=std::uint8_t(word>>(8*k));}
void encode(const AiTrafficIntersection& i,std::vector<unsigned char>& data,unsigned base){
    put(data,base,unsigned(i.kind));put(data,base+8,(unsigned(i.points[0])&65535)|(unsigned(i.points[1])<<16));put(data,base+12,unsigned(i.selected));
    for(unsigned k=0;k<2;++k){data[base+4+k]=std::uint8_t(i.routes[k]);data[base+6+k]=std::uint8_t(i.closest[k]);}
    for(unsigned k=0;k<22;++k){const auto& a=i.actors[k];const unsigned at=base+0x10+k*0x10;
        data[at]=a.active;put(data,at+4,unsigned(a.direction));put(data,at+8,bits(a.distance));put(data,at+12,bits(a.time));}
}
unsigned word(const unsigned char* p){return unsigned(p[0])|(unsigned(p[1])<<8)|(unsigned(p[2])<<16)|(unsigned(p[3])<<24);}
}
int main(int argc,char** argv){
    unsigned index=0;
    for(const auto& c:kNativeIntersectionUpdates){
        auto i=restore(c.before);bool ok=false;
        if(c.operation==0)ok=ai_traffic_intersection_register(i,int(c.args[0]),int(c.args[1]),int(c.args[2]),f(c.args[3]),f(c.args[4]));
        else if(c.operation==1)ok=ai_traffic_intersection_clear(i,int(c.args[0]));
        else ok=ai_traffic_intersection_step(i);
        if(!ok||!equal(i,restore(c.after))){++failures;std::cerr<<"FAIL: original intersection update "<<index<<'\n';}
        ++index;
    }
    index=0;
    for(const auto& c:kNativeIntersectionQueries){
        const auto i=restore(c.state);bool out=false;
        const bool ok=c.operation==0?ai_traffic_intersection_open(i,c.args[0],out):
            ai_traffic_intersection_pair(i,int(c.args[0]),int(c.args[1]),int(c.args[2]),int(c.args[3]),out);
        if(!ok||out!=bool(c.result)){++failures;std::cerr<<"FAIL: original intersection query "<<index<<'\n';}
        ++index;
    }
    {
        auto i=restore(kNativeIntersectionUpdates[0].before);const auto saved=i;const float nan=std::numeric_limits<float>::quiet_NaN();
        check(ai_traffic_intersection_register(i,0,1,101,nan,nan)&&equal(i,saved),"non-traffic no-op precedes ID/float consumption");
        check(!ai_traffic_intersection_register(i,49,3,101,1,1)&&equal(i,saved),"invalid traffic ID cannot address another record");
        check(!ai_traffic_intersection_register(i,72,3,101,1,1)&&equal(i,saved),"upper actor bound refuses transactionally");
        check(!ai_traffic_intersection_register(i,50,3,101,nan,1)&&equal(i,saved),"nonfinite registration refuses");
        check(!ai_traffic_intersection_clear(i,0)&&equal(i,saved),"invalid clear preserves table");
        bool yes=false;check(ai_traffic_intersection_pair(i,0,1,100,2,yes)&&yes,"non-traffic roles return before slot lookup");
        float time=-1;check(ai_traffic_intersection_time(i,0,time)&&time==99999.F,"absent nearest preserves original sentinel");
        check(!ai_traffic_intersection_time(i,2,time)&&time==99999.F,"invalid direction preserves output");
        check(ai_traffic_intersection_register(i,50,3,101,10,1),"register nearest for retained-ID case");
        check(ai_traffic_intersection_register(i,50,3,99,20,2)&&!i.actors[0].active&&i.closest[0]==50&&
            ai_traffic_intersection_time(i,0,time)&&time==2,"registration does not invent nearest lifecycle cleanup");
    }
    {
        auto a=restore(kNativeIntersectionUpdates[0].before);a.points={-32768,32767};
        const auto b=restore(kNativeIntersectionUpdates[7].after);
        std::vector<unsigned char> bytes(2*0x170);encode(a,bytes,0);encode(b,bytes,0x170);
        std::vector<AiTrafficIntersection> tables;
        check(ai_traffic_intersections(bytes.data(),bytes.size(),tables)&&tables.size()==2&&equal(tables[0],a)&&equal(tables[1],b),
            "serialized direction IDs and retained slots match native states");
        const auto saved=tables;
        check(!ai_traffic_intersections(bytes.data(),bytes.size()-1,tables)&&tables.size()==saved.size()&&equal(tables[1],saved[1]),
            "truncated intersection table is transactional");
        check(!ai_traffic_intersections(nullptr,0x170,tables),"missing payload refuses");
        put(bytes,12,2);check(!ai_traffic_intersections(bytes.data(),bytes.size(),tables)&&equal(tables[0],saved[0]),
            "unsafe selected direction is not accepted");
    }
    for(const auto& c:kNativeIntersectionArrival){
        float time=-1;
        check(ai_traffic_intersection_arrival(f(c.distance),f(c.speed),time)&&bits(time)==c.result,
            "actual583C80 signed-speed threshold and arrival publication");
    }
    {
        const float nan=std::numeric_limits<float>::quiet_NaN();float out=7;
        check(ai_traffic_intersection_arrival(0,nan,out)&&out==0,"arrived actor does not consume signed speed");
        out=7;check(!ai_traffic_intersection_arrival(nan,20,out)&&out==7,"invalid arrival refuses transactionally");
        check(!ai_traffic_intersection_arrival(1,nan,out)&&out==7,"positive distance consumes finite speed");
        RacingLineTrack raw;AiTrafficIntersection i;AiRoadChain chain;AiNeighborEntry self;
        std::vector<AiTrafficIntersection> intersections;
        check(!ai_traffic_intersection_distance(raw,intersections,chain,-1,self,0,out)&&out==7,
            "missing limited producer is not a clear road");
        check(!ai_traffic_intersection_extent(raw,i,0,2,out)&&out==7,"invalid projection direction refuses");
        check(ai_traffic_intersection_extent(raw,i,0,0,out)&&out==0,"unmatched raw route is original zero extent");
        i.routes[0]=0;out=7;
        check(!ai_traffic_intersection_extent(raw,i,0,0,out)&&out==7,"matched missing raw point refuses");
    }
    if(argc!=1){
        std::string error;if(argc!=2||!check_arguments(argc,argv,1,error)){std::cerr<<"FAIL: retail root argument\n";return 1;}
        const auto path=argument_path(argv[1])/"Tracks/TRACKB1003.lzc";std::ifstream file(path,std::ios::binary);
        std::vector<unsigned char> stored(std::istreambuf_iterator<char>(file),{});
        if(!file||sha256_hex(sha256(stored.data(),stored.size()))!="ADA8206B5D66E3286BF582E2C7117FE0CC2BA9F6CADB756D922F7D32E7C696BA"){
            std::cerr<<"FAIL: authenticated retail track input\n";return 1;}
        std::vector<unsigned char> image;const auto track=load_race_route_track(path,image);
        if(!track.ok()){std::cerr<<"FAIL: retail track load\n";return 1;}
        RacingLineTrack raw;check(parse_racing_line_image(image.data(),image.size(),{},raw),"raw un-oriented roads parse");
        bool found=false;std::vector<AiTrafficIntersection> intersections;
        for(std::size_t at=0;at+8<=image.size();){const auto tag=word(image.data()+at),size=word(image.data()+at+4);at+=8;
            if(size>image.size()-at){std::cerr<<"FAIL: retail chunk extent\n";return 1;}
            if(tag==0x34123){auto& table=intersections;found=true;
                check(ai_traffic_intersections(image.data()+at,size,table)&&!table.empty(),"external retail intersection records load");
                for(auto& i:table){bool open=false;check(ai_traffic_intersection_open(i,0,open),"retail initial permission consumes valid state");}}
            at+=size;
        }
        check(found,"retail intersection chunk is present");
        for(const auto& c:kNativeIntersectionExtent){
            if(c.intersection<0||unsigned(c.intersection)>=intersections.size()){check(false,"extent intersection reference");continue;}
            float out=0;
            if(!ai_traffic_intersection_extent(raw,intersections[unsigned(c.intersection)],c.route,c.direction,out)||bits(out)!=c.result){
                ++failures;std::cerr<<"FAIL: original raw extent intersection "<<c.intersection<<" route "<<c.route<<" direction "<<c.direction<<" got "<<bits(out)<<" expected "<<c.result<<'\n';}
        }
        for(const auto& c:kNativeIntersectionDistance){
            const auto* route=raw.find(short(c.route));
            if(!route||c.point>=route->points.size()){check(false,"distance raw route reference");continue;}
            AiRoadChain chain;chain.count=1;auto& cell=chain.cells[0];
            cell.point=route->points[c.point];cell.point_index=std::uint16_t(c.point);cell.distance=f(c.coordinate);
            AiTrafficIntersectionRef ref;
            check(ai_traffic_intersection_reference(raw,chain,0,intersections.size(),ref)&&ref.present&&ref.index==unsigned(c.intersection),
                "first_limited raw mask resolves the original intersection identity");
            check(ai_traffic_intersection_reference(raw,chain,-1,intersections.size(),ref)&&!ref.present,
                "known absent limited cell is valid absence");
            ref={true,7};
            check(!ai_traffic_intersection_reference(raw,chain,0,0,ref)&&ref.present&&ref.index==7,
                "missing intersection table cannot turn an admitted crossing into clear road");
            AiNeighborEntry self;self.distance=f(c.along);self.half_extent[1]=f(c.half);float out=0;
            if(!ai_traffic_intersection_distance(raw,intersections,chain,0,self,c.direction,out)||bits(out)!=c.result){
                ++failures;std::cerr<<"FAIL: original forward distance route "<<c.route<<" point "<<c.point<<" got "<<bits(out)<<" expected "<<c.result<<'\n';}
            const auto saved=out;
            check(!ai_traffic_intersection_distance(raw,intersections,chain,1,self,c.direction,out)&&bits(out)==bits(saved),
                "limited index cannot read beyond admitted compact chain");
        }

    }
    std::cout<<"Original intersection lifecycle/parser/geometry/arrival, failures "<<failures<<'\n';return failures?1:0;
}
