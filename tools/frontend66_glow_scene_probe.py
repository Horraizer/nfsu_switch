"""Compare actual resident FE road sprite commands at a later phase to retail code."""
from pathlib import Path
import argparse,subprocess,sys,ast,json,math
sys.dont_write_bytecode=True
parser=argparse.ArgumentParser();parser.add_argument('--retail-root',type=Path,required=True)
retail=parser.parse_args().retail_root
root=Path.cwd();scratch=root/'build/worker-handoff'
cpp=scratch/'menu66-glow-scene-native-probe.cpp';exe=cpp.with_suffix('.exe')
cpp.write_text(r'''#include "nfsu/frontend_scene.h"
#include <iostream>
#include <iomanip>
void emit(const nfsu::RecordedDraw& d){if(d.frontend_sprite_kind!=0||!d.frontend_sprite_view||d.material.alpha[0]<=0)return;
 std::cout<<"SOURCE_QUAD "<<d.frontend_sprite_view<<" [";int k[4][2]={{-1,1},{1,1},{1,-1},{-1,-1}};
 for(unsigned i=0;i<4;++i){if(i)std::cout<<",";std::cout<<"[";
 for(unsigned a=0;a<3;++a){if(a)std::cout<<",";std::cout<<d.placement.rows[12+a]+k[i][0]*d.placement.rows[a]+k[i][1]*d.placement.rows[8+a];}std::cout<<"]";}
 std::cout<<"] "<<d.material.alpha[0]<<"\n";}
int main(int argc,char** argv){nfsu::FrontendGpuScene scene;std::string error;
 if(argc!=2||!scene.load(argv[1],1280,720,error)||!scene.frame(3.F,12000,error)){std::cerr<<error<<"\n";return 1;}
 std::cout<<std::setprecision(10)<<"SOURCE_OFFSET "<<200-scene.commands().owning_draws()[0].placement.rows[12]<<"\n";
 for(const auto& d:scene.commands().owning_draws())emit(d);
 for(const auto& d:scene.commands().reflections)emit(d);
 for(const auto& d:scene.commands().frontend_cube_draws)emit(d);
}
''',encoding='ascii')
libs=list((root/'build').rglob('*.a'))
subprocess.run(['C:/Strawberry/c/bin/g++.exe','-std=c++17','-Iinclude','-Ibuild/generated',str(cpp),'-Wl,--start-group']+[str(x) for x in libs]+['-Wl,--end-group','-lwinmm','-o',str(exe)],check=True)
source=subprocess.run([str(exe),str(retail)],text=True,capture_output=True,check=True).stdout
assert source.startswith('SOURCE_OFFSET ')
offset=float(source.splitlines()[0].split()[1]);assert 0<offset<100
native=subprocess.run([sys.executable,str(root/'tools/frontend66_flare_native.py'),'--retail-root',str(retail),'--road-offset',str(offset),'--display-width','1280'],text=True,capture_output=True,check=True).stdout
source_quads={view:[] for view in (1,4,10,11,12,13,14,15)}
for line in source.splitlines()[1:]:
 _,view,payload=line.split(' ',2);points,alpha=payload.rsplit(' ',1)
 source_quads[int(view)].append((json.loads(points),round(float(alpha)*255)))
for line in native.splitlines():
 if not line.startswith('VIEW '):continue
 view=int(line.split()[1]);draws=ast.literal_eval(line.split('DRAWS ',1)[1])
 actual=source_quads[view];assert len(actual)==len(draws),(view,len(actual),len(draws))
 for _,_,quad in draws:
  matches=[i for i,(points,alpha) in enumerate(actual) if alpha==quad['rgba'][3] and
   all(abs(points[c][a]-quad['corners'][c][a])<.0003 for c in range(4) for a in range(3))]
  assert len(matches)==1,(view,quad,actual)
  actual.pop(matches[0])
 assert not actual
print('PASS: actual source all-view later-phase road sprite counts, all corners and opacity match native')
print('BOUNDARY: original input phase is three seconds; shared display1280 and clipping planes supplied, no occlusion claim')
