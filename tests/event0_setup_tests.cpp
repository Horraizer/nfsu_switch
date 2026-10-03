#include "nfsu/event0_setup.h"
#include "nfsu/arg_path.h"
#include <cstring>
#include <iomanip>
#include <iostream>
#include <limits>
#include <stdexcept>

using namespace nfsu;
namespace {
void require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error("FAIL: "+message);
}
void put(std::vector<unsigned char>& bytes, std::size_t at, std::uint32_t value) {
    for (unsigned i=0;i<4;++i) bytes.at(at+i)=static_cast<unsigned char>(value>>(8*i));
}
void structural_guards() {
    require(!parse_event0_setup({}, {}, {}).ok(),"null images rejected");
    std::vector<unsigned char> bytes(8);
    RaceChunkPayload span; std::vector<RaceTableIssue> issues;
    put(bytes,0,0x30220); put(bytes,4,1);
    require(!find_race_chunk(bytes.data(),bytes.size(),0x30220,span,issues),"short payload rejected");
    bytes.resize(16); put(bytes,4,0); put(bytes,8,0x30220); put(bytes,12,0); issues.clear();
    require(!find_race_chunk(bytes.data(),bytes.size(),0x30220,span,issues),"duplicate chunks rejected");
    bytes.assign(18*8,0);
    for (unsigned i=0;i<18;++i) { put(bytes,8*i,0x80000001); put(bytes,8*i+4,(17-i)*8); }
    issues.clear();
    require(!find_race_chunk(bytes.data(),bytes.size(),0x30220,span,issues),"deep nesting rejected");
    issues.clear();
    require(!find_race_chunk(bytes.data(),bytes.size(),0x30220,span,issues,{1000,1000,0}),"chunk resource limit");
}
void dump(const Event0SetupDescriptor& d) {
    std::cout << std::setprecision(std::numeric_limits<float>::max_digits10);
    std::cout << "{\"event_offset\":"<<d.event.offset<<",\"event\":"<<d.event.number
        <<",\"track_offset\":"<<d.track.offset<<",\"track\":"<<d.race.track
        <<",\"laps\":"<<int(d.race.laps)<<",\"direction\":"<<int(d.race.direction)
        <<",\"difficulty\":"<<int(d.difficulty)<<",\"ai_scalar\":"<<d.selected_ai_scalar
        <<",\"desired_grid\":"<<int(d.desired_player_grid)<<",\"words\":[";
    for (std::size_t i=0;i<83;++i) std::cout<<(i ? "," : "")<<d.raw_event_words[i];
    std::cout<<"],\"actors\":[";
    for (std::size_t i=0;i<4;++i) {
        const auto& a=d.actors[i]; const auto& p=a.visual;
        std::cout<<(i ? "," : "")<<"{\"id\":"<<int(a.config_id)<<",\"role\":"<<int(a.role)
            <<",\"label\":"<<std::quoted(a.event_label)<<",\"profile_label\":"<<(a.label_from_profile ? "true" : "false")
            <<",\"source_offset\":"<<a.event_source_offset<<",\"preset_offset\":"<<p.offset
            <<",\"hash\":"<<p.hash<<",\"name\":"<<std::quoted(p.name)<<",\"car\":"<<std::quoted(p.car)
            <<",\"initial_grid\":"<<int(a.initial_grid)<<",\"final_grid\":"<<int(a.final_grid)
            <<",\"marker_offset\":"<<a.marker.offset<<",\"marker_index\":"<<a.marker.index
            <<",\"position\":["<<a.marker.raw_position[0]<<','<<a.marker.raw_position[1]<<','<<a.marker.raw_position[2]
            <<"],\"angle\":"<<a.marker.raw_angle<<",\"context\":["<<a.difficulty_context[0]<<','
            <<a.difficulty_context[1]<<','<<a.difficulty_context[2]<<"],\"selected_context\":"<<a.selected_context
            <<",\"tuning_selector\":"<<a.tuning_selector<<",\"ai_byte\":"<<int(a.raw_ai_byte)
            <<",\"performance\":";
        if (a.performance) std::cout<<std::quoted(performance_levels_text(*a.performance)); else std::cout<<"null";
        std::cout<<",\"pairs\":[";
        for (std::size_t j=0;j<120;++j) std::cout<<(j ? "," : "")<<'['<<p.pairs[j].slot<<','<<p.pairs[j].value<<']';
        std::cout<<"]}";
    }
    std::cout<<"],\"traffic_policy\":[";
    for (unsigned i=0;i<8;++i) std::cout<<(i ? "," : "")<<int(d.traffic.track_policy[i]);
    std::cout<<"],\"traffic_request\":"<<int(d.traffic.requested_configs)
        <<",\"traffic_live_count\":null,\"authenticated\":"<<(d.authenticated_inputs ? "true" : "false")
        <<",\"runtime_ready\":"<<(d.runtime_ready() ? "true" : "false")<<",\"hashes\":[";
    for (unsigned i=0;i<3;++i) std::cout<<(i ? "," : "")<<std::quoted(d.decoded_sha256[i]);
    std::cout<<"]}\n";
}
void retail(const std::filesystem::path& root, bool print) {
    const auto front=root/"FrontEnd/FrontB.lzc", global=root/"Global/GLOBALB.BUN", track=root/"Tracks/TRACKB1003.lzc";
    const auto result=load_event0_setup(front,global,track);
    require(result.ok(),result.issues.empty() ? "retail descriptor" : result.issues.front().message);
    const auto& d=*result.descriptor;
    require(d.authenticated_inputs && !d.runtime_ready() && d.unsupported.size()==7,"explicit setup readiness");
    require(d.grid_config_ids==std::array<std::int8_t,4>{100,0,102,101},"grid factory association");
    require(d.traffic.requested_configs==6 && !d.traffic.live_count,"request differs from live count");
    require(d.actors[0].performance && d.actors[0].label_from_profile,"human performance/profile provenance");
    require(!performance_levels_equal(*d.actors[0].performance,performance1_levels()),"not generic CLI tuning");
    for (std::size_t i=1;i<4;++i) require(!d.actors[i].performance,"adaptive rival vector unresolved");
    std::vector<unsigned char> f,g,t; std::vector<RaceTableIssue> issues;
    require(load_race_image(front,f,issues) && load_race_image(global,g,issues) && load_race_image(track,t,issues),"retail images");
    const auto parse=[&]() { return parse_event0_setup({f.data(),f.size()},{g.data(),g.size()},{t.data(),t.size()}); };
    require(parse().ok() && !parse().descriptor->authenticated_inputs,"structural parse cannot claim authentication");
    const auto mutate=[&](std::vector<unsigned char>& bytes,std::size_t at,std::uint32_t value,const char* label) {
        const auto original=bytes; put(bytes,at,value); const auto rejected=parse();
        bytes=original; require(!rejected.ok() && !rejected.descriptor && !rejected.issues.empty(),label);
    };
    mutate(f,static_cast<std::size_t>(d.event.offset)+0x128,4,"rival count bound");
    mutate(f,static_cast<std::size_t>(d.event.offset)+0xD0,0,"no profile fallback");
    mutate(f,static_cast<std::size_t>(d.event.offset)+0x13C,0x7FC00000,"non-finite scalar");
    mutate(f,static_cast<std::size_t>(d.event.offset)+0x138,5,"unsupported grid rejected");
    mutate(f,static_cast<std::size_t>(d.event.offset)+332,0,"duplicate event number");
    mutate(f,static_cast<std::size_t>(d.actors[0].visual.offset)+0x48,121,"invalid preset slot");
    mutate(f,static_cast<std::size_t>(d.actors[0].visual.offset)+0x28,0x41414141,"unresolved preset name hash");
    {
        const auto original=f;
        std::memcpy(f.data()+d.actors[1].visual.offset+0x28,
                    f.data()+d.actors[0].visual.offset+0x28,32);
        require(!parse().ok(),"duplicate preset hash rejected"); f=original;
    }
    mutate(f,static_cast<std::size_t>(d.actors[1].event_source_offset)+0x16,0,"unsupported rival tuning");
    mutate(t,static_cast<std::size_t>(d.actors[0].marker.offset)+0x10,0x7F800000,"infinite marker");
    mutate(t,static_cast<std::size_t>(d.actors[0].marker.offset)+0xC,0,"duplicate marker");
    mutate(t,static_cast<std::size_t>(d.actors[0].marker.offset)+8,0,"missing marker");
    mutate(g,static_cast<std::size_t>(d.track.offset)+0xC0,0x007F0000,"traffic ID overflow");
    mutate(g,static_cast<std::size_t>(d.track.offset)+0xC0,0x00FF0000,"negative traffic");
    {
        RaceChunkPayload span; std::vector<RaceTableIssue> chunk_issues;
        require(find_race_chunk(t.data(),t.size(),0x34146,span,chunk_issues),"retail marker span");
        mutate(t,static_cast<std::size_t>(span.begin)-4,
               static_cast<std::uint32_t>(span.end-span.begin-1),"partial marker table");
    }
    f.pop_back(); require(!parse().ok(),"truncated frontend");
    // Returned descriptor remains valid after image storage is destroyed.
    f.clear();g.clear();t.clear();
    require(d.actors[0].visual.name=="DDAY_INTEGRA","owned preset storage");
    require(d.actors[0].visual.pairs[34].slot<=120 || d.actors[0].visual.pairs[34].slot==0xFFFFFFFFU,"ordered pair storage");
    if (print) dump(d);
}
}
int main(int argc,char** argv) {
    try {
        structural_guards();
        if (argc>=2) {
            std::filesystem::path root; std::string error;
            require(argument_to_path(argv[1],root,error),error);
            retail(root,argc==3 && std::string(argv[2])=="--dump");
        }
        if (argc!=3) std::cout<<"PASS: event0 setup guards\n";
        return 0;
    } catch (const std::exception& error) { std::cerr<<error.what()<<'\n'; return 1; }
}
