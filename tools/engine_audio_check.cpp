#include "nfsu/engine_audio.h"
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <algorithm>

int main(int argc,char** argv) {
    try {
        if(argc!=4&&argc!=6&&argc!=7)throw std::runtime_error("usage: engine_audio_check BANK GROUP UPGRADE [GAIN_A GAIN_B [PAN]]");
        std::ifstream in(argv[1],std::ios::binary);if(!in)throw std::runtime_error("bank missing");
        std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(in)),{});
        nfsu::audio::EngineAudioGraph graph;graph.load(bytes,unsigned(std::stoi(argv[2])),unsigned(std::stoi(argv[3])));
        std::cout<<"tick,rpm,load,sample,state,pitch,gain,pan\n";
        for(unsigned tick=0;tick<240;++tick) {
            const float rpm=tick<60?800.0F:tick<120?2500.0F:tick<180?7000.0F:2500.0F;
            const float load=tick<30?0.0F:tick<150?100.0F:tick<210?0.0F:50.0F;
            auto voices=graph.tick(rpm,load,1000.0F/60.0F,
                argc>=6?unsigned(std::stoi(argv[4])):32767U,
                argc>=6?unsigned(std::stoi(argv[5])):32767U,
                argc==7?std::uint16_t(std::stoul(argv[6])):std::uint16_t(32768));
            std::sort(voices.begin(),voices.end(),[](auto& a,auto& b){return a.sample<b.sample;});
            for(const auto& v:voices)std::cout<<tick<<','<<rpm<<','<<load<<','<<v.sample<<','<<v.state<<','<<v.pitch_q12<<','<<v.gain_q15<<','<<v.pan<<'\n';
        }
        return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
