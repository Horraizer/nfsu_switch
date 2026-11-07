#include "nfsu/frontend_music.h"
#include <algorithm>
#include <stdexcept>
namespace nfsu::audio {
unsigned original_music_next(unsigned first,const std::vector<std::uint64_t>& lengths,
                             std::uint64_t frames) noexcept {
    if(lengths.empty()||!frames)return first;
    std::uint64_t total=0;for(auto length:lengths)total+=length;
    if(!total)return first;
    frames%=total;unsigned index=0;
    while(index+1<lengths.size()&&frames>=lengths[index])frames-=lengths[index++];
    return (first+index+1)%unsigned(lengths.size());
}
MenuSong original_menu_song(unsigned index) {
    static constexpr MenuSong songs[]={
        {"Get Low","Lil Jon & the Eastside Boyz","Kings of Crunk"},
        {"Smashing the Gas (Get Faster)","Mystikal","Need for Speed Exclusive"},
        {"Who's Who","Dilated Peoples","Neighborhood Watch"},
        {"Keep It Coming","Nate Dogg","Need for Speed Exclusive"},
        {"Body Rock","X-ecutioners",""},
        {"Need For Speed","Petey Pablo","Need for Speed Exclusive"},
        {"24's","T.I.","Trap Muzik"}};
    return songs[index%7];
}

std::vector<Segment> original_music_playlist(const Bank& bank,
    const std::vector<Segment>& indexed, bool frontend) {
    if (bank.entries.size()!=27 || indexed.size()!=27)
        throw std::runtime_error("original music bank must have 27 entries");
    std::vector<Segment> result;
    const unsigned begin=frontend ? 20 : 1, end=frontend ? 27 : 20;
    for (unsigned id=begin;id<end;++id) {
        const auto& entry=bank.entries[id];
        if(entry.type!=1) throw std::runtime_error("original song entry is not streamed");
        const auto found=std::find_if(indexed.begin(),indexed.end(),
            [&](const Segment& s) { return s.offset==entry.value; });
        if(found==indexed.end()) throw std::runtime_error("original song AST boundary absent");
        result.push_back(*found);
    }
    return result;
}
}
