#include "nfsu/engine_audio.h"
#include "nfsu/collision_audio.h"
#include <fstream>
#include <iostream>
#include <iterator>
int main(int argc,char** argv){try{
 if(argc==17&&std::string(argv[1])=="--car"){
  nfsu::VehicleVec3 v[5];for(unsigned i=0;i<5;++i)v[i]={std::stod(argv[2+i*3]),std::stod(argv[3+i*3]),std::stod(argv[4+i*3])};
  std::cout<<nfsu::audio::car_collision_subtype(v[0],v[1],v[2],v[3],v[4])<<'\n';return 0;
 }
 const bool alternating=argc>1&&std::string(argv[1])=="--alternating";
 const int path_at=alternating?2:1;
 if(argc<=path_at)throw std::runtime_error("usage: effect_audio_probe [--alternating] BANK PARAMS...");
 std::ifstream f(argv[path_at],std::ios::binary);std::vector<unsigned char>b((std::istreambuf_iterator<char>(f)),{});
 std::array<nfsu::audio::EngineAudioGraph,2> graphs;const unsigned count=alternating?2:1;
 for(unsigned i=0;i<count;++i)graphs[i].load_effect(b);
 std::vector<int> p;for(int i=path_at+1;i<argc;++i)p.push_back(std::stoi(argv[i]));
 for(unsigned t=0;t<120;++t){
  // Supplied second-module replacement tests global RNG lifetime on reload.
  if(alternating&&t==60)graphs[1].load_effect(b);
  bool live=false;
  for(unsigned actor=0;actor<count;++actor){auto& g=graphs[actor];if(g.retired())continue;
   live=true;std::cout<<t;if(alternating)std::cout<<":"<<actor;
   g.effect_tick(p,[&](unsigned,const auto&v){std::cout<<" "<<v.sample<<","<<v.state<<","<<v.pitch_q12<<","<<v.gain_q15<<","<<v.pan;return int(v.state);});std::cout<<"\n";
  }
  if(!live)break;
 }
 }catch(const std::exception&e){std::cerr<<e.what()<<"\n";return 1;}}
